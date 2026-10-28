#pragma once

#include <array>

namespace ragexe::render {

using Vec3 = std::array<float, 3>;

// The client's Matrix: a 4 x 3 matrix for row vectors, rows 0 to 2 the linear part and
// row 3 the translation, element (r, c) at r · 3 + c. The functions follow the original's
// order of operations, each rounded to single precision as the client's x87 runs.
using Matrix = std::array<float, 12>;

// The identity at 00605598, which the builders start from.
inline constexpr Matrix kIdentity = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

// SinFloat and CosFloat (00409580, 00409590): the x87's sine and cosine of a float,
// rounded to float.
float SinFloat(float radians);
float CosFloat(float radians);

// SqrtFloat (00409570).
float SqrtFloat(float value);

// MakeRotationX (004095d0) and MakeRotationY (00409620), from the identity.
Matrix MakeRotationX(float radians);
Matrix MakeRotationY(float radians);

// Multiply (00409d90): a, then b.
Matrix Multiply(const Matrix& a, const Matrix& b);

// RotateY (00409770): m, then a rotation about y.
Matrix RotateY(const Matrix& m, float radians);

// MakeLookAt (00409bc0): the view from eye toward target. The columns are the side axis
// up × forward, the true up forward × side and the forward axis, each normalised; the
// translation is −(axis · eye).
Matrix MakeLookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

// Invert (00409ed0): the 3 x 3 part by cofactors over its determinant, and the
// translation −t · inverse.
Matrix Invert(const Matrix& a);

}  // namespace ragexe::render
