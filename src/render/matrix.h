#pragma once

#include <array>

#include "core/x87.h"

namespace ragexe::render {

using Vec3 = std::array<float, 3>;

// The client's Matrix: a 4 x 3 matrix for row vectors, rows 0 to 2 the linear part and
// row 3 the translation, element (r, c) at r · 3 + c. The functions follow the original's
// order of operations, each rounded to single precision as the client's main thread runs
// the x87. Those that take a precision run at double precision as LazyModelLoadThread does:
// the values the original keeps on the x87 stack keep 53 bits, and those it stores as floats
// (each element, and the few locals named below) round to single precision.
using Matrix = std::array<float, 12>;

// The identity at 00605598, which the builders start from.
inline constexpr Matrix kIdentity = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

// SinFloat and CosFloat (00409580, 00409590): the x87's sine and cosine of a float,
// rounded to float.
float SinFloat(float radians);
float CosFloat(float radians);

// SqrtFloat (00409570).
float SqrtFloat(float value);

// MakeRotationX (004095d0), MakeRotationY (00409620) and MakeRotationZ (00409670), from
// the identity.
Matrix MakeRotationX(float radians);
Matrix MakeRotationY(float radians);
Matrix MakeRotationZ(float radians);

// MakeTranslation (004095a0) and MakeScale (004096c0), from the identity.
Matrix MakeTranslation(const Vec3& t);
Matrix MakeScale(const Vec3& s);

// MakeRotationFromKey (00409ae0): the rotation of a unit quaternion (x, y, z, w). Of the
// doubled components' products, x·2x, x·2y and z·2z stay on the x87 stack.
Matrix MakeRotationFromKey(const std::array<float, 4>& q,
                           core::FpuPrecision precision = core::FpuPrecision::kSingle);

// Multiply (00409d90): a, then b.
Matrix Multiply(const Matrix& a, const Matrix& b,
                core::FpuPrecision precision = core::FpuPrecision::kSingle);

// RotateX (00409730), RotateY (00409770), RotateZ (004097b0), Translate (004096f0) and
// Scale (004097f0): m, then that transform.
Matrix RotateX(const Matrix& m, float radians,
               core::FpuPrecision precision = core::FpuPrecision::kSingle);
Matrix RotateY(const Matrix& m, float radians,
               core::FpuPrecision precision = core::FpuPrecision::kSingle);
Matrix RotateZ(const Matrix& m, float radians,
               core::FpuPrecision precision = core::FpuPrecision::kSingle);
Matrix Translate(const Matrix& m, const Vec3& t,
                 core::FpuPrecision precision = core::FpuPrecision::kSingle);
Matrix Scale(const Matrix& m, const Vec3& s,
             core::FpuPrecision precision = core::FpuPrecision::kSingle);

// MakeLookAt (00409bc0): the view from eye toward target. The columns are the side axis
// up × forward, the true up forward × side and the forward axis, each normalised; the
// translation is −(axis · eye).
Matrix MakeLookAt(const Vec3& eye, const Vec3& target, const Vec3& up);

// Invert (00409ed0): the 3 x 3 part by cofactors over its determinant, and the
// translation −t · inverse. The reciprocal of the determinant is stored as a float; the
// first cofactor stays on the x87 stack.
Matrix Invert(const Matrix& a, core::FpuPrecision precision = core::FpuPrecision::kSingle);

}  // namespace ragexe::render
