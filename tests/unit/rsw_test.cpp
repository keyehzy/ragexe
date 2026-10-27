#include "res/rsw.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
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
  std::uint32_t u = 0;
  std::memcpy(&u, &f, 4);
  Put32(out, u);
}

void PutName(std::vector<std::uint8_t>& out, const char* name, std::size_t size) {
  std::vector<std::uint8_t> record(size, 0);
  std::memcpy(record.data(), name, std::strlen(name));
  out.insert(out.end(), record.begin(), record.end());
}

bool AtLeast(std::uint8_t major, std::uint8_t minor, std::uint8_t ma, std::uint8_t mi) {
  return major > ma || (major == ma && minor >= mi);
}

// A world of the given version with one model of the given animation speed and, for 2.1,
// scene-graph bounds whose floats count up from 0.
std::vector<std::uint8_t> Rsw(std::uint8_t major, std::uint8_t minor, float speed = 2.0f) {
  std::vector<std::uint8_t> out = {'G', 'R', 'S', 'W', major, minor};
  PutName(out, "tool.ini", 40);
  PutName(out, "prt.gnd", 40);
  if (AtLeast(major, minor, 1, 4)) PutName(out, "prt.gat", 40);
  PutName(out, "prt.src", 40);
  if (AtLeast(major, minor, 1, 3)) PutFloat(out, -5.0f);
  if (AtLeast(major, minor, 1, 8)) {
    Put32(out, 2);
    for (float f : {0.5f, 3.0f, 60.0f}) PutFloat(out, f);
  }
  if (AtLeast(major, minor, 1, 9)) Put32(out, 4);
  if (AtLeast(major, minor, 1, 5)) {
    Put32(out, 0);   // angle about y
    Put32(out, 90);  // angle about x
    for (float f : {1.0f, 0.9f, 0.8f, 0.8f, 0.5f, 0.2f}) PutFloat(out, f);
  }
  if (AtLeast(major, minor, 1, 7)) PutFloat(out, 0.5f);
  if (AtLeast(major, minor, 1, 6)) {
    for (std::uint32_t v : {1u, 2u, 3u, 4u}) Put32(out, v);
  }
  Put32(out, 3);  // objects
  Put32(out, 1);
  if (AtLeast(major, minor, 1, 3)) {
    PutName(out, "tree", 40);
    Put32(out, 0);
    PutFloat(out, speed);
    Put32(out, 0);
  }
  PutName(out, "tree.rsm", 80);
  PutName(out, "trunk", 80);
  for (int i = 0; i < 9; ++i) PutFloat(out, static_cast<float>(i));
  Put32(out, 2);  // ignored
  out.resize(out.size() + 0x6c, 7);
  Put32(out, 4);
  for (int i = 0; i < 0x74; ++i) out.push_back(static_cast<std::uint8_t>(i));
  if (major == 2 && minor == 1) {
    for (int i = 0; i < 1365 * 12; ++i) PutFloat(out, static_cast<float>(i));
  }
  return out;
}

}  // namespace

TEST_CASE("a 2.1 world") {
  World world;
  std::string error;
  REQUIRE(DecodeWorld(Rsw(2, 1), true, world, &error));
  CHECK(world.ground == "prt.gnd");
  CHECK(world.attr == "prt.gat");
  CHECK(world.extra == "prt.src");
  CHECK(world.water_level == -5.0f);
  CHECK(world.water_type == 2);
  CHECK(world.water_phase_step == 3.0f);
  CHECK(world.water_frame_interval == 4);
  CHECK(world.diffuse == std::array<float, 3>{1.0f, 0.9f, 0.8f});
  CHECK(world.ambient == std::array<float, 3>{0.8f, 0.5f, 0.2f});
  CHECK(world.field_1b0 == std::array<std::int32_t, 4>{1, 2, 3, 4});
  REQUIRE(world.models.size() == 1);
  const WorldModel& model = world.models[0];
  CHECK(model.name == "tree");
  CHECK(model.file == "tree.rsm");
  CHECK(model.node == "trunk");
  CHECK(model.position == std::array<float, 3>{0.0f, 1.0f, 2.0f});
  CHECK(model.scale == std::array<float, 3>{6.0f, 7.0f, 8.0f});
  CHECK(model.animation_speed == 2.0f);
  CHECK(world.sounds.empty());
  REQUIRE(world.effects.size() == 1);
  CHECK(world.effects[0][0x73] == 0x73);
  REQUIRE(world.bounds.size() == 1365);
  CHECK(world.bounds[0].max == std::array<float, 3>{0.0f, 1.0f, 2.0f});
  CHECK(world.bounds[1364].centre[2] == 1364.0f * 12 + 11);
}

