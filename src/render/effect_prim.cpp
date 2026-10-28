#include "render/effect_prim.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

#include "core/angles.h"
#include "render/effect_table.h"
#include "res/act.h"
#include "res/gat.h"
#include "res/sprite.h"

namespace ragexe::render {

namespace {

constexpr std::uint32_t kNoChange = 0x38d1b717;                // 0.0001
constexpr float kNear = 10.0f;                                 // 005e9dc0
constexpr float kHalf = 0.5f;                                  // 005e25b8
constexpr float kQuadTip = std::bit_cast<float>(0x3f555555u);  // 005e9f90, 5/6
constexpr std::uint32_t kVertexZ = 0x358637bd;                 // 1e-6, at the near plane
constexpr std::uint32_t kVertexRhw = 0x3f7fffef;
constexpr float kFar = 1500.0f;                                   // 005e9f60
constexpr float kHundredth = std::bit_cast<float>(0x3c23d70au);   // 005e39dc, 0.01
constexpr float kBounceBack = std::bit_cast<float>(0xbf2aaaabu);  // 005e9e04, -2/3
constexpr float kPixel = std::bit_cast<float>(0x3e124925u);       // 005e9564, 1/7
constexpr float kRhwBias = std::bit_cast<float>(0x3727c5acu);     // 005e9568, 1e-5
constexpr float kEndDistance = 3.0f;                              // 005e29d8
constexpr float kCylinderU = 0.25f;                               // 005e653c

std::int32_t Ftol(float f) {
  if (std::isnan(f) || f <= -2147483904.0f || f >= 2147483648.0f)
    return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(f);
}

float Sin(float degrees) { return core::SinDegrees(Ftol(degrees)); }
float Cos(float degrees) { return core::CosDegrees(Ftol(degrees)); }
float Radians(float degrees) { return core::DegreesToRadians(Ftol(degrees)); }

// TransformPoint (00550ff0): a point through a view, row vectors.
Vec3 TransformPoint(const Matrix& m, const Vec3& p) {
  return {(((m[3] * p[1]) + (m[6] * p[2])) + (p[0] * m[0])) + m[9],
          (((m[1] * p[0]) + (m[4] * p[1])) + (m[7] * p[2])) + m[10],
          (((m[2] * p[0]) + (m[5] * p[1])) + (m[8] * p[2])) + m[11]};
}

// The same as RenderCylinder and RenderActFrameHistory inline it, each sum from z.
Vec3 TransformPointFromZ(const Matrix& m, const Vec3& p) {
  return {(((p[2] * m[6]) + (m[3] * p[1])) + (p[0] * m[0])) + m[9],
          (((p[2] * m[7]) + (m[4] * p[1])) + (m[1] * p[0])) + m[10],
          (((p[2] * m[8]) + (m[5] * p[1])) + (m[2] * p[0])) + m[11]};
}

// SceneRenderer::ProjectPoint (00551050) of a view-space point whose z is already at least
// the near plane. RenderCylinder inlines it once with x scaled before the 1/z.
ScreenVertex ProjectPoint(const Vec3& v, const Projection& projection, bool scale_first = false) {
  ScreenVertex out;
  const float inverse = 1.0f / v[2];
  out.x = (scale_first ? (inverse * projection.scale_x) * v[0]
                       : (inverse * v[0]) * projection.scale_x) +
          static_cast<float>(projection.origin_x);
  out.y = ((v[1] * projection.scale_y) * inverse) + static_cast<float>(projection.origin_y);
  out.z = (((1.0f / inverse) - kNear) * (kFar / (kFar - kNear))) * inverse;
  out.rhw = inverse;
  return out;
}

Vec3 ClampNear(Vec3 v) {
  if (!(v[2] >= kNear)) v[2] = kNear;
  return v;
}

std::uint32_t PackColor(float alpha, const EffectPrim& p) {
  return (static_cast<std::uint32_t>(Ftol(alpha)) & 0xff) << 24 |
         (static_cast<std::uint32_t>(p.red) & 0xff) << 16 |
         (static_cast<std::uint32_t>(p.green) & 0xff) << 8 |
         (static_cast<std::uint32_t>(p.blue) & 0xff);
}

// The alpha's speed turned back at its limits (+0x13c bit 0x80), as UpdatePrim_0 and
// UpdateAlphaAndColor test it.
void Bounce(EffectPrim& p) {
  if (!(p.alpha_speed >= 0.0f)) {
    if (!(p.alpha > p.alpha_min)) p.alpha_speed = p.alpha_speed * -1.0f;
  } else if (p.alpha >= p.alpha_max) {
    p.alpha_speed = p.alpha_speed * -1.0f;
  }
}

// CEffectPrim::UpdateAlphaAndColor (0057d6c0).
void UpdateAlpha(EffectPrim& p) {
  const std::int32_t age = p.age;
  const std::size_t keys = p.key_frames.size();
  if (p.alpha_keys) {
    if (p.key < 0 || static_cast<std::uint32_t>(p.key) > static_cast<std::uint32_t>(keys) - 1u)
      p.key = 0;
    const auto k = static_cast<std::size_t>(p.key);
    if (k < keys && static_cast<float>(age) == p.key_frames[k]) {
      p.alpha = p.key_values[k];
      p.alpha_max = p.key_limits[k];
      ++p.key;
    }
  } else if (p.speed_keys) {
    const auto k = static_cast<std::size_t>(p.key);
    if (p.key >= 0 && k < keys && static_cast<float>(age) == p.key_frames[k]) {
      ++p.key;
      p.alpha_speed = p.key_values[k];
    }
  }
  if ((p.flags & EffectPrim::kBounce) != 0) Bounce(p);
  if (!p.fading) {
    const std::int32_t left = p.owner->duration - p.owner->frame;
    if (p.life - age > left + 1) {
      p.fading = true;
      p.flags &= ~EffectPrim::kBounce;
      p.alpha_speed = -(p.alpha / static_cast<float>(left));
    }
  }
  if (age == p.fade_at) {
    p.flags &= ~EffectPrim::kBounce;
    p.alpha_speed = -((p.alpha / static_cast<float>(p.life - age)) * p.fade_scale);
  }
  float a = p.alpha_speed + p.alpha;
  if (!(a >= p.alpha_min)) a = p.alpha_min;
  if (a > p.alpha_max) a = p.alpha_max;
  p.alpha = a;
  p.color = PackColor(a, p);
}

PrimHistory Snapshot(const EffectPrim& p) {
  PrimHistory h;
  h.position = p.position;
  h.points = p.points;
  h.distance = p.distance;
  h.scale = p.scale;
  h.heading = p.heading;
  h.matrix = p.matrix;
  h.alpha = p.alpha;
  h.color = PackColor(p.alpha, p);
  return h;
}

// CEffectPrim::ShiftHistory (0057da50): each record moves back one, its alpha and scale
// tapering with its place k of n (alpha × (1 − k/n), scale × (1 − k/2n)), and the first
// becomes the primitive as it is now. Red, green and blue stay as ResetHistory left them.
void ShiftHistory(EffectPrim& p) {
  const std::int32_t n = p.history_count;
  for (std::int32_t k = n - 1; k > 0; --k) {
    PrimHistory& to = p.history[static_cast<std::size_t>(k)];
    const PrimHistory& from = p.history[static_cast<std::size_t>(k - 1)];
    to.position = from.position;
    to.points = from.points;
    to.distance = from.distance;
    to.heading = from.heading;
    to.matrix = from.matrix;
    const auto place = static_cast<float>(k);
    to.alpha = p.alpha - ((p.alpha / static_cast<float>(n)) * place);
    to.color = PackColor(to.alpha, p);
    to.scale = p.scale - (((p.scale / static_cast<float>(n)) * place) * kHalf);
  }
  PrimHistory& first = p.history[0];
  const std::int32_t red = first.red, green = first.green, blue = first.blue;
  first = Snapshot(p);
  first.red = red;
  first.green = green;
  first.blue = blue;
}

// CActRes::GetFrameCount (00421a80): 1 for an action of fewer than two frames.
std::int32_t FrameCount(const res::Act* act, std::int32_t action) {
  if (act == nullptr || action < 0 || static_cast<std::size_t>(action) >= act->actions.size())
    return 1;
  const std::size_t n = act->actions[static_cast<std::size_t>(action)].size();
  return n < 2 ? 1 : static_cast<std::int32_t>(n);
}

// CEffectPrim::AdvanceActionFrame (0057d620): AdvanceFrameByCount against the frames of
// the sprite's action.
bool AdvanceAction(EffectPrim& p) {
  if (p.age % p.image_delay == 0) {
    ++p.image;
    const std::int32_t count = FrameCount(p.sprite.act, p.action);
    if (!p.image_loop) {
      if (p.image == count) return false;
    } else {
      p.image %= count;
    }
  }
  return true;
}

// The owner's position plus the offset, where ids 5, 6 and 0xd start each step.
void FollowOwner(EffectPrim& p) {
  for (std::size_t i = 0; i < 3; ++i) p.start[i] = p.owner->position[i] + p.offset[i];
}

void PlaceAtTravel(EffectPrim& p) {
  for (std::size_t i = 0; i < 3; ++i) p.position[i] = p.start[i] + p.travel[i];
}

// The matrix's z row times the speed, plus its translation.
Vec3 AlongZ(const EffectPrim& p, float speed) {
  const Matrix& m = p.matrix;
  return {(speed * m[6]) + m[9], (speed * m[7]) + m[10], (speed * m[8]) + m[11]};
}

// CEffectPrim::AdvanceFrameByCount (0057d680): every image_delay frames the next image,
// wrapping when it loops; false when one that does not loop runs out.
bool AdvanceImage(EffectPrim& p) {
  if (p.age % p.image_delay == 0) {
    ++p.image;
    if (!p.image_loop) {
      if (p.image == p.image_count) return false;
    } else {
      p.image %= p.image_count;
    }
  }
  return true;
}

void TakeChange(std::uint32_t change, float& value) {
  if (change != kNoChange) value = std::bit_cast<float>(change);
}

// CEffectPrim::UpdatePrim_0 (0057e180), ids 0 and 2: flies from the owner's point along
// its heading, turning, growing and changing colour.
bool UpdateFlyer(EffectPrim& p, core::CrtRand& rand) {
  if (p.change_at == p.age) {
    TakeChange(p.changes[0], p.width_speed);
    TakeChange(p.changes[1], p.height_speed);
    TakeChange(p.changes[2], p.speed);
    TakeChange(p.changes[3], p.width_rate);
    TakeChange(p.changes[4], p.height_rate);
    TakeChange(p.changes[5], p.acceleration);
  }
  const std::uint32_t flags = p.flags;
  if ((flags & EffectPrim::kAbsolute) == 0) {
    p.position[0] = p.owner->screen_x + p.offset[0];
    p.position[1] = p.owner->screen_y + p.offset[1];
  } else {
    p.position[0] = p.offset[0];
    p.position[1] = p.offset[1];
  }
  p.speed = p.acceleration + p.speed;
  p.spin = p.spin_rate + p.spin;
  p.width_speed = p.width_speed + p.width_rate;
  p.height_speed = p.height_rate + p.height_speed;
  if ((flags & EffectPrim::kStopGrowth) != 0 && p.age >= p.period) {
    p.width_speed = 0.0f;
    p.width_rate = 0.0f;
    p.height_rate = 0.0f;
    p.height_speed = 0.0f;
  }
  if ((flags & EffectPrim::kBounce) != 0) Bounce(p);
  p.angle = p.spin + p.angle;
  p.width = p.width_speed + p.width;
  p.height = p.height_speed + p.height;
  p.travel[0] = (Sin(p.heading) * p.speed) + p.travel[0];
  p.travel[1] = p.travel[1] - (Cos(p.heading) * p.speed);
  p.red += p.red_speed;
  p.green += p.green_speed;
  p.blue += p.blue_speed;
  p.position[0] = p.travel[0] + p.position[0];
  p.position[1] = p.travel[1] + p.position[1];
  // Red wraps the other way round from green and blue.
  if (p.red > 0xff) {
    p.red = 0;
  } else if (p.red < 0) {
    p.red = 0xff;
  }
  p.green = std::clamp(p.green, 0, 0xff);
  p.blue = std::clamp(p.blue, 0, 0xff);
  if ((p.flags & EffectPrim::kWander) != 0 && p.age % p.period == 0) {
    p.heading = (p.heading - 8.0f) + static_cast<float>(rand.Next() & 0xf);
    p.period = rand.Next() % 10 + 10;
  }
  UpdateAlpha(p);
  return AdvanceImage(p);
}

// CEffectPrim::UpdatePrim_1 (0057e4a0): circles the owner's point at a growing distance.
bool UpdateOrbiter(EffectPrim& p) {
  p.start[0] = p.owner->screen_x + p.offset[0];
  p.start[1] = p.owner->screen_y + p.offset[1];
  p.distance_speed = p.distance_rate + p.distance_speed;
  p.heading_speed = p.heading_rate + p.heading_speed;
  p.width_speed = p.width_rate + p.width_speed;
  p.height_speed = p.height_rate + p.height_speed;
  p.spin = p.spin_rate + p.spin;
  p.distance = p.distance_speed + p.distance;
  p.heading = p.heading_speed + p.heading;
  p.angle = p.spin + p.angle;
  p.width = p.width_speed + p.width;
  p.height = p.height_speed + p.height;
  p.position[0] = (Sin(p.heading) * p.distance) + p.start[0];
  p.position[1] = p.start[1] - (Cos(p.heading) * p.distance);
  UpdateAlpha(p);
  return AdvanceImage(p);
}

// CEffectPrim::UpdatePrim_5 (0057e8e0): a particle flying along its matrix's z, or its own
// step with kStraight, drifting and wandering by its flags.
bool UpdateParticle(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene) {
  FollowOwner(p);
  if (p.age == p.change_at) {
    for (std::size_t i = 0; i < 3; ++i) {
      p.step[i] = std::bit_cast<float>(p.step_changes[i]);
      p.step_speed[i] = std::bit_cast<float>(p.step_changes[i + 3]);
    }
  }
  p.speed = p.acceleration + p.speed;
  p.scale_speed = p.scale_rate + p.scale_speed;
  const std::uint32_t flags = p.flags;
  bool moved = true;
  if ((flags & EffectPrim::kStraight) != 0) {
    for (std::size_t i = 0; i < 3; ++i) p.step[i] = p.step_speed[i] + p.step[i];
  } else if ((flags & EffectPrim::kAlwaysMove) != 0 || p.age > p.move_after) {
    p.step = AlongZ(p, p.speed);
  } else {
    moved = false;
  }
  if (moved) {
    for (std::size_t i = 0; i < 3; ++i) p.travel[i] = p.step[i] + p.travel[i];
    p.angle = p.spin + p.angle;
    p.scale = p.scale_speed + p.scale;
  }
  if ((p.flags & EffectPrim::kDrift) != 0) {
    if (p.age % p.period == 0) {
      p.period = (rand.Next() & 7) + 12;
      p.drift[0] = static_cast<float>(rand.Next() % 5 + 5) * kHundredth;
      p.drift[2] = static_cast<float>(rand.Next() % 5 + 5) * kHundredth;
      if ((rand.Next() & 1) == 0) p.drift[0] = p.drift[0] * -1.0f;
      if ((rand.Next() & 1) == 0) p.drift[2] = p.drift[2] * -1.0f;
    }
    p.travel[0] = p.drift[0] + p.travel[0];
  }
  if ((p.flags & EffectPrim::kWanderWide) != 0 && p.age % p.period == 0) {
    p.speed = static_cast<float>(rand.Next() % 35 + 15) * kHundredth;
    p.heading = (p.heading - 40.0f) + static_cast<float>(rand.Next() % 80);
    p.pitch = (p.pitch - 40.0f) + static_cast<float>(rand.Next() % 80);
    p.matrix = RotateX(MakeRotationY(Radians(p.heading)), Radians(p.pitch));
    p.period = rand.Next() % 10 + 5;
  }
  if ((p.flags & EffectPrim::kWander) != 0 && p.age % p.period == 0) {
    p.heading = (p.heading - 5.0f) + static_cast<float>(rand.Next() % 10);
    p.pitch = (p.pitch - 5.0f) + static_cast<float>(rand.Next() % 10);
    p.matrix = RotateY(MakeRotationX(Radians(p.pitch)), Radians(p.heading));
    p.period = rand.Next() % 15 + 5;
  }
  PlaceAtTravel(p);
  if ((p.flags & EffectPrim::kEndNear) != 0) {
    // CRenderObject 00500530: the distance on the ground to (x, z).
    const float dx = p.owner->target[0] - p.position[0];
    const float dz = p.owner->target[2] - p.position[2];
    if (!(SqrtFloat((dz * dz) + (dx * dx)) > kEndDistance)) return false;
  }
  if ((p.flags & EffectPrim::kFaceCamera) != 0) p.angle = p.facing - scene.view_yaw;
  UpdateAlpha(p);
  ShiftHistory(p);
  return AdvanceAction(p);
}

// CEffectPrim::UpdatePrim_6 (0057f270): a particle thrown along its matrix's z and falling
// along y, bouncing off the ground at two thirds of its speed.
bool UpdateBouncer(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene) {
  FollowOwner(p);
  float speed = p.acceleration + p.speed;
  p.fall_speed = p.fall_rate + p.fall_speed;
  if (!(speed >= 0.0f)) speed = 0.0f;
  p.speed = speed;
  p.step = AlongZ(p, speed);
  p.travel[0] = p.step[0] + p.travel[0];
  p.travel[1] = p.travel[1] + p.fall_speed;
  p.travel[2] = p.step[2] + p.travel[2];
  PlaceAtTravel(p);
  p.scale = p.scale_speed + p.scale;
  if (p.fall_speed > 0.0f) {
    const float ground =
        scene.attr != nullptr ? res::AttrHeight(*scene.attr, p.position[0], p.position[2]) : 0.0f;
    if (ground < p.position[1]) {
      p.fall_speed = p.fall_speed * kBounceBack;
      // effect\EF_Coin%d.wav, rand() % 3 + 1
      if (p.coin_sound) rand.Next();
    }
  }
  UpdateAlpha(p);
  ShiftHistory(p);
  return AdvanceAction(p);
}

// CEffectPrim::UpdatePrim_13 (00580df0): a cylinder moving up its matrix's y, turning, its
// rings and height growing to max_height; with kNarrowing its top closes from fade_at.
bool UpdateCylinder(EffectPrim& p) {
  FollowOwner(p);
  p.speed = p.acceleration + p.speed;
  p.top_radius_speed = p.top_radius_rate + p.top_radius_speed;
  p.bottom_radius_speed = p.bottom_radius_rate + p.bottom_radius_speed;
  p.height_speed = p.height_rate + p.height_speed;
  p.heading_speed = p.heading_rate + p.heading_speed;
  p.pitch_speed = p.pitch_rate + p.pitch_speed;
  p.heading = p.heading_speed + p.heading;
  p.pitch = p.pitch_speed + p.pitch;
  p.matrix = RotateY(MakeRotationX(Radians(p.pitch)), Radians(p.heading));
  const float back = -p.speed;
  const Matrix& m = p.matrix;
  p.step = {(back * m[3]) + m[9], (back * m[4]) + m[10], (back * m[5]) + m[11]};
  for (std::size_t i = 0; i < 3; ++i) p.travel[i] = p.step[i] + p.travel[i];
  PlaceAtTravel(p);
  // The heading turns a second time.
  p.heading = p.heading + p.heading_speed;
  p.top_radius = p.top_radius + p.top_radius_speed;
  p.bottom_radius = p.bottom_radius + p.bottom_radius_speed;
  p.height = p.height_speed + p.height;
  if (p.height > p.max_height) p.height = p.max_height;
  if ((p.flags & EffectPrim::kNarrowing) != 0 && p.age == p.fade_at)
    p.top_radius_speed = -(p.top_radius / static_cast<float>((p.life - p.age) + 1));
  UpdateAlpha(p);
  return AdvanceImage(p);
}

ScreenVertex Vertex(float x, float y, std::uint32_t color, float u, float v) {
  ScreenVertex s;
  s.x = x;
  s.y = y;
  s.z = std::bit_cast<float>(kVertexZ);
  s.rhw = std::bit_cast<float>(kVertexRhw);
  s.diffuse = color;
  s.u = u;
  s.v = v;
  return s;
}

const std::string& TextureOf(const EffectPrim& p) {
  static const std::string kNone;
  const auto i = static_cast<std::size_t>(p.image);
  return p.image >= 0 && i < p.textures.size() ? p.textures[i] : kNone;
}

// CEffectPrim 00581520, ids 0 and 1: a ray of the width either side and twice the height,
// centred on the point or, anchored, rising from it; turned by the angle, half a pixel
// up and left.
void DrawRay(const EffectPrim& p, std::vector<PrimTriangle>& out) {
  const float s = Sin(p.angle);
  const float c = Cos(p.angle);
  const float w = p.width;
  float top = 0.0f;
  float bottom = 0.0f;
  if ((p.flags & EffectPrim::kAnchored) != 0) {
    top = p.height * -2.0f;
  } else {
    bottom = p.height;
    top = -p.height;
  }
  const float left_c = -w * c;
  const float top_s = top * s;
  const float left_top_x = left_c - top_s;
  const float top_c = top * c;
  const float left_s = -w * s;
  const float left_top_y = left_s + top_c;
  const float right_c = w * c;
  const float right_top_x = right_c - top_s;
  const float right_s = w * s;
  const float right_top_y = right_s + top_c;
  const float bottom_s = bottom * s;
  const float left_bottom_x = left_c - bottom_s;
  const float bottom_c = bottom * c;
  const float left_bottom_y = bottom_c + left_s;
  const float right_bottom_x = right_c - bottom_s;
  const float right_bottom_y = bottom_c + right_s;
  const auto at = [&](float x, float y, float u, float v) {
    return Vertex((x + p.position[0]) - kHalf, (y + p.position[1]) - kHalf, p.color, u, v);
  };
  const ScreenVertex left_bottom = at(left_bottom_x, left_bottom_y, 0.0f, 1.0f);
  const ScreenVertex left_top = at(left_top_x, left_top_y, 0.0f, 0.0f);
  const ScreenVertex right_bottom = at(right_bottom_x, right_bottom_y, 1.0f, 1.0f);
  const ScreenVertex right_top = at(right_top_x, right_top_y, 1.0f, 0.0f);
  out.push_back({TextureOf(p), {left_bottom, left_top, right_bottom}});
  out.push_back({TextureOf(p), {right_bottom, left_top, right_top}});
}

// CEffectPrim::RenderRotatedScreenQuad (005812a0), id 2: a diamond of the width and
// height whose side tips sit 5/6 of the way out, its bottom tip opaque white, turned by
// the angle about the point.
void DrawDiamond(const EffectPrim& p, std::vector<PrimTriangle>& out) {
  const float s = Sin(p.angle);
  const float c = Cos(p.angle);
  const float hw = p.width * kHalf;
  const float hh = p.height * kHalf;
  const float tip = hh * kQuadTip;
  const auto turn = [&](ScreenVertex v) {
    const float x = v.x;
    const float y = v.y;
    v.x = ((x * c) - (y * s)) + p.position[0];
    v.y = ((y * c) + (x * s)) + p.position[1];
    return v;
  };
  const ScreenVertex bottom = turn(Vertex(0.0f, hh, 0xffffffff, kHalf, 1.0f));
  const ScreenVertex top = turn(Vertex(0.0f, -hh, p.color, kHalf, 0.0f));
  out.push_back({TextureOf(p), {turn(Vertex(-hw, tip - hh, p.color, 0.0f, kHalf)), bottom, top}});
  out.push_back({TextureOf(p), {turn(Vertex(hw, hh - tip, p.color, 1.0f, kHalf)), bottom, top}});
}

// CEffectPrim::RenderActFrameHistory (00581ff0), ids 5 and 6: the sprite's current frame
// at each history record, scaled by its scale and coloured by its colour.
void DrawHistory(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  const res::Act* act = p.sprite.act;
  const res::Sprite* sprite = p.sprite.sprite;
  if (act == nullptr || sprite == nullptr) return;
  if (p.action < 0 || static_cast<std::size_t>(p.action) >= act->actions.size()) return;
  const std::vector<res::ActFrame>& frames = act->actions[static_cast<std::size_t>(p.action)];
  if (p.image < 0 || static_cast<std::size_t>(p.image) >= frames.size()) return;
  const res::ActFrame& frame = frames[static_cast<std::size_t>(p.image)];
  const Matrix& v = scene.view;
  const Projection& pr = scene.projection;
  const auto width = static_cast<float>(pr.width);
  const auto height = static_cast<float>(pr.height);
  for (std::int32_t k = 0; k < p.history_count; ++k) {
    const PrimHistory& h = p.history[static_cast<std::size_t>(k)];
    const Vec3& e = h.position;
    const Vec3 view = ClampNear(TransformPointFromZ(v, e));
    const float vx = view[0];
    const float vy = view[1];
    const float vz = view[2];
    const float inverse = 1.0f / vz;
    const float sx = ((vx * pr.scale_x) * inverse) + static_cast<float>(pr.center_x);
    const float sy = ((vy * pr.scale_y) * inverse) + static_cast<float>(pr.center_y);
    const float pixel = (inverse * pr.scale_x) * kPixel;
    for (const res::ActItem& item : frame.items) {
      if (item.image < 0) continue;
      const auto index = static_cast<std::size_t>(item.image);
      std::int32_t w = 0, hgt = 0, halved_x = 0, halved_y = 0;
      if (item.image_type == 0) {
        if (index >= sprite->images.size()) continue;
        const res::SpriteImage& im = sprite->images[index];
        w = im.width, hgt = im.height, halved_x = im.halved_x ? 1 : 0,
        halved_y = im.halved_y ? 1 : 0;
      } else {
        if (index >= sprite->images32.size()) continue;
        const res::SpriteImage32& im = sprite->images32[index];
        w = im.width, hgt = im.height, halved_x = im.halved_x ? 1 : 0,
        halved_y = im.halved_y ? 1 : 0;
      }
      const float top = (((static_cast<float>(item.y) * h.scale) * item.scale_y) * pixel) + sy;
      const std::int32_t far_y = (halved_y + 1) * hgt + item.y;
      const float bottom = (((static_cast<float>(far_y) * h.scale) * item.scale_y) * pixel) + sy;
      if (!(height > top) || !(bottom > 0.0f)) continue;
      const std::int32_t far_x = (halved_x + 1) * w + item.x;
      const float far_edge = (((static_cast<float>(far_x) * h.scale) * item.scale_x) * pixel) + sx;
      const float near_edge =
          (((h.scale * item.scale_x) * static_cast<float>(item.x)) * pixel) + sx;
      const bool mirrored = (item.flags & 1) != 0;
      const float left = mirrored ? far_edge : near_edge;
      const float right = mirrored ? near_edge : far_edge;
      const float inner = mirrored ? right : left;
      const float outer = mirrored ? left : right;
      if (!(inner < width) || !(outer > 0.0f)) continue;
      SpriteQuad q;
      q.top = top;
      q.left = left;
      q.bottom = bottom;
      q.right = right;
      q.rhw_bottom = (1.0f / (((bottom - sy) * scene.slope_down) + vz)) + kRhwBias;
      q.rhw_top = (1.0f / (((top - sy) * scene.slope_down) + vz)) + kRhwBias;
      q.color = (p.flags & EffectPrim::kItemColors) != 0
                    ? static_cast<std::uint32_t>(item.color[3]) << 24 |
                          static_cast<std::uint32_t>(item.color[0]) << 16 |
                          static_cast<std::uint32_t>(item.color[1]) << 8 | item.color[2]
                    : h.color;
      q.angle = static_cast<float>(item.angle) + p.angle;
      q.image_type = item.image_type;
      q.image = item.image;
      q.flags = p.quad_flags;
      out.quads.push_back(q);
    }
  }
}

// CEffectPrim::RenderCylinder (00584a20), id 0xd: the side between the bottom ring and
// the top ring (height up the matrix's −y), a side every sweep degrees from 0 while the
// angle is at most 360 less the sweep, the texture's u stepping a quarter a side. The
// angles index the sine and cosine tables directly.
void DrawCylinder(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  const float end = 360.0f - p.sweep;
  if (0.0f > end) return;
  const core::AngleTables& t = core::GetAngleTables();
  const Matrix& m = p.matrix;
  const Projection& pr = scene.projection;
  const auto ring = [&](float c, float s, float r, bool top) {
    const float cr = c * r;
    const float sr = s * r;
    Vec3 q;
    if (!top) {
      for (std::size_t i = 0; i < 3; ++i) q[i] = ((sr * m[6 + i]) + (cr * m[i])) + m[9 + i];
    } else {
      const float up = -p.height;
      for (std::size_t i = 0; i < 3; ++i)
        q[i] = (((sr * m[6 + i]) + (up * m[3 + i])) + (cr * m[i])) + m[9 + i];
    }
    for (std::size_t i = 0; i < 3; ++i) q[i] = q[i] + p.position[i];
    return q;
  };
  const auto vertex = [&](const Vec3& q, float u, float v, bool scale_first = false,
                          bool from_z = false) {
    const Vec3 view = from_z ? TransformPointFromZ(scene.view, q) : TransformPoint(scene.view, q);
    ScreenVertex sv = ProjectPoint(ClampNear(view), pr, scale_first);
    sv.diffuse = p.color;
    sv.u = u;
    sv.v = v;
    return sv;
  };
  const std::string& texture = TextureOf(p);
  float u0 = 0.0f;
  float angle = 0.0f;
  do {
    const auto i0 = static_cast<std::size_t>(Ftol(angle));
    const auto i1 = static_cast<std::size_t>(Ftol(angle + p.sweep));
    const Vec3 p0 = ring(t.cos[i0], t.sin[i0], p.bottom_radius, false);
    const Vec3 p1 = ring(t.cos[i1], t.sin[i1], p.bottom_radius, false);
    const Vec3 p2 = ring(t.cos[i0], t.sin[i0], p.top_radius, true);
    const Vec3 p3 = ring(t.cos[i1], t.sin[i1], p.top_radius, true);
    const float u1 = u0 + kCylinderU;
    out.triangles.push_back(
        {texture,
         {vertex(p0, u0, 1.0f, false, true), vertex(p2, u0, 0.0f, true), vertex(p1, u1, 1.0f)}});
    out.triangles.push_back(
        {texture, {vertex(p1, u1, 1.0f), vertex(p2, u0, 0.0f), vertex(p3, u1, 0.0f)}});
    u0 = u1;
    if (!(u0 < 1.0f)) u0 = 0.0f;
    angle = angle + p.sweep;
  } while (!(angle > end));
}

// The particles of ids 0 and 2: heading and pitch, then 4 to 7 out along the matrix's z,
// 10 up.
void AimParticle(EffectPrim& p, std::int32_t heading, core::CrtRand& rand) {
  p.heading = static_cast<float>(heading);
  p.pitch = static_cast<float>(rand.Next() % 100 - 50);
  p.matrix = RotateX(MakeRotationY(Radians(p.heading)), Radians(p.pitch));
  p.drift = AlongZ(p, static_cast<float>((rand.Next() & 3) + 4));
  p.offset = {p.drift[0], p.drift[1] - 10.0f, p.drift[2]};
}

void SetUpParticle(EffectPrim& p, const PrimSprite& particle) {
  p.fade_at = 0;
  p.sprite = particle;
  p.history_count = 3;
  ResetHistory(p);
}

// The facing ids 0 and 2 aim by: 180 less the actor's, in [0, 360).
std::int32_t EffectFacing(const EffectActor& actor) {
  std::int32_t f = Ftol(180.0f - actor.facing);
  if (f < 0) f += 360;
  return f;
}

EffectPrim MakeCylinder(const RagEffect& effect, std::int32_t life, std::int32_t facing) {
  EffectPrim c = MakePrimitive(&effect, 0xd, {0.0f, 0.0f, 0.0f});
  c.life = life;
  c.heading = static_cast<float>(facing);
  c.pitch = -90.0f;
  c.matrix = RotateY(MakeRotationX(Radians(c.pitch)), Radians(c.heading));
  c.offset = {0.0f, -10.0f, 0.0f};
  c.speed = std::bit_cast<float>(0x3f333333u);  // 0.7
  c.acceleration = (c.speed / static_cast<float>(c.life)) * -0.5f;
  return c;
}

}  // namespace

void ResetHistory(EffectPrim& p) {
  for (std::int32_t k = 0; k < p.history_count; ++k) {
    PrimHistory& h = p.history[static_cast<std::size_t>(k)];
    h = Snapshot(p);
    h.red = p.red;
    h.green = p.green;
    h.blue = p.blue;
  }
}

EffectPrim MakePrimitive(const RagEffect* owner, std::int32_t id, const Vec3& offset) {
  EffectPrim p;
  p.owner = owner;
  p.id = id;
  p.start = owner->position;
  p.position = p.start;
  for (std::size_t i = 0; i < 3; ++i) p.travel[i] = offset[i] + p.travel[i];
  return p;
}

bool UpdatePrimitive(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene) {
  if (p.owner == nullptr) return false;
  switch (p.id) {
    case 0:
    case 2:
      if (!UpdateFlyer(p, rand)) return false;
      break;
    case 1:
      if (!UpdateOrbiter(p)) return false;
      break;
    case 5:
      if (!UpdateParticle(p, rand, scene)) return false;
      break;
    case 6:
      if (!UpdateBouncer(p, rand, scene)) return false;
      break;
    case 0xd: UpdateCylinder(p); break;
    default: return false;
  }
  ++p.age;
  return p.age <= p.life;
}

void DrawPrimitive(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  if (!p.visible) return;
  switch (p.id) {
    case 0:
    case 1: DrawRay(p, out.triangles); break;
    case 2: DrawDiamond(p, out.triangles); break;
    case 5:
    case 6: DrawHistory(p, scene, out); break;
    case 0xd: DrawCylinder(p, scene, out); break;
    default: break;
  }
}

void ProcessHitSpark(RagEffect& effect, const Vec3& actor, const Matrix& view,
                     const Projection& projection, core::CrtRand& rand, bool blue,
                     std::vector<EffectPrim>& added) {
  const ScreenVertex screen = ProjectPoint(ClampNear(TransformPoint(view, actor)), projection);
  effect.screen_x = screen.x;
  effect.screen_y = screen.y;
  if (effect.frame != 0) return;
  std::int32_t n = 0;
  float heading = 0.0f;
  do {
    EffectPrim p = MakePrimitive(&effect, 0, {0.0f, 0.0f, 0.0f});
    p.flags = EffectPrim::kAnchored;
    p.life = rand.Next() % 20 + 10;
    p.angle = (heading - 15.0f) + static_cast<float>(rand.Next() % 30);
    p.heading = p.angle;
    p.speed = static_cast<float>(rand.Next() % 45 + 5) * 0.1f;
    p.acceleration = (p.speed / static_cast<float>(p.life)) * -0.5f;
    p.width = static_cast<float>(rand.Next() % 15 + 5);
    p.height = static_cast<float>(rand.Next() % 20 + 20);
    p.width_speed = -(p.width / static_cast<float>(p.life));
    p.height_speed = 1.5f;
    p.height_rate = 0.25f;
    const auto spread = static_cast<float>(rand.Next() % 5);
    const std::int32_t h = Ftol(heading);
    p.offset[0] = core::SinDegrees(h) * spread;
    p.offset[1] = -(core::CosDegrees(h) * spread);
    p.offset[1] = p.offset[1] - 20.0f;
    if (blue) {
      p.red = 0x78;
      p.green = 0x78;
    }
    p.alpha = 0.0f;
    p.alpha_speed = p.alpha_max * 0.125f;
    p.textures.assign(static_cast<std::size_t>(p.image_count),
                      n % 2 == 0 ? "effect\\lens1.tga" : "effect\\lens2.tga");
    added.push_back(std::move(p));
    heading = heading + 45.0f;
    ++n;
  } while (heading < 360.0f);
}

void ProcessRingBurst(RagEffect& effect, const EffectActor& actor, bool minimal,
                      const PrimSprite& particle, core::CrtRand& rand,
                      std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  const std::int32_t facing = EffectFacing(actor);
  EffectPrim c = MakeCylinder(effect, 10, facing);
  c.top_radius = 10.0f;
  c.bottom_radius = 5.0f;
  c.height = 3.5f;
  c.fade_at = c.life - c.life / 2;
  c.textures = {"effect\\ring_blue.tga"};
  added.push_back(std::move(c));
  if (minimal) return;
  for (int n = 0; n < 2; ++n) {
    EffectPrim p = MakePrimitive(&effect, 5, {0.0f, 0.0f, 0.0f});
    AimParticle(p, facing - 40 + rand.Next() % 80, rand);
    p.speed = static_cast<float>(rand.Next() % 90 + 60) * kHundredth;
    p.life = rand.Next() % 24 + 6;
    p.acceleration = (p.speed / static_cast<float>(p.life)) * -0.5f;
    p.scale = static_cast<float>(rand.Next() % 100 + 60) * kHundredth;
    SetUpParticle(p, particle);
    added.push_back(std::move(p));
  }
  for (int n = 0; n < 2; ++n) {
    EffectPrim p = MakePrimitive(&effect, 6, {0.0f, 0.0f, 0.0f});
    AimParticle(p, facing + 140 + rand.Next() % 80, rand);
    p.matrix = MakeRotationY(Radians(p.heading));
    p.life = rand.Next() % 24 + 6;
    p.speed = static_cast<float>(rand.Next() % 90 + 60) * kHundredth;
    p.acceleration = (p.speed / static_cast<float>(p.life)) * -0.5f;
    p.fall_speed = static_cast<float>(rand.Next() % 90 + 30) * -kHundredth;
    const float fall = p.fall_speed / static_cast<float>(p.life);
    p.fall_rate = -(fall + fall);
    p.scale = static_cast<float>(rand.Next() % 100 + 60) * kHundredth;
    SetUpParticle(p, particle);
    added.push_back(std::move(p));
  }
}

void ProcessHit3(RagEffect& effect, const EffectActor& actor, const PrimSprite& particle,
                 core::CrtRand& rand, std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  const std::int32_t facing = EffectFacing(actor);
  for (const float top : {1.5f, 4.0f}) {
    EffectPrim c = MakeCylinder(effect, 15, facing);
    c.top_radius = top;
    c.bottom_radius = 1.5f;
    c.height_speed = top == 1.5f ? 0.5f : 0.25f;
    c.height_rate = std::bit_cast<float>(0x3e4ccccdu);  // 0.2
    c.fade_at = c.life - c.life / 3;
    c.textures = {"effect\\lens2.tga"};
    added.push_back(std::move(c));
  }
  for (int n = 0; n < 8; ++n) {
    EffectPrim p = MakePrimitive(&effect, 5, {0.0f, 0.0f, 0.0f});
    AimParticle(p, facing - 40 + rand.Next() % 80, rand);
    p.speed = static_cast<float>(rand.Next() % 120 + 80) * kHundredth;
    p.life = rand.Next() % 24 + 6;
    p.acceleration = (p.speed / static_cast<float>(p.life)) * -0.5f;
    p.scale = static_cast<float>(rand.Next() % 100 + 60) * kHundredth;
    p.scale_speed = -(p.scale / static_cast<float>(p.life));
    SetUpParticle(p, particle);
    added.push_back(std::move(p));
  }
}

RagEffect& EffectWorld::Launch(std::int32_t id, const EffectActor& actor) {
  Object o;
  o.effect = std::make_unique<RagEffect>();
  o.effect->id = id;
  o.effect->position = actor.position;
  o.effect->duration = EffectDuration(id);
  o.actor = actor;
  objects_.push_back(std::move(o));
  return *objects_.back().effect;
}

void EffectWorld::Step(const PrimScene& scene) {
  for (auto it = objects_.begin(); it != objects_.end();) {
    bool more = false;
    if (it->effect) {
      RagEffect& e = *it->effect;
      std::vector<EffectPrim> added;
      switch (e.id) {
        case 0: ProcessRingBurst(e, it->actor, minimal_, particle_, rand_, added); break;
        case 1:
          ProcessHitSpark(e, it->actor.position, scene.view, scene.projection, rand_, false, added);
          break;
        case 2: ProcessHit3(e, it->actor, particle_, rand_, added); break;
        default: break;
      }
      for (EffectPrim& p : added) {
        Object o;
        o.prim = std::make_unique<EffectPrim>(std::move(p));
        objects_.push_back(std::move(o));
      }
      ++e.frame;
      more = e.frame < e.duration;
      if (!more) {
        for (Object& o : objects_)
          if (o.prim && o.prim->owner == &e) o.prim->owner = nullptr;
      }
    } else {
      more = UpdatePrimitive(*it->prim, rand_, scene);
    }
    it = more ? std::next(it) : objects_.erase(it);
  }
}

PrimDrawing EffectWorld::Draw(const PrimScene& scene) const {
  PrimDrawing out;
  for (const Object& o : objects_)
    if (o.prim) DrawPrimitive(*o.prim, scene, out);
  return out;
}

}  // namespace ragexe::render
