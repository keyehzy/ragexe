#include "render/matrix.h"

#include <cmath>

namespace ragexe::render {

float SinFloat(float radians) { return static_cast<float>(std::sin(static_cast<double>(radians))); }

float CosFloat(float radians) { return static_cast<float>(std::cos(static_cast<double>(radians))); }

float SqrtFloat(float value) { return std::sqrt(value); }

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

Matrix MakeRotationFromKey(const std::array<float, 4>& q) {
  const float x2 = q[0] + q[0];
  const float y2 = q[1] + q[1];
  const float z2 = q[2] + q[2];
  const float wx = x2 * q[3];
  const float wy = y2 * q[3];
  const float wz = z2 * q[3];
  const float xx = x2 * q[0];
  const float xy = y2 * q[0];
  const float xz = z2 * q[0];
  const float yy = y2 * q[1];
  const float yz = z2 * q[1];
  const float zz = z2 * q[2];
  Matrix m{};
  m[0] = 1.0f - (zz + yy);
  m[1] = xy + wz;
  m[2] = xz - wy;
  m[3] = xy - wz;
  m[4] = 1.0f - (zz + xx);
  m[5] = yz + wx;
  m[6] = xz + wy;
  m[7] = yz - wx;
  m[8] = 1.0f - (yy + xx);
  return m;
}

Matrix Multiply(const Matrix& a, const Matrix& b) {
  Matrix r{};
  r[0] = ((a[0] * b[0]) + (a[1] * b[3])) + (a[2] * b[6]);
  r[3] = ((a[4] * b[3]) + (a[3] * b[0])) + (a[5] * b[6]);
  r[6] = ((a[7] * b[3]) + (a[6] * b[0])) + (a[8] * b[6]);
  r[9] = (((a[10] * b[3]) + (a[9] * b[0])) + (a[11] * b[6])) + b[9];
  r[1] = ((a[1] * b[4]) + (a[2] * b[7])) + (a[0] * b[1]);
  r[4] = ((a[5] * b[7]) + (a[4] * b[4])) + (a[3] * b[1]);
  r[7] = ((a[8] * b[7]) + (a[7] * b[4])) + (a[6] * b[1]);
  r[10] = (((a[11] * b[7]) + (a[10] * b[4])) + (a[9] * b[1])) + b[10];
  r[2] = ((a[2] * b[8]) + (a[0] * b[2])) + (a[1] * b[5]);
  r[5] = ((a[5] * b[8]) + (a[4] * b[5])) + (a[3] * b[2]);
  r[8] = ((a[8] * b[8]) + (a[7] * b[5])) + (a[6] * b[2]);
  r[11] = (((a[11] * b[8]) + (a[10] * b[5])) + (a[9] * b[2])) + b[11];
  return r;
}

Matrix RotateX(const Matrix& m, float radians) { return Multiply(m, MakeRotationX(radians)); }
Matrix RotateY(const Matrix& m, float radians) { return Multiply(m, MakeRotationY(radians)); }
Matrix RotateZ(const Matrix& m, float radians) { return Multiply(m, MakeRotationZ(radians)); }
Matrix Translate(const Matrix& m, const Vec3& t) { return Multiply(m, MakeTranslation(t)); }
Matrix Scale(const Matrix& m, const Vec3& s) { return Multiply(m, MakeScale(s)); }

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

Matrix Invert(const Matrix& a) {
  Matrix r{};
  const float c0 = (a[4] * a[8]) - (a[7] * a[5]);
  const float det = ((c0 * a[0]) - (((a[3] * a[8]) - (a[6] * a[5])) * a[1])) +
                    (((a[7] * a[3]) - (a[6] * a[4])) * a[2]);
  const float inv = 1.0f / det;
  r[0] = c0 * inv;
  r[1] = -(((a[1] * a[8]) - (a[7] * a[2])) * inv);
  r[2] = ((a[1] * a[5]) - (a[2] * a[4])) * inv;
  r[3] = -(((a[3] * a[8]) - (a[6] * a[5])) * inv);
  r[4] = ((a[0] * a[8]) - (a[6] * a[2])) * inv;
  r[5] = -(((a[0] * a[5]) - (a[3] * a[2])) * inv);
  r[6] = ((a[7] * a[3]) - (a[6] * a[4])) * inv;
  r[7] = -(((a[7] * a[0]) - (a[6] * a[1])) * inv);
  r[8] = ((a[4] * a[0]) - (a[3] * a[1])) * inv;
  r[9] = -(((a[11] * r[6]) + (a[9] * r[0])) + (a[10] * r[3]));
  r[10] = -(((a[11] * r[7]) + (r[4] * a[10])) + (a[9] * r[1]));
  r[11] = -(((r[5] * a[10]) + (a[9] * r[2])) + (a[11] * r[8]));
  return r;
}

}  // namespace ragexe::render
