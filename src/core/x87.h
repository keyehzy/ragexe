#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <type_traits>

namespace ragexe::core {

// The x87's precision control: the CRT's default, double, which a new thread such as
// LazyModelLoadThread starts with, or single, which DirectDraw's FPUSETUP leaves the main
// thread at. Under double, values on the x87 stack keep 53 bits until stored as floats.
enum class FpuPrecision : std::uint8_t { kDouble, kSingle };

// a × b rounded once to single precision, as the x87 rounds a product with a double
// operand under the client's precision control (24 bits, DirectDraw's FPUSETUP).
float RoundedProduct(float a, double b);

// a ÷ b rounded once to single precision, as the x87 divides by an integer fild loads
// exactly, under the same precision control.
float RoundedQuotient(float a, double b);

// a + b rounded once to single precision, as the x87 adds a double operand under the same
// precision control.
float RoundedSum(float a, double b);

// A value on the x87 stack, F being float under single precision and double under double:
// IEEE arithmetic at that precision, with the x87's NaNs where ARM's and SSE's differ. An
// invalid operation on numbers (0 · ∞, ∞ − ∞, 0 / 0, √−1) gives the negative "real
// indefinite"; a NaN operand passes through, quieted; of two NaN operands the one with the
// larger significand, and of two with the same significand the positive one.
template <typename F>
class X87 {
  static_assert(std::is_same_v<F, float> || std::is_same_v<F, double>);
  using Bits = std::conditional_t<std::is_same_v<F, float>, std::uint32_t, std::uint64_t>;
  static constexpr int kFractionBits = std::is_same_v<F, float> ? 23 : 52;
  static constexpr Bits kSign = Bits{1} << (sizeof(Bits) * 8 - 1);
  static constexpr Bits kQuiet = Bits{1} << (kFractionBits - 1);
  static constexpr Bits kSignificand = (Bits{1} << kFractionBits) - 1;

 public:
  constexpr X87() = default;
  template <typename A>
    requires std::is_arithmetic_v<A>
  constexpr X87(A v) : v_(static_cast<F>(v)) {}  // NOLINT: loads convert implicitly

  explicit constexpr operator float() const { return static_cast<float>(v_); }
  explicit constexpr operator double() const { return static_cast<double>(v_); }

  friend X87 operator+(X87 a, X87 b) { return Result(a.v_, b.v_, a.v_ + b.v_); }
  friend X87 operator-(X87 a, X87 b) { return Result(a.v_, b.v_, a.v_ - b.v_); }
  friend X87 operator*(X87 a, X87 b) { return Result(a.v_, b.v_, a.v_ * b.v_); }
  friend X87 operator/(X87 a, X87 b) { return Result(a.v_, b.v_, a.v_ / b.v_); }
  friend X87 operator-(X87 a) { return X87(-a.v_); }  // fchs
  friend bool operator<(X87 a, X87 b) { return a.v_ < b.v_; }
  friend bool operator<=(X87 a, X87 b) { return a.v_ <= b.v_; }
  friend bool operator>(X87 a, X87 b) { return a.v_ > b.v_; }
  friend bool operator>=(X87 a, X87 b) { return a.v_ >= b.v_; }

  friend X87 Abs(X87 a) { return X87(std::fabs(a.v_)); }  // fabs
  friend X87 Sqrt(X87 a) {                                // fsqrt
    if (std::isnan(a.v_)) return Quiet(a.v_);
    return a.v_ < F{0} ? Indefinite() : X87(std::sqrt(a.v_));
  }

  // The real indefinite: negative, quiet, with an empty payload.
  static X87 Indefinite() {
    return X87(std::bit_cast<F>(kSign | kQuiet | (~kSignificand & ~kSign)));
  }

 private:
  static X87 Quiet(F v) { return X87(std::bit_cast<F>(std::bit_cast<Bits>(v) | kQuiet)); }

  static X87 Result(F a, F b, F r) {
    const bool nan_a = std::isnan(a);
    const bool nan_b = std::isnan(b);
    if (!nan_a && !nan_b) return std::isnan(r) ? Indefinite() : X87(r);
    if (nan_a && nan_b) {
      const Bits sa = std::bit_cast<Bits>(a) & kSignificand;
      const Bits sb = std::bit_cast<Bits>(b) & kSignificand;
      if (sa != sb) return Quiet(sa > sb ? a : b);
      return Quiet((std::bit_cast<Bits>(a) & kSign) != 0 ? b : a);
    }
    return Quiet(nan_a ? a : b);
  }

  F v_ = 0;
};

}  // namespace ragexe::core
