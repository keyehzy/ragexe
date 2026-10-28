#include "core/x87.h"

#include <cmath>
#include <limits>

namespace ragexe::core {

float RoundedProduct(float a, double b) {
  const double hi = static_cast<double>(a) * b;
  const double lo = std::fma(static_cast<double>(a), b, -hi);
  float f = static_cast<float>(hi);
  const double d = hi - static_cast<double>(f);
  if (lo != 0.0 && d != 0.0) {
    const float g = std::nextafter(f, d > 0.0 ? std::numeric_limits<float>::infinity()
                                              : -std::numeric_limits<float>::infinity());
    if (std::fabs(d) * 2.0 == std::fabs(static_cast<double>(g) - static_cast<double>(f)) &&
        (lo > 0.0) == (d > 0.0))
      f = g;
  }
  return f;
}

}  // namespace ragexe::core
