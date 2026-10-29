#include "render/effect_prim.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

#include "core/angles.h"
#include "core/x87.h"
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
constexpr float kFar = 1500.0f;                                       // 005e9f60
constexpr float kHundredth = std::bit_cast<float>(0x3c23d70au);       // 005e39dc, 0.01
constexpr float kBounceBack = std::bit_cast<float>(0xbf2aaaabu);      // 005e9e04, -2/3
constexpr float kPixel = std::bit_cast<float>(0x3e124925u);           // 005e9564, 1/7
constexpr float kRhwBias = std::bit_cast<float>(0x3727c5acu);         // 005e9568, 1e-5
constexpr float kEndDistance = 3.0f;                                  // 005e29d8
constexpr float kCylinderU = 0.25f;                                   // 005e653c
constexpr float kRingCell = std::bit_cast<float>(0x3d430c31u);        // 005e9ef4, 1/21
constexpr float kRingWave = std::bit_cast<float>(0x3e19999au);        // 005e9ed8, 0.15
constexpr std::uint32_t kSlowWaves = 0x42c60000;                      // 99.0 at +0x1f0
constexpr std::uint32_t kLowWalls = 0x40c00831;                       // 6.001, a ring's height
constexpr float kWallsClose = std::bit_cast<float>(0x3c23d70au);      // 005e9f24, 0.01
constexpr float kBoxGrowth = std::bit_cast<float>(0x3ba3d70au);       // 005e9f28, 0.005
constexpr float kBoxPulse = std::bit_cast<float>(0x3eb33333u);        // 005e9f30, 0.35
constexpr float kBoxBase = std::bit_cast<float>(0x3f266666u);         // 005e9f2c, 0.65
constexpr float kBounceShrink = std::bit_cast<float>(0x3f333333u);    // 005e89c4, 0.7
constexpr float kPerHalfTurn = std::bit_cast<float>(0x3bb60b61u);     // 005e2690, 1/180
constexpr float kBandSpread = std::bit_cast<float>(0x3d75c28fu);      // 005e9f4c, 0.06
constexpr float kBandBack = std::bit_cast<float>(0x3e19999au);        // 005e9ed8, 0.15
constexpr float kBandLift = std::bit_cast<float>(0x3d23d70au);        // 005e9f20, 0.04
constexpr float kBandLength = std::bit_cast<float>(0x3e4ccccdu);      // 005e9f40, 0.2
constexpr float kLeafDriftX = std::bit_cast<float>(0x3e75c28fu);      // 005e9b10, 0.24
constexpr float kLeafDriftZ = std::bit_cast<float>(0x3e99999au);      // 005e2a1c, 0.3
constexpr float kLeafDriftSlowX = std::bit_cast<float>(0x3df5c28fu);  // 005e9edc, 0.12
constexpr float kThirtieth = std::bit_cast<float>(0x3d088889u);       // 005e9e0c, 1/30
constexpr float kEaseOut = std::bit_cast<float>(0xbf2aaaabu);         // 005e9e04, -2/3
constexpr float kThousandth = std::bit_cast<float>(0x3a83126fu);      // 005e9898, 0.001
constexpr float kTrapFall = std::bit_cast<float>(0x3da3d70au);        // 005e9eb0, 0.08
constexpr float kFiftieth = std::bit_cast<float>(0x3ca3d70au);        // 005e9e2c, 0.02
constexpr float kLeafFallSlow = std::bit_cast<float>(0x3cf5c28fu);    // 005e9ed4, 0.03

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
      p.alpha_speed = -core::RoundedQuotient(p.alpha, static_cast<double>(left));
    }
  }
  if (age == p.fade_at) {
    p.flags &= ~EffectPrim::kBounce;
    p.alpha_speed =
        -(core::RoundedQuotient(p.alpha, static_cast<double>(p.life - age)) * p.fade_scale);
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
// step with kStraight, drifting and wandering by its flags. UpdatePrim_21 (0057edd0), id
// 0x15, is the same left where it started: from its start and offset, not the owner's, and
// without kEndNear.
bool UpdateParticle(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene, bool left = false) {
  Vec3 base{};
  if (left) {
    for (std::size_t i = 0; i < 3; ++i) base[i] = p.start[i] + p.offset[i];
  } else {
    FollowOwner(p);
  }
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
  if (left) {
    for (std::size_t i = 0; i < 3; ++i) p.position[i] = base[i] + p.travel[i];
  } else {
    PlaceAtTravel(p);
  }
  if (!left && (p.flags & EffectPrim::kEndNear) != 0) {
    // CRenderObject::GroundDistanceTo (00500530).
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

// CEffectPrim::UpdatePrim_29 (00571a50), id 0x1d: each ring turns 5 degrees a step (3
// when +0x1f0 holds 99), widens by 0.3 (0.1), leans in to 40 (30) degrees, fades in for
// ten steps by 20 (10 for a kind above 0) and out by 2, and its walls swell with a sine
// wave a quarter turn apart from ring to ring.
void UpdateRingWaves(EffectPrim& p) {
  p.position = p.owner->position;
  const bool slow = std::bit_cast<std::uint32_t>(p.acceleration) == kSlowWaves;
  const core::AngleTables& t = core::GetAngleTables();
  for (std::size_t k = 0; k < p.rings.size(); ++k) {
    PrimRing& r = p.rings[k];
    if (!r.active) continue;
    ++r.age;
    r.angle = static_cast<std::int16_t>(r.angle + (slow ? 3 : 5));
    if (r.angle >= 360) r.angle = static_cast<std::int16_t>(r.angle - 360);
    r.radius = r.radius + (slow ? 0.1f : 0.3f);
    r.tilt = static_cast<std::int16_t>(r.tilt - 1);
    const std::int16_t least = slow ? 30 : 40;
    if (r.tilt < least) r.tilt = least;
    if (r.age > 10) {
      r.alpha = static_cast<std::int16_t>(r.alpha - 2);
      if (!(r.alpha > 0)) r.alpha = 0;
    } else {
      r.alpha = static_cast<std::int16_t>(r.alpha + (r.kind > 0 ? 10 : 20));
    }
    const auto phase = static_cast<std::size_t>((static_cast<std::int32_t>(k) * 90 + r.age) % 360);
    for (std::size_t i = 0; i < r.heights.size(); ++i) {
      const float swell = r.height * t.sin[i * 9];
      r.heights[i] = (((t.sin[phase] * r.height) * t.sin[i * 9]) * kRingWave) + swell;
    }
  }
}

// CEffectPrim::UpdatePrim_39 (0056bb30), id 0x27: each ring turns a degree a step; its
// walls rise along a quarter sine from age 200 to 290 and then stand at full height; a
// ring of kind 1 fades by 1 a step after 250, and every ring by 2 from 800, its walls
// growing by 10 a step while it can still fade.
void UpdateRisingRings(EffectPrim& p) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  for (PrimRing& r : p.rings) {
    if (!r.active) continue;
    ++r.age;
    r.angle = static_cast<std::int16_t>(r.angle + 1);
    if (r.angle >= 360) r.angle = static_cast<std::int16_t>(r.angle - 360);
    if (r.kind == 1 && r.age > 250) {
      r.alpha = static_cast<std::int16_t>(r.alpha - 1);
      if (r.alpha < 0) r.alpha = 0;
    }
    if (r.age >= 800) {
      r.alpha = static_cast<std::int16_t>(r.alpha - 2);
      if (r.alpha < 0) {
        r.alpha = 0;
      } else {
        r.height = r.height + 10.0f;
      }
    }
    for (float& h : r.heights) {
      if (r.age < 200) {
        h = 0.0f;
      } else if (r.age <= 290) {
        h = t.sin[static_cast<std::size_t>(r.age - 200)] * r.height;
      } else {
        h = r.height;
      }
      if (!(h >= 0.0f)) h = 0.0f;
    }
  }
}

std::int16_t Wrap360(std::int32_t angle) {
  auto a = static_cast<std::int16_t>(angle);
  if (a >= 360) a = static_cast<std::int16_t>(a - 360);
  return a;
}

// A wall rising a quarter sine of the age to its share of the height (sin 9i), until it
// would pass that share, which settles it.
void RiseWall(PrimRing& r, std::size_t i, const core::AngleTables& t) {
  if (r.age <= 90)
    r.heights[i] = (t.sin[static_cast<std::size_t>(r.age)] * t.sin[i * 9]) * r.height;
  const float full = t.sin[i * 9] * r.height;
  if (full < r.heights[i]) {
    r.heights[i] = full;
    r.flags[i] = 1;
  }
  if (!(r.heights[i] >= 0.0f)) r.heights[i] = 0.0f;
}

// The ring's phase for its walls' wave: the age plus its quarter turn for the first two
// rings, the age plus an eighth turn per ring times 2 (or 3) for the others.
std::size_t WavePhase(const PrimRing& r, std::int32_t k, std::int32_t times) {
  const std::int32_t quarter = k * 90;
  const std::int32_t phase =
      quarter < 180 ? quarter + r.age : (k * (times == 3 ? 30 : 45) + r.age) * times;
  return static_cast<std::size_t>(phase % 360);
}

// CEffectPrim::UpdatePrim_23 (0056b130), id 0x17, and UpdatePrim_25 (0056bca0), id 0x19:
// from ring 3 alone when the scale is 3, else every ring. Rings 0 to 2 turn by their index
// plus 3; each fades in for 20 steps (ring 3 by 8 to 60, or 100 for 0x19; the others by
// 10 to 180) and out over the last 40 of the life; their walls rise and settle. Ring 3 of
// 0x19 waves once its walls have risen.
void UpdateRisingWalls(EffectPrim& p, bool waving) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  const std::int32_t first = std::bit_cast<std::uint32_t>(p.scale) == 0x40400000 ? 3 : 0;
  for (std::int32_t k = first; k < 4; ++k) {
    PrimRing& r = p.rings[static_cast<std::size_t>(k)];
    if (!r.active) continue;
    ++r.age;
    if (k < 3) r.angle = static_cast<std::int16_t>(r.angle + k + 3);
    r.angle = Wrap360(r.angle);
    if (r.age >= p.life - 40) {
      r.alpha = static_cast<std::int16_t>(r.alpha - (k < 3 ? 5 : waving ? 3 : 2));
      if (r.alpha < 0) r.alpha = 0;
    } else if (r.age < 20) {
      if (k == 3) {
        r.alpha = static_cast<std::int16_t>(r.alpha + 8);
        const std::int16_t most = waving ? 100 : 60;
        if (r.alpha > most) r.alpha = most;
      } else {
        r.alpha = static_cast<std::int16_t>(r.alpha + 10);
        if (r.alpha > 180) r.alpha = 180;
      }
    }
    for (std::size_t i = 0; i < r.heights.size(); ++i) {
      if (!waving || k < 3) {
        if (r.flags[i] == 0) RiseWall(r, i, t);
        continue;
      }
      if (r.flags[i] == 0) {
        if (r.age <= 90) {
          r.heights[i] = (t.sin[static_cast<std::size_t>(r.age)] * t.sin[i * 9]) * r.height;
        } else {
          r.flags[i] = 1;
        }
        if (t.sin[i * 9] * r.height < r.heights[i]) r.flags[i] = 1;
        if (!(r.heights[i] >= 0.0f)) r.heights[i] = 0.0f;
      } else {
        const float base = (r.height * t.sin[i * 9]) * 0.55f;
        const auto phase = static_cast<std::size_t>(r.age % 360);
        r.heights[i] = (((t.sin[phase] * r.height) * t.sin[i * 9]) * 0.45f) + base;
      }
    }
  }
}

// CEffectPrim::UpdatePrim_24 (0056bfa0), id 0x18: the rings at a quarter and three
// quarters turn 2 degrees a step, the others 1; each fades in to 80 over 100 steps and out
// over the last 20 of the life; the walls wave by 0.3 of their share.
void UpdateTurningRings(EffectPrim& p) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  for (std::int32_t k = 0; k < 4; ++k) {
    PrimRing& r = p.rings[static_cast<std::size_t>(k)];
    if (!r.active) continue;
    ++r.age;
    const std::int32_t quarter = k * 90;
    r.angle = Wrap360(r.angle + (quarter == 0 || quarter == 180 ? 1 : 2));
    if (r.age >= p.life - 20) {
      r.alpha = static_cast<std::int16_t>(r.alpha - 5);
      if (r.alpha < 0) r.alpha = 0;
    } else if (r.age < 100) {
      r.alpha = static_cast<std::int16_t>(r.alpha + 1);
      if (r.alpha > 80) r.alpha = 80;
    }
    const std::size_t phase = WavePhase(r, k, 2);
    for (std::size_t i = 0; i < r.heights.size(); ++i) {
      const float base = t.sin[i * 9] * r.height;
      r.heights[i] = (((t.sin[phase] * t.sin[i * 9]) * r.height) * 0.3f) + base;
    }
  }
}

// The wave of ids 0x1c and 0x22: each wall the full height plus 0.3 of it on the ring's
// phase.
void WaveFull(PrimRing& r, std::size_t phase, const core::AngleTables& t) {
  for (std::size_t i = 0; i < r.heights.size(); ++i) {
    const float base = r.height;
    r.heights[i] = (((t.sin[phase] * t.sin[i * 9]) * r.height) * 0.3f) + base;
  }
}

// CEffectPrim::UpdatePrim_28 (005715b0), id 0x1c: from its first step each ring widens by
// 0.07 and leans in to 10 degrees (its alpha cut to 0 there); it fades in by 10 while its
// radius is under 4 and out by 3 after, starting again at radius 3.37 and tilt 74 while
// the life has 30 steps left.
void UpdateSpreadingRings(EffectPrim& p) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  for (std::int32_t k = 0; k < 4; ++k) {
    PrimRing& r = p.rings[static_cast<std::size_t>(k)];
    if (!r.active) continue;
    ++r.age;
    if (r.age <= 0) continue;
    r.radius = r.radius + std::bit_cast<float>(0x3d8f5c29u);  // 0.07
    r.tilt = static_cast<std::int16_t>(r.tilt - 1);
    if (r.tilt < 10) {
      r.tilt = 10;
      r.alpha = 0;
    }
    if (r.radius < 4.0f) {
      r.alpha = static_cast<std::int16_t>(r.alpha + 10);
    } else {
      r.alpha = static_cast<std::int16_t>(r.alpha - 3);
      if (!(r.alpha > 0)) {
        r.alpha = 0;
        if (r.age < p.life - 30) {
          r.radius = std::bit_cast<float>(0x4057ae14u);  // 3.37
          r.tilt = 74;
        }
      }
    }
    WaveFull(r, WavePhase(r, k, 2), t);
  }
}

// CEffectPrim::UpdatePrim_34 (00571820), id 0x22: like 0x1c without waiting for the first
// step, widening by 0.04, fading in by 20 under radius 2.5 and out by 2, starting again
// at radius 2.14 and tilt 74 each time it has faded.
void UpdateGrowingRing(EffectPrim& p) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  for (std::int32_t k = 0; k < 4; ++k) {
    PrimRing& r = p.rings[static_cast<std::size_t>(k)];
    if (!r.active) continue;
    ++r.age;
    r.radius = r.radius + std::bit_cast<float>(0x3d23d70au);  // 0.04
    r.tilt = static_cast<std::int16_t>(r.tilt - 1);
    if (r.tilt < 10) {
      r.tilt = 10;
      r.alpha = 0;
    }
    if (r.radius < 2.5f) {
      r.alpha = static_cast<std::int16_t>(r.alpha + 20);
    } else {
      r.alpha = static_cast<std::int16_t>(r.alpha - 2);
      if (!(r.alpha > 0)) {
        r.alpha = 0;
        r.radius = std::bit_cast<float>(0x4008f5c2u);  // 2.14
        r.tilt = 74;
      }
    }
    WaveFull(r, WavePhase(r, k, 2), t);
  }
}

// CEffectPrim::UpdatePrim_48 (00571940), id 0x30: the kind turns a degree a step and sets
// the radius to 3 plus its sine, the angle turns 3; the walls wave by 0.3 of their share.
void UpdateWobblingRing(EffectPrim& p) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  for (std::int32_t k = 0; k < 4; ++k) {
    PrimRing& r = p.rings[static_cast<std::size_t>(k)];
    if (!r.active) continue;
    ++r.age;
    r.kind = Wrap360(r.kind + 1);
    r.radius = t.sin[static_cast<std::size_t>(r.kind)] + 3.0f;
    r.angle = Wrap360(r.angle + 3);
    const std::size_t phase = WavePhase(r, k, 2);
    for (std::size_t i = 0; i < r.heights.size(); ++i) {
      const float base = r.height * t.sin[i * 9];
      r.heights[i] = (((t.sin[phase] * r.height) * t.sin[i * 9]) * 0.3f) + base;
    }
  }
}

// CEffectPrim::UpdatePrim_54 (005716f0), id 0x36, where the primitive stays: ring k turns
// 5 − k degrees a step; from its first step its height is its second flag byte times the
// sine of twice its age, fading by 1 from 135 degrees, until 180, when its first flag
// byte is set and it fades by 10 a step; the walls wave by 0.3 of the height.
void UpdateLiftingRings(EffectPrim& p) {
  const core::AngleTables& t = core::GetAngleTables();
  for (std::int32_t k = 0; k < 4; ++k) {
    PrimRing& r = p.rings[static_cast<std::size_t>(k)];
    if (!r.active) continue;
    ++r.age;
    r.angle = Wrap360((r.angle - k) + 5);
    if (r.age <= 0) continue;
    if (r.flags[0] == 0) {
      const std::int32_t twice = r.age * 2;
      if (twice >= 180) {
        r.flags[0] = 1;
      } else if (twice >= 135) {
        r.alpha = static_cast<std::int16_t>(r.alpha - 1);
        if (r.alpha < 0) r.alpha = 0;
      }
      r.height = static_cast<float>(static_cast<std::int8_t>(r.flags[1])) *
                 t.sin[static_cast<std::size_t>(twice)];
    } else {
      r.alpha = static_cast<std::int16_t>(r.alpha - 10);
      if (r.alpha < 0) r.alpha = 0;
    }
    const std::size_t phase = WavePhase(r, k, 3);
    for (std::size_t i = 0; i < r.heights.size(); ++i) {
      const float base = r.height;
      r.heights[i] = (((t.sin[phase] * r.height) * t.sin[i * 9]) * 0.3f) + base;
    }
  }
}

// CEffectPrim::UpdatePrim_57 (0056b600), id 0x39: a ring whose first flag is 1 turns by its
// index plus 8; each fades in for 16 steps by 5 to 180 and out by 2 from the age in its
// kind; a wall of flag 0 waves at three quarters of the height plus a quarter on a sine
// that runs 4, 3 or 2 times the age (ring 0, 1, the others) and 34 degrees a wall, one of
// flag 1 stands at the height; both rise along the sine of the age for 90 steps.
void UpdateWavingWalls(EffectPrim& p) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  for (std::int32_t k = 0; k < 4; ++k) {
    PrimRing& r = p.rings[static_cast<std::size_t>(k)];
    if (!r.active) continue;
    ++r.age;
    if (r.flags[0] == 1) r.angle = static_cast<std::int16_t>(r.angle + k + 8);
    r.angle = Wrap360(r.angle);
    if (r.age >= r.kind) {
      r.alpha = static_cast<std::int16_t>(r.alpha - 2);
      if (r.alpha < 0) r.alpha = 0;
    } else if (r.age < 16) {
      r.alpha = static_cast<std::int16_t>(r.alpha + 5);
      if (r.alpha > 180) r.alpha = 180;
    }
    const std::int32_t times = k == 0 ? 4 : k == 1 ? 3 : 2;
    for (std::size_t i = 0; i < r.heights.size(); ++i) {
      float& h = r.heights[i];
      if (r.flags[i] == 0) {
        const auto phase =
            static_cast<std::size_t>((static_cast<std::int32_t>(i) * 34 + r.age * times) % 360);
        const float wave = ((t.sin[phase] * r.height) * 0.25f) + (r.height * 0.75f);
        h = wave;
        if (r.age <= 90) h = wave * t.sin[static_cast<std::size_t>(r.age)];
        if (!(h >= 0.0f)) h = 0.0f;
      } else if (r.flags[i] == 1) {
        h = r.height;
        if (r.age <= 90) h = r.height * t.sin[static_cast<std::size_t>(r.age)];
      }
    }
  }
}

// CEffectPrim::UpdatePrim_60 (0056b930), id 0x3c: from its first step each ring widens by
// 0.5, fading in by 24 to 240 for 10 steps and, past radius 7, out by 15; faded, it starts
// again from radius 0, or stops once ring 3's age (which ring 0 also counts) passes 120
// with ring 3's kind 1, or 1400 in any case. Its walls rise with the sine of 9 times the
// age for 10 steps.
void UpdateSpreadingPortal(EffectPrim& p) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  PrimRing& counter = p.rings[3];
  for (std::int32_t k = 0; k < 4; ++k) {
    PrimRing& r = p.rings[static_cast<std::size_t>(k)];
    if (!r.active) continue;
    ++r.age;
    if (k == 0) ++counter.age;
    if (r.age <= 0) continue;
    r.radius = r.radius + kHalf;
    if (r.radius > 7.0f) {
      r.alpha = static_cast<std::int16_t>(r.alpha - 15);
      if (r.alpha < 0) {
        r.alpha = 0;
        if ((counter.age > 120 && counter.kind == 1) || counter.age > 1400) {
          r.active = false;
        } else {
          r.age = 0;
          r.radius = 0.0f;
        }
      }
    }
    if (r.age < 10) {
      r.alpha = static_cast<std::int16_t>(r.alpha + 24);
      if (r.alpha > 240) r.alpha = 240;
    }
    for (float& h : r.heights) {
      h = r.height;
      if (r.age <= 10) h = r.height * t.sin[static_cast<std::size_t>(r.age) * 9];
    }
  }
}

// CEffectPrim::UpdatePrim_62 (0056b500), id 0x3e: each ring closes in by 0.05 a step, back
// to radius 10 and alpha 0 at the centre; its height is its radius and its tilt 90 less 9
// times it; it fades in by 1 to 70 and out by 3 over the last 40 of the life; its unflagged
// walls stand at the height.
void UpdateClosingRings(EffectPrim& p) {
  p.position = p.owner->position;
  for (PrimRing& r : p.rings) {
    if (!r.active) continue;
    ++r.age;
    r.radius = r.radius - std::bit_cast<float>(0x3d4ccccdu);  // 0.05
    if (!(r.radius > 0.0f)) {
      r.radius = 10.0f;
      r.alpha = 0;
    }
    r.height = r.radius;
    r.tilt = static_cast<std::int16_t>(Ftol(90.0f - (r.radius * 9.0f)));
    if (r.age >= p.life - 40) {
      r.alpha = static_cast<std::int16_t>(r.alpha - 3);
      if (r.alpha < 0) r.alpha = 0;
    } else if (r.alpha < 70) {
      r.alpha = static_cast<std::int16_t>(r.alpha + 1);
    }
    for (std::size_t i = 0; i < r.heights.size(); ++i) {
      if (r.flags[i] != 0) continue;
      r.heights[i] = r.height;
      if (!(r.heights[i] >= 0.0f)) r.heights[i] = 0.0f;
    }
  }
}

// The angle tables as they lie in memory, the cosines (00623c6c), their negations, the
// sines (006247b4), theirs, the radians and theirs, 361 each: the facing squares index them
// without reduction, and an angle past 360 reads on into the next. An index before the
// cosines reads the original's other globals; it reads 0 here.
constexpr std::int32_t kCosines = 0;
constexpr std::int32_t kSines = 722;

