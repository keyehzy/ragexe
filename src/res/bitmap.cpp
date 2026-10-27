#include "res/bitmap.h"

#include <cstring>
#include <utility>

#include "res/data_file.h"
#include "stb_image.h"

namespace ragexe::res {

namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

std::uint16_t U16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }

std::uint32_t U32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

std::uint32_t Argb(std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b) {
  return a << 24 | r << 16 | g << 8 | b;
}

// Images larger than this are refused before allocating; the largest in data.grf is far
// smaller, and the original has no limit.
constexpr std::int64_t kMaxPixels = std::int64_t{1} << 26;

// A zero size gives an empty image, as in the original; a negative one is refused.
bool CheckSize(std::int64_t width, std::int64_t height, std::string* error) {
  if (width < 0 || height < 0) return Fail(error, "the image has a negative size");
  if (width * height > kMaxPixels) return Fail(error, "the image is too large");
  return true;
}

}  // namespace

bool DecodeBmp(std::span<const std::uint8_t> data, Bitmap& out, std::string* error) {
  out = Bitmap{};
  if (data.size() < 54 || data[0] != 'B' || data[1] != 'M') {
    return Fail(error, "not a BMP file");
  }
  const std::uint32_t bits_offset = U32(&data[10]);
  const auto width = static_cast<std::int32_t>(U32(&data[18]));
  const auto height = static_cast<std::int32_t>(U32(&data[22]));
  const std::uint16_t bit_count = U16(&data[28]);
  const std::uint32_t colors_used = U32(&data[46]);
  if (bit_count != 24 && bit_count != 8) {
    return Fail(error, "Only 16M or 8bit color format supported");
  }
  if (!CheckSize(width, height, error)) return false;
  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  if (w == 0 || h == 0) {
    out.width = width;
    out.height = height;
    return true;
  }

  // Staged as B, G, R per pixel, top row first.
  std::vector<std::uint8_t> staged(w * h * 3);
  if (bit_count == 8) {
    const std::size_t count = colors_used == 0 ? 256 : colors_used;
    if (count > 256) return Fail(error, "the palette has more than 256 entries");
    if (data.size() < 54 + count * 4) return Fail(error, "the palette runs past the data");
    std::uint8_t palette[256][4] = {};
    std::memcpy(palette, &data[54], count * 4);
    const std::size_t stride = (w + 3) & ~std::size_t{3};
    if (bits_offset > data.size() || stride * h > data.size() - bits_offset) {
      return Fail(error, "the pixels run past the data");
    }
    for (std::size_t row = 0; row < h; ++row) {
      const std::uint8_t* src = &data[bits_offset + row * stride];
      std::uint8_t* dst = &staged[(h - 1 - row) * w * 3];
      for (std::size_t x = 0; x < w; ++x) {
        std::memcpy(dst + x * 3, palette[src[x]], 3);
      }
    }
  } else {
    // The original's padding formula (00427d1c) reduces to this.
    const std::size_t stride = w * 3 + ((4 - (w * 3) % 4) % 4);
    if (bits_offset > data.size() ||
        (h > 0 && stride * (h - 1) + w * 3 > data.size() - bits_offset)) {
      return Fail(error, "the pixels run past the data");
    }
    for (std::size_t row = 0; row < h; ++row) {
      std::memcpy(&staged[(h - 1 - row) * w * 3], &data[bits_offset + row * stride], w * 3);
    }
  }

  out.width = width;
  out.height = height;
  out.pixels.resize(w * h);
  for (std::size_t i = 0; i < w * h; ++i) {
    out.pixels[i] = Argb(0xff, staged[i * 3 + 2], staged[i * 3 + 1], staged[i * 3]);
  }
  return true;
}

bool DecodeTga(std::span<const std::uint8_t> data, Bitmap& out, std::string* error) {
  out = Bitmap{};
  if (data.size() < 18 || data[1] != 0 || (data[2] != 2 && data[2] != 10)) {
    return Fail(error, "not a supported TGA file");
  }
  const bool run_length = data[2] == 10;
  const std::int32_t width = U16(&data[12]);
  const std::int32_t height = U16(&data[14]);
  const bool has_alpha_channel = data[16] == 32;
  if (!CheckSize(width, height, error)) return false;
  const auto w = static_cast<std::size_t>(width);
  const auto h = static_cast<std::size_t>(height);
  out.width = width;
  out.height = height;
  out.pixels.assign(w * h, 0);

  std::size_t pos = 18 + data[0];
  std::uint32_t alpha = 0xff;
  // Reads one pixel's bytes; alpha keeps its last value when the depth has none.
  auto read_pixel = [&](std::uint32_t& pixel) {
    const std::size_t n = has_alpha_channel ? 4 : 3;
    if (pos > data.size() || n > data.size() - pos) return false;
    if (has_alpha_channel) alpha = data[pos + 3];
    pixel = Argb(alpha, data[pos + 2], data[pos + 1], data[pos]);
    pos += n;
    return true;
  };
  // The original writes at row_start + x even past the row's end (into rows already
  // written); the port does the same while inside the image and drops the rest.
  auto put = [&](std::size_t row_start, std::size_t x, std::uint32_t pixel) {
    if (row_start + x < out.pixels.size()) out.pixels[row_start + x] = pixel;
  };

  for (std::size_t row = 0; row < h; ++row) {
    const std::size_t row_start = (h - 1 - row) * w;
    std::size_t x = 0;
    while (x < w) {
      std::uint32_t pixel = 0;
      if (!run_length) {
        if (!read_pixel(pixel)) return Fail(error, "the pixels run past the data");
        put(row_start, x++, pixel);
        continue;
      }
      if (pos >= data.size()) return Fail(error, "the pixels run past the data");
      const std::uint8_t header = data[pos++];
      const std::size_t count = (header & 0x7fu) + 1;
      if ((header & 0x80) != 0) {
        if (!read_pixel(pixel)) return Fail(error, "the pixels run past the data");
        for (std::size_t i = 0; i < count; ++i) put(row_start, x++, pixel);
      } else {
        for (std::size_t i = 0; i < count; ++i) {
          if (!read_pixel(pixel)) return Fail(error, "the pixels run past the data");
          put(row_start, x++, pixel);
        }
      }
    }
  }
  out.has_alpha = true;
  return true;
}

