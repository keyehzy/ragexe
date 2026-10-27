// The emblem decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "fuzz_check.h"
#include "res/ebm.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  std::vector<std::uint8_t> inflated;
  if (ragexe::res::InflateEmblem(input, inflated) == ragexe::res::EmblemInflate::kInflated)
    FUZZ_CHECK(inflated.size() <= ragexe::res::kEmblemBufferSize);
  ragexe::res::Bitmap emblem;
  std::string error;
  if (!ragexe::res::DecodeEmblem(input, emblem, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(emblem.width >= 0 && emblem.width <= ragexe::res::kEmblemSize);
  FUZZ_CHECK(emblem.height >= 0 && emblem.height <= ragexe::res::kEmblemSize);
  FUZZ_CHECK(emblem.pixels.size() >=
             static_cast<std::size_t>(emblem.width) * static_cast<std::size_t>(emblem.height));
  return 0;
}
