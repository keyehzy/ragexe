#include "render/texture.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>
#include <string_view>

using namespace ragexe::render;

namespace {

struct UpdateVector {
  std::uint32_t format, black_transparent;
  std::int32_t x, y, width, height, stride;
  const std::uint32_t* source;
  std::size_t source_size;
  std::uint32_t u_bits, v_bits;
  const std::uint32_t* texels;
};

struct RleVector {
  std::int32_t x, y, width, height;
  std::int16_t image_width, image_height;
  const std::uint8_t* data;
  std::size_t data_size;
  const std::uint32_t* texels;
};

struct SurfaceVector {
  const char* format;
  std::int32_t x, y, width, height, stride;
  const std::uint32_t* source;
  std::size_t source_size;
  const std::uint32_t* pixels;
};

struct FillVector {
  const char* format;
  std::uint32_t colorref, fill;
};

#include "texture_vectors.inc"

constexpr std::uint32_t kUntouched16 = 0xa5a5;
constexpr std::uint32_t kUntouched32 = 0xa5a5a5a5;

PixelChannels Screen(std::string_view format) {
  if (format == "565") return ChannelsFromFormat(16, 0xf800, 0x7e0, 0x1f, 0);
  if (format == "555") return ChannelsFromFormat(16, 0x7c00, 0x3e0, 0x1f, 0);
  return ChannelsFromFormat(32, 0xff0000, 0xff00, 0xff, 0);
}

}  // namespace

TEST_CASE("UpdateTexturePixels matches the original's") {
  for (const UpdateVector& v : kUpdateVectors) {
    const auto format = static_cast<TextureFormat>(v.format);
    INFO("format " << v.format << ", black transparent " << v.black_transparent << ", " << v.width
                   << "x" << v.height << " at " << v.x << "," << v.y);
    TexelImage texture(8, 8, format == TextureFormat::kArgb8888 ? kUntouched32 : kUntouched16);
    REQUIRE(UpdateTexturePixels(texture, format, v.x, v.y, v.width, v.height,
                                {v.source, v.source_size}, v.black_transparent != 0, v.stride));
    for (std::size_t i = 0; i < 64; ++i) {
      INFO("texel " << i);
      CHECK(texture.texels[i] == v.texels[i]);
    }
    const UvScale uv = ImageUvScale(8, 8, static_cast<std::uint32_t>(v.width),
                                    static_cast<std::uint32_t>(v.height));
    CHECK(std::bit_cast<std::uint32_t>(uv.u) == v.u_bits);
    CHECK(std::bit_cast<std::uint32_t>(uv.v) == v.v_bits);
  }
}

TEST_CASE("UpdateTexturePixels refuses an area outside the texture") {
  TexelImage texture(8, 8);
  const std::uint32_t pixels[4] = {1, 2, 3, 4};
  CHECK_FALSE(UpdateTexturePixels(texture, TextureFormat::kArgb8888, 7, 0, 2, 2, pixels, false, 0));
  CHECK_FALSE(
      UpdateTexturePixels(texture, TextureFormat::kArgb8888, -1, 0, 2, 2, pixels, false, 0));
  CHECK(UpdateTexturePixels(texture, TextureFormat::kArgb8888, 6, 6, 2, 2, pixels, false, 0));
  CHECK(texture.at(7, 7) == 4);
}

TEST_CASE("DrawRleImage matches the original's") {
  for (const RleVector& v : kRleVectors) {
    INFO(v.image_width << "x" << v.image_height << " in " << v.width << "x" << v.height << " at "
                       << v.x << "," << v.y);
    TexelImage texture(16, 8, kUntouched16);
    REQUIRE(DrawRleImage(texture, v.x, v.y, v.width, v.height,
                         {v.image_width, v.image_height, {v.data, v.data_size}},
                         std::span<const std::uint32_t, 256>(kRlePalette)));
    for (std::size_t i = 0; i < 128; ++i) {
      INFO("texel " << i);
      CHECK(texture.texels[i] == v.texels[i]);
    }
  }
}

TEST_CASE("DrawRleImage fails where the original overruns") {
  TexelImage texture(16, 8);
  const std::uint8_t data[] = {1, 2, 3};
  const std::span<const std::uint32_t, 256> palette(kRlePalette);
  CHECK_FALSE(DrawRleImage(texture, 0, 0, 2, 1, {3, 1, data}, palette));
  CHECK_FALSE(DrawRleImage(texture, 14, 0, 3, 1, {3, 1, data}, palette));
  CHECK_FALSE(DrawRleImage(texture, 0, 0, 4, 1, {4, 1, data}, palette));
  const std::uint8_t cut[] = {0};
  CHECK_FALSE(DrawRleImage(texture, 0, 0, 1, 1, {1, 1, cut}, palette));
  CHECK(DrawRleImage(texture, 0, 0, 3, 1, {3, 1, data}, palette));
}

TEST_CASE("UpdateSurfacePixels matches the original's") {
  for (const SurfaceVector& v : kSurfaceVectors) {
    INFO(v.format << ", " << v.width << "x" << v.height << " at " << v.x << "," << v.y);
    const PixelChannels screen = Screen(v.format);
    TexelImage surface(8, 8, screen.bit_count == 16 ? kUntouched16 : kUntouched32);
    REQUIRE(UpdateSurfacePixels(surface, screen, v.x, v.y, v.width, v.height,
                                {v.source, v.source_size}, v.stride));
    for (std::size_t i = 0; i < 64; ++i) {
      INFO("pixel " << i);
      CHECK(surface.texels[i] == v.pixels[i]);
    }
  }
}

TEST_CASE("FillValue matches the original's") {
  for (const FillVector& v : kFillVectors) {
    INFO(v.format << ", " << v.colorref);
    CHECK(FillValue(Screen(v.format), v.colorref) == v.fill);
  }
}

TEST_CASE("surface sides and mipmap counts") {
  CHECK(SurfaceSide(0) == 0);
  CHECK(SurfaceSide(1) == 4);
  CHECK(SurfaceSide(4) == 4);
  CHECK(SurfaceSide(801) == 804);
  CHECK(MipmapCount(1, 1) == 1);
  CHECK(MipmapCount(2, 2) == 1);
  CHECK(MipmapCount(4, 4) == 2);
  CHECK(MipmapCount(8, 2) == 1);
  CHECK(MipmapCount(8, 4) == 2);
  CHECK(MipmapCount(256, 256) == 8);
  CHECK(MipmapCount(256, 64) == 6);
}
