#include "res/palette.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "res/data_files.h"
#include "res/resource.h"

using namespace ragexe::res;

namespace {

std::vector<std::uint8_t> PalFile(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  std::vector<std::uint8_t> out;
  for (int i = 0; i < 256; ++i) out.insert(out.end(), {r, g, b, 0x77});
  return out;
}

}  // namespace

TEST_CASE("colours become ARGB1555 with the alpha bit set") {
  const std::vector<std::uint8_t> entries = {0xff, 0x00, 0x00, 0,    0x00, 0xff, 0x00,
                                             0,    0x00, 0x00, 0xff, 0,    0x07, 0x07,
                                             0x07, 9,    0xff, 0xff, 0xff, 0};
  std::vector<std::uint32_t> out(5);
  ConvertColorsToArgb1555(entries, out);
  CHECK(out == std::vector<std::uint32_t>{0xfc00, 0x83e0, 0x801f, 0x8000, 0xffff});
}

TEST_CASE("magenta and its near neighbours become the transparent key") {
  std::vector<std::uint32_t> out(1);
  for (int r : {0xfe, 0xff}) {
    for (int g : {0, 1, 2, 3}) {
      for (int b : {0xfe, 0xff}) {
        const std::vector<std::uint8_t> entry = {static_cast<std::uint8_t>(r),
                                                 static_cast<std::uint8_t>(g),
                                                 static_cast<std::uint8_t>(b), 0};
        ConvertColorsToArgb1555(entry, out);
        CHECK(out[0] == kPaletteKey);
      }
    }
  }
  const std::vector<std::uint8_t> not_quite = {0xff, 4, 0xff, 0};
  ConvertColorsToArgb1555(not_quite, out);
  CHECK(out[0] == 0xfc1f);
}

TEST_CASE("PaletteRes reads the first 1024 bytes and refuses less") {
  PaletteRes palette;
  auto file = PalFile(0x10, 0x20, 0x30);
  file.resize(2000, 0xff);
  REQUIRE(palette.LoadFromBytes(file));
  CHECK(palette.palette()[0] == 0x8886);
  CHECK(palette.palette()[255] == 0x8886);
  file.resize(1023);
  CHECK_FALSE(palette.LoadFromBytes(file));
}

TEST_CASE("palettes load through the resource manager from palette\\") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_palette_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "palette");
  {
    const auto pal = PalFile(0xff, 0x00, 0xff);
    std::ofstream f(dir / "data" / "palette" / "red.pal", std::ios::binary);
    f.write(reinterpret_cast<const char*>(pal.data()), static_cast<std::streamsize>(pal.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterPaletteTypes(manager);
  auto* red = static_cast<PaletteRes*>(manager.Get("Red.pal"));
  REQUIRE(red != nullptr);
  CHECK(red->key().name == "palette\\red.pal");
  CHECK(red->palette()[7] == kPaletteKey);
  CHECK(manager.Get("missing.pal") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
