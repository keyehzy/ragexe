#include "render/matrix.h"

#include <cmath>
#include <cstddef>

namespace ragexe::render {

float SinFloat(float radians) { return static_cast<float>(std::sin(static_cast<double>(radians))); }

float CosFloat(float radians) { return static_cast<float>(std::cos(static_cast<double>(radians))); }

// fsqrt gives the real indefinite for a negative number (core::X87).
float SqrtFloat(float value) { return static_cast<float>(Sqrt(core::X87<float>(value))); }

Matrix MakeRotationX(float radians) {
  Matrix m = kIdentity;
  const float s = SinFloat(radians);
  const float c = CosFloat(radians);
  m[4] = c;
  m[8] = c;
  m[7] = -s;
  m[5] = s;
  return m;
}

Matrix MakeRotationY(float radians) {
  Matrix m = kIdentity;
  const float s = SinFloat(radians);
  const float c = CosFloat(radians);
  m[0] = c;
  m[8] = c;
  m[2] = -s;
  m[6] = s;
  return m;
}

Matrix MakeRotationZ(float radians) {
  Matrix m = kIdentity;
  const float s = SinFloat(radians);
  const float c = CosFloat(radians);
  m[0] = c;
  m[4] = c;
  m[3] = -s;
  m[1] = s;
  return m;
}

Matrix MakeTranslation(const Vec3& t) {
  Matrix m = kIdentity;
  m[9] = t[0];
  m[10] = t[1];
  m[11] = t[2];
  return m;
}

Matrix MakeScale(const Vec3& s) {
  Matrix m = kIdentity;
  m[0] = s[0];
  m[4] = s[1];
  m[8] = s[2];
  return m;
}

namespace {

// T is the x87's value at the thread's precision control: core::X87<float> under single
// precision, core::X87<double> under double.
template <typename T>
std::array<T, 12> Widen(const Matrix& m) {
  std::array<T, 12> r{};
  for (std::size_t i = 0; i < 12; ++i) r[i] = static_cast<T>(m[i]);
  return r;
}

template <typename T>
float Store(T v) {
  return static_cast<float>(v);
}

template <typename T>
Matrix MakeRotationFromKeyAs(const std::array<float, 4>& key) {
  const T q0 = key[0], q1 = key[1], q2 = key[2], q3 = key[3];
  const T x2 = q0 + q0;
  const float y2 = Store(q1 + q1);
  const float z2 = Store(q2 + q2);
  const float wx = Store(x2 * q3);
  const float wy = Store(y2 * q3);
  const float wz = Store(z2 * q3);
  const T xx = x2 * q0;
  const T xy = y2 * q0;
  const float xz = Store(z2 * q0);
  const float yy = Store(y2 * q1);
  const float yz = Store(z2 * q1);
  const T zz = z2 * q2;
  const T one = 1;
  Matrix m{};
  m[0] = Store(one - (zz + yy));
  m[1] = Store(xy + wz);
  m[2] = Store(T{xz} - wy);
  m[3] = Store(xy - wz);
  m[4] = Store(one - (T{Store(zz)} + xx));
  m[5] = Store(T{yz} + wx);
  m[6] = Store(T{xz} + wy);
  m[7] = Store(T{yz} - wx);
  m[8] = Store(one - (T{yy} + xx));
  return m;
}

template <typename T>
Matrix MultiplyAs(const Matrix& left, const Matrix& right) {
  const std::array<T, 12> a = Widen<T>(left);
  const std::array<T, 12> b = Widen<T>(right);
  Matrix r{};
  r[0] = Store(((a[0] * b[0]) + (a[1] * b[3])) + (a[2] * b[6]));
  r[3] = Store(((a[4] * b[3]) + (a[3] * b[0])) + (a[5] * b[6]));
  r[6] = Store(((a[7] * b[3]) + (a[6] * b[0])) + (a[8] * b[6]));
  r[9] = Store((((a[10] * b[3]) + (a[9] * b[0])) + (a[11] * b[6])) + b[9]);
  r[1] = Store(((a[1] * b[4]) + (a[2] * b[7])) + (a[0] * b[1]));
  r[4] = Store(((a[5] * b[7]) + (a[4] * b[4])) + (a[3] * b[1]));
  r[7] = Store(((a[8] * b[7]) + (a[7] * b[4])) + (a[6] * b[1]));
  r[10] = Store((((a[11] * b[7]) + (a[10] * b[4])) + (a[9] * b[1])) + b[10]);
  r[2] = Store(((a[2] * b[8]) + (a[0] * b[2])) + (a[1] * b[5]));
  r[5] = Store(((a[5] * b[8]) + (a[4] * b[5])) + (a[3] * b[2]));
  r[8] = Store(((a[8] * b[8]) + (a[7] * b[5])) + (a[6] * b[2]));
  r[11] = Store((((a[11] * b[8]) + (a[10] * b[5])) + (a[9] * b[2])) + b[11]);
  return r;
}

template <typename T>
Matrix InvertAs(const Matrix& m) {
  const std::array<T, 12> a = Widen<T>(m);
  Matrix out{};
  const T c0 = (a[4] * a[8]) - (a[7] * a[5]);
  const T det = ((c0 * a[0]) - (((a[3] * a[8]) - (a[6] * a[5])) * a[1])) +
                (((a[7] * a[3]) - (a[6] * a[4])) * a[2]);
  const T inv = Store(T{1} / det);
  out[0] = Store(c0 * inv);
  out[1] = Store(-(((a[1] * a[8]) - (a[7] * a[2])) * inv));
  out[2] = Store(((a[1] * a[5]) - (a[2] * a[4])) * inv);
  out[3] = Store(-(((a[3] * a[8]) - (a[6] * a[5])) * inv));
  out[4] = Store(((a[0] * a[8]) - (a[6] * a[2])) * inv);
  out[5] = Store(-(((a[0] * a[5]) - (a[3] * a[2])) * inv));
  out[6] = Store(((a[7] * a[3]) - (a[6] * a[4])) * inv);
  out[7] = Store(-(((a[7] * a[0]) - (a[6] * a[1])) * inv));
  out[8] = Store(((a[4] * a[0]) - (a[3] * a[1])) * inv);
  const std::array<T, 12> r = Widen<T>(out);
  out[9] = Store(-(((a[11] * r[6]) + (a[9] * r[0])) + (a[10] * r[3])));
  out[10] = Store(-(((a[11] * r[7]) + (r[4] * a[10])) + (a[9] * r[1])));
  out[11] = Store(-(((r[5] * a[10]) + (a[9] * r[2])) + (a[11] * r[8])));
  return out;
}

}  // namespace

Matrix MakeRotationFromKey(const std::array<float, 4>& q, core::FpuPrecision precision) {
  return precision == core::FpuPrecision::kDouble ? MakeRotationFromKeyAs<core::X87<double>>(q)
                                                  : MakeRotationFromKeyAs<core::X87<float>>(q);
}

Matrix Multiply(const Matrix& a, const Matrix& b, core::FpuPrecision precision) {
  return precision == core::FpuPrecision::kDouble ? MultiplyAs<core::X87<double>>(a, b)
                                                  : MultiplyAs<core::X87<float>>(a, b);
}

Matrix RotateX(const Matrix& m, float radians, core::FpuPrecision precision) {
  return Multiply(m, MakeRotationX(radians), precision);
}
Matrix RotateY(const Matrix& m, float radians, core::FpuPrecision precision) {
  return Multiply(m, MakeRotationY(radians), precision);
}
Matrix RotateZ(const Matrix& m, float radians, core::FpuPrecision precision) {
  return Multiply(m, MakeRotationZ(radians), precision);
}
Matrix Translate(const Matrix& m, const Vec3& t, core::FpuPrecision precision) {
  return Multiply(m, MakeTranslation(t), precision);
}
Matrix Scale(const Matrix& m, const Vec3& s, core::FpuPrecision precision) {
  return Multiply(m, MakeScale(s), precision);
}

Matrix MakeLookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
  Matrix m = kIdentity;
  Vec3 f = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
  float r = 1.0f / SqrtFloat(((f[2] * f[2]) + (f[0] * f[0])) + (f[1] * f[1]));
  f = {f[0] * r, f[1] * r, f[2] * r};
  Vec3 s = {(f[2] * up[1]) - (f[1] * up[2]), (f[0] * up[2]) - (f[2] * up[0]),
            (f[1] * up[0]) - (f[0] * up[1])};
  r = 1.0f / SqrtFloat(((s[2] * s[2]) + (s[1] * s[1])) + (s[0] * s[0]));
  s = {r * s[0], r * s[1], r * s[2]};
  Vec3 u = {(f[1] * s[2]) - (f[2] * s[1]), (f[2] * s[0]) - (f[0] * s[2]),
            (f[0] * s[1]) - (f[1] * s[0])};
  r = 1.0f / SqrtFloat(((u[2] * u[2]) + (u[1] * u[1])) + (u[0] * u[0]));
  u = {r * u[0], r * u[1], r * u[2]};
  m[0] = s[0];
  m[3] = s[1];
  m[6] = s[2];
  m[2] = f[0];
  m[1] = u[0];
  m[5] = f[1];
  m[4] = u[1];
  m[8] = f[2];
  m[7] = u[2];
  const auto dot = [&](const Vec3& a) {
    return -(((a[2] * eye[2]) + (a[1] * eye[1])) + (a[0] * eye[0]));
  };
  m[9] = dot(s);
  m[10] = dot(u);
  m[11] = dot(f);
  return m;
}

Matrix Invert(const Matrix& a, core::FpuPrecision precision) {
  return precision == core::FpuPrecision::kDouble ? InvertAs<core::X87<double>>(a)
                                                  : InvertAs<core::X87<float>>(a);
}

}  // namespace ragexe::render
