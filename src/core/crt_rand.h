#pragma once

#include <cstdint>

namespace ragexe::core {

// The C runtime's srand and rand (005c2546, 005c2553): a linear congruential generator
// whose state, kept per thread at +0x14 of the CRT's thread data, starts at 1.
class CrtRand {
 public:
  explicit CrtRand(std::uint32_t seed = 1) : state_(seed) {}
  void Seed(std::uint32_t seed) { state_ = seed; }

  // The next value, 0 to 0x7fff: the state times 0x343fd plus 0x269ec3, its bits 16 to 30.
  std::int32_t Next();

  std::uint32_t state() const { return state_; }

 private:
  std::uint32_t state_;
};

}  // namespace ragexe::core