bool DecodeJpeg(std::span<const std::uint8_t> data, Bitmap& out, std::string* error) {
  out = Bitmap{};
  if (data.empty() || data.size() > static_cast<std::size_t>(INT32_MAX)) {
    return Fail(error, "not a JPEG file");
  }
  int width = 0;
  int height = 0;
  int channels = 0;
  if (stbi_info_from_memory(data.data(), static_cast<int>(data.size()), &width, &height,
                            &channels) == 0) {
    return Fail(error, "not a JPEG file");
  }
  if (!CheckSize(width, height, error)) return false;
  stbi_uc* rgb = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height,
                                       &channels, 3);
  if (rgb == nullptr) return Fail(error, "the JPEG does not decode");
  const auto n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  out.width = width;
  out.height = height;
  out.pixels.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t r = rgb[i * 3];
    const std::uint32_t g = rgb[i * 3 + 1];
    const std::uint32_t b = rgb[i * 3 + 2];
    // 004281a0: magenta is the transparent colour.
    out.pixels[i] = (r >= 0xfe && g <= 3 && b >= 0xfe) ? 0x000a0a0au : Argb(0xff, r, g, b);
  }
  stbi_image_free(rgb);
  return true;
}

bool DecodeBitmap(std::string_view name, std::span<const std::uint8_t> data, Bitmap& out,
                  std::string* error) {
  const std::size_t dot = name.rfind('.');
  if (dot == std::string_view::npos) return Fail(error, "");
  const std::string ext = LowerCaseName(name.substr(dot));
  if (ext == ".bmp") return DecodeBmp(data, out, error);
  if (ext == ".tga") return DecodeTga(data, out, error);
  if (ext == ".jpg") return DecodeJpeg(data, out, error);
  return Fail(error, "Unsupport fileformat!");
}

std::unique_ptr<Resource> BitmapRes::Clone() const { return std::make_unique<BitmapRes>(); }

bool BitmapRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "cannot open " + file.path();
    return false;
  }
  return LoadFromMemory(path, file.data());
}

bool BitmapRes::LoadFromMemory(std::string_view name, std::span<const std::uint8_t> data) {
  error_.clear();
  return DecodeBitmap(name, data, bitmap_, &error_);
}

void BitmapRes::Reset() { bitmap_ = Bitmap{}; }

void BitmapRes::Create(std::int32_t width, std::int32_t height) {
  bitmap_ = Bitmap{};
  if (width <= 0 || height <= 0) return;
  bitmap_.width = width;
  bitmap_.height = height;
  bitmap_.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                        0xffffffffu);
}

std::uint32_t BitmapRes::GetPixel(std::int32_t x, std::int32_t y) const {
  if (x < 0 || x >= bitmap_.width || y < 0 || y >= bitmap_.height) return 0x00ff0000;
  return bitmap_.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap_.width) +
                        static_cast<std::size_t>(x)];
}

std::uint8_t AlphaFromMask(std::uint32_t mask) {
  const std::uint32_t sum = ((mask | 0xff00u) >> 8) + ((mask | 0xff0000u) >> 16) + (mask | 0xffu);
  const auto quotient = static_cast<std::int32_t>(sum) / 3;
  return static_cast<std::uint8_t>(static_cast<std::uint32_t>(quotient) & 0xffu);
}

bool BitmapRes::ApplyAlphaBitmap(const DataFiles& files, std::string_view path) {
  BitmapRes mask;
  if (!mask.Load(files, path)) {
    error_ = "Error reading alpha bitmap";
    return false;
  }
  if (bitmap_.pixels.empty()) return false;
  for (std::int32_t y = 0; y < bitmap_.height; ++y) {
    for (std::int32_t x = 0; x < bitmap_.width; ++x) {
      std::uint32_t& pixel =
          bitmap_.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(bitmap_.width) +
                         static_cast<std::size_t>(x)];
      pixel = static_cast<std::uint32_t>(AlphaFromMask(mask.GetPixel(x, y))) << 24 |
              (pixel & 0xffffffu);
    }
  }
  return true;
}

void RegisterBitmapTypes(ResourceManager& manager) {
  for (const char* ext : {"bmp", "tga", "jpg"}) {
    manager.RegisterType(ext, "texture\\", std::make_unique<BitmapRes>());
  }
}

}  // namespace ragexe::res
