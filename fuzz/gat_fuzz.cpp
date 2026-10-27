// The attribute-map decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/gat.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::AttrMap map;
  std::string error;
  if (!ragexe::res::DecodeAttrMap(std::span<const std::uint8_t>(data, size), map, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(map.cells.size() ==
             static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height));
  return 0;
}
