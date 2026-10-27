#include "res/sprite.h"

#include "res/data_file.h"

namespace ragexe::res {

namespace {

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

// Reads little-endian values from the file, failing past its end where the original
// would read garbage.
class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

  bool U16(std::uint16_t& v) {
    if (data_.size() - pos_ < 2) return false;
    v = static_cast<std::uint16_t>(data_[pos_] | data_[pos_ + 1] << 8);
    pos_ += 2;
    return true;
  }
  bool Bytes(std::size_t n, std::span<const std::uint8_t>& out) {
    if (data_.size() - pos_ < n) return false;
    out = data_.subspan(pos_, n);
    pos_ += n;
    return true;
  }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

}  // namespace

std::vector<std::uint8_t> EncodeSpriteRle(std::span<const std::uint8_t> pixels, int width,
                                          int height) {
  std::vector<std::uint8_t> out;
  std::uint16_t count = 0;
  bool run = false;
  std::uint32_t index = 0;
  for (int row = 0; row < height; ++row) {
    for (int column = 0; column < width; ++column, ++index) {
      // 00434d1b: the index is a 16-bit value.
      const std::size_t at = index & 0xffffu;
      const std::uint8_t pixel = at < pixels.size() ? pixels[at] : 0;
      if (pixel == 0) {
        if (run) {
          if (++count > 0xfe) {
            out.push_back(static_cast<std::uint8_t>(count));
            run = false;
            count = 0;
          }
        } else {
          out.push_back(0);
          run = true;
          ++count;
        }
      } else {
        if (run) out.push_back(static_cast<std::uint8_t>(count));
        out.push_back(pixel);
        run = false;
        count = 0;
      }
    }
  }
  if (count != 0) out.push_back(static_cast<std::uint8_t>(count));
  // 00434df0: the size is kept in 16 bits.
  out.resize(out.size() & 0xffffu);
  return out;
}

std::vector<std::uint8_t> DecodeSpriteRle(std::span<const std::uint8_t> rle, int width,
                                          int height) {
  if (width <= 0 || height <= 0) return {};
  std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
  std::size_t pos = 0;
  auto next = [&]() -> std::uint8_t { return pos < rle.size() ? rle[pos++] : (++pos, 0); };
  std::uint16_t pending = 0;
  for (std::uint8_t& pixel : out) {
    if (pending != 0) {
      pixel = 0;
      --pending;
      continue;
    }
    const std::uint8_t b = next();
    if (b == 0) {
      const std::uint8_t count = next();
      if (count != 0) pending = static_cast<std::uint16_t>(count - 1);
    }
    pixel = b;
  }
  return out;
}

std::vector<std::uint8_t> ShrinkSpriteImage(std::span<const std::uint8_t> pixels, int width,
                                            int height, bool halve_x, bool halve_y) {
  const int out_width = halve_x ? width / 2 : width;
  const int out_height = halve_y ? height / 2 : height;
  if (out_width <= 0 || out_height <= 0) return {};
  const auto columns = static_cast<std::size_t>(out_width);
  const auto rows = static_cast<std::size_t>(out_height);
  const auto stride = static_cast<std::size_t>(width);
  std::vector<std::uint8_t> out(columns * rows);
  const std::size_t step_x = halve_x ? 2 : 1;
  const std::size_t step_y = halve_y ? 2 : 1;
  for (std::size_t y = 0; y < rows; ++y) {
    for (std::size_t x = 0; x < columns; ++x) {
      const std::size_t at = step_y * y * stride + step_x * x;
      out[y * columns + x] = at < pixels.size() ? pixels[at] : 0;
    }
  }
  return out;
}

bool DecodeSprite(std::span<const std::uint8_t> data, SpriteScale scale, Sprite& out,
                  std::string* error) {
  out = Sprite{};
  Reader reader(data);
  std::span<const std::uint8_t> magic;
  if (!reader.Bytes(2, magic) || magic[0] != 'S' || magic[1] != 'P') {
    return Fail(error, "Illegal File Format");
  }
  std::uint16_t count8 = 0;
  if (!reader.U16(out.version) || !reader.U16(count8)) return Fail(error, "Illegal File Format");
  if (out.version > 0x201) return Fail(error, "Unsupported Version. Content may be corrupted");

  if (out.version > 0x100) {
    if (data.size() < PaletteRes::kFileSize) return Fail(error, "the palette runs past the file");
    ConvertColorsToArgb1555(data.last(PaletteRes::kFileSize), out.palette);
  }
  std::uint16_t count32 = 0;
  if (out.version >= 0x200 && !reader.U16(count32)) return Fail(error, "the file is truncated");

  const bool shrink = scale.halve_x || scale.halve_y;
  for (std::uint16_t i = 0; i < count8; ++i) {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    if (!reader.U16(width) || !reader.U16(height)) return Fail(error, "the file is truncated");
    SpriteImage image;
    image.width = static_cast<std::int16_t>(width);
    image.height = static_cast<std::int16_t>(height);
    if (image.width < 0 || image.height < 0) return Fail(error, "an image has a negative size");
    const std::size_t pixel_count = std::size_t{width} * height;
    std::span<const std::uint8_t> stored;
    if (out.version < 0x201) {
      if (!reader.Bytes(pixel_count, stored)) return Fail(error, "the file is truncated");
    } else {
      std::uint16_t size = 0;
      if (!reader.U16(size) || !reader.Bytes(size, stored)) {
        return Fail(error, "the file is truncated");
      }
      // Two bytes of run-length data cover at most 255 pixels; an image claiming more than
      // its data can cover would make the original read past it.
      if (pixel_count > std::size_t{128} * size) {
        return Fail(error, "the run-length data cannot cover the image");
      }
    }
    if (!shrink) {
      image.rle = out.version < 0x201 ? EncodeSpriteRle(stored, width, height)
                                      : std::vector<std::uint8_t>(stored.begin(), stored.end());
    } else {
      const std::vector<std::uint8_t> raw =
          out.version < 0x201 ? std::vector<std::uint8_t>(stored.begin(), stored.end())
                              : DecodeSpriteRle(stored, width, height);
      const auto shrunk = ShrinkSpriteImage(raw, width, height, scale.halve_x, scale.halve_y);
      image.rle = EncodeSpriteRle(shrunk, scale.halve_x ? width / 2 : width,
                                  scale.halve_y ? height / 2 : height);
      if (scale.halve_x) {
        image.width = static_cast<std::int16_t>(image.width / 2);
        image.halved_x = true;
      }
      if (scale.halve_y) {
        image.height = static_cast<std::int16_t>(image.height / 2);
        image.halved_y = true;
      }
    }
    out.images.push_back(std::move(image));
  }

  for (std::uint16_t i = 0; i < count32; ++i) {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    if (!reader.U16(width) || !reader.U16(height)) return Fail(error, "the file is truncated");
    SpriteImage32 image;
    image.width = image.pixel_width = static_cast<std::int16_t>(width);
    image.height = image.pixel_height = static_cast<std::int16_t>(height);
    if (image.width < 0 || image.height < 0) return Fail(error, "an image has a negative size");
    std::span<const std::uint8_t> stored;
    if (!reader.Bytes(std::size_t{width} * height * 4, stored)) {
      return Fail(error, "the file is truncated");
    }
    image.pixels.resize(std::size_t{width} * height);
    for (std::size_t row = 0; row < height && width != 0; ++row) {
      // Stored bottom row first; each pixel is alpha, blue, green, red.
      const std::uint8_t* src = &stored[row * width * 4];
      std::uint32_t* dst = &image.pixels[(height - 1 - row) * std::size_t{width}];
      for (std::size_t x = 0; x < width; ++x, src += 4) {
        dst[x] = std::uint32_t{src[0]} << 24 | std::uint32_t{src[3]} << 16 |
                 std::uint32_t{src[2]} << 8 | src[1];
      }
    }
    if (scale.halve_x) {
      image.width = static_cast<std::int16_t>(image.width / 2);
      image.halved_x = true;
    }
    if (scale.halve_y) {
      image.height = static_cast<std::int16_t>(image.height / 2);
      image.halved_y = true;
    }
    out.images32.push_back(std::move(image));
  }
  return true;
}

std::vector<std::uint8_t> SpriteImage::Decode() const {
  return DecodeSpriteRle(rle, width, height);
}

std::unique_ptr<Resource> SprRes::Clone() const { return std::make_unique<SprRes>(scale_); }

bool SprRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "Spr :: Cannot find File : " + std::string(path);
    return false;
  }
  return LoadFromBytes(file.data());
}

bool SprRes::LoadFromBytes(std::span<const std::uint8_t> data) {
  error_.clear();
  return DecodeSprite(data, scale_, sprite_, &error_);
}

void SprRes::Reset() { sprite_ = Sprite{}; }

void RegisterSpriteTypes(ResourceManager& manager, SpriteScale scale) {
  manager.RegisterType("spr", "sprite\\", std::make_unique<SprRes>(scale));
}

}  // namespace ragexe::res
