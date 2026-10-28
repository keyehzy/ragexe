#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "res/resource.h"

namespace ragexe::res {

// A palette as the client keeps it (docs/formats/palette.md): 256 ARGB1555 values in 32
// bits each, magenta replaced by the transparent key.
using Palette = std::array<std::uint32_t, 256>;

// The value magenta becomes (006053fc).
inline constexpr std::uint32_t kPaletteKey = 0x0421;

// C3dDevice::ConvertColorsToArgb1555 (00405440) on 4-byte entries of r, g, b and an
// ignored byte: count is entries.size() / 4.
void ConvertColorsToArgb1555(std::span<const std::uint8_t> entries, std::span<std::uint32_t> out);

// CPaletteRes.
class PaletteRes : public Resource {
 public:
  static constexpr std::size_t kFileSize = 1024;

  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;

  // The palette from the first 1024 bytes of a .pal file.
  bool LoadFromBytes(std::span<const std::uint8_t> data);

  const Palette& palette() const { return palette_; }
  const std::string& error() const override { return error_; }

 private:
  Palette palette_{};
  std::string error_;
};

// The original's registration (static initializer 00431f70).
void RegisterPaletteTypes(ResourceManager& manager);

}  // namespace ragexe::res
