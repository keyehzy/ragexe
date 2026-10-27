// The effect decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/str.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::Str str;
  std::string error;
  if (!ragexe::res::DecodeStr(std::span<const std::uint8_t>(data, size), str, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(str.layers.size() <= ragexe::res::kStrMaxLayers);
  for (const auto& layer : str.layers)
    FUZZ_CHECK(layer.textures.size() <= ragexe::res::kStrMaxTextures);
  return 0;
}
