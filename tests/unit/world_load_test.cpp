#include "render/world_load.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace ragexe;
using namespace ragexe::render;

namespace {

struct ModelVector {
  std::uint32_t position[3];
  int loads;
};

struct WorldVector {
  int width, height;
  int cell[2];
  std::uint32_t centre[2];
  const ModelVector* models;
  std::size_t model_count;
  const int* order;
  std::size_t order_count;
};

#include "world_vectors.inc"

float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }

}  // namespace

TEST_CASE("A world's models load in the original's order", "[world_load]") {
  std::size_t n = 0;
  for (const WorldVector& v : kWorlds) {
    INFO("world " << n++);
    res::AttrMap attr;
    attr.width = v.width;
    attr.height = v.height;
    attr.cell_size = 5;
    const auto centre = CellCentre(attr, v.cell[0], v.cell[1]);
    CHECK(std::bit_cast<std::uint32_t>(centre[0]) == v.centre[0]);
    CHECK(std::bit_cast<std::uint32_t>(centre[1]) == v.centre[1]);
    std::vector<res::WorldModel> models(v.model_count);
    for (std::size_t i = 0; i < v.model_count; ++i)
      for (std::size_t k = 0; k < 3; ++k) models[i].position[k] = Float(v.models[i].position[k]);
    const auto order = ModelLoadOrder(models, centre[0], centre[1],
                                      [&](std::size_t i) { return v.models[i].loads != 0; });
    REQUIRE(order.size() == v.order_count);
    for (std::size_t i = 0; i < order.size(); ++i) {
      INFO("model " << i);
      CHECK(order[i] == static_cast<std::size_t>(v.order[i]));
    }
  }
}
