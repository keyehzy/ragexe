#include "core/x87.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using ragexe::core::X87;

namespace {

struct FloatOpVector {
  std::uint32_t a, b;
  std::uint32_t results[4];  // a + b, a - b, a * b, a / b
};

struct DoubleOpVector {
  std::uint64_t a, b;
  std::uint64_t results[4];
};

#include "x87_vectors.inc"

template <typename F, typename Bits>
Bits BitsOf(X87<F> v) {
  return std::bit_cast<Bits>(static_cast<F>(v));
}

}  // namespace

TEST_CASE("X87<float> computes as the x87 does under single precision", "[x87]") {
  for (const FloatOpVector& v : kFloatOps) {
    const X87<float> a = std::bit_cast<float>(v.a);
    const X87<float> b = std::bit_cast<float>(v.b);
    INFO("a " << std::hex << v.a << " b " << v.b);
    CHECK(BitsOf<float, std::uint32_t>(a + b) == v.results[0]);
    CHECK(BitsOf<float, std::uint32_t>(a - b) == v.results[1]);
    CHECK(BitsOf<float, std::uint32_t>(a * b) == v.results[2]);
    CHECK(BitsOf<float, std::uint32_t>(a / b) == v.results[3]);
  }
  for (const auto& v : kFloatSqrts) {
    INFO("a " << std::hex << v[0]);
    CHECK(BitsOf<float, std::uint32_t>(Sqrt(X87<float>(std::bit_cast<float>(v[0])))) == v[1]);
  }
}

TEST_CASE("X87<double> computes as the x87 does under double precision", "[x87]") {
  for (const DoubleOpVector& v : kDoubleOps) {
    const X87<double> a = std::bit_cast<double>(v.a);
    const X87<double> b = std::bit_cast<double>(v.b);
    INFO("a " << std::hex << v.a << " b " << v.b);
    CHECK(BitsOf<double, std::uint64_t>(a + b) == v.results[0]);
    CHECK(BitsOf<double, std::uint64_t>(a - b) == v.results[1]);
    CHECK(BitsOf<double, std::uint64_t>(a * b) == v.results[2]);
    CHECK(BitsOf<double, std::uint64_t>(a / b) == v.results[3]);
  }
  for (const auto& v : kDoubleSqrts) {
    INFO("a " << std::hex << v[0]);
    CHECK(BitsOf<double, std::uint64_t>(Sqrt(X87<double>(std::bit_cast<double>(v[0])))) == v[1]);
  }
}

TEST_CASE("X87 negation and absolute value only touch the sign", "[x87]") {
  const X87<float> nan = std::bit_cast<float>(0x7fc12345u);
  CHECK(BitsOf<float, std::uint32_t>(-nan) == 0xffc12345u);
  CHECK(BitsOf<float, std::uint32_t>(Abs(-nan)) == 0x7fc12345u);
  CHECK(BitsOf<float, std::uint32_t>(X87<float>::Indefinite()) == 0xffc00000u);
  CHECK(BitsOf<double, std::uint64_t>(X87<double>::Indefinite()) == 0xfff8000000000000u);
}
