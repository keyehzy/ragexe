// The wave decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/wav.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  ragexe::res::Wave wave;
  std::string error;
  if (!ragexe::res::DecodeWave(input, wave, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(ragexe::res::ClassifyWave(input) == wave.type);
  if (wave.type != ragexe::res::WaveFileType::kImaAdpcm) {
    FUZZ_CHECK(wave.sample.size() == size);
  } else if (!wave.sample.empty()) {
    FUZZ_CHECK(wave.sample.size() >= 44);
  }
  return 0;
}
