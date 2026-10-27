// The .imf decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/imf.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::Imf imf;
  std::string error;
  if (!ragexe::res::DecodeImf(std::span<const std::uint8_t>(data, size), imf, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(imf.layers.size() <= ragexe::res::kImfMaxLayers);
  for (const auto& layer : imf.layers) FUZZ_CHECK(layer.size() <= ragexe::res::kImfMaxActions);
  return 0;
}
