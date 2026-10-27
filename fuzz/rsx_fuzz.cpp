// The animated-model decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "fuzz_check.h"
#include "res/rsx.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::AnimModel model;
  std::string error;
  if (!ragexe::res::DecodeAnimModel(std::span<const std::uint8_t>(data, size), model, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  for (const auto& node : model.nodes) {
    FUZZ_CHECK(node.vertices.size() == static_cast<std::size_t>(model.frame_count) *
                                           static_cast<std::size_t>(node.vertex_count));
    FUZZ_CHECK(std::cmp_less(node.parent, model.nodes.size()));
  }
  return 0;
}
