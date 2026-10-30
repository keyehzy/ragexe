#include "core/x87.h"

#include <cmath>
#include <limits>

namespace ragexe::core {

namespace {

// hi rounded to single precision as if the exact value were hi plus a little of lo's sign:
// a tie between two floats goes the way lo leans.
float RoundOnce(double hi, double lo) {
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

}  // namespace

float RoundedProduct(float a, double b) {
  const double hi = static_cast<double>(a) * b;
  return RoundOnce(hi, std::fma(static_cast<double>(a), b, -hi));
}

float RoundedSum(float a, double b) {
  const double x = static_cast<double>(a);
  const double hi = x + b;
  const double other = hi - x;
  return RoundOnce(hi, (x - (hi - other)) + (b - other));
}

float RoundedQuotient(float a, double b) {
  const double hi = static_cast<double>(a) / b;
  const double rest = std::fma(-hi, b, static_cast<double>(a));
  return RoundOnce(hi, b > 0.0 ? rest : -rest);
}

}  // namespace ragexe::core