float TableAt(std::int32_t table, std::int32_t index) {
  static const std::array<float, 2166> kMemory = [] {
    const core::AngleTables& t = core::GetAngleTables();
    std::array<float, 2166> m{};
    for (std::size_t k = 0; k < 361; ++k) {
      m[k] = t.cos[k];
      m[361 + k] = -t.cos[k];
      m[722 + k] = t.sin[k];
      m[1083 + k] = -t.sin[k];
      m[1444 + k] = t.radians[k];
      m[1805 + k] = -t.radians[k];
    }
    return m;
  }();
  const std::int32_t at = table + index;
  return at >= 0 && std::cmp_less(at, kMemory.size()) ? kMemory[static_cast<std::size_t>(at)]
                                                      : 0.0f;
}

// The angles every update of squares facing the camera starts with (the game mode's CView,
// +0xb8), for the first and second records' +0x10: 360 less the camera's yaw, and 450 less
// its pitch within 360.
std::array<std::int16_t, 2> ViewAngles(const PrimScene& scene) {
  auto pitch = static_cast<std::int16_t>(450 - Ftol(scene.view_pitch));
  if (pitch >= 360) pitch = static_cast<std::int16_t>(pitch - 360);
  return {static_cast<std::int16_t>(360 - Ftol(scene.view_yaw)), pitch};
}

// The camera's angles into the facing squares' records, and the primitive at its owner.
void FaceCamera(EffectPrim& p, const PrimScene& scene) {
  const std::array<std::int16_t, 2> angles = ViewAngles(scene);
  p.squares[0].view_angle = angles[0];
  p.squares[1].view_angle = angles[1];
  p.position = p.owner->position;
}

// CEffectPrim::VectorToDegrees (0057ce90): the first whole degree from 0 whose cosine the
// x over the length reaches, mirrored to 360 less it when z is negative; 180 if none does.
std::int16_t VectorToDegrees(float x, float z) {
  const float length = std::sqrt((x * x) + (z * z));
  const float c = x / length;
  const float s = z / length;
  const core::AngleTables& t = core::GetAngleTables();
  for (std::int16_t k = 0; k <= 180; ++k) {
    if (c >= t.cos[static_cast<std::size_t>(k)])
      return s >= 0.0f ? k : static_cast<std::int16_t>(360 - k);
  }
  return 180;
}

std::int16_t Add16(std::int16_t a, std::int32_t b) { return static_cast<std::int16_t>(a + b); }

// A heading's step along the ground: x by its cosine, z by its sine, times the speed.
void Advance(PrimSquare& s) {
  s.center[0] = (TableAt(kCosines, s.heading) * s.speed) + s.center[0];
  s.center[2] = (TableAt(kSines, s.heading) * s.speed) + s.center[2];
}

// The square that leads leaves a trail: the third record takes the second's turn, size,
// phase, alpha less 25 (at least 0), centre and image once the age passes 3, the second the
// first's once it passes 2, and the first the leader's, alpha less 150, once it passes 1.
void LeaveTrail(EffectPrim& p) {
  const auto copy = [](PrimSquare& to, const PrimSquare& from, std::int32_t fade) {
    to.turn = from.turn;
    to.size = from.size;
    to.phase = from.phase;
    to.alpha = std::max<std::int16_t>(Add16(from.alpha, -fade), 0);
    to.center = from.center;
    to.image = from.image;
  };
  std::array<PrimSquare, 4>& q = p.squares;
  if (q[0].age > 3) copy(q[3], q[2], 25);
  if (q[0].age > 2) copy(q[2], q[1], 25);
  if (q[0].age > 1) copy(q[1], q[0], 150);
}

// An angle walking towards its goal, up by one step or down by the other, within 0 and
// 359; past it, a new goal is drawn on the far side: below it, or 359 less a draw below it.
void Walk(float& value, float& goal, core::CrtRand& rand, float up = 1.0f, float down = 1.0f) {
  if (!(goal <= value)) {
    value = value + up;
    if (value > 359.0f) value = 359.0f;
    if (value > goal) {
      const std::int32_t n = Ftol(value);
      goal = static_cast<float>(n == 0 ? 0 : rand.Next() % n);
    }
  } else {
    value = value - down;
    if (value < 0.0f) value = 0.0f;
    if (!(value > goal)) {
      const std::int32_t n = Ftol(value);
      goal = static_cast<float>(359 - (n == 0 ? 0 : rand.Next() % n));
    }
  }
}

// CEffectPrim::UpdatePrim_31 (0057b0f0), id 0x1f, clouds about the player: below mode 99
// each brightens by 3 a frame (mode 1) or 2 (otherwise; mode 0 only while the owner is
// above the floor, its y less) for its first 80 frames, and fades by 1 once its age passes
// +0xc; faded out it starts again at a new place, 25 to 225 out either way (mode 1) or
// within 150 of the owner, at age 300 if mode 0 and the owner is below the floor. Its two
// walking angles drift the centre by a twentieth of their sines; the phase turns a degree a
// frame.
void UpdateClouds(EffectPrim& p, core::CrtRand& rand) {
  for (PrimSquare& s : p.squares) {
    if (!s.active) continue;
    if (s.mode < 99.0f) {
      ++s.age;
      const bool one = std::bit_cast<std::uint32_t>(s.mode) == 0x3f800000;
      if (s.age < 80) {
        if (one) {
          s.alpha = Add16(s.alpha, 3);
        } else if (s.mode != 0.0f || p.position[1] < s.floor) {
          s.alpha = Add16(s.alpha, 2);
        }
      }
      if (s.age > s.heading) {
        s.alpha = Add16(s.alpha, -1);
        if (s.alpha <= 0) {
          s.age = 0;
          s.alpha = 0;
          if (s.mode == 0.0f && p.position[1] > s.floor) s.age = 300;
          if (one) {
            s.center[0] = static_cast<float>(rand.Next() % 200 + 25);
            if (rand.Next() % 2 == 0) s.center[0] = s.center[0] * -1.0f;
            s.center[2] = static_cast<float>(rand.Next() % 200 + 25);
            if (rand.Next() % 2 == 0) s.center[2] = s.center[2] * -1.0f;
          } else {
            s.center[0] = static_cast<float>(rand.Next() % 300 - 150);
            s.center[2] = static_cast<float>(rand.Next() % 300 - 150);
          }
          s.center[0] = s.center[0] + p.position[0];
          s.center[2] = p.position[2] + s.center[2];
        }
      }
      Walk(s.walk_x, s.speed, rand);
      Walk(s.climb, s.walk_z_goal, rand);
      s.center[0] = (TableAt(kSines, Ftol(s.walk_x)) * 0.05f) + s.center[0];
      s.center[2] = (TableAt(kSines, Ftol(s.climb)) * 0.05f) + s.center[2];
    }
    s.phase = s.phase + 1.0f;
    if (!(s.phase < 360.0f)) s.phase = s.phase - 360.0f;
  }
}

// CEffectPrim::UpdatePrim_40 (00572350), id 0x28, a boomerang: the first record turns 15
// degrees a frame and leaves a trail every frame (colour 1) or third. It heads for the
// target, climbing to its height, until it has come its range; then colour 1 trebles its
// speed, swerves up to 44 degrees and fades by 12 a frame, sinking, while colour 0 turns 5
// degrees a frame back to the owner and, within 4 degrees of it, does the same.
void UpdateBoomerang(EffectPrim& p, core::CrtRand& rand) {
  PrimSquare& s = p.squares[0];
  if (!s.active) return;
  ++s.age;
  if (s.age < 1) return;
  if (s.age % (s.color == 1 ? 1 : 3) == 0) LeaveTrail(p);
  s.turn = Add16(s.turn, -15);
  if (s.turn < 0) s.turn = Add16(s.turn, 360);
  if (s.age == 1) s.heading = VectorToDegrees(p.travel[0] - s.center[0], p.travel[2] - s.center[2]);
  if (s.state == 0) {
    if (static_cast<float>(s.age) * s.speed > s.range) {
      if (s.color == 1) {
        s.state = 2;
        s.speed = s.speed * 3.0f;
        if (rand.Next() % 2 == 0) {
          s.heading = Add16(s.heading, rand.Next() % 45);
          if (s.heading >= 360) s.heading = Add16(s.heading, -360);
        } else {
          s.heading = Add16(s.heading, -(rand.Next() % 45));
          if (s.heading < 0) s.heading = Add16(s.heading, 360);
        }
      } else if (s.color == 0) {
        s.state = 1;
      }
    }
    s.center[1] = s.climb + s.center[1];
  } else if (s.state == 1) {
    const std::int16_t back =
        VectorToDegrees(p.position[0] - s.center[0], p.position[2] - s.center[2]);
    if (s.heading < back - 4 || s.heading > back + 4) {
      s.heading = Add16(s.heading, 5);
      if (s.heading >= 360) s.heading = Add16(s.heading, -360);
    } else {
      s.state = 2;
    }
  } else {
    s.alpha = std::max<std::int16_t>(Add16(s.alpha, -12), 0);
    s.center[1] = s.center[1] - s.climb;
  }
  Advance(s);
}

// CEffectPrim::UpdatePrim_43 (00571e90), id 0x2b, sparks thrown past the target: each
// record waits out a negative age, then turns 5 degrees a frame back; on its first frame it
// heads away from the owner through the target, give or take 30 degrees; it brightens by 15
// for 10 frames, then fades by 3 and goes out, climbing all the while.
void UpdateThrownSparks(EffectPrim& p, core::CrtRand& rand) {
  for (PrimSquare& s : p.squares) {
    if (!s.active) continue;
    ++s.age;
    if (s.age <= 0) continue;
    const std::int16_t turn = s.turn;
    s.turn = Add16(turn, -5);
    if (s.turn < 0) s.turn = Add16(turn, 355);
    if (s.age == 1) {
      s.heading = VectorToDegrees(p.travel[0] - p.position[0], p.travel[2] - p.position[2]);
      s.heading = static_cast<std::int16_t>(Add16(s.heading, rand.Next() % 60 - 30) % 360);
    } else if (s.age < 11) {
      s.alpha = Add16(s.alpha, 15);
    } else {
      s.alpha = Add16(s.alpha, -3);
      if (s.alpha < 0) {
        s.alpha = 0;
        s.active = false;
      }
    }
    s.center[1] = s.climb + s.center[1];
    Advance(s);
  }
}

// CEffectPrim::UpdatePrim_45 (0057b6f0), id 0x2d, a square bobbing over the owner: every 6
// frames each active record behind takes the one before's turn, size plus a half, phase,
// alpha less 50, centre and image, colour 2 and mode 0. The first swells (phase 5 a frame),
// bobs 3 either way about 6 above the owner on the sine of its heading (a degree a frame)
// and, colour 7, turns 10 degrees a frame.
void UpdateBobbingSquare(EffectPrim& p) {
  std::array<PrimSquare, 4>& q = p.squares;
  PrimSquare& s = q[0];
  if (!s.active) return;
  ++s.age;
  if (s.age % 6 == 0) {
    for (std::size_t k = 3; k > 0; --k) {
      PrimSquare& to = q[k];
      const PrimSquare& from = q[k - 1];
      if (!to.active) continue;
      to.turn = from.turn;
      to.size = from.size + kHalf;
      to.phase = from.phase;
      to.alpha = std::max<std::int16_t>(Add16(from.alpha, -50), 0);
      to.center = from.center;
      to.image = from.image;
      to.color = 2;
      to.mode = 0.0f;
    }
  }
  s.phase = s.phase + 5.0f;
  if (!(s.phase < 360.0f)) s.phase = s.phase - 360.0f;
  s.heading = Add16(s.heading, 1);
  if (s.heading > 359) s.heading = Add16(s.heading, -360);
  s.center[0] = p.position[0];
  s.center[2] = p.position[2];
  s.base = p.position[1] - 6.0f;
  s.center[1] = (TableAt(kSines, s.heading) * 3.0f) + s.base;
  if (s.color == 7) {
    s.turn = Add16(s.turn, 10);
    if (s.turn > 359) s.turn = Add16(s.turn, -360);
  }
}

// CEffectPrim::UpdatePrim_47 (0057b970), id 0x2f: each record brightens by 25 for 10
// frames, then fades by 5, sinking a fifth a frame; faded out it starts again at the
// owner's height (5 above, state 1). The phase turns 5 degrees a frame and, state 1, the
// square too.
void UpdateSinkingSquares(EffectPrim& p) {
  for (PrimSquare& s : p.squares) {
    if (!s.active) continue;
    ++s.age;
    if (s.age < 11) {
      s.alpha = Add16(s.alpha, 25);
    } else {
      s.alpha = Add16(s.alpha, -5);
      if (s.alpha < 1) {
        s.alpha = 0;
        s.age = 0;
        s.center[1] = s.state == 0 ? p.position[1] : p.position[1] - 5.0f;
      }
      s.center[1] = s.center[1] - std::bit_cast<float>(0x3e4ccccdu);  // 0.2
    }
    s.phase = s.phase + 5.0f;
    if (!(s.phase < 360.0f)) s.phase = s.phase - 360.0f;
    if (s.state == 1) {
      const std::int16_t turn = s.turn;
      s.turn = Add16(turn, 5);
      if (s.turn > 359) s.turn = Add16(turn, -355);
    }
  }
}

// CEffectPrim::UpdatePrim_51 (005728c0), id 0x33, a square thrown in an arc: the first
// record leaves a trail every other frame and turns 10 degrees a frame (never reduced
// past 360). It heads for the target over its steps (+0x71), climbing to the target's
// height, lifted on half a sine 10 high (colour 0) or 30; 5% past the steps it fades by 20
// a frame where it stands.
void UpdateArcingSquare(EffectPrim& p) {
  PrimSquare& s = p.squares[0];
  if (!s.active) return;
  ++s.age;
  if (s.age % 2 == 1) LeaveTrail(p);
  s.turn = Add16(s.turn, 10);
  if (s.turn < 0) s.turn = Add16(s.turn, 360);
  if (s.age == 1) s.heading = VectorToDegrees(p.travel[0] - s.center[0], p.travel[2] - s.center[2]);
  float t = static_cast<float>(s.age) / static_cast<float>(static_cast<std::int8_t>(s.state));
  if (!(t <= std::bit_cast<float>(0x3f866666u))) {  // 1.05
    s.alpha = std::max<std::int16_t>(Add16(s.alpha, -20), 0);
    return;
  }
  if (!(t <= 1.0f)) t = 1.0f;
  const std::int32_t arc = Ftol(t * 180.0f);
  s.base = s.climb + s.base;
  s.center[1] = s.base - (TableAt(kSines, arc) * (s.color == 0 ? 10.0f : 30.0f));
  Advance(s);
}

// CEffectPrim::UpdatePrim_55 (005727d0), id 0x37: the first record swells (phase 3 a
// frame), brightens by 4 for 20 frames and fades by 1 after 100.
void UpdatePulsingSquare(EffectPrim& p) {
  PrimSquare& s = p.squares[0];
  if (!s.active) return;
  ++s.age;
  s.phase = s.phase + 3.0f;
  if (!(s.phase < 360.0f)) s.phase = s.phase - 360.0f;
  if (s.age <= 20) s.alpha = Add16(s.alpha, 4);
  if (s.age > 100) s.alpha = std::max<std::int16_t>(Add16(s.alpha, -1), 0);
}

// CEffectPrim::UpdatePrim_56 (00572020), id 0x38, rising sparks: each record waits out a
// negative age, then turns 5 degrees a frame back, takes a random heading, brightens by 15
// for 10 frames and fades by 3 and goes out, rising by its climb. State 1 flashes the
// second image for two frames (the first record at 35 and 36, each next 7 sooner).
void UpdateRisingSparks(EffectPrim& p, core::CrtRand& rand) {
  for (std::size_t k = 0; k < p.squares.size(); ++k) {
    PrimSquare& s = p.squares[k];
    if (!s.active) continue;
    ++s.age;
    if (s.age <= 0) continue;
    const std::int16_t turn = s.turn;
    s.turn = Add16(turn, -5);
    if (s.turn < 0) s.turn = Add16(turn, 355);
    if (s.age == 1) {
      s.heading = static_cast<std::int16_t>(rand.Next() % 360);
    } else if (s.age < 11) {
      s.alpha = Add16(s.alpha, 15);
    } else {
      if (s.state == 1) {
        const auto flash = static_cast<std::int32_t>(35 - 7 * k);
        s.image = s.age == flash || s.age == flash + 1 ? 1 : 0;
      }
      s.alpha = Add16(s.alpha, -3);
      if (s.alpha < 0) {
        s.alpha = 0;
        s.active = false;
      }
    }
    s.center[1] = s.center[1] - s.climb;
    Advance(s);
  }
}

// CEffectPrim::UpdatePrim_26 (0056dfb0) and UpdatePrim_37 (0056e010), ids 0x1a and 0x25:
// the first two records' swell phases (their tilts) turn 3 degrees a frame, active or not;
// 0x1a then steps its image.
void UpdateSwellingSquares(EffectPrim& p, bool images) {
  p.position = p.owner->position;
  for (std::size_t k = 0; k < 2; ++k) {
    PrimRing& r = p.rings[k];
    const std::int16_t tilt = r.tilt;
    r.tilt = static_cast<std::int16_t>(tilt + 3);
    if (r.tilt > 359) r.tilt = static_cast<std::int16_t>(tilt - 357);
  }
  if (images) AdvanceImage(p);
}

// CEffectPrim::UpdatePrim_64 (0056e070), id 0x40: the first record's age counts on, and
// from its first frame it fades by 1 every odd one.
void UpdateFadingSquare(EffectPrim& p) {
  p.position = p.owner->position;
  PrimRing& r = p.rings[0];
  ++r.age;
  if (r.age <= 0) return;
  if (r.age % 2 > 0) r.alpha = static_cast<std::int16_t>(r.alpha - 1);
  if (r.alpha < 0) r.alpha = 0;
}

// The camera's angles into the motes' records, and the primitive at its owner.
void FaceCameraMotes(EffectPrim& p, const PrimScene& scene) {
  const std::array<std::int16_t, 2> angles = ViewAngles(scene);
  p.motes[0].view_angle = angles[0];
  p.motes[1].view_angle = angles[1];
  p.position = p.owner->position;
}

// The order the motes' updates and drawings take them in (+0xa0, +0x88, +0xac, +0x94).
constexpr std::array<std::size_t, 4> kMoteOrder = {2, 0, 3, 1};

// CEffectPrim::UpdatePrim_27 (0056e0d0), id 0x1b, rising motes: each active record's eight
// angles walk 3 up or 2 down towards their goals. Each mote above the ground drifts by
// 0.15 of the sines of two of them (x and z by angles 0 and 2 forward, 4 and 6 back, 8
// forward and 10 back, 12 back and 14 forward, in the motes' order); past 30 up it starts
// again from the centre up to 99 down; and it rises by the speed.
void UpdateRisingMotes(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene) {
  FaceCameraMotes(p, scene);
  constexpr float kDrift = 0.15f;  // 005e9ed8
  for (PrimMotes& r : p.motes) {
    if (!r.active) continue;
    for (std::size_t k = 0; k < 16; k += 2) Walk(r.values[k], r.values[k + 1], rand, 3.0f, 2.0f);
    for (std::size_t n = 0; n < 4; ++n) {
      Vec3& m = r.points[kMoteOrder[n]];
      if (m[1] < 0.0f) {
        const float dx = TableAt(kSines, Ftol(r.values[4 * n])) * kDrift;
        const float dz = TableAt(kSines, Ftol(r.values[4 * n + 2])) * kDrift;
        m[0] = n == 0 || n == 2 ? dx + m[0] : m[0] - dx;
        m[2] = n == 0 || n == 3 ? dz + m[2] : m[2] - dz;
        if (m[1] < -30.0f) m = {0.0f, static_cast<float>(rand.Next() % 100), 0.0f};
      }
      m[1] = m[1] - r.speed;
    }
  }
}

// CEffectPrim::UpdatePrim_30 (0057a900), id 0x1e, a pulsing mote: the first record
// brightens by 1 a frame for 200 frames; three angles walk a degree towards their goals,
// and the mote sits at their sines (x, z and y by the first, second and third) plus 7 along
// the phase, which turns a degree a frame, and the climb; the swell phase turns 5 to 9
// degrees a frame.
void UpdatePulsingMote(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene) {
  FaceCameraMotes(p, scene);
  PrimMotes& r = p.motes[0];
  if (!r.active) return;
  ++r.age;
  if (r.age < 201) r.alpha = static_cast<std::int16_t>(r.alpha + 1);
  for (std::size_t k = 0; k < 6; k += 2) Walk(r.values[k], r.values[k + 1], rand);
  Vec3& m = r.points[0];
  m[0] = TableAt(kSines, Ftol(r.values[0]));
  m[2] = TableAt(kSines, Ftol(r.values[2]));
  m[1] = TableAt(kSines, Ftol(r.values[4]));
  float& phase = r.values[6];
  phase = phase + 1.0f;
  if (!(phase < 360.0f)) phase = phase - 360.0f;
  m[0] = (TableAt(kSines, Ftol(phase)) * 7.0f) + m[0];
  m[2] = (TableAt(kCosines, Ftol(phase)) * 7.0f) + m[2];
  m[1] = r.values[7] + m[1];
  float& swell = r.values[8];
  swell = static_cast<float>(rand.Next() % 5 + 5) + swell;
  if (!(swell < 360.0f)) swell = swell - 360.0f;
}

// CEffectPrim::UpdatePrim_33 (0056e4a0), id 0x21, popping motes: per active record and
// mote, a count runs up a frame at a time to 10 while the alpha grows by 25; then the alpha
// falls by 12, and below 12 the mote starts again at a random angle and image, 3 to 10 up.
void UpdatePoppingMotes(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene) {
  FaceCameraMotes(p, scene);
  for (PrimMotes& r : p.motes) {
    if (!r.active) continue;
    for (std::size_t n = 0; n < 4; ++n) {
      float* v = &r.values[4 * n];
      v[0] = v[0] + 1.0f;
      if (v[0] >= 10.0f) {
        v[1] = v[1] - 12.0f;
        if (v[1] < 12.0f) {
          v[0] = 0.0f;
          v[1] = 0.0f;
          v[3] = static_cast<float>(rand.Next() % 360);
          v[2] = static_cast<float>(rand.Next() % 5);
          r.points[kMoteOrder[n]][1] = static_cast<float>(-3 - rand.Next() % 8);
        }
      } else {
        v[1] = v[1] + 25.0f;
      }
    }
  }
}

// The camera's angles into the pieces' records, and with follow the primitive at its
// owner.
void FaceCameraPieces(EffectPrim& p, const PrimScene& scene, bool follow) {
  const std::array<std::int16_t, 2> angles = ViewAngles(scene);
  p.pieces[0].view_angle = angles[0];
  p.pieces[1].view_angle = angles[1];
  if (follow) p.position = p.owner->position;
}

// CEffectPrim::UpdatePrim_41 (00571d80), id 0x29: each record waits out a negative age,
// then flies out by its growth a frame, brightens by 15 (colour 0) or 25 for 10 frames and
// fades by 7 after 25; its tilt turns 5 degrees a frame.
void UpdateBurstSquares(EffectPrim& p) {
  for (PrimPiece& s : p.pieces) {
    if (!s.active) continue;
    ++s.age;
    if (s.age > 0) s.distance = s.distance + s.growth;
    if (s.age < 11 && s.age > 0) {
      s.alpha = Add16(s.alpha, s.color == 0 ? 15 : 25);
    } else if (s.age > 25) {
      s.alpha = Add16(s.alpha, -7);
      if (s.alpha < 1) s.alpha = 0;
    }
    if (s.turn > 359) s.turn = Add16(s.turn, -360);
    const float tilt = s.values[1] + 5.0f;
    s.values[1] = tilt >= 360.0f ? tilt - 360.0f : tilt;
  }
}

