#include "res/sprite.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "res/data_files.h"
#include "res/palette.h"
#include "res/resource.h"

using namespace ragexe::res;

namespace {

void Put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

struct Image8 {
  std::uint16_t width;
  std::uint16_t height;
  std::vector<std::uint8_t> pixels;
};

struct Image32 {
  std::uint16_t width;
  std::uint16_t height;
  std::vector<std::uint32_t> pixels;  // top row first, ARGB
};

// A sprite file of the given version: raw images below 0x201, run-length ones at 0x201.
std::vector<std::uint8_t> Spr(std::uint16_t version, const std::vector<Image8>& images,
                              const std::vector<Image32>& images32 = {}) {
  std::vector<std::uint8_t> out = {'S', 'P'};
  Put16(out, version);
  Put16(out, static_cast<std::uint32_t>(images.size()));
  if (version >= 0x200) Put16(out, static_cast<std::uint32_t>(images32.size()));
  for (const Image8& image : images) {
    Put16(out, image.width);
    Put16(out, image.height);
    if (version < 0x201) {
      out.insert(out.end(), image.pixels.begin(), image.pixels.end());
    } else {
      const auto rle = EncodeSpriteRle(image.pixels, image.width, image.height);
      Put16(out, static_cast<std::uint32_t>(rle.size()));
      out.insert(out.end(), rle.begin(), rle.end());
    }
  }
  for (const Image32& image : images32) {
    Put16(out, image.width);
    Put16(out, image.height);
    for (std::size_t row = image.height; row-- > 0;) {
      for (std::size_t x = 0; x < image.width; ++x) {
        const std::uint32_t p = image.pixels[row * image.width + x];
        out.insert(out.end(),
                   {static_cast<std::uint8_t>(p >> 24), static_cast<std::uint8_t>(p),
                    static_cast<std::uint8_t>(p >> 8), static_cast<std::uint8_t>(p >> 16)});
      }
    }
  }
  if (version > 0x100) {
    for (int i = 0; i < 256; ++i) {
      out.insert(out.end(), {static_cast<std::uint8_t>(i), 0, 0, 0});
    }
  }
  return out;
}

}  // namespace

TEST_CASE("the zero-run encoding: runs of up to 255 zeros, other bytes as they are") {
  const std::vector<std::uint8_t> pixels = {5, 0, 0, 0, 7, 0};
  CHECK(EncodeSpriteRle(pixels, 3, 2) == std::vector<std::uint8_t>{5, 0, 3, 7, 0, 1});
  CHECK(DecodeSpriteRle(EncodeSpriteRle(pixels, 3, 2), 3, 2) == pixels);

  const std::vector<std::uint8_t> zeros(300, 0);
  CHECK(EncodeSpriteRle(zeros, 300, 1) == std::vector<std::uint8_t>{0, 255, 0, 45});
  CHECK(DecodeSpriteRle(EncodeSpriteRle(zeros, 300, 1), 300, 1) == zeros);
}

TEST_CASE("the decoder: a 0 with a count of 0 is one zero; data that runs out reads as 0") {
  CHECK(DecodeSpriteRle(std::vector<std::uint8_t>{0, 0, 9}, 2, 1) ==
        std::vector<std::uint8_t>{0, 9});
  CHECK(DecodeSpriteRle(std::vector<std::uint8_t>{4}, 3, 1) == std::vector<std::uint8_t>{4, 0, 0});
  CHECK(DecodeSpriteRle(std::vector<std::uint8_t>{0, 5}, 2, 1) == std::vector<std::uint8_t>{0, 0});
}

TEST_CASE("the encoder's pixel index and size are 16-bit, as in the original") {
  // 300 x 300 pixels: past index 65535 the encoder reads from the start again.
  std::vector<std::uint8_t> pixels(std::size_t{300} * 300);
  for (std::size_t i = 0; i < pixels.size(); ++i)
    pixels[i] = static_cast<std::uint8_t>(i % 251 + 1);
  const auto rle = EncodeSpriteRle(pixels, 300, 300);
  CHECK(rle.size() == (90000u & 0xffffu));
  CHECK(rle[65536 % 65536] == pixels[0]);
}

