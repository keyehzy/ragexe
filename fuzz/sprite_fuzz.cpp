// The sprite decoder on arbitrary files. Input: one byte choosing the detail level (bit 0
// halves the width, bit 1 the height), then the file.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/sprite.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 1) return 0;
  const ragexe::res::SpriteScale scale{(data[0] & 1) != 0, (data[0] & 2) != 0};
  ragexe::res::Sprite sprite;
  std::string error;
  if (!ragexe::res::DecodeSprite(std::span<const std::uint8_t>(data + 1, size - 1), scale, sprite,
                                 &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  for (const auto& image : sprite.images) {
    FUZZ_CHECK(image.width >= 0 && image.height >= 0);
    FUZZ_CHECK(image.Decode().size() ==
               static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
  }
  for (const auto& image : sprite.images32) {
    FUZZ_CHECK(image.pixels.size() == static_cast<std::size_t>(image.pixel_width) *
                                          static_cast<std::size_t>(image.pixel_height));
  }
  return 0;
}