// CEffectPrim::UpdatePrim_42 (00571e30), id 0x2a: each record waits out a negative age,
// then moves out by its growth a frame, brightening by 15 from frame 11 to 20 and fading by
// 5 otherwise.
void UpdateRays(EffectPrim& p) {
  for (PrimPiece& s : p.pieces) {
    if (!s.active) continue;
    ++s.age;
    if (s.age > 0) s.distance = s.growth + s.distance;
    if (s.age < 21 && s.age > 10) {
      s.alpha = Add16(s.alpha, 15);
    } else {
      s.alpha = Add16(s.alpha, -5);
      if (s.alpha < 1) s.alpha = 0;
    }
  }
}

// CEffectPrim::UpdatePrim_50 (0057b440), id 0x32, fireworks: each record waits out a
// negative age; then every third frame its trail moves on (the centre to +0xa0, that to
// +0x94, that to +0xac), it sparkles (image 1 at frames 15 and 19 of every 20, 2 at 17)
// from its delay plus 160 to plus 180, and it turns 5 degrees a frame. Stage 0 rises a
// quarter a frame until the age passes 154, then bursts at alpha 250; stage 1 flies out
// along its direction at its speed, falling 0.02 more each frame, the speed 0.98 of itself,
// fading by 1 or (half the time) 2. After 2000 frames it starts again 15 short of the
// owner in a new direction and speed.
void UpdateFireworks(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene) {
  FaceCameraPieces(p, scene, true);
  for (PrimPiece& s : p.pieces) {
    if (!s.active) continue;
    ++s.age;
    if (s.age <= 0) continue;
    if (s.age % 3 == 0) {
      s.points[3] = s.points[1];
      s.points[1] = s.points[2];
      s.points[2] = s.points[0];
    }
    s.image = 0;
    if (s.age <= s.delay + 180 && s.age >= s.delay + 160) {
      const auto m = static_cast<std::int8_t>(s.age % 20);
      if (m == 15 || m == 19) {
        s.image = 1;
      } else if (m == 17) {
        s.image = 2;
      }
    }
    s.turn = Add16(s.turn, 5);
    if (s.turn >= 360) s.turn = Add16(s.turn, -360);
    Vec3& c = s.points[0];
    if (s.length == 0) {
      c[1] = c[1] - 0.25f;
      if (s.age > 154) {
        s.length = 1;
        s.alpha = 250;
      }
    } else if (s.length == 1) {
      const float out = TableAt(kCosines, s.pitch) * s.distance;
      c[1] = ((TableAt(kSines, s.pitch) * s.distance) + c[1]) + std::bit_cast<float>(0x3ca3d70au);
      c[0] = (out * TableAt(kCosines, s.yaw)) + c[0];
      c[2] = (out * TableAt(kSines, s.yaw)) + c[2];
      s.distance = s.distance * std::bit_cast<float>(0x3f7ae148u);  // 0.98
      s.alpha = Add16(s.alpha, -1);
      if (rand.Next() % 2 == 0) s.alpha = Add16(s.alpha, -1);
      if (s.alpha < 0) s.alpha = 0;
    }
    if (s.age > 2000) {
      s.age = 0;
      s.length = 0;
      s.yaw = static_cast<std::int16_t>(rand.Next() % 360);
      c = p.position;
      c[0] = c[0] - 15.0f;
      s.distance = (static_cast<float>(rand.Next() % 100) * std::bit_cast<float>(0x3b83126fu)) +
                   std::bit_cast<float>(0x3ecccccdu);  // 0.004, 0.4
    }
  }
}

// CEffectPrim::UpdatePrim_52 (00572c40) and UpdatePrim_53 (00572ce0), ids 0x34 and 0x35:
// each record brightens by 18 for 10 frames; 0x34 fades by 15 after 20. 0x35 follows its
// owner.
void UpdateBeams(EffectPrim& p, const PrimScene& scene, bool fade) {
  FaceCameraPieces(p, scene, !fade);
  for (PrimPiece& s : p.pieces) {
    if (!s.active) continue;
    ++s.age;
    if (s.age < 11) {
      s.alpha = Add16(s.alpha, 18);
    } else if (s.age > 20) {
      if (fade) s.alpha = Add16(s.alpha, -15);
      if (s.alpha < 0) s.alpha = 0;
    }
  }
}

// CEffectPrim::UpdatePrim_65 (0056b300), id 0x41: each record waits out a negative age;
// for its first 19 frames its ten alphas grow by 13 to 70, 90, ... 250; then all but the
// last fade by 5, and after 140 frames the last too, the size growing a hundredth a frame
// while it does. For 49 frames the size shrinks by a tenth.
void UpdateAsuraSquares(EffectPrim& p, const PrimScene& scene) {
  FaceCameraPieces(p, scene, false);
  for (PrimPiece& s : p.pieces) {
    if (!s.active) continue;
    ++s.age;
    if (s.age <= 0) continue;
    if (s.age < 20) {
      for (std::size_t k = 0; k < s.values.size(); ++k) {
        const auto limit = static_cast<float>(70 + 20 * static_cast<std::int32_t>(k));
        s.values[k] = s.values[k] + 13.0f;
        if (!(s.values[k] < limit)) s.values[k] = limit;
      }
    } else {
      for (std::size_t k = 0; k + 1 < s.values.size(); ++k) {
        s.values[k] = s.values[k] - 5.0f;
        if (s.values[k] < 0.0f) s.values[k] = 0.0f;
      }
      if (s.age > 140) {
        float& last = s.values.back();
        last = last - 5.0f;
        if (last > 0.0f) s.distance = s.distance * std::bit_cast<float>(0x3f8147aeu);  // 1.01
        if (last < 0.0f) last = 0.0f;
      }
    }
    if (s.age < 50) s.distance = s.distance - std::bit_cast<float>(0x3dcccccdu);  // 0.1
  }
}

// CEffectPrim::UpdatePrim_32 (00571bb0), id 0x20: each ring closes in by a hundredth a
// step, fades in by 10 for 10 steps and then out by 1; faded out it goes back to radius 20
// and tilt 90, and stops once past age 30. Every wall stands at the height times the sine
// of the age.
void UpdateClosingWalls(EffectPrim& p) {
  for (PrimRing& r : p.rings) {
    if (!r.active) continue;
    ++r.age;
    r.radius = r.radius - kWallsClose;
    if (r.age < 10) {
      r.alpha = Add16(r.alpha, 10);
    } else {
      r.alpha = Add16(r.alpha, -1);
      if (r.alpha <= 0) {
        r.alpha = 0;
        if (r.age > 30) r.active = false;
        r.radius = 20.0f;
        r.tilt = 90;
      }
    }
    r.heights.fill(TableAt(kSines, r.age) * r.height);
  }
}

// CEffectPrim::UpdatePrim_58 (0056b7f0), id 0x3a, at its owner: each ring's centre stands
// at its orbit along its angle, which it reduces past 360 but never turns; it fades in by
// 5 to 180 for 10 steps and, from the age in its kind, out by 2. Wall i stands at the
// height times sin 9i, and for 90 steps times the sine of the age too, never below 0.
void UpdateOrbitingRings(EffectPrim& p) {
  p.position = p.owner->position;
  const core::AngleTables& t = core::GetAngleTables();
  for (PrimRing& r : p.rings) {
    if (!r.active) continue;
    ++r.age;
    if (r.angle >= 360) r.angle = Add16(r.angle, -360);
    r.center = {(TableAt(kCosines, r.angle) * r.orbit) + 0.0f, 0.0f,
                (TableAt(kSines, r.angle) * r.orbit) + 0.0f};
    if (r.age >= r.kind) {
      r.alpha = Add16(r.alpha, -2);
      if (r.alpha < 0) r.alpha = 0;
    } else if (r.age < 10) {
      r.alpha = Add16(r.alpha, 5);
      if (r.alpha > 180) r.alpha = 180;
    }
    for (std::size_t i = 0; i < r.heights.size(); ++i) {
      float h = t.sin[9 * i] * r.height;
      if (r.age <= 90) h = h * TableAt(kSines, r.age);
      r.heights[i] = h >= 0.0f ? h : 0.0f;
    }
  }
}

// CEffectPrim::UpdatePrim_59 (0056ba70), id 0x3b, at its owner: each ring waits out a
// negative age, then turns 5 degrees a step and opens by 3 to 120 degrees, fading in by
// 10 to 250 for its first 11 steps and out by 3 after 1400.
void UpdateRaisedRings(EffectPrim& p) {
  p.position = p.owner->position;
  for (PrimRing& r : p.rings) {
    if (!r.active) continue;
    ++r.age;
    if (r.age <= 0) continue;
    const std::int16_t angle = r.angle;
    r.angle = Add16(angle, 5);
    if (r.angle >= 360) r.angle = Add16(angle, -355);
    r.span = Add16(r.span, 3);
    if (r.span > 120) r.span = 120;
    if (r.age > 1400) {
      r.alpha = Add16(r.alpha, -3);
      if (r.alpha < 0) r.alpha = 0;
    }
    if (r.age < 12) {
      r.alpha = Add16(r.alpha, 10);
      if (r.alpha > 250) r.alpha = 250;
    }
  }
}

// CEffectPrim::UpdatePrim_35 (00571c40), id 0x23: each box fades in by 9 for 10 steps and
// then out by 1, stopping once faded past age 30; its half-length and half-width grow by
// 0.005 a step, and its height is the full height times the sine of the age.
void UpdateBox(EffectPrim& p) {
  for (PrimBand& b : p.bands) {
    if (!b.active) continue;
    ++b.age;
    if (b.age < 10) {
      b.alpha = Add16(b.alpha, 9);
    } else {
      b.alpha = Add16(b.alpha, -1);
      if (b.alpha <= 0) {
        b.alpha = 0;
        if (b.age > 30) b.active = false;
      }
    }
    b.values[0] = b.values[0] + kBoxGrowth;
    b.values[1] = b.values[1] + kBoxGrowth;
    b.values[2] = TableAt(kSines, b.age) * b.size;
  }
}

// CEffectPrim::UpdatePrim_61 (00571cb0), id 0x3d: each box waits out a negative age; its
// phase turns a degree a step, and from the age it fades at it fades by 2. Its height is
// 0.65 of the full height plus 0.35 of it on the phase's sine, for 90 steps times the
// sine of the age too.
void UpdateBoxWalls(EffectPrim& p) {
  for (PrimBand& b : p.bands) {
    if (!b.active) continue;
    ++b.age;
    if (b.age <= 0) continue;
    b.phase = Add16(b.phase, 1);
    if (b.age > b.fade_at) {
      b.alpha = Add16(b.alpha, -2);
      if (b.alpha < 0) b.alpha = 0;
    }
    float h = (TableAt(kSines, b.phase % 360) * b.size) * kBoxPulse;
    h = h + (b.size * kBoxBase);
    if (b.age < 90) h = h * TableAt(kSines, b.age);
    b.values[2] = h;
  }
}

// CEffectPrim::UpdatePrim_63 (0056bc30), id 0x3f: every record, active or not, ages and
// has its angle reduced past 360; the second and fourth pulse their radius by half the
// sine of half their age (within 720) about the radius in their first value.
void UpdatePentagonBand(EffectPrim& p) {
  for (std::size_t k = 0; k < p.bands.size(); ++k) {
    PrimBand& b = p.bands[k];
    ++b.age;
    if (b.angle >= 360) b.angle = Add16(b.angle, -360);
    if (k == 1 || k == 3) b.size = (TableAt(kSines, b.age % 720 / 2) * 0.5f) + b.values[0];
  }
}

// CEffectPrim::UpdatePrim_66 (005722d0), id 0x42: each band waits out a negative age, then
// brightens by 8 for 5 steps and fades by 4; its far end lies its length on from the near
// along its angle.
void UpdateArcBands(EffectPrim& p) {
  for (PrimBand& b : p.bands) {
    if (!b.active) continue;
    ++b.age;
    if (b.age <= 0) continue;
    if (b.age <= 5) {
      b.alpha = Add16(b.alpha, 8);
    } else {
      b.alpha = Add16(b.alpha, -4);
      if (b.alpha < 0) b.alpha = 0;
    }
    b.points[1][0] = (TableAt(kCosines, b.angle) * b.length) + b.points[0][0];
    b.points[1][2] = (TableAt(kSines, b.angle) * b.length) + b.points[0][2];
  }
}

// CEffectPrim::UpdatePrim_67 (0056b460), id 0x43: each trail waits out a negative age, then
// fades by 5 after 35 steps; its newest segment goes 8 degrees further through its
// bounce, a bounce more past 180, and every segment takes the one before's place.
void UpdateBouncingBands(EffectPrim& p) {
  for (PrimBand& b : p.bands) {
    if (!b.active) continue;
    ++b.age;
    if (b.age <= 0) continue;
    if (b.age > 35) b.alpha = Add16(b.alpha, -5);
    if (b.alpha < 0) b.alpha = 0;
    const float through = b.values[0] + 8.0f;
    b.values[0] = through;
    if (through > 180.0f) {
      b.values[0] = through - 180.0f;
      b.bounces[0] = static_cast<std::int8_t>(b.bounces[0] + 1);
    }
    for (std::size_t i = b.values.size() - 1; i > 0; --i) {
      b.values[i] = b.values[i - 1];
      b.bounces[i] = b.bounces[i - 1];
    }
  }
}

// Matrix::MakeAxisRotation (004099d0): a turn about the axis by the angle in radians, the
// quaternion of half the angle's sine along the axis and its cosine, with no translation.
Matrix MakeAxisRotation(const Vec3& axis, float radians) {
  const float half = radians * 0.5f;
  const float s = SinFloat(half);
  return MakeRotationFromKey({s * axis[0], s * axis[1], s * axis[2], CosFloat(half)});
}

// CRenderObject::TurnTowards (005005b0): the facing that looks at (x, z) from where it
// stands: fpatan of the x and the negated z apart, over π (005e94f4, as a float) and times
// 180, within 0 to 360.
float TurnTowards(const Vec3& from, float x, float z) {
  const float dx = x - from[0];
  const float dz = -(z - from[2]);
  const double turns = std::atan2(static_cast<double>(dx), static_cast<double>(dz)) /
                       static_cast<double>(std::bit_cast<float>(0x40490fd8u));
  const float facing = static_cast<float>(turns) * 180.0f;
  float out = facing < 360.0f ? facing : facing - 360.0f;
  if (out < 0.0f) out = out + 360.0f;
  return out;
}

// The end every sprite trail's update shares: the angle with the camera under
// kFaceCamera, then the alpha, the history and the sprite's frame.
bool EndTrailStep(EffectPrim& p, const PrimScene& scene) {
  if ((p.flags & EffectPrim::kFaceCamera) != 0) p.angle = p.facing - scene.view_yaw;
  UpdateAlpha(p);
  ShiftHistory(p);
  return AdvanceAction(p);
}

// CEffectPrim::UpdatePrim_7 (0057f4a0): a particle circling its owner (its offset added) at
// the distance along the heading, both changing by speeds that change by rates, the
// offset's y by the fall; at change_at the fall and its rate take +0x1cc and +0x1d0; with
// distance_keys, at each key frame the distance's speed and rate take the next of the key
// values and limits. The matrix turns about z by the pitch, its value taken as radians.
bool UpdateCirclingParticle(EffectPrim& p) {
  p.start = p.owner->position;
  p.distance_speed = p.distance_speed + p.distance_rate;
  p.fall_speed = p.fall_speed + p.fall_rate;
  p.heading_speed = p.heading_rate + p.heading_speed;
  p.distance = p.distance_speed + p.distance;
  p.heading = p.heading_speed + p.heading;
  p.offset[1] = p.fall_speed + p.offset[1];
  for (std::size_t i = 0; i < 3; ++i) p.start[i] = p.offset[i] + p.start[i];
  p.angle = p.spin + p.angle;
  if (p.age == p.change_at) {
    p.fall_speed = std::bit_cast<float>(p.changes[2]);
    p.fall_rate = std::bit_cast<float>(p.changes[5]);
  }
  const auto key = static_cast<std::size_t>(p.key);
  if (p.distance_keys && key < p.key_frames.size() &&
      static_cast<float>(p.age) == p.key_frames[key]) {
    ++p.key;
    p.distance_speed = key < p.key_values.size() ? p.key_values[key] : 0.0f;
    p.distance_rate = key < p.key_limits.size() ? p.key_limits[key] : 0.0f;
  }
  p.matrix = MakeRotationY(Radians(p.heading));
  p.travel = AlongZ(p, p.distance);
  for (std::size_t i = 0; i < 3; ++i) p.position[i] = p.travel[i] + p.start[i];
  p.scale = p.scale_speed + p.scale;
  p.pitch = p.pitch_speed + p.pitch;
  p.matrix = RotateZ(p.matrix, p.pitch);
  UpdateAlpha(p);
  ShiftHistory(p);
  return AdvanceAction(p);
}

// CEffectPrim::UpdatePrim_8 (0057f6d0): a shot from its owner (its offset added) flying
// along its matrix's z at its speed less its fall, each changing by its rate, while it
// circles its axis at the pitch's distance by the angle (the drift).
bool UpdateCirclingShot(EffectPrim& p) {
  for (std::size_t i = 0; i < 3; ++i) p.start[i] = p.owner->position[i] + p.offset[i];
  p.speed = p.acceleration + p.speed;
  p.pitch_speed = p.pitch_rate + p.pitch_speed;
  p.fall_speed = p.fall_rate + p.fall_speed;
  p.pitch = p.pitch_speed + p.pitch;
  p.step = AlongZ(p, p.speed - p.fall_speed);
  p.orbit = MakeAxisRotation(p.axis, Radians(p.angle));
  const Matrix& o = p.orbit;
  p.drift = {(p.pitch * o[3]) + o[9], (p.pitch * o[4]) + o[10], (p.pitch * o[5]) + o[11]};
  p.scale = p.scale_speed + p.scale;
  for (std::size_t i = 0; i < 3; ++i) p.travel[i] = p.travel[i] - p.step[i];
  for (std::size_t i = 0; i < 3; ++i) p.position[i] = (p.start[i] + p.travel[i]) + p.drift[i];
  UpdateAlpha(p);
  ShiftHistory(p);
  return AdvanceAction(p);
}

// CEffectPrim::UpdatePrim_38 (00568300), id 0x26, a falling leaf: with its first record
// active, two angles walk 3 up or 2 down towards their goals, the leaf falls by the record's
// speed and drifts by their sines (0.24 along x and 0.3 along z, or half that in mode 1);
// below its owner it starts again within 150 of it, 200 above. The primitive stands at the
// leaf.
bool UpdateFallingLeaf(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene) {
  p.position = p.owner->position;
  PrimMotes& r = p.motes[0];
  if (r.active) {
    Walk(r.values[0], r.values[1], rand, 3.0f, 2.0f);
    Walk(r.values[2], r.values[3], rand, 3.0f, 2.0f);
    Vec3& at = r.points[2];
    at[1] = r.speed + at[1];
    const bool slow = r.mode != 0;
    at[0] = (TableAt(kSines, Ftol(r.values[0])) * (slow ? kLeafDriftSlowX : kLeafDriftX)) + at[0];
    at[2] = (TableAt(kSines, Ftol(r.values[2])) * (slow ? kRingWave : kLeafDriftZ)) + at[2];
    if (at[1] > p.position[1]) {
      at[0] = (static_cast<float>(rand.Next() % 300) + p.position[0]) - 150.0f;
      at[1] = p.position[1] - 200.0f;
      at[2] = (static_cast<float>(rand.Next() % 300) + p.position[2]) - 150.0f;
    }
    p.travel = at;
  }
  p.position = p.travel;
  return EndTrailStep(p, scene);
}

