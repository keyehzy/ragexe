#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "render/device.h"

namespace ragexe::render {

// CTexture's pixel formats (+0x10), each matched to a texture format the device lists
// (EnumTextureFormatCallback, 004171f0; docs/render/texture.md).
enum class TextureFormat : std::uint32_t {
  kArgb1555 = 0,
  kArgb4444 = 1,
  kRgb565 = 2,
  kArgb8888 = 4,
  kBumpMap = 5,  // the water's: two signed bytes a texel
};

// The texels of a texture or surface, top row first, one value each: 16-bit formats in
// the low half.
struct TexelImage {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint32_t> texels;

  TexelImage() = default;
  TexelImage(std::uint32_t w, std::uint32_t h, std::uint32_t fill = 0)
      : width(w), height(h), texels(static_cast<std::size_t>(w) * h, fill) {}
  std::uint32_t& at(std::uint32_t x, std::uint32_t y) {
    return texels[static_cast<std::size_t>(y) * width + x];
  }
};

// UpdatePixels' conversion of one 32-bit ARGB pixel; nullopt where it leaves the texel as
// it was. Magenta becomes 0, the transparent texel; with black_is_transparent, so does
// black (in 565, magenta is then kept as it was).
std::optional<std::uint32_t> ConvertTexel(TextureFormat format, std::uint32_t argb,
                                          bool black_is_transparent);

// The part of a texture an image of this size covers, as texture coordinates
// (00416810): the image, divided by the smallest whole factor that fits it, over the
// texture's size.
struct UvScale {
  float u = 1.0f;
  float v = 1.0f;
};
UvScale ImageUvScale(std::uint32_t texture_width, std::uint32_t texture_height,
                     std::uint32_t image_width, std::uint32_t image_height);

// CTexture::UpdatePixels (vf01, 00416990): a width × height image of ARGB pixels, rows
// stride apart (0: width), into the texture at (x, y). An image larger than the texture
// is sampled down by the smallest whole factors that fit it. False where DirectDraw
// would refuse the lock: the area does not lie inside the texture.
bool UpdateTexturePixels(TexelImage& texture, TextureFormat format, std::int32_t x, std::int32_t y,
                         std::int32_t width, std::int32_t height,
                         std::span<const std::uint32_t> pixels, bool black_is_transparent,
                         std::int32_t stride);

// A sprite image in CSprRes' run-length form: width × height palette indices, where 0
// and a count stand for that many zeros.
struct RleImage {
  std::int16_t width = 0;
  std::int16_t height = 0;
  std::span<const std::uint8_t> data;
};

// CTexture::DrawRleImage (00416d80): the image through a palette of 16-bit texels into
// a 16-bit texture at (x, y), with the rest of the width × height area zeroed. A run
// carries across rows, and a run of length 0 zeroes everything after it. False where
// the original writes outside the area or the texture, or reads past the data.
bool DrawRleImage(TexelImage& texture, std::int32_t x, std::int32_t y, std::int32_t width,
                  std::int32_t height, const RleImage& image,
                  std::span<const std::uint32_t, 256> palette);

// CSurface::UpdatePixels (vf01, 00416130): ARGB pixels into a surface in the screen's
// format, without sampling. Magenta (0xff00ff whatever the alpha) is left out, so the
// surface keeps what it had there. False where the area does not lie inside the surface.
bool UpdateSurfacePixels(TexelImage& surface, const PixelChannels& screen, std::int32_t x,
                         std::int32_t y, std::int32_t width, std::int32_t height,
                         std::span<const std::uint32_t> pixels, std::int32_t stride);

// FillColor (CTexture 00416750, CSurface 00416330): a COLORREF (0x00bbggrr) in the
// screen's 16-bit packing, whatever the target's format, as the colour-fill Blt gets it.
std::uint32_t FillValue(const PixelChannels& screen, std::uint32_t colorref);

// CSurface's size (00416420): each side rounded up to a multiple of 4.
std::uint32_t SurfaceSide(std::uint32_t side);

// The mipmap count CTexture::CreateSurface (00417070) asks for: 1, plus one for each
// power of two from 2 that is below both the width and the height.
std::uint32_t MipmapCount(std::uint32_t width, std::uint32_t height);

}  // namespace ragexe::render
