#include "render/matrix.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include "render/model.h"

using namespace ragexe::render;
using ragexe::core::FpuPrecision;

namespace {

struct MatrixVector {
  std::uint32_t a[12], b[12];
  std::uint32_t product[2][12];  // at single precision, then at double
  std::uint32_t inverse[2][12];
};

struct KeyRotationVector {
  std::uint32_t q[4];
  std::uint32_t matrix[2][12];
};

struct TrackVector {
  std::size_t count;
  std::int32_t frames[4];
  std::uint32_t values[16];  // four per key
  std::int32_t time;
  std::uint32_t matrix[2][12];
};

#include "matrix_vectors.inc"

constexpr FpuPrecision kPrecisions[2] = {FpuPrecision::kSingle, FpuPrecision::kDouble};

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }

Matrix MatrixOf(const std::uint32_t* bits) {
  Matrix m{};
  for (std::size_t i = 0; i < 12; ++i) m[i] = F(bits[i]);
  return m;
}

void CheckMatrix(const Matrix& got, const std::uint32_t* want) {
  for (std::size_t i = 0; i < 12; ++i) {
    INFO("element " << i);
    CHECK(std::bit_cast<std::uint32_t>(got[i]) == want[i]);
  }
}

}  // namespace

TEST_CASE("Multiply and Invert compute as the original does at either precision", "[matrix]") {
  std::size_t index = 0;
  for (const MatrixVector& v : kMatrixVectors) {
    INFO("case " << index++);
    for (std::size_t p = 0; p < 2; ++p) {
      INFO("precision " << p);
      CheckMatrix(Multiply(MatrixOf(v.a), MatrixOf(v.b), kPrecisions[p]), v.product[p]);
      CheckMatrix(Invert(MatrixOf(v.a), kPrecisions[p]), v.inverse[p]);
    }
  }
}

TEST_CASE("MakeRotationFromKey computes as the original does at either precision", "[matrix]") {
  std::size_t index = 0;
  for (const KeyRotationVector& v : kKeyRotationVectors) {
    INFO("case " << index++);
    for (std::size_t p = 0; p < 2; ++p) {
      INFO("precision " << p);
      CheckMatrix(MakeRotationFromKey({F(v.q[0]), F(v.q[1]), F(v.q[2]), F(v.q[3])}, kPrecisions[p]),
                  v.matrix[p]);
    }
  }
}

// NodeMatrixAt with the identity for the other two kinds of key gives one track's matrix.
TEST_CASE("Key tracks interpolate as the original does at either precision", "[matrix]") {
  std::size_t index = 0;
  for (const TrackVector& v : kTranslationTracks) {
    INFO("translation track " << index++ << " at " << v.time);
    NodeKeys keys;
    for (std::size_t k = 0; k < v.count; ++k)
      keys.translation.push_back(
          {v.frames[k], {F(v.values[k * 4]), F(v.values[k * 4 + 1]), F(v.values[k * 4 + 2])}});
    keys.rotation.push_back({0, {0.0f, 0.0f, 0.0f, 1.0f}});
    keys.scale = {1.0f, 1.0f, 1.0f};
    for (std::size_t p = 0; p < 2; ++p) {
      INFO("precision " << p);
      CheckMatrix(NodeMatrixAt(keys, v.time, kPrecisions[p]), v.matrix[p]);
    }
  }
  index = 0;
  for (const TrackVector& v : kRotationTracks) {
    INFO("rotation track " << index++ << " at " << v.time);
    NodeKeys keys;
    keys.translation.push_back({0, {0.0f, 0.0f, 0.0f}});
    for (std::size_t k = 0; k < v.count; ++k)
      keys.rotation.push_back({v.frames[k],
                               {F(v.values[k * 4]), F(v.values[k * 4 + 1]), F(v.values[k * 4 + 2]),
                                F(v.values[k * 4 + 3])}});
    keys.scale = {1.0f, 1.0f, 1.0f};
    for (std::size_t p = 0; p < 2; ++p) {
      INFO("precision " << p);
      CheckMatrix(NodeMatrixAt(keys, v.time, kPrecisions[p]), v.matrix[p]);
    }
  }
}

TEST_CASE("SqrtFloat, SinFloat and CosFloat give the original's floats", "[matrix]") {
  for (const auto& v : kSqrtFloats) {
    INFO("x " << std::hex << v[0]);
    CHECK(std::bit_cast<std::uint32_t>(SqrtFloat(F(v[0]))) == v[1]);
  }
  for (const auto& v : kTrigFloats) {
    INFO("x " << std::hex << v[0]);
    CHECK(std::bit_cast<std::uint32_t>(SinFloat(F(v[0]))) == v[1]);
    CHECK(std::bit_cast<std::uint32_t>(CosFloat(F(v[0]))) == v[2]);
  }
}
