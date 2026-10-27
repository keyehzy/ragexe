#include "res/bitmap.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
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

// A BMP with a 40-byte info header; rows are given bottom row first, already padded.
std::vector<std::uint8_t> Bmp(std::int32_t width, std::int32_t height, std::uint16_t bits,
                              const std::vector<std::uint32_t>& palette,
                              const std::vector<std::uint8_t>& rows) {
  std::vector<std::uint8_t> out = {'B', 'M'};
  const auto offset = static_cast<std::uint32_t>(54 + palette.size() * 4);
  Put32(out, offset + static_cast<std::uint32_t>(rows.size()));
  Put32(out, 0);
  Put32(out, offset);
  Put32(out, 40);
  Put32(out, static_cast<std::uint32_t>(width));
  Put32(out, static_cast<std::uint32_t>(height));
  Put16(out, 1);
  Put16(out, bits);
  Put32(out, 0);
  Put32(out, static_cast<std::uint32_t>(rows.size()));
  Put32(out, 2835);
  Put32(out, 2835);
  Put32(out, static_cast<std::uint32_t>(palette.size()));
  Put32(out, 0);
  for (std::uint32_t c : palette) Put32(out, c);  // B, G, R, 0 in memory
  out.insert(out.end(), rows.begin(), rows.end());
  return out;
}

std::vector<std::uint8_t> TgaHeader(std::uint8_t type, std::uint16_t width, std::uint16_t height,
                                    std::uint8_t depth, std::uint8_t id_length = 0) {
  std::vector<std::uint8_t> out = {id_length, 0, type, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Put16(out, width);
  Put16(out, height);
  out.push_back(depth);
  out.push_back(0);
  for (std::uint8_t i = 0; i < id_length; ++i) out.push_back(0xee);
  return out;
}

void AppendBgra(std::vector<std::uint8_t>& out, std::uint32_t argb, bool alpha) {
  out.push_back(static_cast<std::uint8_t>(argb));
  out.push_back(static_cast<std::uint8_t>(argb >> 8));
  out.push_back(static_cast<std::uint8_t>(argb >> 16));
  if (alpha) out.push_back(static_cast<std::uint8_t>(argb >> 24));
}

std::vector<std::uint8_t> Jpeg(int width, int height, std::uint8_t r, std::uint8_t g,
                               std::uint8_t b) {
  std::vector<std::uint8_t> rgb;
  for (int i = 0; i < width * height; ++i) rgb.insert(rgb.end(), {r, g, b});
  std::vector<std::uint8_t> out;
  stbi_write_jpg_to_func(
      [](void* context, void* data, int size) {
        auto* bytes = static_cast<std::vector<std::uint8_t>*>(context);
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes->insert(bytes->end(), p, p + size);
      },
      &out, width, height, 3, rgb.data(), 100);
  return out;
}

}  // namespace

TEST_CASE("24-bit BMP rows are padded to 4 bytes and stored bottom-up") {
  // 3 x 2: 9 bytes of pixels and 3 of padding per row.
  const std::vector<std::uint8_t> rows = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0, 0, 0,  // bottom row
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0, 0, 0,  // top row
  };
  Bitmap bitmap;
  std::string error;
  REQUIRE(DecodeBmp(Bmp(3, 2, 24, {}, rows), bitmap, &error));
  CHECK(bitmap.width == 3);
  CHECK(bitmap.height == 2);
  CHECK_FALSE(bitmap.has_alpha);
  const std::vector<std::uint32_t> want = {0xff131211, 0xff161514, 0xff191817,
                                           0xff030201, 0xff060504, 0xff090807};
  CHECK(bitmap.pixels == want);
}

