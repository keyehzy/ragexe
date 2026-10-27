#include "res/rsx.h"

#include <bit>
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
  for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}

void PutFloat(std::vector<std::uint8_t>& out, float f) {
  Put32(out, std::bit_cast<std::uint32_t>(f));
}

void PutName(std::vector<std::uint8_t>& out, const char* name) {
  std::vector<std::uint8_t> record(40, 0);
  std::memcpy(record.data(), name, std::strlen(name));
  out.insert(out.end(), record.begin(), record.end());
}

// An animated model of the given version with two frames: a root "door" of one triangle
// that moves up by 1 in frame 1, with per-frame texture coordinates, and a child "knob"
// of one point with shared ones.
std::vector<std::uint8_t> Rsx(std::uint8_t major, std::uint8_t minor,
                              std::uint32_t end_marker = 0x12345678) {
  std::vector<std::uint8_t> out = {'G', 'R', 'S', 'X', major, minor};
  if (major == 1 && minor > 1) {
    for (float f : {0.5f, 1.0f, 2.0f, 3.0f}) PutFloat(out, f);
    if (minor == 3) Put32(out, 128);
  }
  Put32(out, 1);  // +0x140
  Put32(out, 2);  // frames
  Put32(out, 160);
  out.resize(out.size() + 16, 0);
  Put32(out, 2);
  PutName(out, "door.bmp");
  PutName(out, "knob.bmp");
  PutName(out, "door");
  Put32(out, 2);
  for (int k = 0; k < 2; ++k) {
    Put32(out, 0x12345678);
    PutName(out, k == 0 ? "door" : "knob");
    PutName(out, k == 0 ? "" : "door");
    Put32(out, 1);
    Put32(out, static_cast<std::uint32_t>(k));  // texture
    const int vertices = k == 0 ? 3 : 1;
    Put32(out, static_cast<std::uint32_t>(vertices));
    for (int frame = 0; frame < 2; ++frame) {
      for (int v = 0; v < vertices; ++v) {
        PutFloat(out, static_cast<float>(v));
        PutFloat(out, static_cast<float>(frame));
        PutFloat(out, 0.0f);
      }
    }
    Put32(out, 1);                              // texture coordinates
    Put32(out, static_cast<std::uint32_t>(k));  // shared for the knob
    for (int set = 0; set < (k == 0 ? 2 : 1); ++set) {
      Put32(out, 0xffffffff);
      PutFloat(out, 0.25f * static_cast<float>(set));
      PutFloat(out, 0.5f);
    }
    Put32(out, k == 0 ? 1 : 0);  // faces
    if (k == 0) {
      for (std::uint32_t v : {0x00010000u, 0x00000002u, 0x00000000u}) Put32(out, v);
      for (int i = 0; i < 3; ++i) Put32(out, 0);
    }
    Put32(out, k == 1 ? end_marker : 0x12345678);
  }
  return out;
}

}  // namespace

TEST_CASE("an animated model: frames, shared texture coordinates, links") {
  for (auto [major, minor] : {std::pair{1, 0}, {1, 2}, {1, 3}, {2, 3}}) {
    INFO(major << "." << minor);
    AnimModel model;
    std::string error;
    REQUIRE(DecodeAnimModel(Rsx(static_cast<std::uint8_t>(major), static_cast<std::uint8_t>(minor)),
                            model, &error));
    const bool extra = major == 1 && minor > 1;
    CHECK(model.field_14c == (extra ? 0.5f : 1.0f));
    CHECK(model.field_154 == (extra ? Vec3{1.0f, 2.0f, 3.0f} : Vec3{}));
    CHECK(model.field_160 == (major == 1 && minor == 3 ? 128 : 255));
    CHECK(model.frame_count == 2);
    CHECK(model.root == "door");
    REQUIRE(model.nodes.size() == 2);
    const AnimModelNode& door = model.nodes[0];
    CHECK(door.children == std::vector<int>{1});
    CHECK(model.nodes[1].parent == 0);
    REQUIRE(door.vertices.size() == 6);
    CHECK(door.vertices[4] == Vec3{1.0f, 1.0f, 0.0f});  // frame 1, vertex 1
    CHECK(door.tex_coords.size() == 2);
    CHECK(door.tex_coords[1].u == 0.25f);
    CHECK(model.nodes[1].tex_coords.size() == 1);
    REQUIRE(door.faces.size() == 1);
    CHECK(door.faces[0].vertices == std::array<std::uint16_t, 3>{0, 1, 2});
  }
}

TEST_CASE("animated models the original refuses or reads past") {
  AnimModel model;
  std::string error;
  CHECK_FALSE(DecodeAnimModel(Rsx(1, 3, 0x12345677), model, &error));
  CHECK(error == "data corrupt");
  auto any_magic = Rsx(1, 3);
  std::memcpy(any_magic.data(), "ABCD", 4);
  CHECK(DecodeAnimModel(any_magic, model, &error));  // the magic is never checked
  const auto whole = Rsx(1, 3);
  for (std::size_t size = 0; size < whole.size(); ++size) {
    INFO("size " << size);
    CHECK_FALSE(DecodeAnimModel(std::span(whole.data(), size), model, &error));
  }
}

TEST_CASE("RsxRes loads from model\\ through the resource manager") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_rsx_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "model");
  {
    const auto rsx = Rsx(1, 3);
    std::ofstream f(dir / "data" / "model" / "door.rsx", std::ios::binary);
    f.write(reinterpret_cast<const char*>(rsx.data()), static_cast<std::streamsize>(rsx.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterAnimModelTypes(manager);
  auto* door = static_cast<RsxRes*>(manager.Get("Door.rsx"));
  REQUIRE(door != nullptr);
  CHECK(door->model().nodes.size() == 2);
  CHECK(manager.Get("missing.rsx") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
