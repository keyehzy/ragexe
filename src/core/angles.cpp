#include "core/angles.h"

#include <cmath>

namespace ragexe::core {

namespace {

constexpr float kPi = 3.1415920257568359375f;          // 005e2684
constexpr float kInverse180 = 0.0055555556900799274f;  // 005e2690

AngleTables Build() {
  AngleTables t;
  for (std::size_t k = 0; k < t.sin.size(); ++k) {
    const float angle = static_cast<float>(k) * kPi * kInverse180;
    // The x87's sine and cosine, rounded once to float.
    t.sin[k] = static_cast<float>(std::sin(static_cast<double>(angle)));
    t.cos[k] = static_cast<float>(std::cos(static_cast<double>(angle)));
  }
  return t;
}

float Lookup(const std::array<float, 361>& table, std::int32_t degrees) {
  if (degrees > 360 || degrees < -360) degrees %= 360;
  if (degrees >= 0) return table[static_cast<std::size_t>(degrees)];
  const std::int32_t index = 361 + degrees;
  return table[static_cast<std::size_t>(index)];
}

}  // namespace

const AngleTables& GetAngleTables() {
  static const AngleTables tables = Build();
  return tables;
}

float SinDegrees(std::int32_t degrees) { return Lookup(GetAngleTables().sin, degrees); }
float CosDegrees(std::int32_t degrees) { return Lookup(GetAngleTables().cos, degrees); }

}  // namespace ragexe::core