// CEffectPrim::UpdatePrim_44 (00568580), id 0x2c, and UpdatePrim_49 (00568630), id 0x31: at
// the owner, or with the first record active at its point; 0x2c moves its travel there too.
bool UpdateStandingSprite(EffectPrim& p, const PrimScene& scene, bool travels) {
  p.position = p.owner->position;
  const PrimMotes& r = p.motes[0];
  if (travels) {
    if (r.active) p.travel = r.points[2];
    p.position = p.travel;
  } else if (r.active) {
    p.position = r.points[2];
  }
  return EndTrailStep(p, scene);
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

// CEffectPrim::RenderActFrameHistory (00581ff0), ids 5 to 8, 0x15, 0x26, 0x2c and 0x31: the
// sprite's current frame at each history record, scaled by its scale and coloured by its
// colour.
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
      out.quad_sprites.push_back(sprite);
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

// The view transform as DrawPresetColorQuad writes it out, x's sum from x.
Vec3 TransformPointFromX(const Matrix& m, const Vec3& p) {
  return {(((p[0] * m[0]) + (p[2] * m[6])) + (p[1] * m[3])) + m[9],
          (((p[2] * m[7]) + (p[1] * m[4])) + (p[0] * m[1])) + m[10],
          (((p[2] * m[8]) + (p[1] * m[5])) + (p[0] * m[2])) + m[11]};
}

// The colours of DrawPresetColorQuad's presets 1 to 11 (00569ff4), red, green and blue.
struct Preset {
  std::int32_t red, green, blue;
};
constexpr std::array<Preset, 11> kPresets = {{{255, 175, 175},
                                              {195, 195, 255},
                                              {255, 255, 255},
                                              {100, 100, 255},
                                              {255, 255, 255},
                                              {255, 100, 100},
                                              {100, 255, 100},
                                              {255, 255, 170},
                                              {140, 210, 140},
                                              {255, 89, 182},
                                              {170, 170, 255}}};

// CEffectPrim::DrawPresetColorQuad (00569fc0): a wall from four corners relative to the
// primitive (the last step's bottom, the bottom, the top, the last step's top) as two
// triangles of the first texture, u the cell's 21sts, coloured by the preset the scale
// truncates to (white outside 1 to 11; preset 3 forces alpha 80), blended SRCALPHA onto
// ONE for a scale below 4 other than 1 and onto INVSRCALPHA otherwise, with AddPrimitive's
// flags +0x128.
void DrawPresetQuad(const EffectPrim& p, const PrimScene& scene, std::int32_t cell,
                    std::int16_t alpha, const std::array<Vec3, 4>& corners, PrimDrawing& out) {
  std::int32_t a = alpha;
  Preset c{255, 255, 255};
  const std::int32_t preset = Ftol(p.scale);
  if (preset >= 1 && preset <= 11) c = kPresets[static_cast<std::size_t>(preset - 1)];
  if (preset == 3) a = 0x50;
  const auto u = [](std::int32_t v) { return static_cast<std::uint32_t>(v); };
  const std::uint32_t color = ((u(a) * 256 + u(c.red)) * 256 + u(c.green)) * 256 + u(c.blue);
  const float u0 = cell == 0 ? 0.0f : static_cast<float>(cell) * kRingCell;
  const float u1 = static_cast<float>(cell + 1) * kRingCell;
  std::array<Vec3, 4> q = corners;
  for (Vec3& v : q)
    for (std::size_t i = 0; i < 3; ++i) v[i] = v[i] + p.position[i];
  const std::uint32_t dest =
      std::bit_cast<std::uint32_t>(p.scale) != 0x3f800000 && p.scale < 4.0f ? 6 : 2;
  const auto vertex = [&](const Vec3& v, bool scale_first, float u, float tv) {
    ScreenVertex sv =
        ProjectPoint(ClampNear(TransformPointFromX(scene.view, v)), scene.projection, scale_first);
    sv.diffuse = color;
    sv.u = u;
    sv.v = tv;
    return sv;
  };
  PrimTriangle t;
  t.texture = p.textures.empty() ? std::string() : p.textures[0];
  t.flags = p.quad_flags;
  t.src_blend = 5;
  t.dest_blend = dest;
  t.vertices = {vertex(q[0], false, u0, 1.0f), vertex(q[1], true, u1, 1.0f),
                vertex(q[2], true, u1, 0.0f)};
  out.triangles.push_back(t);
  t.vertices = {vertex(q[3], false, u0, 0.0f), vertex(q[2], false, u1, 0.0f),
                vertex(q[0], false, u0, 1.0f)};
  out.triangles.push_back(t);
}

// CEffectPrim::RenderRingWalls (0056c0d0), ids 0x1d and 0x27: each active ring's span in
// steps of a 21st from its angle (a full ring's 21st step back at its start), at its
// radius; each wall leans out by the tilt to its height and joins the last step's. The
// angles index the tables directly, without reduction.
void DrawRingWalls(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  const core::AngleTables& t = core::GetAngleTables();
  for (const PrimRing& r : p.rings) {
    if (!r.active || r.span <= 0) continue;
    const std::int32_t step = r.span / 21;
    const bool low = std::bit_cast<std::uint32_t>(r.height) == kLowWalls;
    Vec3 last_bottom{};
    Vec3 last_top{};
    std::int32_t cell = 0;
    for (std::int32_t a = 0; a < r.span; a += step) {
      std::int32_t index = (r.angle + a) % 360;
      if (r.span == 360 && cell == 20) index = r.angle;
      const float c = t.cos[static_cast<std::size_t>(index)];
      const float s = t.sin[static_cast<std::size_t>(index)];
      const Vec3 bottom = {c * r.radius, low ? -2.0f : 0.0f, s * r.radius};
      const auto tilt = static_cast<std::size_t>(r.tilt);
      const float wall = r.heights[static_cast<std::size_t>(cell)];
      const float out_by = t.cos[tilt] * wall;
      float up = t.sin[tilt] * wall;
      if (low) up = up + 2.0f;
      const Vec3 top = {(out_by * c) + bottom[0], -up, (out_by * s) + bottom[2]};
      if (cell > 0)
        DrawPresetQuad(p, scene, cell - 1, r.alpha, {last_bottom, bottom, top, last_top}, out);
      cell = std::min(cell + 1, 20);
      last_bottom = bottom;
      last_top = top;
      if (step == 0) break;
    }
  }
}

// The view transform as DrawQuad writes it out for a quad's first corner: x's sum from z,
// y's and z's from x, then z, then y.
Vec3 TransformPointFromXZ(const Matrix& m, const Vec3& p) {
  return {(((p[2] * m[6]) + (p[1] * m[3])) + (p[0] * m[0])) + m[9],
          (((p[0] * m[1]) + (p[2] * m[7])) + (p[1] * m[4])) + m[10],
          (((p[0] * m[2]) + (p[2] * m[8])) + (p[1] * m[5])) + m[11]};
}

// What a DrawQuad call is given besides its corners.
struct QuadLook {
  std::int16_t alpha;
  bool add;           // blended SRCALPHA onto ONE, not INVSRCALPHA
  std::int8_t image;  // the index into textures
  bool absolute;      // the corners in the world, not relative to the primitive
  std::int32_t red, green, blue;
};

// CEffectPrim::DrawQuad (0056a960): four corners (relative to the primitive unless
// absolute) as two triangles of the texture, the first (A, B, C) and the second (D, C, A),
// u and v 0 to 1 from A's (0, 1); coloured alpha, red, green, blue; SRCALPHA onto ONE or
// INVSRCALPHA, with AddPrimitive's flags +0x128. The second C is projected inline, x scaled
// before the 1/z.
void DrawQuad(const EffectPrim& p, const PrimScene& scene, std::array<Vec3, 4> q,
              const QuadLook& look, PrimDrawing& out) {
  const auto u = [](std::int32_t v) { return static_cast<std::uint32_t>(v); };
  const std::uint32_t color =
      ((u(look.alpha) * 256 + (u(look.red) & 0xff)) * 256 + (u(look.green) & 0xff)) * 256 +
      (u(look.blue) & 0xff);
  if (!look.absolute) {
    for (Vec3& v : q)
      for (std::size_t i = 0; i < 3; ++i) v[i] = v[i] + p.position[i];
  }
  const auto vertex = [&](const Vec3& v, bool first, bool scale_first, float tu, float tv) {
    const Vec3 t = first ? TransformPointFromXZ(scene.view, v) : TransformPointFromZ(scene.view, v);
    ScreenVertex sv = ProjectPoint(ClampNear(t), scene.projection, scale_first);
    sv.diffuse = color;
    sv.u = tu;
    sv.v = tv;
    return sv;
  };
  PrimTriangle t;
  if (look.image >= 0) {
    const auto image = static_cast<std::size_t>(static_cast<std::uint8_t>(look.image));
    if (image < p.textures.size()) t.texture = p.textures[image];
  }
  t.flags = p.quad_flags;
  t.src_blend = 5;
  t.dest_blend = look.add ? 6 : 2;
  const ScreenVertex a = vertex(q[0], true, false, 0.0f, 1.0f);
  t.vertices = {a, vertex(q[1], false, false, 1.0f, 1.0f), vertex(q[2], false, false, 1.0f, 0.0f)};
  out.triangles.push_back(t);
  t.vertices = {vertex(q[3], false, false, 0.0f, 0.0f), vertex(q[2], false, true, 1.0f, 0.0f), a};
  out.triangles.push_back(t);
}

// CEffectPrim::RotateQuadPoints (0057caa0): each corner turned about x by the second
// angle (y and z), then about y by the first (x and z).
void RotateQuadPoints(std::array<Vec3, 4>& q, std::int16_t yaw, std::int16_t pitch) {
  const float pc = TableAt(kCosines, pitch);
  const float ps = TableAt(kSines, pitch);
  for (Vec3& v : q) {
    const float z = v[2];
    v[2] = (z * pc) - (v[1] * ps);
    v[1] = (v[1] * pc) + (z * ps);
  }
  const float yc = TableAt(kCosines, yaw);
  const float ys = TableAt(kSines, yaw);
  for (Vec3& v : q) {
    const float x = v[0];
    v[0] = (x * yc) - (v[2] * ys);
    v[2] = (v[2] * yc) + (x * ys);
  }
}

// CEffectPrim::RenderFacingSquares (0057bab0), ids 0x1f, 0x28, 0x2b, 0x2d, 0x2f, 0x33, 0x37
// and 0x38: each active record with some alpha as a square of its image about its centre,
// corners at its turn and every 90 degrees on (each step reduced once past 360), at its
// size swollen by a twentieth of the phase's sine, turned to face the camera. Mode 2
// colours it (200, 0, 60) onto ONE; otherwise the first record's colour picks (and colour
// 9 its shade); colours 2 and up draw the corners in the order B, A, D, C; others draw
// nothing.
void DrawFacingSquares(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  const PrimSquare& lead = p.squares[0];
  for (const PrimSquare& s : p.squares) {
    if (!s.active || s.alpha < 1) continue;
    const std::int32_t swell = Ftol(s.phase);
    const float r = ((TableAt(kSines, swell) * s.size) * 0.05f) + s.size;
    std::array<Vec3, 4> q{};
    std::int32_t a = s.turn;
    for (std::size_t k = 0; k < 4; ++k) {
      q[k] = {TableAt(kCosines, a) * r, 0.0f, TableAt(kSines, a) * r};
      a += 90;
      if (a >= 360) a -= 360;
    }
    RotateQuadPoints(q, lead.view_angle, p.squares[1].view_angle);
    for (Vec3& v : q)
      for (std::size_t i = 0; i < 3; ++i) v[i] = v[i] + s.center[i];
    const auto image = static_cast<std::int8_t>(s.image);
    const std::array<Vec3, 4> swapped = {q[1], q[0], q[3], q[2]};
    const auto draw = [&](bool swap, bool add, std::int32_t red, std::int32_t green,
                          std::int32_t blue) {
      DrawQuad(p, scene, swap ? swapped : q, {s.alpha, add, image, true, red, green, blue}, out);
    };
    if (std::bit_cast<std::uint32_t>(s.mode) == 0x40000000) {
      draw(false, true, 200, 0, 60);
      continue;
    }
    switch (lead.color) {
      case 0: draw(false, true, 255, 255, 255); break;
      case 1: draw(false, false, 150, 150, 250); break;
      case 2: draw(true, false, 130, 130, 250); break;
      case 3: draw(true, false, 200, 200, 100); break;
      case 4: draw(true, true, 255, 255, 255); break;
      case 7: draw(true, false, 255, 255, 255); break;
      case 8: draw(true, true, 135, 255, 135); break;
      case 9:
        switch (lead.shade) {
          case 1: draw(true, true, 255, 135, 135); break;
          case 2: draw(true, true, 255, 205, 135); break;
          case 3: draw(true, true, 255, 255, 135); break;
          case 4: draw(true, true, 255, 255, 255); break;
          case 5: draw(true, true, 135, 135, 255); break;
          default: break;
        }
        break;
      case 10: draw(true, false, 250, 250, 150); break;
      case 11: draw(true, false, 220, 250, 220); break;
      default: break;
    }
  }
}

// CEffectPrim::RenderHorizontalSquares (0056e6b0), ids 0x1a, 0x25 and 0x40: each active
// record as a flat square of the first texture at the height of its first wall, corners
// 90 degrees apart from its angle (from 23 degrees a record when the scale is not 0),
// 0.8 of the radius (a double) out plus a tenth of the radius times 1 plus the sine of the
// tilt; white, onto ONE when the scale is 0, relative to the primitive. The original keeps
// the corners at +0x88 of the record.
void DrawHorizontalSquares(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  const bool unscaled = p.scale == 0.0f;
  for (std::size_t k = 0; k < p.rings.size(); ++k) {
    const PrimRing& r = p.rings[k];
    if (!r.active) continue;
    std::int32_t a = unscaled ? r.angle : static_cast<std::int32_t>(k) * 23;
    const float swell = ((TableAt(kSines, r.tilt) + 1.0f) * r.radius) * 0.1f;
    const float reach = core::RoundedProduct(r.radius, 0.8) + swell;
    std::array<Vec3, 4> q{};
    for (Vec3& v : q) {
      v = {reach * TableAt(kCosines, a), r.heights[0], TableAt(kSines, a) * reach};
      a += 90;
      if (a >= 360) a -= 360;
    }
    DrawQuad(p, scene, q, {r.alpha, unscaled, 0, false, 255, 255, 255}, out);
  }
}

// The four corners of a square of the half-width, 90 degrees apart from 0 (or from 45),
// turned to face the camera by the first two records' angles.
std::array<Vec3, 4> FacingCorners(const EffectPrim& p, float half, std::int32_t from) {
  std::array<Vec3, 4> q{};
  for (std::size_t k = 0; k < 4; ++k) {
    const auto a = static_cast<std::int32_t>(from + 90 * static_cast<std::int32_t>(k));
    q[k] = {TableAt(kCosines, a) * half, 0.0f, TableAt(kSines, a) * half};
  }
  RotateQuadPoints(q, p.motes[0].view_angle, p.motes[1].view_angle);
  return q;
}

std::array<Vec3, 4> Moved(std::array<Vec3, 4> q, const Vec3& by) {
  for (Vec3& v : q)
    for (std::size_t i = 0; i < 3; ++i) v[i] = v[i] + by[i];
  return q;
}

// CEffectPrim::RenderRisingSquares (0056e920), id 0x1b: each active record's motes, in
// their order, as white squares of its size facing the camera, drawn twice, while above
// the ground: alpha 250, fading from 20 up to gone at 30 by the first corner's height.
void DrawRisingMotes(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimMotes& r : p.motes) {
    if (!r.active) continue;
    const std::array<Vec3, 4> corners = FacingCorners(p, r.size, 0);
    for (const std::size_t n : kMoteOrder) {
      const std::array<Vec3, 4> q = Moved(corners, r.points[n]);
      const float y = q[0][1];
      std::int16_t alpha = 250;
      if (y < -20.0f)
        alpha = static_cast<std::int16_t>(std::max(Ftol(((y + 20.0f) * 25.0f) + 250.0f), 0));
      if (!(y <= 0.0f)) continue;
      for (int twice = 0; twice < 2; ++twice)
        DrawQuad(p, scene, q, {alpha, false, 0, false, 255, 255, 255}, out);
    }
  }
}

// CEffectPrim::RenderPulsingSquares (0057ab90), id 0x1e: with the first record active and
// some alpha, its mote as a blue square of its size swollen by a twentieth of the swell
// phase's sine, then a yellow one of half the size plus that, both facing the camera.
void DrawPulsingMote(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  const PrimMotes& r = p.motes[0];
  if (!r.active || r.alpha < 1) return;
  const float swell = TableAt(kSines, Ftol(r.values[8])) * r.size;
  const float outer = (swell * 0.05f) + r.size;
  DrawQuad(p, scene, Moved(FacingCorners(p, outer, 0), r.points[0]),
           {r.alpha, false, 0, false, 0, 0, 255}, out);
  const float inner = (r.size * kHalf) + (swell * 0.05f);
  DrawQuad(p, scene, Moved(FacingCorners(p, inner, 0), r.points[0]),
           {r.alpha, false, 0, false, 255, 255, 0}, out);
}

// CEffectPrim::RenderOrbitingSquares (005708f0), id 0x21: each active record's motes at 4
// out along their angles (the original writes x and z back into the record), as squares
// of its size from 45 degrees facing the camera, of their image, coloured (100, 100, 255)
// at their alpha. Whether the corners go A, B, C, D or B, A, D, C depends on the image (0
// and 2 one way, the rest the other) and on whether the angle less the camera's (the first
// record's +0x10) is within 70 to -80 or beyond 270 either way.
void DrawPoppingMotes(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimMotes& r : p.motes) {
    if (!r.active) continue;
    std::array<Vec3, 4> points = r.points;
    for (std::size_t k = 0; k < 4; ++k) {
      const float angle = r.values[4 * k + 3];
      points[kMoteOrder[k]][0] = TableAt(kCosines, Ftol(angle)) * 4.0f;
      points[kMoteOrder[k]][2] = TableAt(kSines, Ftol(angle)) * 4.0f;
    }
    const std::array<Vec3, 4> corners = FacingCorners(p, r.size, 45);
    for (std::size_t k = 0; k < 4; ++k) {
      const float* v = &r.values[4 * k];
      const Vec3& at = points[kMoteOrder[k]];
      const std::int32_t d = Ftol(v[3] - static_cast<float>(p.motes[0].view_angle));
      const bool front = (d < 70 && d > -80) || d < -270 || d > 270;
      const bool plain = v[2] == 0.0f || std::bit_cast<std::uint32_t>(v[2]) == 0x40000000;
      std::array<Vec3, 4> q = Moved(corners, at);
      if (front != plain) q = {q[1], q[0], q[3], q[2]};
      DrawQuad(p, scene, q,
               {static_cast<std::int16_t>(Ftol(v[1])), false, static_cast<std::int8_t>(Ftol(v[2])),
                false, 100, 100, 255},
               out);
    }
  }
}

// An angle a quarter turn on, reduced once past 359.
std::int32_t QuarterOn(std::int32_t a) { return a + 90 > 359 ? a - 270 : a + 90; }

// CEffectPrim::RenderTiltedSquare (0056fb50), id 0x29: each active record with some alpha
// as a square of its half-diagonal, corners a quarter turn apart from its turn, tilted by
// its tilt about z, at its distance out along its direction from its centre; (100, 150,
// 255), or (250, 250, 155) for colour 1.
void DrawBurstSquares(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimPiece& s : p.pieces) {
    if (!s.active || s.alpha < 1) continue;
    const float flat = TableAt(kCosines, s.pitch) * s.distance;
    const float up = TableAt(kSines, s.pitch) * s.distance;
    const float along_x = flat * TableAt(kCosines, s.yaw);
    const float along_z = TableAt(kSines, s.yaw) * flat;
    const std::int32_t tilt = Ftol(s.values[1]);
    const float tc = TableAt(kCosines, tilt);
    const float ts = TableAt(kSines, tilt);
    const Vec3& at = s.points[0];
    std::array<Vec3, 4> q{};
    std::int32_t a = s.turn;
    for (Vec3& v : q) {
      const float c = TableAt(kCosines, a) * s.values[0];
      const float n = TableAt(kSines, a) * s.values[0];
      v = {((tc * c) + along_x) + at[0], (n + up) + at[1], ((c * ts) + along_z) + at[2]};
      a = QuarterOn(a);
    }
    if (s.color == 0) {
      DrawQuad(p, scene, q, {s.alpha, false, 0, true, 100, 150, 255}, out);
    } else {
      DrawQuad(p, scene, q, {s.alpha, false, 0, true, 250, 250, 155}, out);
    }
  }
}

// CEffectPrim::RenderRotatedRectangle (0056fea0), id 0x2a: each active record with some
// alpha as a rectangle twice its half-width across, from its length in half-widths short
// of its distance out to as far past it, turned up by its pitch and round by its yaw,
// relative to the primitive: white for colour 1, otherwise (200, 200, 255) onto ONE.
void DrawRays(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimPiece& s : p.pieces) {
    if (!s.active || s.alpha < 1) continue;
    const float w = s.values[0];
    const float far = (static_cast<float>(s.length) * w) + s.distance;
    const float near = (static_cast<float>(-s.length) * w) + s.distance;
    const float back = w * -1.0f;
    const float pc = TableAt(kCosines, s.pitch);
    const float ps = TableAt(kSines, s.pitch);
    const float yc = TableAt(kCosines, s.yaw);
    const float ys = TableAt(kSines, s.yaw);
    const auto corner = [&](float along, float across) {
      const float flat = (pc * along) - (ps * across);
      return Vec3{flat * yc, (ps * along) + (pc * across), ys * flat};
    };
    const std::array<Vec3, 4> q = {corner(far, back), corner(far, w), corner(near, w),
                                   corner(near, back)};
    if (s.color == 1) {
      DrawQuad(p, scene, q, {s.alpha, false, 0, false, 255, 255, 255}, out);
    } else {
      DrawQuad(p, scene, q, {s.alpha, true, 0, false, 200, 200, 255}, out);
    }
  }
}

// Four corners a quarter turn apart from the angle, the half-diagonal out, turned to face
// the camera by the first two records' angles.
std::array<Vec3, 4> FacingPieceCorners(const EffectPrim& p, float half, std::int32_t from) {
  std::array<Vec3, 4> q{};
  std::int32_t a = from;
  for (Vec3& v : q) {
    v = {TableAt(kCosines, a) * half, 0.0f, TableAt(kSines, a) * half};
    a = QuarterOn(a);
  }
  RotateQuadPoints(q, p.pieces[0].view_angle, p.pieces[1].view_angle);
  return q;
}

// CEffectPrim::RenderColoredFacingSquares (0057c620), id 0x32: each active record with
// some alpha as two squares facing the camera from its turn, at its centre and at the first
// point of its trail, the second half a unit smaller and at half the alpha; coloured by its
// colour, blue, red, green, yellow or (4) magenta, or, sparkling, white at a quarter of the
// alpha and the sparkle's image.
void DrawFireworks(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  struct Rgb {
    std::int32_t r, g, b;
  };
  constexpr std::array<Rgb, 5> kColors = {
      {{70, 70, 255}, {255, 70, 70}, {70, 255, 70}, {255, 255, 90}, {255, 70, 255}}};
  for (const PrimPiece& s : p.pieces) {
    if (!s.active || s.alpha < 1) continue;
    const Rgb rgb = s.image != 0 ? Rgb{255, 255, 255} : kColors[std::min<std::size_t>(s.color, 4)];
    float less = 0.0f;
    for (std::int32_t i = 0; i < 2; ++i) {
      const std::array<Vec3, 4> q =
          Moved(FacingPieceCorners(p, s.width - less, s.turn), s.points[i == 0 ? 0 : 2]);
      std::int32_t alpha = s.alpha / (i + 1);
      if (s.image != 0) alpha /= 4;
      DrawQuad(p, scene, q,
               {static_cast<std::int16_t>(alpha), false, static_cast<std::int8_t>(s.image), true,
                rgb.r, rgb.g, rgb.b},
               out);
      less = less + (i == 0 ? kHalf : 0.25f);
    }
  }
}

// CEffectPrim::RenderFacingSquareLayers (0057c0f0), id 0x34: each active record with some
// alpha as two thin wedges from the primitive out to its distance along its angle, facing
// the camera: 3 degrees either way, white at half the alpha, then 5 either way, (130, 130,
// 255) at the alpha.
void DrawBeams(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimPiece& s : p.pieces) {
    if (!s.active || s.alpha < 1) continue;
    const auto wedge = [&](std::int32_t spread) {
      std::int32_t from = s.yaw - spread;
      if (from < 0) from = s.yaw + 360 - spread;
      std::int32_t to = s.yaw + spread;
      if (to > 359) to = s.yaw - 360 + spread;
      std::array<Vec3, 4> q = {
          Vec3{},
          {TableAt(kCosines, from) * s.distance, 0.0f, TableAt(kSines, from) * s.distance},
          {TableAt(kCosines, to) * s.distance, 0.0f, TableAt(kSines, to) * s.distance},
          Vec3{}};
      RotateQuadPoints(q, p.pieces[0].view_angle, p.pieces[1].view_angle);
      return q;
    };
    const auto half = static_cast<std::int16_t>(Ftol(static_cast<float>(s.alpha) * kHalf));
    DrawQuad(p, scene, wedge(3), {half, false, 0, false, 255, 255, 255}, out);
    DrawQuad(p, scene, wedge(5), {s.alpha, false, 0, false, 130, 130, 255}, out);
  }
}

// CEffectPrim::RenderFacingRectangles (0057c3b0), id 0x35: each active record with some
// alpha as a line twice its distance long and twice its half-width across, along its angle,
// moved its offset aside, facing the camera; white onto ONE.
void DrawSlashLines(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimPiece& s : p.pieces) {
    if (!s.active || s.alpha < 1) continue;
    const std::int32_t a = s.yaw;
    const std::int32_t ahead = a < 0 ? a + 360 : a;
    const std::int32_t left = a < 90 ? a + 270 : a - 90;
    const std::int32_t right = QuarterOn(a);
    const std::int32_t back = a < 180 ? a + 180 : a - 180;
    const float aside_x = TableAt(kCosines, left) * s.growth;
    const float aside_z = TableAt(kSines, left) * s.growth;
    const float near_x = (TableAt(kCosines, left) * s.width) + aside_x;
    const float near_z = (TableAt(kSines, left) * s.width) + aside_z;
    const float far_x = (TableAt(kCosines, right) * s.width) + aside_x;
    const float far_z = (TableAt(kSines, right) * s.width) + aside_z;
    const auto end = [&](std::int32_t e, float x, float z) {
      return Vec3{(TableAt(kCosines, e) * s.distance) + x, 0.0f,
                  (TableAt(kSines, e) * s.distance) + z};
    };
    std::array<Vec3, 4> q = {end(ahead, far_x, far_z), end(back, far_x, far_z),
                             end(back, near_x, near_z), end(ahead, near_x, near_z)};
    RotateQuadPoints(q, p.pieces[0].view_angle, p.pieces[1].view_angle);
    DrawQuad(p, scene, q, {s.alpha, true, 0, false, 255, 255, 255}, out);
  }
}

