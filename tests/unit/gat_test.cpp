#include "res/gat.h"

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

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

std::vector<std::uint8_t> Gat(std::uint8_t major, std::uint8_t minor, std::int32_t width,
                              std::int32_t height) {
  std::vector<std::uint8_t> out = {'G', 'R', 'A', 'T', major, minor};
  Put32(out, static_cast<std::uint32_t>(width));
  Put32(out, static_cast<std::uint32_t>(height));
  for (std::int32_t i = 0; i < width * height; ++i) {
    for (int corner = 0; corner < 4; ++corner) {
      const float h = static_cast<float>(i) + 0.25f * static_cast<float>(corner);
      std::uint32_t bits = 0;
      std::memcpy(&bits, &h, 4);
      Put32(out, bits);
    }
    Put32(out, static_cast<std::uint32_t>(i % 2));
  }
  return out;
}

}  // namespace

TEST_CASE("attribute maps: cells row by row, four heights and an attribute") {
  AttrMap map;
  std::string error;
  REQUIRE(DecodeAttrMap(Gat(1, 2, 3, 2), map, &error));
  CHECK(map.width == 3);
  CHECK(map.height == 2);
  CHECK(map.cell_size == 5);
  REQUIRE(map.cells.size() == 6);
  CHECK(map.cells[4].heights[0] == 4.0f);
  CHECK(map.cells[4].heights[3] == 4.75f);
  CHECK(map.cells[5].attribute == 1);
}

TEST_CASE("an unsupported version still loads, empty, as in the original") {
  AttrMap map;
  std::string error;
  std::string warning;
  for (auto [major, minor] : {std::pair{2, 0}, std::pair{1, 3}, std::pair{0, 9}}) {
    REQUIRE(
        DecodeAttrMap(Gat(static_cast<std::uint8_t>(major), static_cast<std::uint8_t>(minor), 2, 2),
                      map, &error, &warning));
    CHECK(warning == "C3dAttr::Unsupported Version");
    CHECK(map.cells.empty());
    CHECK(map.width == 0);
  }
}

TEST_CASE("attribute maps the port refuses") {
  AttrMap map;
  std::string error;
  auto bad = Gat(1, 2, 1, 1);
  bad[0] = 'X';
  CHECK_FALSE(DecodeAttrMap(bad, map, &error));
  auto short_file = Gat(1, 2, 4, 4);
  short_file.resize(short_file.size() - 1);
  CHECK_FALSE(DecodeAttrMap(short_file, map, &error));
  CHECK_FALSE(DecodeAttrMap(Gat(1, 2, -1, 4), map, &error));
}

TEST_CASE("attribute maps load through the resource manager with no folder") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_gat_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data");
  {
    const auto gat = Gat(1, 2, 2, 2);
    std::ofstream f(dir / "data" / "prontera.gat", std::ios::binary);
    f.write(reinterpret_cast<const char*>(gat.data()), static_cast<std::streamsize>(gat.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterAttrTypes(manager);
  auto* map = static_cast<AttrRes*>(manager.Get("Prontera.gat"));
  REQUIRE(map != nullptr);
  CHECK(map->key().name == "prontera.gat");
  CHECK(map->map().cells.size() == 4);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
