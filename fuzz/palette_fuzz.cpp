// PaletteRes::LoadFromBytes on arbitrary bytes: it loads exactly when there are 1024 of
// them, and each value is then the transparent key or an opaque ARGB1555 colour.

#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz_check.h"
#include "res/palette.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::PaletteRes palette;
  const bool loaded = palette.LoadFromBytes(std::span(data, size));
  FUZZ_CHECK(loaded == (size >= ragexe::res::PaletteRes::kFileSize));
  FUZZ_CHECK(loaded == palette.error().empty());
  if (!loaded) return 0;
  for (std::size_t i = 0; i < palette.palette().size(); ++i) {
    const std::uint32_t value = palette.palette()[i];
    const bool magenta = data[i * 4] >= 0xfe && data[i * 4 + 1] <= 3 && data[i * 4 + 2] >= 0xfe;
    FUZZ_CHECK(magenta ? value == ragexe::res::kPaletteKey : (value & ~0x7fffu) == 0x8000u);
  }
  return 0;
}