// CEffectPrim::RenderRotatedSquares (0056c4a0), id 0x41: each active record as ten squares
// facing the camera, of its size plus 0, 3, ... 27 out to 45, 135, 225 and 315 degrees,
// moved its offset along x and 30 up, each at its own alpha (if above 0) and the record's
// image, black onto ONE, relative to the primitive. The original keeps the corners at
// +0x88 of the record.
void DrawAsuraSquares(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimPiece& s : p.pieces) {
    if (!s.active) continue;
    for (std::size_t k = 0; k < s.values.size(); ++k) {
      const float r = static_cast<float>(3 * static_cast<std::int32_t>(k)) + s.distance;
      std::array<Vec3, 4> q{};
      for (std::size_t c = 0; c < 4; ++c) {
        const auto a = static_cast<std::int32_t>(45 + 90 * c);
        q[c] = {(TableAt(kCosines, a) * r) + s.growth, 0.0f, TableAt(kSines, a) * r};
      }
      RotateQuadPoints(q, p.pieces[0].view_angle, p.pieces[1].view_angle);
      for (Vec3& v : q) v[1] = v[1] - 30.0f;
      const std::int32_t alpha = Ftol(s.values[s.values.size() - 1 - k]);
      if (alpha <= 0) continue;
      DrawQuad(p, scene, {q[1], q[0], q[3], q[2]},
               {static_cast<std::int16_t>(alpha), true, static_cast<std::int8_t>(s.color), false, 0,
                0, 0},
               out);
    }
  }
}

// The four ways the walls of 0x20's rings lean, less each wall's angle (one reduction past
// 360): rings 0 to 3 mirror their angles about 225, 135, 45 and 315 degrees.
constexpr std::array<std::int32_t, 4> kWallLeans = {450, 270, 90, 630};

// CEffectPrim::RenderSlantedRingWalls (0056cc90), id 0x20: each active ring's span in steps
// of a ninth from its angle (a full ring's 21st step back at its start) at its radius,
// moved by its first two flag bytes (signed) along x and z. Each wall leans up by the tilt
// to its height and out the ring's way (kWallLeans) and joins the last step's. The
// original keeps the corners at +0x88 of the record.
void DrawSlantedWalls(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (std::size_t k = 0; k < p.rings.size(); ++k) {
    const PrimRing& r = p.rings[k];
    if (!r.active || r.span <= 0) continue;
    const std::int32_t step = r.span / 9;
    const auto dx = static_cast<float>(static_cast<std::int8_t>(r.flags[0]));
    const auto dz = static_cast<float>(static_cast<std::int8_t>(r.flags[1]));
    const auto moved = [dx, dz](const Vec3& v) { return Vec3{dx + v[0], v[1], dz + v[2]}; };
    Vec3 last_bottom{};
    Vec3 last_top{};
    std::int32_t cell = 0;
    for (std::int32_t a = 0; a < r.span; a += step) {
      std::int32_t index = (r.angle + a) % 360;
      if (r.span == 360 && cell == 20) index = r.angle;
      const Vec3 bottom = {TableAt(kCosines, index) * r.radius, 0.0f,
                           TableAt(kSines, index) * r.radius};
      const float wall = r.heights[static_cast<std::size_t>(cell)];
      const float out_by = TableAt(kCosines, r.tilt) * wall;
      const float up = TableAt(kSines, r.tilt) * wall;
      std::int32_t lean = kWallLeans[k] - index;
      if (lean >= 360) lean -= 360;
      const Vec3 top = {(TableAt(kCosines, lean) * out_by) + bottom[0], -up,
                        (TableAt(kSines, lean) * out_by) + bottom[2]};
      if (cell > 0) {
        DrawPresetQuad(p, scene, cell - 1, r.alpha,
                       {moved(last_bottom), moved(bottom), moved(top), moved(last_top)}, out);
      }
      cell = std::min(cell + 1, 20);
      last_bottom = bottom;
      last_top = top;
      if (step == 0) break;
    }
  }
}

// CEffectPrim::RenderOffsetRingWalls (0056c2d0), id 0x3a: as RenderRingWalls, each ring
// about its own centre, its walls' tops not lifted with it.
void DrawOffsetRingWalls(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimRing& r : p.rings) {
    if (!r.active || r.span <= 0) continue;
    const std::int32_t step = r.span / 21;
    Vec3 last_bottom{};
    Vec3 last_top{};
    std::int32_t cell = 0;
    for (std::int32_t a = 0; a < r.span; a += step) {
      std::int32_t index = (r.angle + a) % 360;
      if (r.span == 360 && cell == 20) index = r.angle;
      const float c = TableAt(kCosines, index);
      const float s = TableAt(kSines, index);
      const Vec3 bottom = {(c * r.radius) + r.center[0], r.center[1], (s * r.radius) + r.center[2]};
      const float wall = r.heights[static_cast<std::size_t>(cell)];
      const float out_by = TableAt(kCosines, r.tilt) * wall;
      const Vec3 top = {(out_by * c) + bottom[0], -(TableAt(kSines, r.tilt) * wall),
                        (out_by * s) + bottom[2]};
      if (cell > 0)
        DrawPresetQuad(p, scene, cell - 1, r.alpha, {last_bottom, bottom, top, last_top}, out);
      cell = std::min(cell + 1, 20);
      last_bottom = bottom;
      last_top = top;
      if (step == 0) break;
    }
  }
}

// CEffectPrim::RenderRaisedRingWalls (0056caa0), id 0x3b: as RenderRingWalls, each ring
// lifted its height (+4) above the primitive. The original keeps the corners at +0x88 of
// the record.
void DrawRaisedRingWalls(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimRing& r : p.rings) {
    if (!r.active || r.span <= 0) continue;
    const std::int32_t step = r.span / 21;
    Vec3 last_bottom{};
    Vec3 last_top{};
    std::int32_t cell = 0;
    for (std::int32_t a = 0; a < r.span; a += step) {
      std::int32_t index = (r.angle + a) % 360;
      if (r.span == 360 && cell == 20) index = r.angle;
      const float c = TableAt(kCosines, index);
      const float s = TableAt(kSines, index);
      const Vec3 bottom = {c * r.radius, -r.height, s * r.radius};
      const float wall = r.heights[static_cast<std::size_t>(cell)];
      const float out_by = TableAt(kCosines, r.tilt) * wall;
      const float up = TableAt(kSines, r.tilt) * wall;
      const Vec3 top = {(out_by * c) + bottom[0], -(up + r.height), (out_by * s) + bottom[2]};
      if (cell > 0)
        DrawPresetQuad(p, scene, cell - 1, r.alpha, {last_bottom, bottom, top, last_top}, out);
      cell = std::min(cell + 1, 20);
      last_bottom = bottom;
      last_top = top;
      if (step == 0) break;
    }
  }
}

// A box's four corners 1 up, the half-length along its angle and the half-width across:
// (+, +), (+, −), (−, −), (−, +). The original keeps them at +0x88 of the record.
std::array<Vec3, 4> BoxCorners(const PrimBand& b) {
  const float c = TableAt(kCosines, b.angle);
  const float s = TableAt(kSines, b.angle);
  const auto corner = [c, s](float x, float z) {
    return Vec3{(x * c) - (z * s), -1.0f, (x * s) + (z * c)};
  };
  const float x = b.values[0];
  const float z = b.values[1];
  return {corner(x, z), corner(x, -z), corner(-x, -z), corner(-x, z)};
}

// The four walls between a box's corners up to its height, each from a corner to the next
// and up, in the look given.
void DrawBoxSides(const EffectPrim& p, const PrimScene& scene, const PrimBand& b,
                  const std::array<Vec3, 4>& q, const QuadLook& look, PrimDrawing& out) {
  const auto top = [&b](const Vec3& v) { return Vec3{v[0], v[1] - b.values[2], v[2]}; };
  for (std::size_t i = 0; i < 4; ++i) {
    const Vec3& a = q[i];
    const Vec3& next = q[(i + 1) % 4];
    DrawQuad(p, scene, {a, next, top(next), top(a)}, look, out);
  }
}

// CEffectPrim::RenderBox (0056f2b0), id 0x23: each active box with some alpha as its four
// walls, white onto ONE, then its floor both ways at half the alpha, relative to the
// primitive.
void DrawBox(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimBand& b : p.bands) {
    if (!b.active || b.alpha <= 0) continue;
    const std::array<Vec3, 4> q = BoxCorners(b);
    DrawBoxSides(p, scene, b, q, {b.alpha, true, 0, false, 255, 255, 255}, out);
    const QuadLook floor = {static_cast<std::int16_t>(b.alpha / 2), false, 0, false, 255, 255, 255};
    DrawQuad(p, scene, {q[0], q[1], q[2], q[3]}, floor, out);
    DrawQuad(p, scene, {q[3], q[2], q[1], q[0]}, floor, out);
  }
}

// CEffectPrim::RenderBoxWalls (0056f770), id 0x3d: each active box with some alpha as its
// four walls, relative to the primitive: white for a fourth value of 0, (235, 255, 235)
// onto ONE for 1, and for any other the last box's look (the original's is unset at
// first).
void DrawBoxWalls(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  QuadLook look = {0, false, 0, false, 255, 255, 255};
  for (const PrimBand& b : p.bands) {
    if (!b.active || b.alpha <= 0) continue;
    const float color = b.values[3];
    if (std::isnan(color) || color == 0.0f) {
      look = {0, false, 0, false, 255, 255, 255};
    } else if (std::bit_cast<std::uint32_t>(color) == 0x3f800000) {
      look = {0, true, 0, false, 235, 255, 235};
    }
    look.alpha = b.alpha;
    DrawBoxSides(p, scene, b, BoxCorners(b), look, out);
  }
}

// CEffectPrim::RenderPentagonBand (00570750), id 0x3f: each active record with some alpha
// as a five-sided tube of its radius from its second point to its fourth, the sides from
// its angle 72 degrees apart; (230, 255, 230), in the world.
void DrawPentagonBand(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  for (const PrimBand& b : p.bands) {
    if (!b.active || b.alpha <= 0) continue;
    const Vec3& low = b.points[1];
    const Vec3& high = b.points[3];
    Vec3 last_near{};
    Vec3 last_far{};
    for (std::int32_t i = 0; i <= 5; ++i) {
      auto a = static_cast<std::int16_t>(i * 72 + b.angle);
      if (a >= 360) a = static_cast<std::int16_t>(a - 360);
      const float x = b.size * TableAt(kCosines, a);
      const float z = b.size * TableAt(kSines, a);
      const Vec3 near = {low[0] + x, low[1], low[2] + z};
      const Vec3 far = {high[0] + x, high[1], high[2] + z};
      if (i > 0)
        DrawQuad(p, scene, {last_near, near, far, last_far},
                 {b.alpha, false, 0, true, 230, 255, 230}, out);
      last_near = near;
      last_far = far;
    }
  }
}

// CEffectPrim::RenderArcBands (005704e0), id 0x42: each active band with some alpha as two
// five-sided tubes from its first point to its second, lifted by its first value, their
// sides 72 degrees apart about the line across its angle: one of its radius, (255, 255,
// 55) onto ONE, then one a quarter wider, (255, 255, 200); in the world.
void DrawArcBands(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  const core::AngleTables& t = core::GetAngleTables();
  for (const PrimBand& b : p.bands) {
    if (!b.active || b.alpha <= 0) continue;
    const std::int32_t across = static_cast<std::int16_t>(b.angle + 90) % 360;
    const float ac = TableAt(kCosines, across);
    const float as = TableAt(kSines, across);
    const Vec3& from = b.points[0];
    const Vec3& to = b.points[1];
    for (std::int32_t layer = 0; layer < 2; ++layer) {
      const QuadLook look = layer == 0 ? QuadLook{b.alpha, true, 0, true, 255, 255, 55}
                                       : QuadLook{b.alpha, false, 0, true, 255, 255, 200};
      Vec3 last_near{};
      Vec3 last_far{};
      for (std::size_t j = 0; j <= 5; ++j) {
        const float radius = layer == 0 ? b.size : b.size + 0.25f;
        const float rs = radius * t.sin[72 * j];
        const float rc = radius * t.cos[72 * j];
        const float dx = rc * ac;
        const float dz = rc * as;
        const float y = rs - b.values[0];
        const Vec3 near = {from[0] + dx, y + from[1], from[2] + dz};
        const Vec3 far = {to[0] + dx, y + to[1], to[2] + dz};
        if (j > 0) DrawQuad(p, scene, {last_near, near, far, last_far}, look, out);
        last_near = near;
        last_far = far;
      }
    }
  }
}

// Where a segment of 0x43's trail lies from its start: out along the heading two lengths a
// bounce made, each 0.7 of the last, and the share of the next its degrees through it
// make; up 1.5 lengths times the sine of those degrees, shrunk the same.
struct BouncePoint {
  float out;
  float up;
};

BouncePoint BounceAt(float length, std::int8_t bounces, float through) {
  float out = 0.0f;
  float shrink = 1.0f;
  for (std::int32_t n = 0; n < bounces; ++n) {
    const float once = shrink * length;
    out = out + (once + once);
    shrink = shrink * kBounceShrink;
  }
  if (through > 0.0f) {
    const float part = ((through * kPerHalfTurn) * shrink) * length;
    out = out + (part + part);
  }
  const auto degrees = static_cast<std::int16_t>(Ftol(through));
  const float up = (TableAt(kSines, degrees) * length) * -1.5f;
  float again = 1.0f;
  for (std::int32_t n = 0; n < bounces; ++n) again = again * kBounceShrink;
  return {out, up * again};
}

// CEffectPrim::RenderBandSegments (0056c6c0), id 0x43: each active trail with some alpha as
// 20 segments, each from one record's bounce point to the next's, a three-sided tube of
// the radius about the line across their direction (VectorToDegrees, a quarter turn on),
// at the alpha less 5 a segment while some is left; (255, 200, 10 + 7 a segment), in the
// world.
void DrawBandSegments(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out) {
  const core::AngleTables& t = core::GetAngleTables();
  for (const PrimBand& b : p.bands) {
    if (!b.active || b.alpha <= 0) continue;
    const float hc = TableAt(kCosines, b.heading);
    const float hs = TableAt(kSines, b.heading);
    const Vec3& start = b.points[0];
    for (std::size_t i = 0; i < 20; ++i) {
      const BouncePoint from = BounceAt(b.length, b.bounces[i], b.values[i]);
      const BouncePoint to = BounceAt(b.length, b.bounces[i + 1], b.values[i + 1]);
      const Vec3 a = {from.out * hc, from.up, from.out * hs};
      const Vec3 z = {to.out * hc, to.up, to.out * hs};
      const std::int16_t back = VectorToDegrees(a[0] - z[0], a[2] - z[2]);
      const std::int32_t across = static_cast<std::int16_t>(back + 90) % 360;
      const Vec3 near_center = {a[0] + start[0], a[1] + start[1], a[2] + start[2]};
      const Vec3 far_center = {z[0] + start[0], z[1] + start[1], z[2] + start[2]};
      const auto segment = static_cast<std::int32_t>(i);
      const std::int32_t alpha = b.alpha - 5 * segment;
      const QuadLook look = {static_cast<std::int16_t>(alpha), false, 0, true, 255, 200,
                             (10 + 7 * segment) & 0xff};
      Vec3 last_near{};
      Vec3 last_far{};
      for (std::size_t j = 0; j <= 3; ++j) {
        const float ry = b.size * t.sin[120 * j];
        const float rc = b.size * t.cos[120 * j];
        const float dx = rc * TableAt(kCosines, across);
        const float dz = rc * TableAt(kSines, across);
        const Vec3 near = {near_center[0] + dx, ry + near_center[1], dz + near_center[2]};
        const Vec3 far = {far_center[0] + dx, ry + far_center[1], dz + far_center[2]};
        if (j > 0 && alpha > 0) DrawQuad(p, scene, {last_near, near, far, last_far}, look, out);
        last_near = near;
        last_far = far;
      }
    }
  }
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
    case 7:
      if (!UpdateCirclingParticle(p)) return false;
      break;
    case 8: UpdateCirclingShot(p); break;
    case 0x15:
      if (!UpdateParticle(p, rand, scene, true)) return false;
      break;
    case 0x26: UpdateFallingLeaf(p, rand, scene); break;
    case 0x2c: UpdateStandingSprite(p, scene, true); break;
    case 0x31: UpdateStandingSprite(p, scene, false); break;
    case 0xd: UpdateCylinder(p); break;
    case 0x17: UpdateRisingWalls(p, false); break;
    case 0x18: UpdateTurningRings(p); break;
    case 0x19: UpdateRisingWalls(p, true); break;
    case 0x1c: UpdateSpreadingRings(p); break;
    case 0x1d: UpdateRingWaves(p); break;
    case 0x22: UpdateGrowingRing(p); break;
    case 0x27: UpdateRisingRings(p); break;
    case 0x30: UpdateWobblingRing(p); break;
    case 0x36: UpdateLiftingRings(p); break;
    case 0x39: UpdateWavingWalls(p); break;
    case 0x3c: UpdateSpreadingPortal(p); break;
    case 0x3e: UpdateClosingRings(p); break;
    case 0x1a: UpdateSwellingSquares(p, true); break;
    case 0x25: UpdateSwellingSquares(p, false); break;
    case 0x40: UpdateFadingSquare(p); break;
    case 0x1b: UpdateRisingMotes(p, rand, scene); break;
    case 0x1e: UpdatePulsingMote(p, rand, scene); break;
    case 0x21: UpdatePoppingMotes(p, rand, scene); break;
    case 0x29: UpdateBurstSquares(p); break;
    case 0x2a: UpdateRays(p); break;
    case 0x32: UpdateFireworks(p, rand, scene); break;
    case 0x34: UpdateBeams(p, scene, true); break;
    case 0x35: UpdateBeams(p, scene, false); break;
    case 0x41: UpdateAsuraSquares(p, scene); break;
    case 0x20: UpdateClosingWalls(p); break;
    case 0x3a: UpdateOrbitingRings(p); break;
    case 0x3b: UpdateRaisedRings(p); break;
    case 0x23: UpdateBox(p); break;
    case 0x3d: UpdateBoxWalls(p); break;
    case 0x3f: UpdatePentagonBand(p); break;
    case 0x42: UpdateArcBands(p); break;
    case 0x43: UpdateBouncingBands(p); break;
    case 0x1f:
    case 0x28:
    case 0x2b:
    case 0x2d:
    case 0x2f:
    case 0x33:
    case 0x37:
    case 0x38:
      FaceCamera(p, scene);
      switch (p.id) {
        case 0x1f: UpdateClouds(p, rand); break;
        case 0x28: UpdateBoomerang(p, rand); break;
        case 0x2b: UpdateThrownSparks(p, rand); break;
        case 0x2d: UpdateBobbingSquare(p); break;
        case 0x2f: UpdateSinkingSquares(p); break;
        case 0x33: UpdateArcingSquare(p); break;
        case 0x37: UpdatePulsingSquare(p); break;
        default: UpdateRisingSparks(p, rand); break;
      }
      break;
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
    case 6:
    case 7:
    case 8:
    case 0x15:
    case 0x26:
    case 0x2c:
    case 0x31: DrawHistory(p, scene, out); break;
    case 0xd: DrawCylinder(p, scene, out); break;
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x1c:
    case 0x1d:
    case 0x22:
    case 0x27:
    case 0x30:
    case 0x36:
    case 0x39:
    case 0x3c:
    case 0x3e: DrawRingWalls(p, scene, out); break;
    case 0x1f:
    case 0x28:
    case 0x2b:
    case 0x2d:
    case 0x2f:
    case 0x33:
    case 0x37:
    case 0x38: DrawFacingSquares(p, scene, out); break;
    case 0x1a:
    case 0x25:
    case 0x40: DrawHorizontalSquares(p, scene, out); break;
    case 0x1b: DrawRisingMotes(p, scene, out); break;
    case 0x1e: DrawPulsingMote(p, scene, out); break;
    case 0x21: DrawPoppingMotes(p, scene, out); break;
    case 0x29: DrawBurstSquares(p, scene, out); break;
    case 0x2a: DrawRays(p, scene, out); break;
    case 0x32: DrawFireworks(p, scene, out); break;
    case 0x34: DrawBeams(p, scene, out); break;
    case 0x35: DrawSlashLines(p, scene, out); break;
    case 0x41: DrawAsuraSquares(p, scene, out); break;
    case 0x20: DrawSlantedWalls(p, scene, out); break;
    case 0x3a: DrawOffsetRingWalls(p, scene, out); break;
    case 0x3b: DrawRaisedRingWalls(p, scene, out); break;
    case 0x23: DrawBox(p, scene, out); break;
    case 0x3d: DrawBoxWalls(p, scene, out); break;
    case 0x3f: DrawPentagonBand(p, scene, out); break;
    case 0x42: DrawArcBands(p, scene, out); break;
    case 0x43: DrawBandSegments(p, scene, out); break;
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

namespace {

// What the ring effects share: CreatePrimitive of the id, flags 5 (translucent, by the
// blends), as long as the effect, the texture given.
EffectPrim MakeRings(const RagEffect& effect, std::int32_t id, const char* texture) {
  EffectPrim p = MakePrimitive(&effect, id, {0.0f, 0.0f, 0.0f});
  p.quad_flags = 5;
  p.life = effect.duration;
  p.textures = {texture};
  return p;
}

}  // namespace

void AddRingWaves(RagEffect& effect, const EffectActor& actor, const char* texture,
                  std::int16_t kind, bool slow, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x1d, texture);
  p.scale = 0.0f;
  if (slow) p.acceleration = std::bit_cast<float>(kSlowWaves);
  for (std::size_t k = 0; k < p.rings.size(); ++k) {
    PrimRing& r = p.rings[k];
    r.active = true;
    r.span = 360;
    r.height = 25.0f;
    r.angle = static_cast<std::int16_t>(k * 90);
    r.radius = static_cast<float>(k) + 1.0f;
    r.tilt = 80;
    r.kind = kind;
  }
  added.push_back(std::move(p));
}

void AddRisingRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                    std::int8_t variant, std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  effect.position = actor.position;
  EffectPrim p = MakeRings(effect, 0x27, texture);
  if (variant < 2) {
    p.scale = 4.0f;
  } else if (variant == 2) {
    p.scale = 7.0f;
  } else if (variant == 3) {
    p.scale = 6.0f;
  }
  for (std::size_t k = 0; k < p.rings.size(); ++k) {
    PrimRing& r = p.rings[k];
    r.active = true;
    r.age = static_cast<std::int32_t>(k) * 30;
    r.span = 360;
    r.height = 120.0f;
    r.angle = static_cast<std::int16_t>(k * 90);
    const float half = static_cast<float>(k) * kHalf;
    r.alpha = variant == 1 ? 70 : 50;
    r.radius = half + (variant == 1 ? 11.0f : 2.0f);
    r.tilt = 89;
  }
  added.push_back(std::move(p));
}

void AddFadingRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                    std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x27, texture);
  p.scale = 5.0f;
  for (std::size_t k = 0; k < p.rings.size(); ++k) {
    PrimRing& r = p.rings[k];
    r.active = true;
    r.age = 200;
    r.span = 360;
    r.height = 120.0f;
    r.angle = static_cast<std::int16_t>(k * 90);
    r.alpha = 40;
    r.radius = (static_cast<float>(k) * kHalf) + 6.0f;
    r.tilt = static_cast<std::int16_t>(83 - static_cast<std::int32_t>(k));
    r.kind = 1;
  }
  added.push_back(std::move(p));
}

namespace {

struct RingSetup {
  std::int16_t span;
  float height;
  std::int32_t age;
  std::int16_t angle;
  std::int16_t alpha;
  std::int16_t tilt;
  float radius;
  std::int16_t kind;
};

void SetRing(PrimRing& r, const RingSetup& s) {
  r.active = true;
  r.span = s.span;
  r.height = s.height;
  r.age = s.age;
  r.angle = s.angle;
  r.alpha = s.alpha;
  r.tilt = s.tilt;
  r.radius = s.radius;
  r.kind = s.kind;
}

float Bits(std::uint32_t b) { return std::bit_cast<float>(b); }

}  // namespace

