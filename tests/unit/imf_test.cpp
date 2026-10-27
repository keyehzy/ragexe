#include "res/imf.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
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

// A file of the given version whose layers hold the given actions' frame counts; frame k
// of each action is (k % 2, 10 * k, -k).
std::vector<std::uint8_t> ImfFile(float version,
                                  const std::vector<std::vector<std::int32_t>>& layers) {
  std::vector<std::uint8_t> out;
  Put32(out, std::bit_cast<std::uint32_t>(version));
  Put32(out, 0);
  Put32(out, static_cast<std::uint32_t>(static_cast<std::int32_t>(layers.size()) - 1));
  for (const auto& actions : layers) {
    Put32(out, static_cast<std::uint32_t>(actions.size()));
    for (std::int32_t frames : actions) {
      Put32(out, static_cast<std::uint32_t>(frames));
      for (std::int32_t k = 0; k < frames; ++k) {
        for (std::int32_t v : {k % 2, 10 * k, -k}) Put32(out, static_cast<std::uint32_t>(v));
      }
    }
  }
  return out;
}

}  // namespace

TEST_CASE("layers of actions of frames") {
  Imf imf;
  std::string error;
  REQUIRE(DecodeImf(ImfFile(1.01f, {{2, 0, 3}, {1}}), imf, &error));
  REQUIRE(imf.layers.size() == 2);
  REQUIRE(imf.layers[0].size() == 3);
  CHECK(imf.layers[0][1].empty());
  REQUIRE(imf.layers[0][2].size() == 3);
  CHECK(imf.layers[0][2][1].order == 1);
  CHECK(imf.layers[0][2][2].x == 20);
  CHECK(imf.layers[0][2][2].y == -2);
  CHECK(imf.layers[1].size() == 1);
}

TEST_CASE("a negative layer or count reads nothing, as in the original") {
  Imf imf;
  std::string error;
  REQUIRE(DecodeImf(ImfFile(1.01f, {}), imf, &error));  // the last layer is -1
  CHECK(imf.layers.empty());
  REQUIRE(DecodeImf(ImfFile(1.01f, {{-4, 1}}), imf, &error));
  CHECK(imf.layers[0][0].empty());
  CHECK(imf.layers[0][1].size() == 1);
}

TEST_CASE("files the original refuses, overflows its tables with, or reads past") {
  Imf imf;
  std::string error;
  CHECK_FALSE(DecodeImf(ImfFile(1.0f, {{1}}), imf, &error));
  CHECK(error == "an old version's format");
  CHECK_FALSE(DecodeImf(ImfFile(std::nanf(""), {{1}}), imf, &error));
  CHECK(DecodeImf(ImfFile(1.01f, std::vector<std::vector<std::int32_t>>(15, {1})), imf, &error));
  CHECK_FALSE(
      DecodeImf(ImfFile(1.01f, std::vector<std::vector<std::int32_t>>(16, {1})), imf, &error));
  CHECK(DecodeImf(ImfFile(1.01f, {std::vector<std::int32_t>(104, 1)}), imf, &error));
  CHECK_FALSE(DecodeImf(ImfFile(1.01f, {std::vector<std::int32_t>(105, 1)}), imf, &error));
  const auto whole = ImfFile(1.01f, {{2, 1}, {3}});
  for (std::size_t size = 0; size < whole.size(); ++size) {
    INFO("size " << size);
    CHECK_FALSE(DecodeImf(std::span(whole.data(), size), imf, &error));
  }
}

TEST_CASE("ImfRes loads from imf\\ through the resource manager") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_imf_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "imf");
  {
    const auto bytes = ImfFile(1.01f, {{1}, {1}});
    std::ofstream f(dir / "data" / "imf" / "knight.imf", std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterImfTypes(manager);
  auto* knight = static_cast<ImfRes*>(manager.Get("Knight.imf"));
  REQUIRE(knight != nullptr);
  CHECK(knight->imf().layers.size() == 2);
  CHECK(manager.Get("missing.imf") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
