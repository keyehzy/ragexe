#include "render/texture.h"

#include <cstddef>

namespace ragexe::render {

namespace {

// The smallest whole factor k with side / k within limit (signed division, as
// UpdatePixels does it).
std::int32_t SampleFactor(std::int32_t side, std::uint32_t limit) {
  for (std::int32_t k = 1;; ++k) {
    const auto quotient = static_cast<std::uint32_t>(side / k);
    if (quotient <= limit) return k;
  }
}

bool InsideImage(const TexelImage& image, std::int32_t x, std::int32_t y, std::int32_t width,
                 std::int32_t height) {
  return x >= 0 && y >= 0 && static_cast<std::int64_t>(x) + width <= image.width &&
         static_cast<std::int64_t>(y) + height <= image.height;
}

std::uint32_t Pack(std::uint32_t byte, const PixelChannel& c) {
  return (byte >> (c.loss & 31)) << (c.shift & 31);
}

}  // namespace

std::optional<std::uint32_t> ConvertTexel(TextureFormat format, std::uint32_t argb,
                                          bool black_is_transparent) {
  const std::uint32_t p = argb;
  switch (format) {
    case TextureFormat::kArgb1555: {
      const std::uint32_t v =
          (p >> 6 & 0x3e0) + (p >> 3 & 0x1f) + (p >> 16 & 0x8000) + (p >> 9 & 0x7c00);
      if (v == 0xfc1f) return 0;
      if (black_is_transparent && (v & 0x7fff) == 0) return 0;
      return v;
    }
    case TextureFormat::kArgb4444: {
      const std::uint32_t v =
          (p >> 12 & 0xf00) + (p >> 8 & 0xf0) + (p >> 4 & 0xf) + (p >> 16 & 0xf000);
      const std::uint32_t rgb = v & 0xfff;
      if (rgb == 0xf0f) return 0;
      if (black_is_transparent && rgb == 0) return 0;
      return v;
    }
    case TextureFormat::kRgb565: {
      const std::uint32_t v = (p >> 5 & 0x7e0) + (p >> 3 & 0x1f) + (p >> 8 & 0xf800);
      if (black_is_transparent && v == 0xf81f) return std::nullopt;
      return v;
    }
    case TextureFormat::kArgb8888: return p;
    case TextureFormat::kBumpMap: break;
  }
  return std::nullopt;
}

UvScale ImageUvScale(std::uint32_t texture_width, std::uint32_t texture_height,
                     std::uint32_t image_width, std::uint32_t image_height) {
  const auto scale = [](std::uint32_t texture, std::uint32_t image) {
    std::uint32_t k = 1;
    while (texture < image / k) ++k;
    const std::uint32_t fitted = image / k;
    return static_cast<float>(fitted) / static_cast<float>(static_cast<std::int32_t>(texture));
  };
  return {scale(texture_width, image_width), scale(texture_height, image_height)};
}

bool UpdateTexturePixels(TexelImage& texture, TextureFormat format, std::int32_t x, std::int32_t y,
                         std::int32_t width, std::int32_t height,
                         std::span<const std::uint32_t> pixels, bool black_is_transparent,
                         std::int32_t stride) {
  if (stride == 0) stride = width;
  if (width <= 0 || height <= 0) return true;
  const std::int32_t kx = SampleFactor(width, texture.width);
  const std::int32_t ky = SampleFactor(height, texture.height);
  const std::int32_t x0 = x / kx;
  const std::int32_t y0 = y / ky;
  const std::int32_t columns = width / kx;
  const std::int32_t rows = height / ky;
  if (columns == 0 || rows == 0) return true;
  if (!InsideImage(texture, x0, y0, columns, rows)) return false;
  for (std::int32_t r = 0; r < rows; ++r) {
    const std::size_t row = static_cast<std::size_t>(r) * ky * stride;
    for (std::int32_t c = 0; c < columns; ++c) {
      const std::size_t i = row + static_cast<std::size_t>(c) * kx;
      if (i >= pixels.size()) return false;
      if (auto t = ConvertTexel(format, pixels[i], black_is_transparent)) {
        texture.at(static_cast<std::uint32_t>(x0 + c), static_cast<std::uint32_t>(y0 + r)) = *t;
      }
    }
  }
  return true;
}

bool DrawRleImage(TexelImage& texture, std::int32_t x, std::int32_t y, std::int32_t width,
                  std::int32_t height, const RleImage& image,
                  std::span<const std::uint32_t, 256> palette) {
  if (width < image.width || height < image.height) return false;
  if (!InsideImage(texture, x, y, width, height)) return false;
  std::size_t pos = 0;
  std::uint32_t run = 0;  // unsigned, as the original's count wraps from 0 to all ones
  std::int32_t r = 0;
  for (; r < image.height; ++r) {
    const auto ty = static_cast<std::uint32_t>(y + r);
    std::int32_t c = 0;
    for (; c < image.width; ++c) {
      std::uint32_t& t = texture.at(static_cast<std::uint32_t>(x + c), ty);
      if (run != 0) {
        t = 0;
        --run;
        continue;
      }
      if (pos >= image.data.size()) return false;
      const std::uint8_t b = image.data[pos++];
      if (b == 0) {
        if (pos >= image.data.size()) return false;
        t = 0;
        run = static_cast<std::uint32_t>(image.data[pos++]) - 1;
      } else {
        t = palette[b] & 0xffff;
      }
    }
    for (; c < width; ++c) texture.at(static_cast<std::uint32_t>(x + c), ty) = 0;
  }
  for (; r < height; ++r) {
    for (std::int32_t c = 0; c < width; ++c) {
      texture.at(static_cast<std::uint32_t>(x + c), static_cast<std::uint32_t>(y + r)) = 0;
    }
  }
  return true;
}

bool UpdateSurfacePixels(TexelImage& surface, const PixelChannels& screen, std::int32_t x,
                         std::int32_t y, std::int32_t width, std::int32_t height,
                         std::span<const std::uint32_t> pixels, std::int32_t stride) {
  if (stride == 0) stride = width;
  if (width <= 0 || height <= 0) return true;
  if (!InsideImage(surface, x, y, width, height)) return false;
  const std::uint32_t key =
      (Pack(0xff, screen.red) + Pack(0, screen.green) + Pack(0xff, screen.blue)) & 0xffff;
  for (std::int32_t r = 0; r < height; ++r) {
    for (std::int32_t c = 0; c < width; ++c) {
      const std::size_t i = static_cast<std::size_t>(r) * stride + c;
      if (i >= pixels.size()) return false;
      const std::uint32_t p = pixels[i];
      std::uint32_t& t =
          surface.at(static_cast<std::uint32_t>(x + c), static_cast<std::uint32_t>(y + r));
      if (screen.bit_count == 16) {
        const std::uint32_t v = (Pack(p & 0xff, screen.blue) + Pack(p >> 8 & 0xff, screen.green) +
                                 Pack(p >> 16 & 0xff, screen.red)) &
                                0xffff;
        if (v != key) t = v;
      } else if ((p & 0xffffff) != 0xff00ff) {
        t = p;
      }
    }
  }
  return true;
}

std::uint32_t FillValue(const PixelChannels& screen, std::uint32_t colorref) {
  return (Pack(colorref >> 8 & 0xff, screen.green) + Pack(colorref >> 16 & 0xff, screen.blue) +
          Pack(colorref & 0xff, screen.red)) &
         0xffff;
}

std::uint32_t ExpandTexel(TextureFormat format, std::uint32_t t) {
  const auto five = [](std::uint32_t v) { return v << 3 | v >> 2; };
  const auto six = [](std::uint32_t v) { return v << 2 | v >> 4; };
  const auto four = [](std::uint32_t v) { return v << 4 | v; };
  switch (format) {
    case TextureFormat::kArgb1555:
      return ((t & 0x8000) != 0 ? 0xff000000u : 0u) | five(t >> 10 & 0x1f) << 16 |
             five(t >> 5 & 0x1f) << 8 | five(t & 0x1f);
    case TextureFormat::kArgb4444:
      return four(t >> 12 & 0xf) << 24 | four(t >> 8 & 0xf) << 16 | four(t >> 4 & 0xf) << 8 |
             four(t & 0xf);
    case TextureFormat::kRgb565:
      return 0xff000000u | five(t >> 11 & 0x1f) << 16 | six(t >> 5 & 0x3f) << 8 | five(t & 0x1f);
    default: return t;
  }
}

std::uint32_t SurfaceSide(std::uint32_t side) { return side + ((0u - (side & 3)) & 3); }

std::uint32_t MipmapCount(std::uint32_t width, std::uint32_t height) {
  std::uint32_t count = 1;
  for (std::uint64_t p = 2; p < width && p < height; p <<= 1) ++count;
  return count;
}

}  // namespace ragexe::render
