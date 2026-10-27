// The world decoder on arbitrary files, the first byte choosing the ISLIGHTMAP option.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/rsw.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) return 0;
  ragexe::res::World world;
  std::string error;
  if (!ragexe::res::DecodeWorld(std::span<const std::uint8_t>(data + 1, size - 1),
                                (data[0] & 1) != 0, world, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(world.bounds.empty() || world.bounds.size() == 1365);
  for (float c : world.ambient) FUZZ_CHECK(!(c > 1.0f) || data[0] & 1);
  return 0;
}