TEST_CASE("older versions take defaults, and below 1.3 a model takes the last name read") {
  World world;
  std::string error;
  REQUIRE(DecodeWorld(Rsw(1, 2), true, world, &error));
  CHECK(world.attr.empty());
  CHECK(world.extra == "prt.src");
  CHECK(world.water_level == 0.0f);
  CHECK(world.water_frame_interval == 3);
  CHECK(world.light_angle_x == 45);
  CHECK(world.ambient == std::array<float, 3>{0.3f, 0.3f, 0.3f});
  CHECK(world.field_1b0 == std::array<std::int32_t, 4>{-500, 500, -500, 500});
  REQUIRE(world.models.size() == 1);
  CHECK(world.models[0].name == "prt.src");
  CHECK(world.models[0].animation_speed == 1.0f);
  CHECK(world.bounds.empty());
}

TEST_CASE("animation speeds above 100 or not above 0 become 1") {
  for (float speed : {150.0f, 0.0f, -1.0f, std::nanf("")}) {
    World world;
    std::string error;
    REQUIRE(DecodeWorld(Rsw(2, 0, speed), true, world, &error));
    CHECK(world.models[0].animation_speed == 1.0f);
  }
}

TEST_CASE("without lightmaps the ambient colour is brightened by half, up to 1") {
  World world;
  std::string error;
  REQUIRE(DecodeWorld(Rsw(2, 0), false, world, &error));
  CHECK(world.ambient[0] == 1.0f);
  CHECK(world.ambient[1] == 0.75f);
  CHECK(world.ambient[2] == static_cast<float>(static_cast<double>(0.2f) * 1.5));
}

TEST_CASE("the light vector is the second row of the rotation") {
  // About x by 90 degrees and y by 0: the row (0, cos, sin) is (0, 0, 1).
  const auto v = WorldLightVector(0, 90);
  CHECK(std::fabs(v[0]) < 1e-6f);
  CHECK(std::fabs(v[1]) < 1e-6f);
  CHECK(v[2] == 1.0f);
  const auto d = WorldLightVector(45, 45);
  CHECK(std::fabs(d[0] - 0.5f) < 1e-6f);
  CHECK(std::fabs(d[1] - 0.70710678f) < 1e-6f);
  CHECK(std::fabs(d[2] - 0.5f) < 1e-6f);
}

TEST_CASE("worlds the original refuses, and truncated ones") {
  World world;
  std::string error;
  auto bad = Rsw(2, 1);
  bad[0] = 'X';
  CHECK_FALSE(DecodeWorld(bad, true, world, &error));
  CHECK(error == "Invalid Format");
  CHECK_FALSE(DecodeWorld(Rsw(2, 2), true, world, &error));
  CHECK(error == "C3dWorldRes :: Unsupported Version");
  const auto whole = Rsw(2, 0);
  for (std::size_t size = 0; size < whole.size(); ++size) {
    INFO("size " << size);
    CHECK_FALSE(DecodeWorld(std::span(whole.data(), size), true, world, &error));
  }
}

TEST_CASE("RswRes loads through the resource manager") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_rsw_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data");
  {
    const auto rsw = Rsw(2, 0);
    std::ofstream f(dir / "data" / "prontera.rsw", std::ios::binary);
    f.write(reinterpret_cast<const char*>(rsw.data()), static_cast<std::streamsize>(rsw.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterWorldTypes(manager, false);
  auto* prontera = static_cast<RswRes*>(manager.Get("Prontera.rsw"));
  REQUIRE(prontera != nullptr);
  CHECK(prontera->world().ambient[1] == 0.75f);
  CHECK(manager.Get("missing.rsw") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
