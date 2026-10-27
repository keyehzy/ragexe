#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/palette.h"
#include "res/resource.h"

namespace ragexe::res {

// The detail level's halving of sprite images (00688a7c, 00688a80).
struct SpriteScale {
  bool halve_x = false;
  bool halve_y = false;
};

// An 8-bit image, kept run-length encoded as the client keeps it (docs/formats/sprite.md).
struct SpriteImage {
  std::int16_t width = 0;  // after halving
  std::int16_t height = 0;
  bool halved_x = false;
  bool halved_y = false;
  std::vector<std::uint8_t> rle;

  // DecodeRle (00434e50): width × height palette indices, top row first.
  std::vector<std::uint8_t> Decode() const;
};

// A 32-bit image: pixels at the size stored in the file, top row first, each
// A << 24 | R << 16 | G << 8 | B; width and height are the record's, after halving.
struct SpriteImage32 {
  std::int16_t width = 0;
  std::int16_t height = 0;
  bool halved_x = false;
  bool halved_y = false;
  std::int16_t pixel_width = 0;
  std::int16_t pixel_height = 0;
  std::vector<std::uint32_t> pixels;
};

struct Sprite {
  std::uint16_t version = 0;
  Palette palette{};  // all 0 for versions without one
  std::vector<SpriteImage> images;
  std::vector<SpriteImage32> images32;
};

// CSprRes::Load (004341d0) on a whole .spr file.
bool DecodeSprite(std::span<const std::uint8_t> data, SpriteScale scale, Sprite& out,
                  std::string* error);

// EncodeRle (00434cb0), with its 16-bit pixel index and 16-bit size.
std::vector<std::uint8_t> EncodeSpriteRle(std::span<const std::uint8_t> pixels, int width,
                                          int height);
// DecodeRle (00434e50); data that runs out reads as 0.
std::vector<std::uint8_t> DecodeSpriteRle(std::span<const std::uint8_t> rle, int width, int height);
// ShrinkImage (00434ed0).
std::vector<std::uint8_t> ShrinkSpriteImage(std::span<const std::uint8_t> pixels, int width,
                                            int height, bool halve_x, bool halve_y);

// CSprRes.
class SprRes : public Resource {
 public:
  explicit SprRes(SpriteScale scale = {}) : scale_(scale) {}

  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  bool LoadFromBytes(std::span<const std::uint8_t> data);

  const Sprite& sprite() const { return sprite_; }
  const std::string& error() const { return error_; }

 private:
  SpriteScale scale_;
  Sprite sprite_;
  std::string error_;
};

// The original's registration (static initializer 00433ee0), at a detail level.
void RegisterSpriteTypes(ResourceManager& manager, SpriteScale scale = {});

}  // namespace ragexe::res
