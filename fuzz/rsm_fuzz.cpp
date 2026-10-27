// The model decoder on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "fuzz_check.h"
#include "res/rsm.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::Model model;
  std::string error;
  if (!ragexe::res::DecodeModel(std::span<const std::uint8_t>(data, size), model, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  for (const auto& node : model.nodes) {
    const auto& mesh = node.mesh;
    FUZZ_CHECK(mesh.face_normals.size() == mesh.faces.size());
    FUZZ_CHECK(mesh.corner_normals.size() == mesh.faces.size());
    for (std::size_t k = 1; k < mesh.faces.size(); ++k)
      FUZZ_CHECK(mesh.faces[k - 1].texture <= mesh.faces[k].texture);
    FUZZ_CHECK(std::cmp_less(node.parent, model.nodes.size()));
  }
  return 0;
}