void AddRingTrio(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame == 99999998) {
    effect.frame = 0;
    effect.duration = 99999999;
  }
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x17, "effect\\ring_blue.tga");
  p.scale = 4.0f;
  SetRing(p.rings[0], {315, 15.0f, 0, 0, 120, 55, Bits(0x4079999a), 0});
  SetRing(p.rings[1], {315, 13.0f, 0, 90, 120, 50, Bits(0x40833333), 0});
  SetRing(p.rings[2], {315, 11.0f, 0, 180, 120, 45, Bits(0x4089999a), 0});
  added.push_back(std::move(p));
}

void AddTurningRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x18, texture);
  p.scale = 0.0f;
  SetRing(p.rings[0], {360, 40.0f, 0, 180, 0, 90, 8.0f, 0});
  SetRing(p.rings[1], {360, 39.0f, 0, 270, 0, 90, Bits(0x40fccccd), 0});
  SetRing(p.rings[2], {360, 38.0f, 0, 0, 0, 90, Bits(0x40f9999a), 0});
  SetRing(p.rings[3], {360, 37.0f, 0, 90, 0, 90, Bits(0x40f66666), 0});
  added.push_back(std::move(p));
}

void AddRandomRing(RagEffect& effect, const EffectActor& actor, const char* texture,
                   std::int8_t preset, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x22, texture);
  p.scale = static_cast<float>(preset);
  const auto angle = static_cast<std::int16_t>(rand.Next() % 360);
  const auto alpha = static_cast<std::int16_t>(rand.Next() % 100);
  SetRing(p.rings[0], {360, 15.0f, 0, angle, alpha, 80, Bits(0x40266666), 0});
  added.push_back(std::move(p));
}

void AddTwirlRing(RagEffect& effect, const EffectActor& actor, const char* texture,
                  std::int8_t preset, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x30, texture);
  p.scale = static_cast<float>(preset);
  const float height = static_cast<float>(rand.Next() % 10) + 25.0f;
  const auto angle = static_cast<std::int16_t>(rand.Next() % 360);
  const auto alpha = static_cast<std::int16_t>(50 - rand.Next() % 10);
  const auto kind = static_cast<std::int16_t>(rand.Next() % 360);
  SetRing(p.rings[0], {315, height, 0, angle, alpha, 90, 0.0f, kind});
  added.push_back(std::move(p));
}

void AddTeleportationRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                           std::int16_t lift, std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  effect.position = actor.position;
  EffectPrim p = MakeRings(effect, 0x36, texture);
  p.scale = 4.0f;
  const auto up = static_cast<float>(lift * 60);
  const auto in = static_cast<float>(lift * 2);
  SetRing(p.rings[0], {360, up + 100.0f, 0, 180, 100, 90, 1.5f - in, 0});
  SetRing(p.rings[1], {360, up + 70.0f, 0, 270, 80, 89, 3.0f - in, 0});
  SetRing(p.rings[2], {360, up + 40.0f, 0, 0, 60, 88, 4.0f - in, 0});
  SetRing(p.rings[3], {360, up + 15.0f, 0, 90, 40, 87, 5.0f - in, 0});
  for (PrimRing& r : p.rings) {
    r.flags[0] = 0;
    r.flags[1] = static_cast<std::uint8_t>(Ftol(r.height));
  }
  added.push_back(std::move(p));
}

void AddTeleportationWalls(RagEffect& effect, const EffectActor& actor, const char* texture,
                           std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x39, texture);
  p.scale = 8.0f;
  SetRing(p.rings[3], {360, 15.0f, 0, 0, 0, 81, 3.5f, 60});
  SetRing(p.rings[2], {360, 34.0f, 0, 90, 0, 84, 3.0f, 60});
  SetRing(p.rings[1], {360, 48.0f, 0, 180, 0, 87, 2.5f, 60});
  SetRing(p.rings[0], {360, 65.0f, 0, 270, 0, 90, 2.0f, 60});
  added.push_back(std::move(p));
}

void AddReadyPortal(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added) {
  // effect\EF_ReadyPortal.wav plays when the place is taken.
  if (effect.frame % 14 == 0) effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x3c, "effect\\ring_blue.tga");
  p.scale = 5.0f;
  const float low = Bits(kLowWalls);
  SetRing(p.rings[0], {360, low, 0, 0, 0, 2, 0.0f, 100});
  SetRing(p.rings[1], {360, low, -10, 25, 0, 3, 0.0f, 100});
  SetRing(p.rings[2], {360, low, -20, 50, 0, 4, 0.0f, 100});
  for (std::size_t k = 0; k < 3; ++k) p.rings[k].flags.fill(1);
  p.rings[3].active = false;
  p.rings[3].age = 0;
  p.rings[3].kind = 1;
  added.push_back(std::move(p));
}

void AddGlassWall(RagEffect& effect, const EffectActor& actor, const char* texture,
                  core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x39, texture);
  p.scale = 10.0f;
  const auto first = static_cast<std::int16_t>(rand.Next() % 360);
  SetRing(p.rings[0], {360, 34.0f, 0, first, 0, 90, Bits(0x40666666), 3080});
  const auto second = static_cast<std::int16_t>(rand.Next() % 360);
  SetRing(p.rings[1], {360, 37.0f, 0, second, 0, 90, Bits(0x40533333), 3080});
  const auto third = static_cast<std::int16_t>(rand.Next() % 360);
  SetRing(p.rings[2], {360, 40.0f, 0, third, 0, 90, 3.0f, 3080});
  added.push_back(std::move(p));
}

namespace {

// The item pictures the arcing squares throw (유저인터페이스\item\빈병.bmp, 빈포션병.bmp
// and 돌.bmp, 006181fc, 006181d8 and 006181bc).
// clang-format off
constexpr const char* kEmptyBottle = "\300\257\300\372\300\316\305\315\306\344\300\314\275\272\\item\\\272\363\272\264.bmp";
constexpr const char* kEmptyPotionBottle = "\300\257\300\372\300\316\305\315\306\344\300\314\275\272\\item\\\272\363\306\367\274\307\272\264.bmp";
constexpr const char* kStone = "\300\257\300\372\300\316\305\315\306\344\300\314\275\272\\item\\\265\271.bmp";
// clang-format on

// What the square effects share: CreatePrimitive of the id, flags 5, as long as the
// effect, the textures given.
EffectPrim MakeSquares(const RagEffect& effect, std::int32_t id,
                       std::vector<std::string> textures) {
  EffectPrim p = MakePrimitive(&effect, id, {0.0f, 0.0f, 0.0f});
  p.quad_flags = 5;
  p.life = effect.duration;
  p.image_count = static_cast<std::int32_t>(textures.size());
  p.textures = std::move(textures);
  return p;
}

// (target − actor) along the ground, the distance 0x28 and 0x33 head out.
float RangeTo(const RagEffect& effect) {
  const float x = effect.target[0] - effect.position[0];
  const float z = effect.target[2] - effect.position[2];
  return std::sqrt((x * x) + (z * z));
}

// The four rising sparks of pok1.tga (and pok3.tga to flash) the healing effects add:
// from back short of the actor to spread − 1 past that, either way along the ground, lift
// above it, each of its size plus up to 0.9 and its climb plus up to a hundredth of
// climb_range.
struct SparkSetup {
  bool flash;
  float size;
  std::int32_t spread;
  float back;
  float lift;
  std::int32_t climb_range;
  float climb;
  std::uint8_t color;
};

EffectPrim MakeSparks(const RagEffect& effect, const SparkSetup& setup, core::CrtRand& rand) {
  std::vector<std::string> textures = {"effect\\pok1.tga"};
  if (setup.flash) textures.emplace_back("effect\\pok3.tga");
  EffectPrim p = MakeSquares(effect, 0x38, std::move(textures));
  p.scale = 0.0f;
  p.position = effect.position;
  for (PrimSquare& s : p.squares) {
    s.active = true;
    s.size = (static_cast<float>(rand.Next() % 10) * 0.1f) + setup.size;
    s.center[0] =
        (static_cast<float>(rand.Next() % setup.spread) + effect.position[0]) - setup.back;
    s.center[2] =
        (static_cast<float>(rand.Next() % setup.spread) + effect.position[2]) - setup.back;
    s.center[1] = effect.position[1] - setup.lift;
    s.climb = (static_cast<float>(rand.Next() % setup.climb_range) * 0.01f) + setup.climb;
    s.state = setup.flash ? 1 : 0;
    s.color = setup.color;
  }
  return p;
}

}  // namespace

void AddClouds(RagEffect& effect, const Vec3& player, std::int8_t variant, core::CrtRand& rand,
               std::vector<EffectPrim>& added) {
  effect.position = player;
  if (effect.frame == 99999998) {
    effect.frame = 0;
    effect.duration = 99999999;
  }
  if (effect.frame != 0) return;
  EffectPrim p = MakePrimitive(&effect, 0x1f, {0.0f, 0.0f, 0.0f});
  p.quad_flags = 5;
  p.life = effect.duration;
  p.image_count = 3;
  const std::int32_t cloud = rand.Next() % 3;
  p.textures = {cloud == 0   ? "effect\\cloud4.tga"
                : cloud == 1 ? "effect\\cloud1.tga"
                             : "effect\\cloud2.tga"};
  const Vec3& at = effect.position;
  for (PrimSquare& s : p.squares) {
    s.active = true;
    s.age = 0;
    s.mode = static_cast<float>(variant);
    s.turn = 45;
    if (variant == 0) {
      s.floor = -152.0f;
      if (s.floor < at[1]) s.age = 300;
    }
    s.alpha = 0;
    s.image = 0;
    s.heading = static_cast<std::int16_t>(rand.Next() % 200 + 300);
    if (variant == 1) {
      s.center[1] = static_cast<float>(rand.Next() % 10) + 40.0f;
      s.center[0] = static_cast<float>(rand.Next() % 200 + 25);
      if (rand.Next() % 2 == 0) s.center[0] = s.center[0] * -1.0f;
      s.center[2] = static_cast<float>(rand.Next() % 200 + 25);
      if (rand.Next() % 2 == 0) s.center[2] = s.center[2] * -1.0f;
    } else {
      s.center[1] = static_cast<float>(rand.Next() % 10) - (variant == 0 ? 125.0f : 80.0f);
      s.center[0] = static_cast<float>(rand.Next() % 300 - 150);
      s.center[2] = static_cast<float>(rand.Next() % 300 - 150);
    }
    s.center[0] = at[0] + s.center[0];
    s.center[2] = s.center[2] + at[2];
    s.walk_x = static_cast<float>(rand.Next() % 360);
    s.speed = static_cast<float>(rand.Next() % 360);
    s.climb = static_cast<float>(rand.Next() % 360);
    s.walk_z_goal = static_cast<float>(rand.Next() % 360);
    s.size = static_cast<float>(rand.Next() % 20) + 30.0f;
    s.phase = static_cast<float>(rand.Next() % 360);
    s.color = 0;
  }
  added.push_back(std::move(p));
}

void AddBoomerang(RagEffect& effect, const EffectActor& actor, const char* texture, bool ivy,
                  std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x28, {texture});
  p.scale = 0.0f;
  p.travel = effect.target;
  const Vec3& at = effect.position;
  PrimSquare& s = p.squares[0];
  s.active = true;
  s.age = 0;
  s.range = RangeTo(effect);
  s.size = ivy ? (static_cast<float>(effect.level) * 0.4f) + 2.0f : 7.0f;
  s.phase = 0.0f;
  s.alpha = 255;
  s.center = {at[0], at[1] - 8.0f, at[2]};
  s.heading = 0;
  s.speed = 2.0f;
  s.climb = (effect.target[1] - at[1]) * (s.speed / s.range);
  s.turn = 0;
  s.color = ivy ? 1 : 0;
  for (std::size_t k = 1; k < 4; ++k) {
    p.squares[k].active = true;
    p.squares[k].alpha = 0;
  }
  added.push_back(std::move(p));
}

void AddThrownSparks(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                     std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x2b, {"effect\\thunder_center.bmp"});
  p.scale = 0.0f;
  p.travel = effect.target;
  p.position = effect.position;
  for (PrimSquare& s : p.squares) {
    s.active = true;
    s.age = rand.Next() % 10 - 40;
    s.size = (static_cast<float>(rand.Next() % 16) * 0.05f) + 0.1f;
    s.phase = 0.0f;
    s.alpha = 0;
    s.center = {effect.target[0], effect.target[1] - 8.0f, effect.target[2]};
    s.heading = 0;
    s.speed = (static_cast<float>(rand.Next() % 50) * 0.01f) + 0.75f;
    s.climb = (static_cast<float>(rand.Next() % 200) * 0.01f) - 1.0f;
    s.turn = 0;
    s.color = 2;
  }
  added.push_back(std::move(p));
}

void AddBobbingSquare(RagEffect& effect, const EffectActor& actor, const char* texture,
                      std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  std::string name = texture;
  if (variant == 5) {
    const std::int32_t n = rand.Next() % 3;
    name = n == 0   ? "effect\\\267\271\265\345\301\252\275\272\305\346.bmp"
           : n == 1 ? "effect\\\272\355\267\347\301\252\275\272\305\346.bmp"
                    : "effect\\\277\273\267\316\277\354\301\252\275\272\305\346.bmp";
  } else if (variant == 2) {
    name = rand.Next() % 2 == 0 ? "effect\\melody_a.bmp" : "effect\\melody_b.bmp";
  } else if (variant == 8) {
    name = "effect\\spell_0" + std::to_string(rand.Next() % 8 + 1) + ".bmp";
  }
  EffectPrim p = MakeSquares(effect, 0x2d, {name});
  p.scale = 0.0f;
  PrimSquare& s = p.squares[0];
  s.active = true;
  s.age = 0;
  s.size = variant == 1 ? 5.0f : variant == 4 ? 15.0f : variant == 6 ? 8.0f : 3.0f;
  s.phase = 0.0f;
  s.alpha = 200;
  s.center[0] = effect.position[0];
  s.center[2] = effect.position[2];
  s.base = effect.position[1] - 6.0f;
  s.heading = static_cast<std::int16_t>(rand.Next() % 360);
  s.turn = 45;
  s.color = variant == 3 ? 3 : variant == 5 || variant == 6 ? 4 : variant == 7 ? 7 : 2;
  if (variant == 2) {
    for (std::size_t k = 1; k < 4; ++k) {
      p.squares[k].active = true;
      p.squares[k].alpha = 0;
    }
  }
  added.push_back(std::move(p));
}

void AddSinkingSquares(RagEffect& effect, const EffectActor& actor, const char* texture,
                       std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x2f, {texture});
  const std::size_t count = variant == 1 ? 1 : 2;
  for (std::size_t k = 0; k < count; ++k) {
    PrimSquare& s = p.squares[k];
    s.active = true;
    s.age = rand.Next() % 11;
    if (variant == 0) {
      s.size = static_cast<float>(rand.Next() % 6) + 5.0f;
    } else if (variant == 1) {
      s.size = (static_cast<float>(rand.Next() % 20) * 0.1f) + 3.0f;
    }
    s.phase = 0.0f;
    s.alpha = 0;
    s.center[0] = effect.position[0];
    s.center[2] = effect.position[2];
    if (variant == 0) {
      s.base = effect.position[1];
    } else if (variant == 1) {
      s.base = effect.position[1] - 5.0f;
    }
    s.turn = 45;
    s.color = 2;
    s.state = static_cast<std::uint8_t>(variant);
  }
  added.push_back(std::move(p));
}

void AddArcingSquare(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::int8_t variant, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x33, {texture});
  p.scale = 0.0f;
  p.travel = effect.target;
  const Vec3& at = effect.position;
  PrimSquare& s = p.squares[0];
  s.active = true;
  s.age = 0;
  s.range = RangeTo(effect);
  s.size = variant == 2 ? 2.0f : 3.0f;
  s.phase = 0.0f;
  s.alpha = 255;
  s.center = {at[0], 0.0f, at[2]};
  s.base = at[1] - 8.0f;
  s.heading = 0;
  s.speed = variant == 2 ? 2.0f : 1.0f;
  s.climb = (effect.target[1] - at[1]) * (s.speed / s.range);
  s.turn = 0;
  if (variant == 0) {
    s.color = 8;
  } else if (variant == 1) {
    s.color = 9;
    s.shade = static_cast<std::uint8_t>(effect.level);
  } else if (variant == 2) {
    s.color = 0;
  }
  for (std::int32_t n = 0; n < 100; ++n) {
    if (s.range < static_cast<float>(n) * s.speed) {
      s.state = static_cast<std::uint8_t>(n);
      break;
    }
  }
  for (std::size_t k = 1; k < 4; ++k) {
    p.squares[k].active = true;
    p.squares[k].alpha = 0;
  }
  added.push_back(std::move(p));
}

void AddPulsingSquare(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                      std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x37, {"effect\\pikapika2.bmp"});
  p.scale = 0.0f;
  PrimSquare& s = p.squares[0];
  s.active = true;
  s.age = 0;
  s.size = 10.0f;
  s.phase = 0.0f;
  s.alpha = 0;
  s.center = {effect.position[0], effect.position[1] - 12.0f, effect.position[2]};
  s.floor = std::bit_cast<float>(0x3ca3d70au);  // 0.02
  s.turn = static_cast<std::int16_t>(rand.Next() % 360);
  s.color = 1;
  s.phase = static_cast<float>(rand.Next() % 360);
  added.push_back(std::move(p));
}

void AddThunderSparks(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                      std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x38, {"effect\\thunder_center.bmp"});
  p.scale = 0.0f;
  p.position = effect.position;
  const Vec3& at = effect.position;
  for (PrimSquare& s : p.squares) {
    s.active = true;
    s.age = rand.Next() % 10 - 10;
    s.size = (static_cast<float>(rand.Next() % 16) * 0.03f) + 0.1f;
    s.phase = 0.0f;
    s.alpha = 0;
    s.center[0] = (static_cast<float>(rand.Next() % 9) + at[0]) - 4.0f;
    s.center[2] = (static_cast<float>(rand.Next() % 9) + at[2]) - 4.0f;
    s.center[1] = at[1];
    s.heading = 0;
    s.speed = static_cast<float>(rand.Next() % 10) * 0.01f;
    s.climb = (static_cast<float>(rand.Next() % 40) * 0.01f) + 0.2f;
    s.turn = 0;
    s.color = 10;
  }
  added.push_back(std::move(p));
}

void AddHealingSparks(RagEffect& effect, const EffectActor& actor, std::int8_t variant,
                      core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame > 20 || effect.frame % 4 != 0) return;
  added.push_back(MakeSparks(
      effect,
      {true, 1.5f, 7, 3.0f, 6.0f, 20, 0.2f, static_cast<std::uint8_t>(variant == 0 ? 11 : 1)},
      rand));
}

void AddHealingWalls(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame == 0) {
    EffectPrim p = MakeRings(effect, 0x39, texture);
    p.scale = variant == 0 ? 9.0f : 5.0f;
    PrimRing& first = p.rings[0];
    first.active = true;
    first.span = 360;
    first.angle = static_cast<std::int16_t>(rand.Next() % 360);
    first.tilt = 90;
    if (variant == 0) {
      first.kind = 60;
      first.height = 40.0f;
      first.radius = Bits(0x40933333);  // 4.6
    } else {
      first.kind = 30;
      first.height = static_cast<float>(rand.Next() % 20) + 40.0f;
      first.radius = 4.5f;
      first.radius = (static_cast<float>(rand.Next() % 100) * 0.005f) + first.radius;
    }
    PrimRing& second = p.rings[1];
    second.active = true;
    second.span = 360;
    second.angle = static_cast<std::int16_t>(rand.Next() % 360);
    second.tilt = 90;
    if (variant == 0) {
      second.kind = 60;
      second.height = 40.0f;
      second.radius = Bits(0x4099999a);  // 4.8
    } else {
      second.kind = 30;
      second.height = first.height;
      second.radius = first.radius + 0.2f;
    }
    added.push_back(std::move(p));
    return;
  }
  if (effect.frame > (variant != 0 ? 25 : 50) || effect.frame % 6 != 0) return;
  added.push_back(MakeSparks(
      effect, {variant != 0, 2.0f, 13, 6.0f, 0.0f, 40, variant != 0 ? 0.4f : 0.3f, 11}, rand));
}

void AddHealingRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  const std::int16_t kind = variant == 0 ? 60 : 40;
  if (effect.frame == 0) {
    EffectPrim p = MakeRings(effect, 0x39, texture);
    p.scale = variant == 2 ? 4.0f : 9.0f;
    const float low = variant == 0 ? 12.0f : 18.0f;
    SetRing(p.rings[0], {360, 60.0f, 0, 0, 0, 90, Bits(0x40933333), kind});
    SetRing(p.rings[1], {360, 60.0f, 0, 180, 0, 90, 5.0f, kind});
    const auto third = static_cast<std::int16_t>(rand.Next() % 360);
    SetRing(p.rings[2], {360, low, 0, third, 0, 50, 5.0f, kind});
    const auto fourth = static_cast<std::int16_t>(rand.Next() % 360);
    SetRing(p.rings[3], {360, low, 0, fourth, 0, 48, Bits(0x40a66666), kind});
    added.push_back(std::move(p));
  } else if (effect.frame < 51 && effect.frame % 6 == 0 && variant < 2) {
    added.push_back(
        MakeSparks(effect, {true, 2.0f, 13, 6.0f, 0.0f, 40, variant == 0 ? 0.4f : 0.6f, 11}, rand));
  }
  if (variant != 1) return;
  if (effect.frame == 1) {
    EffectPrim p = MakeRings(effect, 0x39, "effect\\alpha_down.tga");
    p.scale = 9.0f;
    SetRing(p.rings[0], {360, 90.0f, 0, 0, 0, 90, Bits(0x40866666), 40});
    SetRing(p.rings[1], {360, 90.0f, 0, 90, 0, 90, Bits(0x4089999a), 40});
    const auto third = static_cast<std::int16_t>(rand.Next() % 360);
    SetRing(p.rings[2], {360, 10.0f, 0, third, 0, 46, Bits(0x40a9999a), 30});
    const auto fourth = static_cast<std::int16_t>(rand.Next() % 360);
    SetRing(p.rings[3], {360, 10.0f, 0, fourth, 0, 44, Bits(0x40accccd), 30});
    added.push_back(std::move(p));
  }
  if (effect.frame < 51 && effect.frame % 9 == 2)
    added.push_back(MakeSparks(effect, {true, 2.4f, 13, 6.0f, 0.0f, 40, 0.4f, 11}, rand));
}

void AddTwinSquares(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame == 99999998) {
    effect.frame = 0;
    effect.duration = 99999999;
  }
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x1a, {"effect\\pikapika2.bmp"});
  p.scale = 4.0f;
  for (std::size_t k = 0; k < 2; ++k) {
    PrimRing& r = p.rings[k];
    r.active = true;
    r.radius = 15.0f;
    r.tilt = static_cast<std::int16_t>(k * 180);
    r.alpha = 200;
    r.heights[0] = -1.0f;
  }
  added.push_back(std::move(p));
}

void AddSwellingSquare(RagEffect& effect, const EffectActor& actor, const char* texture,
                       core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x25, {texture});
  p.scale = 5.0f;
  PrimRing& r = p.rings[0];
  r.active = true;
  r.radius = 7.0f;
  r.tilt = static_cast<std::int16_t>(rand.Next() % 360);
  r.angle = 0;
  r.alpha = 100;
  r.heights[0] = -2.0f;
  added.push_back(std::move(p));
}

