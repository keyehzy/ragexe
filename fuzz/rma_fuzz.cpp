// The .rma text decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "fuzz_check.h"
#include "res/rma.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::Rma rma;
  std::string error;
  if (!ragexe::res::DecodeRma(std::span<const std::uint8_t>(data, size), rma, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(rma.actions.size() <= static_cast<std::size_t>(ragexe::res::kRmaMaxAction) + 1);
  // An action number never given keeps the index 0, names or not.
  for (const auto& action : rma.actions)
    FUZZ_CHECK(action.rsx >= -1 &&
               (action.rsx == 0 || std::cmp_less(action.rsx, rma.rsx_names.size())));
  return 0;
}
