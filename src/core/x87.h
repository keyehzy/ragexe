#pragma once

namespace ragexe::core {

// a × b rounded once to single precision, as the x87 rounds a product with a double
// operand under the client's precision control (24 bits, DirectDraw's FPUSETUP).
float RoundedProduct(float a, double b);

// a ÷ b rounded once to single precision, as the x87 divides by an integer fild loads
// exactly, under the same precision control.
float RoundedQuotient(float a, double b);

}  // namespace ragexe::core
