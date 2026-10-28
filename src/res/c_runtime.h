#pragma once

#include <cstdint>
#include <string_view>

namespace ragexe::res {

// isspace in the C locale; bytes of 0x80 and above never count.
bool IsCSpace(char c);

// atoi (atol in the C runtime): leading spaces, a sign, then digits, wrapping at 32 bits.
std::int32_t AtoI(std::string_view s);

// atof: leading spaces, a sign, digits with a decimal point, and an exponent after e, E,
// d or D. Nothing else, so no hexadecimal, inf or nan; 0 when there are no digits.
double AtoF(std::string_view s);

}  // namespace ragexe::res
