#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/resource.h"

namespace ragexe::res {

// A 32-bit image as CBitmapRes holds it (docs/formats/bitmap.md): top row first, each
// pixel A << 24 | R << 16 | G << 8 | B.
struct Bitmap {
  std::int32_t width = 0;
  std::int32_t height = 0;
  bool has_alpha = false;  // +0x110: set by TGA only
  std::vector<std::uint32_t> pixels;
};

// LoadBmp (00427b90): 8- and 24-bit, bottom-up.
bool DecodeBmp(std::span<const std::uint8_t> data, Bitmap& out, std::string* error);
// LoadTga (00427e30): types 2 and 10, 24- and 32-bit, bottom-up.
bool DecodeTga(std::span<const std::uint8_t> data, Bitmap& out, std::string* error);
// LoadJpeg (004280d0), with stb_image in place of ijl15.dll; magenta becomes 0x000a0a0a.
bool DecodeJpeg(std::span<const std::uint8_t> data, Bitmap& out, std::string* error);
// LoadFromMemory (004278a0): by the extension of name (.bmp, .tga, .jpg, any case).
bool DecodeBitmap(std::string_view name, std::span<const std::uint8_t> data, Bitmap& out,
                  std::string* error);

// CBitmapRes.
class BitmapRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  bool LoadFromMemory(std::string_view name, std::span<const std::uint8_t> data) override;
  void Reset() override;

  // Create (00427b30): white, opaque, no alpha flag.
  void Create(std::int32_t width, std::int32_t height);
  // GetPixel (00427980): 0x00ff0000 outside the image.
  std::uint32_t GetPixel(std::int32_t x, std::int32_t y) const;
  // ApplyAlphaBitmap (004279d0): the alpha of each pixel from the bitmap at path.
  bool ApplyAlphaBitmap(const DataFiles& files, std::string_view path);

  const Bitmap& bitmap() const { return bitmap_; }
  // Why the last load failed, as the client's message box would have said.
  const std::string& error() const { return error_; }

 private:
  Bitmap bitmap_;
  std::string error_;
};

// The alpha ApplyAlphaBitmap derives from a mask pixel (00427a9a).
std::uint8_t AlphaFromMask(std::uint32_t mask);

// The original's registrations (static initializers 004275f0, 00427740, 00427790).
void RegisterBitmapTypes(ResourceManager& manager);

}  // namespace ragexe::res
