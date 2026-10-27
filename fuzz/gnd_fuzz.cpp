// The ground decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/gnd.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::Ground ground;
  std::string error;
  if (!ragexe::res::DecodeGround(std::span<const std::uint8_t>(data, size), ground, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(ground.cells.size() ==
             static_cast<std::size_t>(ground.width) * static_cast<std::size_t>(ground.height));
  for (const auto& texture : ground.textures) FUZZ_CHECK(texture.find('\0') == std::string::npos);
  return 0;
}