void AddRisingMotes(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                    std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame == 99999998) {
    effect.frame = 0;
    effect.duration = 99999999;
  }
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x1b, {"effect\\freezing_circle.bmp"});
  p.scale = 4.0f;
  for (PrimMotes& r : p.motes) {
    r.active = true;
    r.speed = Bits(0x3e19999a);  // 0.15
    r.size = Bits(0x3f4ccccd);   // 0.8
    for (std::size_t n = 0; n < 4; ++n) {
      r.points[kMoteOrder[n]] = {0.0f, static_cast<float>(rand.Next() % 100), 0.0f};
      for (std::size_t k = 0; k < 4; ++k)
        r.values[4 * n + k] = static_cast<float>(rand.Next() % 360);
    }
  }
  added.push_back(std::move(p));
}

void AddPulsingMotes(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                     std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0 || !(effect.target[0] > 0.0f)) return;
  std::int32_t n = 0;
  do {
    EffectPrim p = MakeSquares(effect, 0x1e, {"effect\\thunder_center.bmp"});
    PrimMotes& r = p.motes[0];
    r.active = true;
    for (std::size_t k = 0; k < 6; ++k) r.values[k] = static_cast<float>(rand.Next() % 360);
    r.values[6] = static_cast<float>(n * 72);
    r.values[7] = -15.0f;
    r.size = 2.0f;
    r.values[8] = static_cast<float>(rand.Next() % 360);
    added.push_back(std::move(p));
    ++n;
  } while (static_cast<float>(n) < effect.target[0]);
}

void AddPoppingMotes(RagEffect& effect, const EffectActor& actor, std::int8_t count,
                     core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x21,
                             {"effect\\super1.bmp", "effect\\super2.bmp", "effect\\super3.bmp",
                              "effect\\super4.bmp", "effect\\super5.bmp"});
  constexpr std::array<float, 4> kStarts = {70.0f, 130.0f, 190.0f, 250.0f};
  for (std::int32_t k = 0; k < count; ++k) {
    PrimMotes& r = p.motes[static_cast<std::size_t>(k)];
    r.active = true;
    r.size = 3.5f;
    for (std::size_t n = 0; n < 4; ++n) {
      r.points[kMoteOrder[n]][1] = static_cast<float>(-(rand.Next() % 10));
      float* v = &r.values[4 * n];
      v[0] = 10.0f;
      v[1] = kStarts[n];
      v[2] = static_cast<float>(rand.Next() % 5);
      v[3] = static_cast<float>(rand.Next() % 360);
    }
  }
  added.push_back(std::move(p));
}

void AddBurstSquares(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                     std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  effect.position = actor.position;
  EffectPrim p = MakeSquares(effect, 0x29, {"effect\\white01.bmp"});
  effect.position[1] = effect.position[1] - 9.0f;
  for (PrimPiece& s : p.pieces) {
    s.active = true;
    s.points[0] = effect.target;
    s.age = -70 - rand.Next() % 30;
    s.distance = 12.0f;
    s.growth = 1.5f;
    s.yaw = static_cast<std::int16_t>(rand.Next() % 360);
    s.pitch = static_cast<std::int16_t>(rand.Next() % 360);
    s.turn = static_cast<std::int16_t>(rand.Next() % 360);
    s.values[1] = static_cast<float>(rand.Next() % 360);
    s.values[0] = 3.5f;
    s.alpha = 0;
    s.color = 0;
  }
  added.push_back(std::move(p));
}

void AddRays(RagEffect& effect, const EffectActor& actor, const char* texture, std::uint8_t color,
             bool short_rays, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  if (!short_rays) effect.position = actor.position;
  if (effect.frame != 0) return;
  effect.position = actor.position;
  EffectPrim p = MakeSquares(effect, 0x2a, {texture});
  effect.position[1] = effect.position[1] - 9.0f;
  for (PrimPiece& s : p.pieces) {
    s.active = true;
    s.age = short_rays ? -20 - rand.Next() % 10 : -10 - rand.Next() % 25;
    s.distance = -10.0f;
    s.growth = short_rays ? 2.0f : 3.0f;
    s.yaw = static_cast<std::int16_t>(rand.Next() % 360);
    s.pitch = static_cast<std::int16_t>(rand.Next() % 360);
    s.values[0] = short_rays ? 1.0f : Bits(0x3f333333);  // 0.7
    s.alpha = 0;
    s.color = color;
    s.length = static_cast<std::int8_t>(short_rays ? 16 : 24);
  }
  added.push_back(std::move(p));
}

void AddFireworks(RagEffect& effect, const Vec3& player, core::CrtRand& rand,
                  std::vector<EffectPrim>& added) {
  effect.position = player;
  if (effect.frame != 0) return;
  EffectPrim p =
      MakeSquares(effect, 0x32, {"effect\\pok1.tga", "effect\\pok3.tga", "effect\\pok2.tga"});
  effect.position[0] = effect.position[0] - 15.0f;
  for (PrimPiece& s : p.pieces) {
    s.active = true;
    s.age = -300 - rand.Next() % 50;
    s.turn = static_cast<std::int16_t>(rand.Next() % 360);
    s.points.fill(effect.position);
    s.alpha = 0;
    s.width = 2.0f;
    s.color = static_cast<std::uint8_t>(rand.Next() % 5);
    s.length = 0;
    s.image = 0;
    s.delay = static_cast<std::int8_t>(rand.Next() % 4 * 20);
    s.distance = (static_cast<float>(rand.Next() % 100) * Bits(0x3b83126f)) + Bits(0x3ecccccd);
    s.pitch = static_cast<std::int16_t>(rand.Next() % 360);
    s.yaw = static_cast<std::int16_t>(rand.Next() % 360);
  }
  added.push_back(std::move(p));
}

void AddBeams(RagEffect& effect, const EffectActor& actor, std::int16_t angle,
              std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeSquares(effect, 0x34, {"effect\\shockwave_c.bmp"});
  effect.position[1] = effect.position[1] - 9.0f;
  for (std::size_t k = 0; k < p.pieces.size(); ++k) {
    PrimPiece& s = p.pieces[k];
    s.active = true;
    s.age = 0;
    s.distance = 100.0f;
    s.yaw = static_cast<std::int16_t>(angle + 90 * static_cast<std::int32_t>(k));
    s.alpha = 0;
  }
  added.push_back(std::move(p));
}

void AddSlashLines(RagEffect& effect, const EffectActor& actor, bool right,
                   std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 100) return;
  EffectPrim p = MakeSquares(effect, 0x35, {"effect\\blue_ivy.bmp"});
  p.quad_flags = 0x201;  // the constructor's 1, and 0x200
  effect.position[1] = effect.position[1] - 9.0f;
  for (std::size_t k = 0; k < p.pieces.size(); ++k) {
    PrimPiece& s = p.pieces[k];
    s.active = true;
    s.age = 0;
    s.distance = 100.0f;
    s.width = 1.0f;
    const auto n = static_cast<float>(k);
    s.growth = right ? (n * 2.5f) + 15.0f : (n * -2.5f) - 15.0f;
    s.yaw = 30;
    s.alpha = 0;
  }
  added.push_back(std::move(p));
}

void AddSpreadingRings(RagEffect& effect, const EffectActor& actor, std::int16_t alpha,
                       const char* texture, std::int8_t preset, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  effect.duration = std::max(effect.duration, 60);
  EffectPrim p = MakeRings(effect, 0x1c, texture);
  p.scale = preset == 1 ? 8.0f : 5.0f;
  constexpr std::array<std::int16_t, 4> kAngles = {180, 270, 0, 90};
  constexpr std::array<std::int16_t, 4> kAlphas = {135, 90, 45, 0};
  for (std::size_t k = 0; k < p.rings.size(); ++k) {
    const auto n = static_cast<std::int32_t>(k);
    const float height = static_cast<float>((preset == 2 ? 25 : 20) - n);
    SetRing(p.rings[k], {360, height, 0, kAngles[k], static_cast<std::int16_t>(alpha + kAlphas[k]),
                         80, Bits(0x40833333), preset});  // 4.1
    if (effect.duration < 61) {
      p.rings[k].alpha = 0;
      p.rings[k].age = -5 * n;
    }
  }
  added.push_back(std::move(p));
}

void AddAsuraStrike(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame == 0) {
    effect.duration = std::max(effect.duration, 280);
    // effect\EF_BeginSpell.wav plays here.
    AddSpreadingRings(effect, actor, 45, "effect\\ring_white.tga", 2, added);
    AddSpreadingRings(effect, actor, 25, "effect\\ring_white.tga", 2, added);
  }
  if (effect.frame != 1 && effect.frame != 21) return;
  const bool first = effect.frame == 1;
  EffectPrim p =
      MakeSquares(effect, 0x41,
                  first ? std::vector<std::string>{"effect\\asura3.tga", "effect\\asura2.tga",
                                                   "effect\\asura1.tga"}
                        : std::vector<std::string>{"effect\\asura4.tga", "effect\\asura5.tga",
                                                   "effect\\asura6.tga"});
  p.scale = 0.0f;
  for (std::size_t k = 0; k < 3; ++k) {
    PrimPiece& s = p.pieces[k];
    const auto n = static_cast<std::int32_t>(k);
    s.active = true;
    s.age = first ? -40 + 20 * n : -40 - 20 * n;
    s.distance = 18.0f;
    s.alpha = 0;
    s.growth = static_cast<float>(first ? -6 - 12 * n : 6 + 12 * n);
    s.color = static_cast<std::uint8_t>(k);
    s.values.fill(0.0f);
  }
  added.push_back(std::move(p));
}

void AddGrandCross(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  // effect\cru_grand cross.wav plays here.
  EffectPrim p = MakeRings(effect, 0x20, "effect\\ring_white.tga");
  p.scale = 1.0f;
  constexpr std::array<std::int16_t, 4> kAngles = {180, 90, 0, 270};
  constexpr std::array<std::array<std::int8_t, 2>, 4> kOffsets = {
      {{23, 23}, {23, -23}, {-23, -23}, {-23, 23}}};
  for (std::size_t k = 0; k < p.rings.size(); ++k) {
    PrimRing& r = p.rings[k];
    SetRing(r, {90, 120.0f, 0, kAngles[k], 0, 90, Bits(0x419f3333), 0});
    r.flags[0] = static_cast<std::uint8_t>(kOffsets[k][0]);
    r.flags[1] = static_cast<std::uint8_t>(kOffsets[k][1]);
  }
  added.push_back(std::move(p));
}

void AddBox(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x23, "effect\\ring_yellow.tga");
  p.scale = 1.0f;
  for (std::size_t k = 0; k < 2; ++k) {
    PrimBand& b = p.bands[k];
    b.active = true;
    b.age = 0;
    b.size = 60.0f;
    b.angle = static_cast<std::int16_t>(k * 90);
    b.alpha = 0;
    b.values[0] = 24.0f;
    b.values[1] = Bits(0x404ccccd);
    b.values[2] = 0.0f;
  }
  added.push_back(std::move(p));
}

namespace {

// Rings whose every wall starts at one height, and their flag bytes at one value.
void SetRingWalls(PrimRing& r, const RingSetup& s, float wall, std::uint8_t flag) {
  SetRing(r, s);
  r.heights.fill(wall);
  r.flags.fill(flag);
}

}  // namespace

void AddCloudRings(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  // _heal_effect.wav plays here.
  EffectPrim p = MakeRings(effect, 0x3b, "effect\\cloud11.tga");
  p.scale = 4.0f;
  SetRingWalls(p.rings[0], {90, 8.0f, 0, 0, 0, 90, 3.0f, 0}, 3.0f, 0);
  SetRingWalls(p.rings[1], {70, 8.0f, 0, 10, 0, 90, Bits(0x404ccccd), 0}, 2.5f, 0);
  SetRingWalls(p.rings[2], {50, 8.0f, 0, 20, 0, 90, Bits(0x4059999a), 0}, 2.0f, 0);
  SetRingWalls(p.rings[3], {30, 8.0f, 0, 30, 0, 90, Bits(0x40666666), 0}, 1.5f, 0);
  added.push_back(std::move(p));
}

void AddOpeningPortal(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                      std::vector<EffectPrim>& added) {
  if (effect.frame == 0) {
    EffectPrim p = MakeRings(effect, 0x39, "effect\\magic_violet.tga");
    p.scale = 4.0f;
    for (std::size_t k = 0; k < 3; ++k) {
      const auto angle = static_cast<std::int16_t>(rand.Next() % 360);
      const auto radius = static_cast<float>(4 - static_cast<std::int32_t>(k));
      SetRingWalls(p.rings[k], {360, 50.0f, 0, angle, 0, 90, radius, 1400}, 0.0f, 1);
    }
    p.rings[3].active = false;
    added.push_back(std::move(p));
  }
  if (effect.frame == 1) {
    effect.position = actor.position;
    EffectPrim p = MakeRings(effect, 0x3c, "effect\\ring_blue.tga");
    p.scale = 4.0f;
    const float low = Bits(kLowWalls);
    SetRingWalls(p.rings[0], {360, low, 0, 0, 0, 2, 0.0f, 3111}, 0.0f, 1);
    SetRingWalls(p.rings[1], {360, low, -10, 25, 0, 3, 0.0f, 3111}, 0.0f, 1);
    SetRingWalls(p.rings[2], {360, low, -20, 50, 0, 4, 0.0f, 3111}, 0.0f, 1);
    p.rings[3].active = false;
    p.rings[3].age = 0;
    p.rings[3].kind = 0;
    added.push_back(std::move(p));
  }
  if (effect.frame == 2) {
    EffectPrim p = MakeRings(effect, 0x3b, "effect\\cloud11.tga");
    p.scale = 4.0f;
    SetRingWalls(p.rings[0], {30, 1.5f, 0, 0, 0, 90, 9.0f, 0}, 1.5f, 0);
    SetRingWalls(p.rings[1], {30, 3.5f, 0, 90, 0, 90, 7.5f, 0}, 1.5f, 0);
    SetRingWalls(p.rings[2], {30, 5.5f, 0, 180, 0, 90, 6.0f, 0}, 1.5f, 0);
    SetRingWalls(p.rings[3], {30, 7.5f, 0, 270, 0, 90, 4.5f, 0}, 1.5f, 0);
    added.push_back(std::move(p));
  }
}

void AddBoxWalls(RagEffect& effect, const EffectActor& actor, const char* texture,
                 std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  EffectPrim p = MakeRings(effect, 0x3d, texture);
  p.scale = 1.0f;
  PrimBand& b = p.bands[0];
  b.active = true;
  b.age = 0;
  b.phase = static_cast<std::int16_t>(rand.Next() % 360);
  const bool first = variant == 0;
  b.size = first ? 50.0f : 16.0f;
  b.angle = 0;
  b.alpha = first ? 30 : 120;
  b.fade_at = first ? 864 : 1880;
  b.values[0] = first ? 5.0f : 2.5f;
  b.values[1] = b.values[0];
  b.values[2] = 0.0f;
  b.values[3] = static_cast<float>(variant);
  added.push_back(std::move(p));
}

void AddPentagonBand(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::int8_t variant, std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  effect.position = actor.position;
  EffectPrim p = MakeRings(effect, 0x3f, texture);
  for (std::size_t k = 0; k < p.bands.size(); ++k) {
    PrimBand& b = p.bands[k];
    const auto n = static_cast<std::int32_t>(k);
    b.active = true;
    b.age = 0;
    b.alpha = variant == 2 ? 30 : variant == 10 ? 25 : 40;
    b.length = 0.0f;
    if (k == 3) {
      b.age = 180;
      b.size = variant == 1 ? 4.0f : variant == 2 ? 1.0f : 2.0f;
    } else if (variant == 1) {
      b.size = static_cast<float>(n * 2) + 4.0f;
    } else if (variant == 2) {
      b.size = (static_cast<float>(n) * 0.5f) + 1.0f;
    } else {
      b.size = static_cast<float>(n) + 2.0f;
    }
    b.values[0] = b.size;
    b.angle = static_cast<std::int16_t>(n * 25);
    b.points[1] = effect.position;
    effect.target = effect.position;
    const bool short_band = variant == 10;
    effect.target[1] = effect.target[1] - (short_band ? 60.0f : 300.0f);
    effect.target[0] = effect.target[0] - (short_band ? 15.0f : 70.0f);
    effect.target[2] = effect.target[2] - (short_band ? 15.0f : 70.0f);
    b.points[3] = effect.target;
  }
  added.push_back(std::move(p));
}

void AddArcBands(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                 std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  // effect\EF_hit2.wav plays at frames 10 and 30, effect\EF_hit4.wav at 20.
  if (effect.frame >= 2) return;
  EffectPrim p = MakeRings(effect, 0x42, "effect\\cloud11.tga");
  p.scale = 0.0f;
  p.travel = effect.target;
  const float dx = effect.target[0] - effect.position[0];
  const float dz = effect.target[2] - effect.position[2];
  const auto spread = [&rand](float at) {
    return ((static_cast<float>(rand.Next() % 100) * kBandSpread) - 3.0f) + at;
  };
  for (PrimBand& b : p.bands) {
    b.active = true;
    b.age = -static_cast<std::int32_t>(rand.Next() % 20);
    b.angle = VectorToDegrees(dx, dz);
    b.alpha = 0;
    Vec3& from = b.points[0];
    from[0] = spread(effect.position[0]);
    from[2] = spread(effect.position[2]);
    from[1] = effect.position[1];
    b.values[1] = static_cast<float>(rand.Next() % 100) * kBandBack;
    from[0] = from[0] - (TableAt(kCosines, b.angle) * b.values[1]);
    from[2] = from[2] - (TableAt(kSines, b.angle) * b.values[1]);
    b.points[1] = {from[0], effect.position[1], from[2]};
    b.size = 0.25f;
    b.values[0] = (static_cast<float>(rand.Next() % 100) * kBandLift) + 8.0f;
    b.length = (static_cast<float>(rand.Next() % 100) * kBandLength) + 20.0f;
  }
  added.push_back(std::move(p));
}

void AddBouncingBands(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                      std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame < 0 || effect.frame >= 4) return;
  EffectPrim p = MakeRings(effect, 0x43, "effect\\white02.bmp");
  p.image_count = 1;
  p.scale = 0.0f;
  for (PrimBand& b : p.bands) {
    b.active = true;
    b.age = -static_cast<std::int32_t>(rand.Next() % 26);
    b.size = Bits(0x3dcccccd);
    b.alpha = static_cast<std::int16_t>(rand.Next() % 51 + 125);
    b.heading = static_cast<std::int16_t>(rand.Next() % 360);
    b.length = static_cast<float>(rand.Next() % 8) + 6.0f;
    b.bounces.fill(0);
    b.values.fill(0.0f);
    b.points[0] = effect.position;
  }
  added.push_back(std::move(p));
}

namespace {

// CRenderObject::SetAction (00500de0), as primitives feel it: the action, from its first
// frame.
void SetAction(EffectPrim& p, std::int32_t action) {
  p.action = action;
  p.image = 0;
}

// The cylinders of alpha_down.tga effects 7 and 0xe stand round the actor: as long as the
// effect, fading in to their limit over 30 frames and out over the last 20.
EffectPrim MakeColumn(const RagEffect& effect, float radius, float height, float limit) {
  EffectPrim c = MakePrimitive(&effect, 0xd, {0.0f, 0.0f, 0.0f});
  c.life = effect.duration;
  c.top_radius = radius;
  c.bottom_radius = radius;
  c.height = height;
  c.alpha = 0.0f;
  c.alpha_max = limit;
  c.alpha_speed = c.alpha_max * kThirtieth;
  c.fade_at = c.life - 20;
  c.textures = {"effect\\alpha_down.tga"};
  return c;
}

// The sparks effects 7 and 0xe raise: id 7 at a random heading 3 out, rising by 1.2 a
// frame, slowing by two thirds of that over its life, fading from a third of it from the
// end.
EffectPrim MakeRisingSpark(const RagEffect& effect, std::int32_t life, const PrimSprite& sprite,
                           core::CrtRand& rand) {
  EffectPrim p = MakePrimitive(&effect, 7, {0.0f, 0.0f, 0.0f});
  rand.Next();
  p.distance = 3.0f;
  p.life = life;
  p.heading = static_cast<float>(rand.Next() % 360);
  p.fall_speed = std::bit_cast<float>(0xbf99999au);  // -1.2
  p.fall_rate = (p.fall_speed / static_cast<float>(p.life)) * kEaseOut;
  p.matrix = MakeRotationY(Radians(p.heading));
  p.offset = AlongZ(p, p.distance);
  p.scale = std::bit_cast<float>(0x3f0ccccdu);  // 0.55
  p.fade_at = p.life - p.life / 3;
  p.sprite = sprite;
  ResetHistory(p);
  return p;
}

// The offset of the spiralling sparks (0x47, 0x48 and 0x56): along the matrix's z by the
// distance, 30 down its y.
Vec3 SpiralOffset(const EffectPrim& p) {
  const Matrix& m = p.matrix;
  return {((p.distance * m[6]) - (m[3] * 30.0f)) + m[9],
          ((p.distance * m[7]) - (m[4] * 30.0f)) + m[10],
          ((p.distance * m[8]) - (m[5] * 30.0f)) + m[11]};
}

// A shot of id 8 at the effect towards its target (effects 0xf, 0x75, 0xd8 and 0xd9): 10
// up, facing the target (TurnTowards, the heading its negation), covering the ground
// distance (CRenderObject::GroundDistanceTo, 00500530) over its life (twice over when
// fast), falling at the fall less twice that over its life and circling the matrix's z at
// a pitch from −2 shrinking the same way.
EffectPrim MakeShot(const RagEffect& effect, std::int32_t life, float fall, bool fast) {
  EffectPrim p = MakePrimitive(&effect, 8, {0.0f, 0.0f, 0.0f});
  p.life = life;
  p.offset = {0.0f, -10.0f, 0.0f};
  const float dx = effect.target[0] - p.position[0];
  const float dz = effect.target[2] - p.position[2];
  p.distance = SqrtFloat((dx * dx) + (dz * dz));
  p.facing = TurnTowards(p.position, effect.target[0], effect.target[2]);
  p.heading = -p.facing;
  const float speed = p.distance / static_cast<float>(p.life);
  p.speed = fast ? speed + speed : speed;
  p.fall_speed = fall;
  p.fall_rate = (p.fall_speed / static_cast<float>(p.life)) * -2.0f;
  p.pitch_speed = -2.0f;
  p.pitch_rate = (p.pitch_speed / static_cast<float>(p.life)) * -2.0f;
  return p;
}

// The rest of a shot once its angle is set: its matrix along the heading, and its axis the
// matrix's z plus its translation.
void AimShot(EffectPrim& p, const PrimSprite& sprite, std::int32_t history) {
  p.matrix = MakeRotationY(Radians(p.heading));
  const Matrix& m = p.matrix;
  p.drift = {m[9] + m[6], m[10] + m[7], m[11] + m[8]};
  p.axis = p.drift;
  p.sprite = sprite;
  p.history_count = history;
  ResetHistory(p);
}

// The action 0x10b's sprite takes by the way the actor lies from the target on screen: 0
// from 180 degrees, 1 from 90, 2 from 0 and 3 from 270.
std::int32_t QuarterAction(std::int16_t seen) {
  if (seen >= 180 && seen < 270) return 0;
  if (seen >= 90 && seen < 180) return 1;
  if (seen >= 0 && seen < 90) return 2;
  return 3;
}

// The first record of the standing and falling sprites, the others cleared.
PrimMotes& OnlyRecord(EffectPrim& p) {
  for (PrimMotes& r : p.motes) r.active = false;
  p.motes[0].active = true;
  return p.motes[0];
}

}  // namespace

void AddHealingColumn(RagEffect& effect, const EffectActor& actor, bool minimal,
                      const PrimSprite& particle, core::CrtRand& rand,
                      std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame == 0) {
    effect.duration = 100;
    added.push_back(MakeColumn(effect, 4.5f, 35.0f, 90.0f));
    // _heal_effect.wav plays here.
  }
  if (!minimal && effect.frame % 9 == 0)
    added.push_back(MakeRisingSpark(effect, 50, particle, rand));
}

