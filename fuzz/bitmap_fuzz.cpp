// The bitmap decoders on arbitrary bytes. Input: one byte choosing the format (0 BMP,
// 1 TGA, 2 JPEG, by its value modulo 3), then the file.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/bitmap.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 1) return 0;
  static const char* const kNames[] = {"a.bmp", "a.tga", "a.jpg"};
  ragexe::res::Bitmap bitmap;
  std::string error;
  const bool ok = ragexe::res::DecodeBitmap(
      kNames[data[0] % 3], std::span<const std::uint8_t>(data + 1, size - 1), bitmap, &error);
  if (ok) {
    FUZZ_CHECK(bitmap.width >= 0 && bitmap.height >= 0);
    FUZZ_CHECK(bitmap.pixels.size() ==
               static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height));
  } else {
    FUZZ_CHECK(!error.empty());
  }
  return 0;
}
