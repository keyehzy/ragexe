#pragma once

#include <array>
#include <cstdint>

namespace ragexe::core {

// The sine and cosine tables InitAngleTables (0040b2f0) fills at start-up: whole degrees
// from 0 to 360, each angle k · π · (1/180) in single precision (π as the float at
// 005e2684), its sine and cosine rounded to float.
struct AngleTables {
  std::array<float, 361> sin{};
  std::array<float, 361> cos{};
};

const AngleTables& GetAngleTables();

// SinDegrees and CosDegrees (0040b3c0, 0040b400): angles beyond ±360 are reduced with C's
// remainder. A negative angle −k reads the table 361 − k entries in, one degree past
// 360 − k, since the negative lookup starts just after the last entry.
float SinDegrees(std::int32_t degrees);
float CosDegrees(std::int32_t degrees);

}  // namespace ragexe::core