void AddBlueColumns(RagEffect& effect, const EffectActor& actor, const PrimSprite& sparks,
                    core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame == 0) {
    effect.duration = 100;
    for (const auto& [radius, height, limit] :
         {std::array<float, 3>{2.5f, 15.0f, 16.0f}, std::array<float, 3>{3.5f, 25.0f, 32.0f},
          std::array<float, 3>{4.5f, 35.0f, 64.0f}}) {
      EffectPrim c = MakeColumn(effect, radius, height, limit);
      c.heading_speed = 5.0f;
      c.red = 32;
      c.green = 176;
      c.blue = 232;
      added.push_back(std::move(c));
    }
    // _heal_effect.wav plays here.
  }
  if (effect.frame % 2 == 0) added.push_back(MakeRisingSpark(effect, 30, sparks, rand));
}

void AddSoulStrike(RagEffect& effect, const PrimSprite& particle, std::vector<EffectPrim>& added) {
  std::int32_t base = 0, count = 0, step = 0;
  switch (effect.level) {
    case 1: base = 0, count = 1, step = 1; break;
    case 2: base = -90, count = 2, step = 180; break;
    case 3: base = -90, count = 3, step = 90; break;
    case 4: base = -90, count = 4, step = 60; break;
    case 5: base = -90, count = 5, step = 45; break;
    default: break;
  }
  const std::int32_t at = effect.frame + 5;
  const std::int32_t shot = at / 11;
  if (at % 11 != 0 || shot > count) return;
  // effect\EF_SoulStrike.wav plays with each shot.
  EffectPrim p = MakeShot(effect, 40, 3.5f, false);
  const std::int32_t angle = shot * step + base;
  p.angle = static_cast<float>(angle);
  p.scale = 2.75f;
  AimShot(p, particle, 12);
  added.push_back(std::move(p));
}

void AddSpiralSpark(RagEffect& effect, const PrimSprite& particle, std::vector<EffectPrim>& added) {
  if (effect.frame % 130 != 0) return;
  EffectPrim p = MakePrimitive(&effect, 7, {0.0f, 0.0f, 0.0f});
  effect.duration = 200;
  p.life = 300;
  p.distance = std::bit_cast<float>(0x3dcccccdu);        // 0.1
  p.distance_speed = std::bit_cast<float>(0x3e4ccccdu);  // 0.2
  p.fall_speed = std::bit_cast<float>(0x3dcccccdu);      // 0.1
  p.fall_rate = std::bit_cast<float>(0x3ba3d70au);       // 0.005
  p.heading = 2.0f;
  p.heading_speed = 15.0f;
  p.heading_rate = std::bit_cast<float>(0xbc23d70au);  // -0.01
  p.scale = std::bit_cast<float>(0x3f99999au);         // 1.2
  p.matrix = MakeRotationY(Radians(p.heading));
  p.offset = SpiralOffset(p);
  p.sprite = particle;
  ResetHistory(p);
  added.push_back(std::move(p));
}

void AddWideningSpirals(RagEffect& effect, const EffectActor& actor, const PrimSprite& particle,
                        std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame % 10 != 0) return;
  EffectPrim p = MakePrimitive(&effect, 7, {0.0f, 0.0f, 0.0f});
  effect.duration = 500 - effect.frame;
  p.life = 300;
  p.distance = std::bit_cast<float>(0x3dcccccdu);        // 0.1
  p.distance_speed = std::bit_cast<float>(0x3e19999au);  // 0.15
  p.fall_speed = std::bit_cast<float>(0x3e19999au);      // 0.15
  p.heading = 2.0f;
  p.heading_speed = 8.0f;
  p.heading_rate = std::bit_cast<float>(0xbc23d70au);  // -0.01
  p.scale = std::bit_cast<float>(0x3f99999au);         // 1.2
  p.matrix = MakeRotationY(Radians(p.heading));
  p.offset = SpiralOffset(p);
  p.distance_keys = true;
  p.key = 0;
  p.key_frames.push_back(95.0f);
  p.key_values.push_back(std::bit_cast<float>(0xbe19999au));  // -0.15
  p.key_limits.push_back(p.distance_rate);
  p.sprite = particle;
  ResetHistory(p);
  added.push_back(std::move(p));
}

void AddRemoveTrap(RagEffect& effect, const PrimSprite& particle, core::CrtRand& rand,
                   std::vector<EffectPrim>& added) {
  // effect\hunter_removetrap.wav plays on the first frame.
  if (effect.frame != 0 && effect.frame != 7) return;
  for (std::int32_t n = 0; n < 12; ++n) {
    EffectPrim p = MakePrimitive(&effect, 7, {0.0f, 0.0f, 0.0f});
    p.life = 50;
    p.distance_speed = std::bit_cast<float>(0x3e4ccccdu);  // 0.2
    rand.Next();
    p.fall_speed = -2.0f;
    p.fall_rate = (static_cast<float>(rand.Next() % 10) * kThousandth) + kTrapFall;
    p.heading = static_cast<float>(rand.Next() % 360);
    p.scale = std::bit_cast<float>(0x3f4ccccdu);  // 0.8
    p.matrix = MakeRotationY(Radians(p.heading));
    p.offset = AlongZ(p, p.distance);
    p.fade_at = 35;
    p.sprite = particle;
    ResetHistory(p);
    added.push_back(std::move(p));
  }
}

bool AddAspersio(RagEffect& effect, const PrimSprite& particle, core::CrtRand& rand,
                 std::vector<EffectPrim>& added) {
  // effect\priest_aspersio.wav plays at frame 70.
  const std::int32_t f = effect.frame;
  if (f % 5 == 0 && f > 60 && f < 140) {
    EffectPrim p = MakePrimitive(&effect, 7, {0.0f, 0.0f, 0.0f});
    effect.duration = 400;
    p.life = 40;
    p.distance = 1.5f;
    p.fall_speed = std::bit_cast<float>(0x3e428f5cu);  // 0.19
    p.fall_rate = std::bit_cast<float>(0x3c23d70au);   // 0.01
    p.heading = static_cast<float>(rand.Next() % 360);
    p.scale = std::bit_cast<float>(0x3f99999au);  // 1.2
    p.matrix = MakeRotationY(Radians(p.heading));
    p.offset = SpiralOffset(p);
    p.fade_at = 30;
    p.sprite = particle;
    ResetHistory(p);
    added.push_back(std::move(p));
  }
  // The .str plays on, 80 up (AdvanceStr), until the frame meets the duration.
  return effect.frame != effect.duration;
}

void AddWaterBall(RagEffect& effect, const PrimSprite& ball, std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  // effect\wizard_waterball_chulung.wav plays here.
  EffectPrim p = MakeShot(effect, 40, 3.5f, false);
  p.angle = 0.0f;
  p.scale = 0.75f;
  AimShot(p, ball, 12);
  added.push_back(std::move(p));
}

void AddTripleShot(RagEffect& effect, const PrimSprite& particle, bool blue,
                   std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  for (std::int32_t angle = -90; angle < 180; angle += 90) {
    EffectPrim p = MakeShot(effect, 60, 2.0f, true);
    p.angle = static_cast<float>(angle);
    p.red = blue ? 100 : 250;
    p.green = 100;
    p.blue = blue ? 250 : 100;
    p.scale = std::bit_cast<float>(0x3fd9999au);  // 1.7
    AimShot(p, particle, 7);
    added.push_back(std::move(p));
  }
}

void AddSnowfall(RagEffect& effect, const Vec3& player, const res::AttrMap* attr,
                 const PrimSprite& snow, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = player;
  if (effect.duration - effect.frame <= 160) return;
  for (std::int32_t n = 0; n < 2; ++n) {
    EffectPrim p = MakePrimitive(&effect, 0x15, {0.0f, 0.0f, 0.0f});
    p.start = effect.position;
    p.life = 320;
    p.matrix = MakeRotationY(core::DegreesToRadians(static_cast<std::int32_t>(rand.Next() % 360)));
    const auto out = static_cast<float>(static_cast<std::int32_t>(rand.Next() % 300) - 300);
    const float ground =
        attr != nullptr ? res::AttrHeight(*attr, effect.position[0], effect.position[2]) : 0.0f;
    const float up = ground - 100.0f;
    const Matrix& m = p.matrix;
    p.offset = {((out * m[6]) + (up * m[3])) + m[9], ((out * m[7]) + (up * m[4])) + m[10],
                ((out * m[8]) + (up * m[5])) + m[11]};
    p.matrix = MakeRotationX(core::DegreesToRadians(90));
    p.speed = -0.5f;
    p.acceleration = 0.0f;
    p.scale = static_cast<float>(rand.Next() % 15 + 25) * kFiftieth;
    p.spin = 0.0f;
    p.alpha = 0.0f;
    p.alpha_speed = p.alpha_max * std::bit_cast<float>(0x3dcccccdu);  // 0.1
    p.fade_at = p.life - p.life / 5;
    p.sprite = snow;
    ResetHistory(p);
    added.push_back(std::move(p));
  }
}

void AddFallingLeaves(RagEffect& effect, const EffectActor& actor, const PrimSprite& leaf,
                      bool maple, core::CrtRand& rand, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame == 99999998) {
    effect.frame = 0;
    effect.duration = 99999999;
  }
  if (effect.frame % 2 != 0 || effect.frame >= (maple ? 200 : 300)) return;
  EffectPrim p = MakePrimitive(&effect, 0x26, {0.0f, 0.0f, 0.0f});
  p.life = effect.duration;
  PrimMotes& r = OnlyRecord(p);
  r.speed = maple ? static_cast<float>(rand.Next() % 4 + 2) * kLeafFallSlow
                  : static_cast<float>(rand.Next() % 2 + 2) * std::bit_cast<float>(0x3dcccccdu);
  p.scale = static_cast<float>(rand.Next() % 6 + (maple ? 35 : 22)) * kHundredth;
  const Vec3& at = effect.position;
  r.points[2][0] = (static_cast<float>(rand.Next() % 300) + at[0]) - 150.0f;
  r.points[2][1] = at[1] - 200.0f;
  r.points[2][2] = (static_cast<float>(rand.Next() % 300) + at[2]) - 150.0f;
  for (std::size_t k = 0; k < 4; ++k) r.values[k] = static_cast<float>(rand.Next() % 360);
  r.mode = maple ? 1 : 0;
  p.alpha = 200.0f;
  p.sprite = leaf;
  SetAction(p, maple ? 0 : static_cast<std::int32_t>(rand.Next() % 3));
  ResetHistory(p);
  added.push_back(std::move(p));
}

void AddTargetSprite(RagEffect& effect, const EffectActor& actor, const PrimSprite& sprite,
                     float view_yaw, std::vector<EffectPrim>& added) {
  effect.position = actor.position;
  if (effect.frame != 0) return;
  // effect\mon_침투경.wav plays here.
  EffectPrim p = MakePrimitive(&effect, 0x2c, {0.0f, 0.0f, 0.0f});
  p.life = effect.duration;
  PrimMotes& r = OnlyRecord(p);
  r.speed = 0.0f;
  p.scale = 2.5f;
  r.points[2] = effect.target;
  p.alpha = 200.0f;
  p.sprite = sprite;
  const std::int16_t back =
      VectorToDegrees(effect.position[0] - effect.target[0], effect.position[2] - effect.target[2]);
  auto seen = static_cast<std::int16_t>(static_cast<std::int16_t>(Ftol(view_yaw) + back) % 360);
  if (seen < 0) seen = static_cast<std::int16_t>(seen + 360);
  p.image_delay = 2;
  p.quad_flags |= 0x200;
  SetAction(p, QuarterAction(seen));
  ResetHistory(p);
  added.push_back(std::move(p));
}

void AddDemonstration(RagEffect& effect, const EffectActor& actor, const PrimSprite& sprite,
                      std::vector<EffectPrim>& added) {
  if (effect.frame != 0) return;
  effect.position = actor.position;
  EffectPrim p = MakePrimitive(&effect, 0x31, {0.0f, 0.0f, 0.0f});
  p.life = effect.duration;
  PrimMotes& r = OnlyRecord(p);
  r.speed = 0.0f;
  p.scale = std::bit_cast<float>(0x3f99999au);  // 1.2
  r.points[2] = effect.position;
  r.points[2][1] = r.points[2][1] - 1.0f;
  p.alpha = 200.0f;
  p.sprite = sprite;
  p.image_delay = 4;
  SetAction(p, 0);
  ResetHistory(p);
  added.push_back(std::move(p));
}

PrimSprite EffectWorld::Sprite(const std::string& name) const {
  const auto it = sprites_.find(name);
  return it == sprites_.end() ? PrimSprite{} : it->second;
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
      bool ended = false;
      switch (e.id) {
        case 0:
          ProcessRingBurst(e, it->actor, minimal_, Sprite(kParticleSprite), rand_, added);
          break;
        case 1:
          ProcessHitSpark(e, it->actor.position, scene.view, scene.projection, rand_, false, added);
          break;
        case 2: ProcessHit3(e, it->actor, Sprite(kParticleSprite), rand_, added); break;
        case 0xe1: AddRingWaves(e, it->actor, "effect\\ring_red.tga", 0, false, added); break;
        case 0xec: AddRingWaves(e, it->actor, "effect\\ring_blue.tga", 0, false, added); break;
        case 0xed: AddRingWaves(e, it->actor, "effect\\magic_green.tga", 0, false, added); break;
        case 0xee: AddRingWaves(e, it->actor, "effect\\ring_white.tga", 0, false, added); break;
        case 0x105: AddRingWaves(e, it->actor, "effect\\ring_yellow.tga", 0, true, added); break;
        case 0x107: AddRingWaves(e, it->actor, "effect\\ring_yellow.tga", 1, false, added); break;
        case 0xe7: AddRisingRings(e, it->actor, "effect\\ring_blue.tga", 0, added); break;
        case 0xf7: AddRisingRings(e, it->actor, "effect\\ring_blue.tga", 1, added); break;
        case 0x103: AddRisingRings(e, it->actor, "effect\\magic_green.tga", 2, added); break;
        case 0x104: AddRisingRings(e, it->actor, "effect\\ring_red.tga", 3, added); break;
        case 0xc8: AddRingTrio(e, it->actor, added); break;
        case 0xde: AddTurningRings(e, it->actor, "effect\\ring_black.tga", added); break;
        case 0xfc: AddTurningRings(e, it->actor, "effect\\ring_yellow.tga", added); break;
        case 0xef: AddRandomRing(e, it->actor, "effect\\ring_red.tga", 0, rand_, added); break;
        case 0xf0: AddRandomRing(e, it->actor, "effect\\ring_blue.tga", 0, rand_, added); break;
        case 0xf1: AddRandomRing(e, it->actor, "effect\\magic_green.tga", 0, rand_, added); break;
        case 0x118: AddTwirlRing(e, it->actor, "effect\\twirl.bmp", 8, rand_, added); break;
        case 0x11d: AddTwirlRing(e, it->actor, "effect\\twirl.bmp", 4, rand_, added); break;
        case 0x130:
          AddTeleportationRings(e, it->actor, "effect\\Magic_Violet.tga", 0, added);
          break;
        case 0x13a: AddTeleportationWalls(e, it->actor, "effect\\ring_yellow.tga", added); break;
        case 0x13b: AddGlassWall(e, it->actor, "effect\\alpha_down.tga", rand_, added); break;
        case 0x13c: AddReadyPortal(e, it->actor, added); break;
        case 0xe5:
        case 0xe6:
        case 0xe9: {
          const std::int8_t variant = e.id == 0xe5 ? 0 : e.id == 0xe6 ? 1 : 2;
          for (std::int32_t n = 0; n < (variant == 1 ? 60 : 40); ++n)
            AddClouds(e, player_, variant, rand_, added);
          break;
        }
        case 0xf9:
          // effect\cru_shield boomerang.wav plays as it starts.
          AddBoomerang(e, it->actor, "effect\\shield_boomerang.bmp", false, added);
          break;
        case 0x109: AddBoomerang(e, it->actor, "effect\\blue_ivy.bmp", true, added); break;
        case 0x108:
        case 0x113:
          for (std::int32_t n = 0; n < (e.id == 0x108 ? 5 : 20); ++n)
            AddThrownSparks(e, it->actor, rand_, added);
          break;
        case 0x116: AddBobbingSquare(e, it->actor, "effect\\zz.bmp", 1, rand_, added); break;
        case 0x117: AddBobbingSquare(e, it->actor, "effect\\pocket.bmp", 3, rand_, added); break;
        case 0x119: AddBobbingSquare(e, it->actor, "", 2, rand_, added); break;
        case 0x11a: AddBobbingSquare(e, it->actor, "effect\\twirl.bmp", 7, rand_, added); break;
        case 0x11c: AddBobbingSquare(e, it->actor, "", 5, rand_, added); break;
        case 0x11e: AddBobbingSquare(e, it->actor, "effect\\melody_b.bmp", 0, rand_, added); break;
        case 0x120: AddBobbingSquare(e, it->actor, "", 8, rand_, added); break;
        case 0x121:
          AddBobbingSquare(e, it->actor, "effect\\idun_apple.bmp", 6, rand_, added);
          break;
        case 0x123: AddBobbingSquare(e, it->actor, "effect\\melody_a.bmp", 0, rand_, added); break;
        case 0x125: AddBobbingSquare(e, it->actor, "effect\\kiss.bmp", 4, rand_, added); break;
        case 0x11b: AddSinkingSquares(e, it->actor, "effect\\safeline.bmp", 0, rand_, added); break;
        case 0x12a: AddArcingSquare(e, it->actor, kEmptyBottle, 0, added); break;
        case 299: AddArcingSquare(e, it->actor, kEmptyPotionBottle, 1, added); break;
        case 0x134: AddArcingSquare(e, it->actor, kStone, 2, added); break;
        case 0x135: AddPulsingSquare(e, it->actor, rand_, added); break;
        case 0x136:
          for (std::int32_t n = 0; n < 20; ++n) AddThunderSparks(e, it->actor, rand_, added);
          break;
        case 0x14b: AddHealingSparks(e, it->actor, 0, rand_, added); break;
        case 0x14c: AddHealingSparks(e, it->actor, 1, rand_, added); break;
        case 0x138: AddHealingWalls(e, it->actor, "effect\\alpha_down.tga", 0, rand_, added); break;
        case 0x140: AddHealingWalls(e, it->actor, "effect\\alpha_down.tga", 1, rand_, added); break;
        case 0x139: AddHealingRings(e, it->actor, "effect\\ring_white.tga", 0, rand_, added); break;
        case 0x145: AddHealingRings(e, it->actor, "effect\\ring_white.tga", 1, rand_, added); break;
        case 0xfd: AddHealingRings(e, it->actor, "effect\\ring_blue.tga", 2, rand_, added); break;
        case 0xc9: AddTwinSquares(e, it->actor, added); break;
        case 0xca: AddRisingMotes(e, it->actor, rand_, added); break;
        case 0xe4: AddPulsingMotes(e, it->actor, rand_, added); break;
        case 0xcb: AddPoppingMotes(e, it->actor, 1, rand_, added); break;
        case 0xfe:
          // effect\mon_금강불괴.wav (006182e8) plays on the first frame.
          AddPoppingMotes(e, it->actor, 4, rand_, added);
          break;
        case 0xf2: AddSwellingSquare(e, it->actor, "effect\\aaa copy.bmp", rand_, added); break;
        case 0xe3:
          // effect\EF_Bash.wav plays on the first frame, effect\rog_intimidate.wav at 35.
          for (std::int32_t n = 0; n < 20; ++n) AddBurstSquares(e, it->actor, rand_, added);
          break;
        case 0x106:
        case 0x10a:
          for (std::int32_t n = 0; n < (e.id == 0x106 ? 20 : 24); ++n)
            AddRays(e, it->actor, "effect\\lens1.tga", e.id == 0x106 ? 0 : 1, false, rand_, added);
          break;
        case 0x114:
          for (std::int32_t n = 0; n < 20; ++n)
            AddRays(e, it->actor, "effect\\lens2.tga", 2, true, rand_, added);
          break;
        case 0x129: AddFireworks(e, player_, rand_, added); break;
        case 300:
          AddBeams(e, it->actor, 30, added);
          AddBeams(e, it->actor, 60, added);
          break;
        case 0x12f:
          AddSlashLines(e, it->actor, false, added);
          AddSlashLines(e, it->actor, true, added);
          break;
        case 0x148: AddAsuraStrike(e, it->actor, added); break;
        case 7:
          AddHealingColumn(e, it->actor, minimal_, Sprite(kParticleSprite), rand_, added);
          break;
        case 0xe: AddBlueColumns(e, it->actor, Sprite(kEffectSprites[0]), rand_, added); break;
        case 0xf: AddSoulStrike(e, Sprite(kParticleSprite), added); break;
        case 0x47: AddSpiralSpark(e, Sprite(kParticleSprite), added); break;
        case 0x48: AddWideningSpirals(e, it->actor, Sprite(kParticleSprite), added); break;
        case 100: AddRemoveTrap(e, Sprite(kParticleSprite), rand_, added); break;
        case 0x56: ended = !AddAspersio(e, Sprite(kParticleSprite), rand_, added); break;
        case 0x75: AddWaterBall(e, Sprite(kEffectSprites[6]), added); break;
        case 0xd8:
        case 0xd9: AddTripleShot(e, Sprite(kParticleSprite), e.id == 0xd9, added); break;
        case 0xa2:
          AddSnowfall(e, player_, scene.attr, Sprite(kEffectSprites[1]), rand_, added);
          break;
        case 0xa3:
        case 0x14d: {
          const bool maple = e.id == 0x14d;
          AddFallingLeaves(e, it->actor, Sprite(kEffectSprites[maple ? 3 : 2]), maple, rand_,
                           added);
          break;
        }
        case 0x10b:
          AddTargetSprite(e, it->actor, Sprite(kEffectSprites[4]), scene.view_yaw, added);
          break;
        case 0x12e: AddDemonstration(e, it->actor, Sprite(kEffectSprites[5]), added); break;
        case 0xe2:
          AddGrandCross(e, it->actor, added);
          AddBox(e, it->actor, added);
          break;
        case 0xe0: AddCloudRings(e, it->actor, added); break;
        case 0x13d: AddOpeningPortal(e, it->actor, rand_, added); break;
        case 0x13e: AddBoxWalls(e, it->actor, "effect\\ring_red.tga", 0, rand_, added); break;
        case 0x13f: AddBoxWalls(e, it->actor, "effect\\alpha_down.tga", 1, rand_, added); break;
        case 0x133:
        case 0x142:
        case 0x143:
        case 0x144: {
          const std::int8_t variant = e.id == 0x133   ? 0
                                      : e.id == 0x142 ? 1
                                      : e.id == 0x143 ? 2
                                                      : 10;
          AddPentagonBand(e, it->actor, "effect\\cloud11.tga", variant, added);
          break;
        }
        case 0x149: AddArcBands(e, it->actor, rand_, added); break;
        case 0x14a: AddBouncingBands(e, it->actor, rand_, added); break;
        case 0xf8:
          AddFadingRings(e, it->actor, "effect\\ring_yellow.tga", added);
          // AdvanceStr, which answers false without a .str (InitEffect loads none for
          // 0xf8): the effect ends before its frame counts on.
          ended = true;
          break;
        default: break;
      }
      for (EffectPrim& p : added) {
        Object o;
        o.prim = std::make_unique<EffectPrim>(std::move(p));
        objects_.push_back(std::move(o));
      }
      if (!ended) ++e.frame;
      more = !ended && e.frame < e.duration;
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
