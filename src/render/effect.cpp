#include "render/effect.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

namespace ragexe::render {

namespace {

constexpr float kNear = 10.0f;     // 005e9dc0
constexpr float kFar = 1500.0f;    // 005e9dbc
constexpr float kFrameX = 320.0f;  // 005e9df4, the centre of the effect editor's frame
constexpr float kFrameY = 240.0f;  // 005e9df0
constexpr double kTurnStep = std::bit_cast<double>(0x3fd6800000000063ull);  // 005e9df8
constexpr float kPi = std::bit_cast<float>(0x40490fd8u);                    // 005e9db8
constexpr float kInverse180 = std::bit_cast<float>(0x3bb60b61u);            // 005e2690
constexpr std::uint32_t kVertexZ = 0x3727c5ac;    // 1e-5, at the near plane
constexpr std::uint32_t kVertexRhw = 0x3f7fff58;  // 0.99999
constexpr std::int32_t kNoKey = -1;

std::int32_t Ftol(float f) {
  if (std::isnan(f) || f <= -2147483904.0f || f >= 2147483648.0f)
    return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(f);
}

// a × b rounded once to single precision, as the x87 rounds a product with a double
// operand under the client's precision control.
float RoundedProduct(float a, double b) {
  const double hi = static_cast<double>(a) * b;
  const double lo = std::fma(static_cast<double>(a), b, -hi);
  float f = static_cast<float>(hi);
  const double d = hi - static_cast<double>(f);
  if (lo != 0.0 && d != 0.0) {
    const float g = std::nextafter(f, d > 0.0 ? std::numeric_limits<float>::infinity()
                                              : -std::numeric_limits<float>::infinity());
    if (std::fabs(d) * 2.0 == std::fabs(static_cast<double>(g) - static_cast<double>(f)) &&
        (lo > 0.0) == (d > 0.0))
      f = g;
  }
  return f;
}

const res::StrKey* KeyAt(const res::StrLayer& layer, std::int64_t i) {
  if (i < 0 || static_cast<std::size_t>(i) >= layer.keys.size()) return nullptr;
  return &layer.keys[static_cast<std::size_t>(i)];
}

}  // namespace

float StrLayerState::Get(std::size_t i) const { return std::bit_cast<float>(values[i]); }
void StrLayerState::Set(std::size_t i, float f) { values[i] = std::bit_cast<std::uint32_t>(f); }

StrPlayer::StrPlayer(const res::Str& str) : str_(&str) {
  states_.resize(str.layers.size());
  keys_.resize(str.layers.size());
  moved_.resize(str.layers.size());
  Reset();
}

void StrPlayer::Reset() {
  frame_ = 0;
  finished_ = 0;
  limit_ = static_cast<std::int32_t>(str_->layers.size());
  if (str_->layers.empty() || str_->layers[0].keys.empty()) --limit_;
  for (std::int32_t& k : keys_) k = 0;
}

bool StrPlayer::Advance(const Matrix& view, const Vec3& position, const Projection& projection,
                        float offset_x, float offset_y, float offset_z) {
  if (!(finished_ < limit_)) return false;
  const Matrix& m = view;
  const Vec3& p = position;
  const float x = (((m[6] * p[2]) + (m[3] * p[1])) + (m[0] * p[0])) + m[9];
  const float y = (((m[7] * p[2]) + (m[4] * p[1])) + (m[1] * p[0])) + m[10];
  float z = (((m[8] * p[2]) + (m[5] * p[1])) + (m[2] * p[0])) + m[11];
  if (!(z >= kNear)) z = kNear;
  const float inverse = 1.0f / z;
  const float sx = ((inverse * projection.scale_x) * x) + static_cast<float>(projection.origin_x);
  const float sy = ((y * projection.scale_y) * inverse) + static_cast<float>(projection.origin_y);
  const float range = kFar / (kFar - kNear);
  const float depth = (((1.0f / inverse) - kNear) * range) * inverse;
  screen_x_ = sx + offset_x;
  screen_y_ = sy + offset_y;
  screen_z_ = depth + offset_z;

  // Modes 3 and 4 keep a quotient in the stack slot of the z offset, so a later layer's
  // mode 4 starts from what an earlier layer left there.
  float slot = offset_z;
  for (std::size_t l = 0; l < str_->layers.size(); ++l) {
    moved_[l] = false;
    const res::StrLayer& layer = str_->layers[l];
    std::int32_t& current = keys_[l];
    if (layer.keys.empty() || current == kNoKey) continue;
    std::int64_t at = current;
    const res::StrKey* key = KeyAt(layer, at);
    if (key == nullptr) continue;
    const std::int32_t since = frame_ - key->frame;
    if (since < 0) continue;
    if (since != 0) {
      const res::StrKey* next = KeyAt(layer, at + 1);
      if (next != nullptr && next->frame == frame_) {
        ++current;
        if (current >= static_cast<std::int32_t>(layer.keys.size()) - 1) {
          current = -1;
          ++finished_;
        }
        key = next;
        ++at;
      }
    }
    StrLayerState& s = states_[l];
    if (key->type == 0) {
      s.values = key->values;
      const res::StrKey* next = KeyAt(layer, at + 1);
      if (current != -1 && next != nullptr && next->frame == key->frame) ++current;
    } else {
      const auto add = [&](std::size_t i) {
        s.Set(i, std::bit_cast<float>(key->values[i]) + s.Get(i));
      };
      for (std::size_t i = 0; i <= 0x11; ++i) add(i);
      for (std::size_t i = 0x16; i <= 0x19; ++i) add(i);
      add(str_value::kAngle);
      const float step = std::bit_cast<float>(key->values[str_value::kTextureStep]);
      const float count = static_cast<float>(layer.texture_count);
      switch (static_cast<std::int32_t>(key->values[str_value::kTextureMode])) {
        case 1: add(str_value::kTexture); break;
        case 2: {
          const float v = step + s.Get(str_value::kTexture);
          s.Set(str_value::kTexture, v < count || std::isnan(v) ? v : count - 1.0f);
          break;
        }
        case 3: {
          float v = step + s.Get(str_value::kTexture);
          if (!(count > v)) {
            slot = static_cast<float>(Ftol(v / count));
            v = v - (count * slot);
          }
          s.Set(str_value::kTexture, v);
          break;
        }
        case 4: {
          float v = s.Get(str_value::kTexture) - step;
          if (!(v >= 0.0f)) {
            v = v - (count * slot);
            slot = static_cast<float>(Ftol(v / count));
            if (!(v >= 0.0f)) v = v + count;
          }
          s.Set(str_value::kTexture, v);
          break;
        }
        case 5: {
          const std::int32_t v =
              Ftol((static_cast<float>(since) * step) + s.Get(str_value::kTexture));
          const std::int32_t n = layer.texture_count - 1;
          if (n == 0) break;
          const std::int32_t q = v / n;
          s.Set(str_value::kTexture,
                static_cast<float>((q & 1) != 0 ? (q + 1) * n - v : v - q * n));
          break;
        }
        default: break;
      }
    }
    moved_[l] = true;
  }
  return true;
}

std::vector<EffectTriangle> StrPlayer::Draw() const {
  using namespace str_value;
  std::vector<EffectTriangle> out;
  for (std::size_t l = 1; l < str_->layers.size(); ++l) {
    if (!moved_[l]) continue;
    const StrLayerState& s = states_[l];
    const auto channel = [&](std::size_t i) {
      return static_cast<std::uint32_t>(Ftol(s.Get(i))) & 0xff;
    };
    const std::uint32_t color =
        channel(kRed + 3) << 24 | channel(kRed) << 16 | channel(kRed + 1) << 8 | channel(kRed + 2);
    std::array<ScreenVertex, 4> v{};
    for (ScreenVertex& vertex : v) {
      vertex.z = std::bit_cast<float>(kVertexZ);
      vertex.rhw = std::bit_cast<float>(kVertexRhw);
      vertex.diffuse = color;
      vertex.specular = 0xffffffff;
    }
    const float u0 = s.Get(kU);
    const float u1 = s.Get(kUSize) + s.Get(kU);
    if (!(s.Get(kCornerX) >= s.Get(kCornerX + 1))) {
      v[1].x = s.Get(kCornerX);
      v[1].u = u0;
      v[3].x = s.Get(kCornerX + 1);
      v[3].u = u1;
      v[0].x = s.Get(kCornerX + 3);
      v[0].u = u0;
      v[2].x = s.Get(kCornerX + 2);
      v[2].u = u1;
    } else {
      v[1].x = s.Get(kCornerX + 1);
      v[1].u = u1;
      v[3].x = s.Get(kCornerX);
      v[3].u = u0;
      v[0].x = s.Get(kCornerX + 2);
      v[0].u = u1;
      v[2].x = s.Get(kCornerX + 3);
      v[2].u = u0;
    }
    const float v0 = s.Get(kV);
    const float v1 = s.Get(kVSize) + s.Get(kV);
    if (!(s.Get(kCornerY + 1) >= s.Get(kCornerY + 2))) {
      v[3].y = s.Get(kCornerY + 1);
      v[3].v = v0;
      v[2].y = s.Get(kCornerY + 2);
      v[2].v = v1;
      v[1].y = s.Get(kCornerY);
      v[1].v = v0;
      v[0].y = s.Get(kCornerY + 3);
      v[0].v = v1;
    } else {
      v[3].y = s.Get(kCornerY + 2);
      v[3].v = v1;
      v[2].y = s.Get(kCornerY + 1);
      v[2].v = v0;
      v[1].y = s.Get(kCornerY + 3);
      v[1].v = v1;
      v[0].y = s.Get(kCornerY);
      v[0].v = v0;
    }
    const float angle = (RoundedProduct(s.Get(kAngle), kTurnStep) * kPi) * kInverse180;
    const float sine = SinFloat(angle);
    const float cosine = CosFloat(angle);
    for (ScreenVertex& vertex : v) {
      const float x0 = vertex.x;
      const float y0 = vertex.y;
      vertex.x = (((x0 * cosine) - (y0 * sine)) + (s.Get(kX) - kFrameX)) + screen_x_;
      vertex.y = (((y0 * cosine) + (x0 * sine)) + (s.Get(kY) - kFrameY)) + screen_y_;
    }
    std::int32_t texture = Ftol(s.Get(kTexture));
    if (texture <= 0) texture = 0;
    EffectTriangle t;
    t.layer = static_cast<std::int32_t>(l);
    t.texture = texture;
    t.src_blend = s.values[kSourceBlend];
    t.dest_blend = s.values[kDestBlend];
    t.mode = s.values[kStageMode];
    t.vertices = {v[0], v[1], v[2]};
    out.push_back(t);
    t.vertices = {v[2], v[1], v[3]};
    out.push_back(t);
  }
  return out;
}

}  // namespace ragexe::render
