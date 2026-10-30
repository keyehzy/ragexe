#include "render/world_load.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/x87.h"

namespace ragexe::render {

namespace {

constexpr float kHalf = 0.5f;            // 005e25b8
constexpr float kNearDistance = 200.0f;  // 005e8974

}  // namespace

std::array<float, 2> CellCentre(const res::AttrMap& attr, std::int32_t cell_x,
                                std::int32_t cell_y) {
  const auto size = static_cast<double>(attr.cell_size);
  const float half_cell = core::RoundedProduct(kHalf, size);
  const float half_width = core::RoundedProduct(kHalf, static_cast<double>(attr.width));
  const float half_height = core::RoundedProduct(kHalf, static_cast<double>(attr.height));
  const float x = core::RoundedSum(-half_width, static_cast<double>(cell_x));
  const float z = core::RoundedSum(-half_height, static_cast<double>(cell_y));
  return {core::RoundedProduct(x, size) + half_cell, core::RoundedProduct(z, size) + half_cell};
}

std::vector<std::size_t> ModelLoadOrder(std::span<const res::WorldModel> models, float player_x,
                                        float player_z,
                                        const std::function<bool(std::size_t)>& loads) {
  std::vector<std::size_t> order;
  std::vector<std::pair<float, std::size_t>> far;
  for (std::size_t i = 0; i < models.size(); ++i) {
    const auto& p = models[i].position;
    const float dx = p[0] - player_x;
    const float dy = p[1] - 0.0f;
    const float dz = p[2] - player_z;
    const float distance = std::sqrt(((dx * dx) + (dy * dy)) + (dz * dz));
    // fcom against 200 and C0 or C3: below, equal or unordered.
    if (!(distance > kNearDistance)) {
      if (loads(i)) order.push_back(i);
    } else {
      far.emplace_back(distance, i);
    }
  }
  std::sort(far.begin(), far.end());
  for (const auto& [distance, i] : far)
    if (loads(i)) order.push_back(i);
  return order;
}

}  // namespace ragexe::render
