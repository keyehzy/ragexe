#include "res/palette.h"

#include <algorithm>

#include "res/data_file.h"

namespace ragexe::res {

void ConvertColorsToArgb1555(std::span<const std::uint8_t> entries, std::span<std::uint32_t> out) {
  const std::size_t count = std::min(entries.size() / 4, out.size());
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint32_t r = entries[i * 4];
    const std::uint32_t g = entries[i * 4 + 1];
    const std::uint32_t b = entries[i * 4 + 2];
    out[i] = (r >= 0xfe && g <= 3 && b >= 0xfe)
                 ? kPaletteKey
                 : 0x8000u | (r & 0xf8u) << 7 | (g & 0xf8u) << 2 | b >> 3;
  }
}

std::unique_ptr<Resource> PaletteRes::Clone() const { return std::make_unique<PaletteRes>(); }

bool PaletteRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    // 004320e0: the message the client shows before GetResource's own fallbacks.
    error_ = "CPaletteRes :: Cannot find File : " + std::string(path);
    return false;
  }
  return LoadFromBytes(file.data());
}

bool PaletteRes::LoadFromBytes(std::span<const std::uint8_t> data) {
  if (data.size() < kFileSize) {
    error_ = "the palette is shorter than 1024 bytes";
    return false;
  }
  error_.clear();
  ConvertColorsToArgb1555(data.first(kFileSize), palette_);
  return true;
}

void RegisterPaletteTypes(ResourceManager& manager) {
  manager.RegisterType("pal", "palette\\", std::make_unique<PaletteRes>());
}

}  // namespace ragexe::res
