// The action decoder on arbitrary files. Input: four bytes for the matching sprite's
// images (width, height, halving bits, how many images there are), then the file.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "fuzz_check.h"
#include "res/act.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 4) return 0;
  const ragexe::res::ActImageSize image{data[0], data[1], (data[2] & 1) != 0, (data[2] & 2) != 0};
  const std::int32_t images = data[3];
  const ragexe::res::ActImageLookup lookup =
      [&](std::int32_t type, std::int32_t index) -> std::optional<ragexe::res::ActImageSize> {
    if (type < 0 || type > 1 || index < 0 || index >= images) return std::nullopt;
    return image;
  };
  ragexe::res::Act act;
  std::string error;
  if (!ragexe::res::DecodeAct(std::span<const std::uint8_t>(data + 4, size - 4), lookup, act,
                              &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(act.delays.size() == act.actions.size());
  for (const auto& frames : act.actions) {
    for (const auto& frame : frames) {
      FUZZ_CHECK(frame.items.size() <= 1000);
      FUZZ_CHECK(std::cmp_less_equal(frame.items.size(), act.max_items));
    }
  }
  return 0;
}
