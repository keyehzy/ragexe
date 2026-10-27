#include "res/gnd.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "res/data_files.h"
#include "res/resource.h"

using namespace ragexe::res;

namespace {

void Put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  Put16(out, v & 0xffff);
  Put16(out, v >> 16);
}

void PutFloat(std::vector<std::uint8_t>& out, float f) {
  std::uint32_t u = 0;
  std::memcpy(&u, &f, 4);
  Put32(out, u);
}

// A 2 x 1 ground of the given minor version: textures "a.bmp" and "b.bmp", one lightmap,
// two surfaces, and cells whose surface indices are (0, -1, 1) and (1, -1, -1).
std::vector<std::uint8_t> Gnd(std::uint8_t minor) {
  std::vector<std::uint8_t> out = {'G', 'R', 'G', 'N', 1, minor};
  Put32(out, 2);  // width
  Put32(out, 1);  // height
  PutFloat(out, 10.0f);
  Put32(out, 2);   // textures
  Put32(out, 80);  // name size
  for (const char* name : {"a.bmp", "b.bmp"}) {
    std::vector<std::uint8_t> record(80, 0);
    std::memcpy(record.data(), name, std::strlen(name));
    out.insert(out.end(), record.begin(), record.end());
  }
  for (std::uint32_t v : {1u, 8u, 8u, 1u}) Put32(out, v);
  for (int i = 0; i < 256; ++i) out.push_back(static_cast<std::uint8_t>(i));
  Put32(out, 2);  // surfaces
  for (int k = 0; k < 2; ++k) {
    for (int i = 0; i < 8; ++i) PutFloat(out, static_cast<float>(k) + static_cast<float>(i) / 8);
    Put16(out, static_cast<std::uint32_t>(k == 0 ? -1 : 1));  // texture
    Put16(out, 0xfffe);                                       // lightmap
    Put32(out, 0xff808080);                                   // colour
  }
  const std::int32_t surfaces[2][3] = {{0, -1, 1}, {1, -1, -1}};
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < 4; ++i) PutFloat(out, static_cast<float>(c * 4 + i));
    for (std::int32_t s : surfaces[c]) {
      if (minor == 7) {
        Put32(out, static_cast<std::uint32_t>(s));
      } else {
        Put16(out, static_cast<std::uint32_t>(s));
      }
    }
    if (minor != 7) Put16(out, 0);
  }
  return out;
}

}  // namespace

TEST_CASE("every minor version gives the same ground") {
  for (int minor : {5, 6, 7}) {
    INFO("minor " << minor);
    Ground ground;
    std::string error;
    REQUIRE(DecodeGround(Gnd(static_cast<std::uint8_t>(minor)), ground, &error));
    CHECK(ground.minor == minor);
    CHECK(ground.width == 2);
    CHECK(ground.height == 1);
    CHECK(ground.cell_size == 10.0f);
    CHECK(ground.textures == std::vector<std::string>{"a.bmp", "b.bmp"});
    REQUIRE(ground.lightmaps.size() == 1);
    CHECK(ground.lightmaps[0].alpha[63] == 63);
    CHECK(ground.lightmaps[0].color[0] == 64);
    REQUIRE(ground.surfaces.size() == 2);
    CHECK(ground.surfaces[0].texture == -1);
    CHECK(ground.surfaces[0].lightmap == 0xfffe);  // unsigned, as the builder reads it
    CHECK(ground.surfaces[1].u[1] == 1.125f);
    CHECK(ground.surfaces[1].v[0] == 1.5f);
    CHECK(ground.surfaces[1].color == 0xff808080);
    REQUIRE(ground.cells.size() == 2);
    CHECK(ground.cells[1].heights[3] == 7.0f);
    CHECK(ground.cells[0].surfaces == std::array<std::int32_t, 3>{0, -1, 1});
    CHECK(ground.cells[1].surfaces == std::array<std::int32_t, 3>{1, -1, -1});
  }
}

TEST_CASE("grounds the original refuses, cannot use, or reads past") {
  Ground ground;
  std::string error;
  CHECK_FALSE(DecodeGround(Gnd(8), ground, &error));
  CHECK(error == "an unsupported version");

  std::vector<std::uint8_t> unversioned = {2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  unversioned.resize(unversioned.size() + 160 + 0x18, 0);
  CHECK_FALSE(DecodeGround(unversioned, ground, &error));

  auto unterminated = Gnd(7);
  std::memset(&unterminated[26], 'x', 80);
  CHECK_FALSE(DecodeGround(unterminated, ground, &error));
  CHECK(error == "a texture name without a NUL in its record");

  const auto whole = Gnd(7);
  for (std::size_t size = 0; size < whole.size(); ++size) {
    INFO("size " << size);
    CHECK_FALSE(DecodeGround(std::span(whole.data(), size), ground, &error));
  }
}

TEST_CASE("GndRes loads through the resource manager") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_gnd_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data");
  {
    const auto gnd = Gnd(7);
    std::ofstream f(dir / "data" / "prontera.gnd", std::ios::binary);
    f.write(reinterpret_cast<const char*>(gnd.data()), static_cast<std::streamsize>(gnd.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterGroundTypes(manager);
  auto* prontera = static_cast<GndRes*>(manager.Get("Prontera.gnd"));
  REQUIRE(prontera != nullptr);
  CHECK(prontera->key().name == "prontera.gnd");
  CHECK(prontera->ground().cells.size() == 2);
  CHECK(manager.Get("missing.gnd") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