TEST_CASE("ShrinkImage keeps every second column and row") {
  const std::vector<std::uint8_t> pixels = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};  // 4 x 3
  CHECK(ShrinkSpriteImage(pixels, 4, 3, true, false) ==
        std::vector<std::uint8_t>{1, 3, 5, 7, 9, 11});
  CHECK(ShrinkSpriteImage(pixels, 4, 3, false, true) == std::vector<std::uint8_t>{1, 2, 3, 4});
  CHECK(ShrinkSpriteImage(pixels, 4, 3, true, true) == std::vector<std::uint8_t>{1, 3});
}

TEST_CASE("sprites of versions 0x200 and 0x201 decode to the same images") {
  const Image8 a{3, 2, {1, 0, 0, 0, 0, 2}};
  const Image8 b{2, 2, {7, 7, 0, 7}};
  const Image32 t{2, 2, {0x80112233, 0x40445566, 0x20778899, 0x10aabbcc}};
  for (int version : {0x200, 0x201}) {
    Sprite sprite;
    std::string error;
    REQUIRE(
        DecodeSprite(Spr(static_cast<std::uint16_t>(version), {a, b}, {t}), {}, sprite, &error));
    CHECK(sprite.version == version);
    REQUIRE(sprite.images.size() == 2);
    CHECK(sprite.images[0].Decode() == a.pixels);
    CHECK(sprite.images[1].Decode() == b.pixels);
    CHECK(sprite.images[0].width == 3);
    CHECK_FALSE(sprite.images[0].halved_x);
    REQUIRE(sprite.images32.size() == 1);
    CHECK(sprite.images32[0].pixels == t.pixels);
    CHECK(sprite.palette[0] == 0x8000);
    CHECK(sprite.palette[255] == (0x8000u | (0xf8u << 7)));
  }
}

TEST_CASE("at a lower detail level images are halved and flagged") {
  const Image8 a{4, 3, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}};
  const Image32 t{4, 2, std::vector<std::uint32_t>(8, 0xff000000)};
  Sprite sprite;
  std::string error;
  REQUIRE(DecodeSprite(Spr(0x201, {a}, {t}), {true, true}, sprite, &error));
  CHECK(sprite.images[0].width == 2);
  CHECK(sprite.images[0].height == 1);
  CHECK(sprite.images[0].halved_x);
  CHECK(sprite.images[0].halved_y);
  CHECK(sprite.images[0].Decode() == std::vector<std::uint8_t>{1, 3});
  CHECK(sprite.images32[0].width == 2);
  CHECK(sprite.images32[0].height == 1);
  CHECK(sprite.images32[0].pixel_width == 4);
  CHECK(sprite.images32[0].pixels.size() == 8);
}

TEST_CASE("sprites the original refuses, and truncated ones") {
  Sprite sprite;
  std::string error;
  auto bad = Spr(0x201, {});
  bad[0] = 'X';
  CHECK_FALSE(DecodeSprite(bad, {}, sprite, &error));
  CHECK(error == "Illegal File Format");
  CHECK_FALSE(DecodeSprite(Spr(0x202, {}), {}, sprite, &error));
  CHECK(error == "Unsupported Version. Content may be corrupted");
  std::vector<std::uint8_t> huge = {'S',  'P',  0x01, 0x02, 1, 0, 0, 0,
                                    0xff, 0x7f, 0xff, 0x7f, 2, 0, 0, 0xff};
  huge.resize(huge.size() + 1024);
  CHECK_FALSE(DecodeSprite(huge, {}, sprite, &error));
  CHECK(error == "the run-length data cannot cover the image");
  auto truncated = Spr(0x200, {{8, 8, std::vector<std::uint8_t>(64, 1)}});
  truncated.resize(40);
  CHECK_FALSE(DecodeSprite(truncated, {}, sprite, &error));
}

TEST_CASE("SprRes loads through the resource manager at its detail level") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_sprite_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "sprite");
  {
    const auto spr = Spr(0x201, {{4, 2, {1, 2, 3, 4, 5, 6, 7, 8}}});
    std::ofstream f(dir / "data" / "sprite" / "poring.spr", std::ios::binary);
    f.write(reinterpret_cast<const char*>(spr.data()), static_cast<std::streamsize>(spr.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterSpriteTypes(manager, {true, false});
  auto* poring = static_cast<SprRes*>(manager.Get("Poring.spr"));
  REQUIRE(poring != nullptr);
  CHECK(poring->key().name == "sprite\\poring.spr");
  CHECK(poring->sprite().images[0].width == 2);
  CHECK(poring->sprite().images[0].Decode() == std::vector<std::uint8_t>{1, 3, 5, 7});
  CHECK(manager.Get("missing.spr") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