TEST_CASE("8-bit BMP pixels go through the palette; entries past the count are 0") {
  // 5 x 1: one row of 5 indices padded to 8; the palette has 2 entries.
  const std::vector<std::uint8_t> rows = {0, 1, 1, 0, 7, 0xaa, 0xaa, 0xaa};
  Bitmap bitmap;
  std::string error;
  REQUIRE(DecodeBmp(Bmp(5, 1, 8, {0x00102030, 0x00405060}, rows), bitmap, &error));
  const std::vector<std::uint32_t> want = {0xff102030, 0xff405060, 0xff405060, 0xff102030,
                                           0xff000000};
  CHECK(bitmap.pixels == want);
}

TEST_CASE("BMPs the original refuses, and ones the port refuses to stay in bounds") {
  Bitmap bitmap;
  std::string error;
  CHECK_FALSE(DecodeBmp(Bmp(2, 2, 4, std::vector<std::uint32_t>(16), std::vector<std::uint8_t>(8)),
                        bitmap, &error));
  CHECK(error == "Only 16M or 8bit color format supported");
  auto not_bmp = Bmp(1, 1, 24, {}, {1, 2, 3, 0});
  not_bmp[0] = 'X';
  CHECK_FALSE(DecodeBmp(not_bmp, bitmap, &error));
  CHECK_FALSE(DecodeBmp(Bmp(4, 4, 24, {}, std::vector<std::uint8_t>(10)), bitmap, &error));
  CHECK_FALSE(
      DecodeBmp(Bmp(1, 1, 8, std::vector<std::uint32_t>(300), {0, 0, 0, 0}), bitmap, &error));
  CHECK_FALSE(DecodeBmp(Bmp(1, -1, 24, {}, {1, 2, 3, 0}), bitmap, &error));
  REQUIRE(DecodeBmp(Bmp(0, 5, 24, {}, {}), bitmap, &error));
  CHECK(bitmap.pixels.empty());
}

TEST_CASE("32-bit TGAs keep their alpha; 24-bit ones are opaque") {
  auto tga = TgaHeader(2, 2, 2, 32, 3);
  for (std::uint32_t argb : {0x80010203u, 0x40040506u, 0x20070809u, 0x100a0b0cu}) {
    AppendBgra(tga, argb, true);
  }
  Bitmap bitmap;
  std::string error;
  REQUIRE(DecodeTga(tga, bitmap, &error));
  CHECK(bitmap.has_alpha);
  const std::vector<std::uint32_t> want = {0x20070809, 0x100a0b0c, 0x80010203, 0x40040506};
  CHECK(bitmap.pixels == want);

  auto opaque = TgaHeader(2, 1, 1, 24);
  AppendBgra(opaque, 0x00112233, false);
  REQUIRE(DecodeTga(opaque, bitmap, &error));
  CHECK(bitmap.pixels == std::vector<std::uint32_t>{0xff112233});
  CHECK(bitmap.has_alpha);
}

TEST_CASE("run-length TGA packets, including one that runs past its row") {
  // 2 x 2. The bottom row (stored first) is a run of 3: two pixels fill it and the third
  // would land past the image, so it is dropped. The top row is a literal of 1 and a run
  // of 2 whose last pixel lands on the bottom row's first, as in the original.
  auto tga = TgaHeader(10, 2, 2, 32);
  tga.push_back(0x80 | 2);
  AppendBgra(tga, 0xff0000aa, true);
  tga.push_back(0);
  AppendBgra(tga, 0xff0000bb, true);
  tga.push_back(0x80 | 1);
  AppendBgra(tga, 0xff0000cc, true);
  Bitmap bitmap;
  std::string error;
  REQUIRE(DecodeTga(tga, bitmap, &error));
  const std::vector<std::uint32_t> want = {0xff0000bb, 0xff0000cc, 0xff0000cc, 0xff0000aa};
  CHECK(bitmap.pixels == want);
}

