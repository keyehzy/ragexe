#include "render/effect_prim.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

#include "core/angles.h"
#include "render/effect_table.h"

namespace ragexe::render {

namespace {

constexpr std::uint32_t kNoChange = 0x38d1b717;                // 0.0001
constexpr float kNear = 10.0f;                                 // 005e9dc0
constexpr float kHalf = 0.5f;                                  // 005e25b8
constexpr float kQuadTip = std::bit_cast<float>(0x3f555555u);  // 005e9f90, 5/6
constexpr std::uint32_t kVertexZ = 0x358637bd;                 // 1e-6, at the near plane
constexpr std::uint32_t kVertexRhw = 0x3f7fffef;

std::int32_t Ftol(float f) {
  if (std::isnan(f) || f <= -2147483904.0f || f >= 2147483648.0f)
    return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(f);
}

float Sin(float degrees) { return core::SinDegrees(Ftol(degrees)); }
float Cos(float degrees) { return core::CosDegrees(Ftol(degrees)); }

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
  p.color = (static_cast<std::uint32_t>(Ftol(a)) & 0xff) << 24 |
            (static_cast<std::uint32_t>(p.red) & 0xff) << 16 |
            (static_cast<std::uint32_t>(p.green) & 0xff) << 8 |
            (static_cast<std::uint32_t>(p.blue) & 0xff);
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

}  // namespace

EffectPrim MakePrimitive(const RagEffect* owner, std::int32_t id, const Vec3& offset) {
  EffectPrim p;
  p.owner = owner;
  p.id = id;
  p.start = owner->position;
  p.position = p.start;
  for (std::size_t i = 0; i < 3; ++i) p.travel[i] = offset[i] + p.travel[i];
  return p;
}

bool UpdatePrimitive(EffectPrim& p, core::CrtRand& rand) {
  if (p.owner == nullptr) return false;
  switch (p.id) {
    case 0:
    case 2:
      if (!UpdateFlyer(p, rand)) return false;
      break;
    case 1:
      if (!UpdateOrbiter(p)) return false;
      break;
    default: return false;
  }
  ++p.age;
  return p.age <= p.life;
}

void DrawPrimitive(const EffectPrim& p, std::vector<PrimTriangle>& out) {
  if (!p.visible) return;
  switch (p.id) {
    case 0:
    case 1: DrawRay(p, out); break;
    case 2: DrawDiamond(p, out); break;
    default: break;
  }
}

void ProcessHitSpark(RagEffect& effect, const Vec3& actor, const Matrix& view,
                     const Projection& projection, core::CrtRand& rand, bool blue,
                     std::vector<EffectPrim>& added) {
  const Matrix& m = view;
  const Vec3& a = actor;
  const float x = (((m[3] * a[1]) + (m[6] * a[2])) + (a[0] * m[0])) + m[9];
  const float y = (((m[1] * a[0]) + (m[4] * a[1])) + (m[7] * a[2])) + m[10];
  float z = (((m[2] * a[0]) + (m[5] * a[1])) + (m[8] * a[2])) + m[11];
  if (!(z >= kNear)) z = kNear;
  const float inverse = 1.0f / z;
  effect.screen_x = ((inverse * x) * projection.scale_x) + static_cast<float>(projection.origin_x);
  effect.screen_y = ((y * projection.scale_y) * inverse) + static_cast<float>(projection.origin_y);
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

RagEffect& EffectWorld::Launch(std::int32_t id, const Vec3& actor) {
  Object o;
  o.effect = std::make_unique<RagEffect>();
  o.effect->id = id;
  o.effect->position = actor;
  o.effect->duration = EffectDuration(id);
  o.actor = actor;
  objects_.push_back(std::move(o));
  return *objects_.back().effect;
}

void EffectWorld::Step(const Matrix& view, const Projection& projection) {
  for (auto it = objects_.begin(); it != objects_.end();) {
    bool more = false;
    if (it->effect) {
      RagEffect& e = *it->effect;
      std::vector<EffectPrim> added;
      if (e.id == 1) ProcessHitSpark(e, it->actor, view, projection, rand_, false, added);
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
      more = UpdatePrimitive(*it->prim, rand_);
    }
    it = more ? std::next(it) : objects_.erase(it);
  }
}

std::vector<PrimTriangle> EffectWorld::Draw() const {
  std::vector<PrimTriangle> out;
  for (const Object& o : objects_)
    if (o.prim) DrawPrimitive(*o.prim, out);
  return out;
}

}  // namespace ragexe::render
