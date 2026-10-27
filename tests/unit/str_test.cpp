#include "res/str.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "res/data_files.h"
#include "res/resource.h"

using namespace ragexe::res;

namespace {

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}

// An effect whose layers have the given texture and key counts; texture k is "t<k>.bmp"
// and key k is at frame 10·k with type k % 2 and values k, k+1, ….
std::vector<std::uint8_t> StrFile(const std::vector<std::pair<std::int32_t, std::int32_t>>& layers,
                                  std::int32_t version = 0x94) {
  std::vector<std::uint8_t> out = {'S', 'T', 'R', 'M'};
  Put32(out, static_cast<std::uint32_t>(version));
  Put32(out, 60);
  Put32(out, 90);
  Put32(out, static_cast<std::uint32_t>(layers.size()));
  out.resize(out.size() + 16, 0);
  for (auto [textures, keys] : layers) {
    Put32(out, static_cast<std::uint32_t>(textures));
    for (std::int32_t k = 0; k < textures; ++k) {
      std::vector<std::uint8_t> name(0x80, 0);
      const std::string text = "t" + std::to_string(k) + ".bmp";
      std::memcpy(name.data(), text.data(), text.size());
      out.insert(out.end(), name.begin(), name.end());
    }
    Put32(out, static_cast<std::uint32_t>(keys));
    for (std::int32_t k = 0; k < keys; ++k) {
      Put32(out, static_cast<std::uint32_t>(10 * k));
      Put32(out, static_cast<std::uint32_t>(k % 2));
      for (std::int32_t v = 0; v < 29; ++v) Put32(out, static_cast<std::uint32_t>(k + v));
    }
  }
  return out;
}

}  // namespace

TEST_CASE("an effect: header, layers, texture names and keys") {
  Str str;
  std::string error;
  REQUIRE(DecodeStr(StrFile({{2, 3}, {0, 1}}), str, &error));
  CHECK(str.field_114 == 60);
  CHECK(str.field_110 == 90);
  REQUIRE(str.layers.size() == 2);
  CHECK(str.layers[0].textures == std::vector<std::string>{"t0.bmp", "t1.bmp"});
  REQUIRE(str.layers[0].keys.size() == 3);
  CHECK(str.layers[0].keys[2].frame == 20);
  CHECK(str.layers[0].keys[1].type == 1);
  CHECK(str.layers[0].keys[2].values[28] == 30u);
  CHECK(str.layers[1].textures.empty());
}

TEST_CASE("negative counts read nothing, as in the original") {
  Str str;
  std::string error;
  REQUIRE(DecodeStr(StrFile({{-2, -5}, {1, 1}}), str, &error));
  CHECK(str.layers[0].texture_count == -2);
  CHECK(str.layers[0].keys.empty());
  CHECK(str.layers[1].keys.size() == 1);
}

TEST_CASE("effects the original refuses, overflows its array with, or reads past") {
  Str str;
  std::string error;
  CHECK_FALSE(DecodeStr(StrFile({{1, 1}}, 0x93), str, &error));
  CHECK(error == "LoadAniClip: Version is incorrect");
  CHECK(DecodeStr(StrFile(std::vector<std::pair<std::int32_t, std::int32_t>>(60, {0, 0})), str,
                  &error));
  CHECK_FALSE(DecodeStr(StrFile(std::vector<std::pair<std::int32_t, std::int32_t>>(61, {0, 0})),
                        str, &error));
  CHECK(DecodeStr(StrFile({{110, 0}}), str, &error));
  CHECK_FALSE(DecodeStr(StrFile({{111, 0}}), str, &error));
  const auto whole = StrFile({{1, 2}, {2, 1}});
  for (std::size_t size = 0; size < whole.size(); ++size) {
    INFO("size " << size);
    CHECK_FALSE(DecodeStr(std::span(whole.data(), size), str, &error));
  }
}

TEST_CASE("StrRes loads from texture\\effect\\ through the resource manager") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_str_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "texture" / "effect");
  {
    const auto bytes = StrFile({{1, 1}});
    std::ofstream f(dir / "data" / "texture" / "effect" / "spark.str", std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterStrTypes(manager);
  auto* spark = static_cast<StrRes*>(manager.Get("Spark.str"));
  REQUIRE(spark != nullptr);
  CHECK(spark->str().layers.size() == 1);
  CHECK(manager.Get("missing.str") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