TEST_CASE("TGAs the original refuses, and truncated ones") {
  Bitmap bitmap;
  std::string error;
  CHECK_FALSE(DecodeTga(TgaHeader(1, 1, 1, 8), bitmap, &error));
  auto with_map = TgaHeader(2, 1, 1, 24);
  with_map[1] = 1;
  CHECK_FALSE(DecodeTga(with_map, bitmap, &error));
  CHECK_FALSE(DecodeTga(TgaHeader(2, 4, 4, 32), bitmap, &error));
  auto rle = TgaHeader(10, 4, 1, 32);
  rle.push_back(3);
  CHECK_FALSE(DecodeTga(rle, bitmap, &error));
}

TEST_CASE("JPEGs decode, and magenta becomes transparent") {
  Bitmap bitmap;
  std::string error;
  REQUIRE(DecodeJpeg(Jpeg(16, 8, 100, 150, 200), bitmap, &error));
  CHECK(bitmap.width == 16);
  CHECK(bitmap.height == 8);
  CHECK_FALSE(bitmap.has_alpha);
  for (std::uint32_t pixel : bitmap.pixels) {
    CHECK(pixel >> 24 == 0xff);
    CHECK(static_cast<int>(pixel >> 16 & 0xff) - 100 <= 2);
    CHECK(100 - static_cast<int>(pixel >> 16 & 0xff) <= 2);
  }
  REQUIRE(DecodeJpeg(Jpeg(8, 8, 255, 0, 255), bitmap, &error));
  for (std::uint32_t pixel : bitmap.pixels) CHECK(pixel == 0x000a0a0a);
  CHECK_FALSE(DecodeJpeg(std::vector<std::uint8_t>{0xff, 0xd8, 0xff}, bitmap, &error));
}

TEST_CASE("the decoder is chosen by the name's extension") {
  Bitmap bitmap;
  std::string error;
  CHECK(DecodeBitmap("texture\\A.BMP", Bmp(1, 1, 24, {}, {1, 2, 3, 0}), bitmap, &error));
  CHECK_FALSE(DecodeBitmap("texture\\a.png", {}, bitmap, &error));
  CHECK(error == "Unsupport fileformat!");
  CHECK_FALSE(DecodeBitmap("noextension", {}, bitmap, &error));
}

TEST_CASE("alpha from a mask pixel, as ApplyAlphaBitmap computes it") {
  // From the disassembly (00427a9a): an average that ORs where it should mask.
  CHECK(AlphaFromMask(0xffffffff) == 0xa9);
  CHECK(AlphaFromMask(0xff000000) == 0xa9);
  CHECK(AlphaFromMask(0xff808080) == 0xa9);
  CHECK(AlphaFromMask(0x00ff0000) == 0xff);
  CHECK(AlphaFromMask(0x00000000) == 0xff);
}

TEST_CASE("BitmapRes: through the resource manager, Create and GetPixel") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_bitmap_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "texture");
  {
    const auto bmp = Bmp(1, 1, 24, {}, {0x80, 0x80, 0x80, 0});
    std::ofstream f(dir / "data" / "texture" / "dot.bmp", std::ios::binary);
    f.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterBitmapTypes(manager);
  auto* dot = static_cast<BitmapRes*>(manager.Get("DOT.BMP"));
  REQUIRE(dot != nullptr);
  CHECK(dot->key().name == "texture\\dot.bmp");
  CHECK(dot->GetPixel(0, 0) == 0xff808080);
  CHECK(dot->GetPixel(1, 0) == 0x00ff0000);
  CHECK(dot->GetPixel(0, 1) == 0x00ff0000);
  CHECK(manager.Get("missing.jpg") == nullptr);

  BitmapRes canvas;
  canvas.Create(2, 3);
  CHECK(canvas.bitmap().pixels == std::vector<std::uint32_t>(6, 0xffffffff));
  CHECK(canvas.ApplyAlphaBitmap(files, "texture\\dot.bmp"));
  CHECK(canvas.GetPixel(0, 0) == 0xa9ffffff);
  CHECK(canvas.GetPixel(1, 2) == 0xffffffff);
  CHECK_FALSE(canvas.ApplyAlphaBitmap(files, "texture\\missing.bmp"));
  manager.Clear();
  std::filesystem::remove_all(dir);
}
