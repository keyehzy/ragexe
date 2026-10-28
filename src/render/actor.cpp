#include "render/actor.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

#include "core/angles.h"

namespace ragexe::render {

namespace {

constexpr float kNear = 10.0f;                                       // 005e269c, 005e94fc
constexpr float kFar = 1500.0f;                                      // 005e2698
constexpr float kSizeScale = std::bit_cast<float>(0x3ae4b17fu);      // 005e26ac, 0.0017447917
constexpr float kSlope = std::bit_cast<float>(0x3e23d70au);          // 00605648, 0.16
constexpr float kViewDistance = 300.0f;                              // 00614880
constexpr float kLayerStep = std::bit_cast<float>(0x3716feb5u);      // 005e8ac0, 9e-6
constexpr float kDepthBias = std::bit_cast<float>(0x3816feb5u);      // 005e8abc, 3.6e-5
constexpr float kPixel = std::bit_cast<float>(0x3e124925u);          // 005e9564, 1/7
constexpr float kInverse255 = std::bit_cast<float>(0x3b808081u);     // 005e88c8
constexpr float kShadowBias = std::bit_cast<float>(0x3727c5acu);     // 005e9568, 1e-5
constexpr float kFlatLayers = 20.0f;                                 // 005e9560
constexpr std::int32_t kShadowPitch = 45;                            // 004cf9cc
constexpr float kDirectionStep = std::bit_cast<float>(0x3cb60b61u);  // 005e9518, 1/45
constexpr float kFullTurn = 360.0f;                                  // 005e2a5c
constexpr std::uint32_t kTickMantissa = 0xaaaaab;                    // 005e9000, 1/24
constexpr int kTickExponent = -28;
constexpr std::uint32_t kUnkeyed = 0xffff0000;
constexpr std::uint32_t kOpaque = 0x101;  // translucent, writing depth
constexpr std::uint32_t kTranslucent = 1;

// The image record an item names, or null.
struct ImageSize {
  std::int32_t width = 0, height = 0, halved_x = 0, halved_y = 0;
};

std::optional<ImageSize> ImageOf(const res::Sprite& sprite, std::int32_t type, std::int32_t index) {
  if (index < 0) return std::nullopt;
  const auto i = static_cast<std::size_t>(index);
  if (type == 0) {
    if (i >= sprite.images.size()) return std::nullopt;
    const res::SpriteImage& im = sprite.images[i];
    return ImageSize{im.width, im.height, im.halved_x ? 1 : 0, im.halved_y ? 1 : 0};
  }
  if (i >= sprite.images32.size()) return std::nullopt;
  const res::SpriteImage32& im = sprite.images32[i];
  return ImageSize{im.width, im.height, im.halved_x ? 1 : 0, im.halved_y ? 1 : 0};
}

// __ftol: truncation, and the integer indefinite outside the int range.
std::int32_t Ftol(float f) {
  if (std::isnan(f) || f <= -2147483904.0f || f >= 2147483648.0f)
    return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(f);
}

// GetFrameCount (00421a80): 1 for an action of fewer than two frames.
std::int32_t FrameCount(const res::Act& act, std::int32_t action) {
  if (action < 0 || static_cast<std::size_t>(action) >= act.actions.size()) return 1;
  const std::size_t n = act.actions[static_cast<std::size_t>(action)].size();
  return n < 2 ? 1 : static_cast<std::int32_t>(n);
}

// Strictly ordered: an unordered comparison fails.
bool Above(float a, float b) { return a > b; }

std::uint8_t Channel(std::uint8_t item, float object) {
  return static_cast<std::uint8_t>(
      static_cast<std::int32_t>((static_cast<float>(item) * object) * kInverse255));
}

}  // namespace

void SetSpriteScalesForSize(const Projection& projection, SpriteScales& scales) {
  scales.size = static_cast<float>(projection.height) * kSizeScale;
  scales.slope_down = kSlope / projection.height_ratio;
  scales.slope_up = kSlope / projection.height_ratio;
}

void StepSpriteScales(float pitch, float distance, const Projection& projection,
                      SpriteScales& scales) {
  const auto degrees = static_cast<std::int32_t>(-pitch);
  const float tangent = core::SinDegrees(degrees) / core::CosDegrees(degrees);
  const float cotangent = core::CosDegrees(degrees) / core::SinDegrees(degrees);
  scales.slope_down = (kSlope * tangent) / projection.height_ratio;
  scales.slope_up = (kSlope * cotangent) / projection.height_ratio;
  const float sine = core::SinDegrees(degrees) / core::SinDegrees(kShadowPitch);
  scales.layer_step = (sine * (kViewDistance / distance)) * kLayerStep;
  scales.depth_bias = (kViewDistance / distance) * kDepthBias;
}

std::int32_t ActorDirection(float facing, float camera_yaw) {
  float d = -camera_yaw - facing;
  while (d < 0.0f) d += kFullTurn;
  while (d >= kFullTurn) d -= kFullTurn;
  return Ftol(d * kDirectionStep);
}

void UpdateActorFrame(RenderObject& object, const res::Act& act, float camera_yaw,
                      std::uint32_t now) {
  object.action = object.base_action + ActorDirection(object.facing, camera_yaw);
  // The milliseconds, exact on the x87, times 1/24 rounded once to single precision.
  const std::uint64_t product = static_cast<std::uint64_t>(now - object.start_time) * kTickMantissa;
  const float ticks = std::ldexp(static_cast<float>(product), kTickExponent);
  const std::int32_t count = FrameCount(act, object.action);
  if (object.finished || object.held) {
    if (object.frame > count - 1) object.frame = count - 1;
    return;
  }
  const float f = ticks / object.delay;
  if (object.play_mode == 0) {
    object.frame = Ftol(f) % count;
  } else if (object.play_mode == 1) {
    object.frame = Ftol(f) % count;
    const float runs = f / static_cast<float>(count);
    if (!(runs < 1.0f)) {
      object.frame = count - 1;
      if (!(runs < object.repeats)) object.finished = true;
    }
  }
}

ActorBody DrawActorBody(const RenderObject& object, const res::Act& act, const res::Sprite& sprite,
                        const Matrix& view, const Projection& projection,
                        const SpriteScales& scales, const std::array<float, 3>& ground_color) {
  ActorBody out;
  const Matrix& m = view;
  const Vec3& p = object.position;
  const float x = (((m[6] * p[2]) + (m[3] * p[1])) + (p[0] * m[0])) + m[9];
  const float y = (((m[7] * p[2]) + (m[1] * p[0])) + (m[4] * p[1])) + m[10];
  float z = (((m[8] * p[2]) + (m[2] * p[0])) + (m[5] * p[1])) + m[11];
  if (!(z >= kNear)) z = kNear;
  const float inverse = 1.0f / z;
  const float sx = ((x * projection.scale_x) * inverse) + static_cast<float>(projection.center_x);
  const float sy = ((y * projection.scale_y) * inverse) + static_cast<float>(projection.center_y);
  const float pixel = (inverse * projection.scale_x) * kPixel;
  out.pixel_scale = pixel / scales.size;
  const float offset =
      ((projection.width_ratio * static_cast<float>(object.offset_x)) * out.pixel_scale) *
      object.scale;
  const float bias = out.pixel_scale * scales.depth_bias;

  if (object.action < 0 || static_cast<std::size_t>(object.action) >= act.actions.size())
    return out;
  const std::vector<res::ActFrame>& frames = act.actions[static_cast<std::size_t>(object.action)];
  if (object.frame < 0 || static_cast<std::size_t>(object.frame) >= frames.size()) return out;
  const res::ActFrame& frame = frames[static_cast<std::size_t>(object.frame)];

  const float alpha = static_cast<float>(object.color >> 24);
  const float red = static_cast<float>(object.color >> 16 & 0xff) * ground_color[0];
  const float green = static_cast<float>(object.color >> 8 & 0xff) * ground_color[1];
  const float blue = static_cast<float>(object.color & 0xff) * ground_color[2];
  const auto width = static_cast<float>(projection.width);
  const auto height = static_cast<float>(projection.height);
  const float scale = object.scale;

  for (std::size_t index = 0; index < frame.items.size(); ++index) {
    const res::ActItem& item = frame.items[index];
    const auto image = ImageOf(sprite, item.image_type, item.image);
    if (!image) continue;
    const std::int32_t far_y = (image->halved_y + 1) * image->height + item.y;
    const std::int32_t far_x = (image->halved_x + 1) * image->width + item.x;
    const float top = (((static_cast<float>(item.y) * item.scale_y) * scale) * pixel) + sy;
    const float bottom =
        ((((static_cast<float>(far_y) * item.scale_y) * scale) * pixel) + sy) + 1.0f;
    const float near_edge = (((static_cast<float>(item.x) * item.scale_x) * scale) * pixel) + sx;
    const float far_edge = (((static_cast<float>(far_x) * item.scale_x) * scale) * pixel) + sx;
    const bool mirrored = (item.flags & 1) != 0;
    const float left = mirrored ? far_edge : near_edge;
    const float right = (mirrored ? near_edge : far_edge) + 1.0f;
    const float inner = mirrored ? right : left;
    const float outer = mirrored ? left : right;
    if (!Above(height, top) || !Above(bottom, 0.0f) || !Above(width, inner) || !Above(outer, 0.0f))
      continue;

    SpriteQuad q;
    if (object.flat) {
      const float depth =
          (((static_cast<float>(static_cast<std::int32_t>(index)) * scales.layer_step) +
            (1.0f / kNear)) -
           (scales.layer_step * kFlatLayers)) +
          (static_cast<float>(object.depth_index) * scales.layer_step);
      q.rhw_bottom = depth;
      q.rhw_top = depth;
    } else {
      const float step =
          static_cast<float>(object.depth_index + static_cast<std::int32_t>(index) + 3) *
          scales.layer_step;
      const auto rhw = [&](float edge) {
        const float d = object.depth_up ? z - ((edge - sy) * scales.slope_up)
                                        : ((edge - sy) * scales.slope_down) + z;
        return ((1.0f / d) + step) + bias;
      };
      q.rhw_bottom = rhw(bottom);
      q.rhw_top = rhw(top);
    }
    if (!object.keyed || object.key_color == kUnkeyed) {
      q.color = static_cast<std::uint32_t>(Channel(item.color[3], alpha)) << 24 |
                static_cast<std::uint32_t>(Channel(item.color[0], red)) << 16 |
                static_cast<std::uint32_t>(Channel(item.color[1], green)) << 8 |
                Channel(item.color[2], blue);
    } else {
      q.color = kUnkeyed;
    }
    q.top = top;
    q.left = left + offset;
    q.bottom = bottom;
    q.right = right + offset;
    q.angle = static_cast<float>(item.angle + object.angle);
    q.image_type = item.image_type;
    q.image = item.image;
    q.flags = (q.color >> 24) >= 0xff ? kOpaque : kTranslucent;
    out.quads.push_back(q);
  }
  return out;
}

std::optional<SpriteQuad> DrawActorShadow(const RenderObject& object, float ground_height,
                                          float scale, const res::Act& shadow_act,
                                          const res::Sprite& shadow_sprite, const Matrix& view,
                                          const Projection& projection,
                                          const SpriteScales& scales) {
  if (!object.shadow || object.shadow_scale == 0.0f || scale == 0.0f) return std::nullopt;
  if (std::isnan(object.shadow_scale) || std::isnan(scale)) return std::nullopt;
  if (shadow_act.actions.empty() || shadow_act.actions[0].empty() ||
      shadow_act.actions[0][0].items.empty())
    return std::nullopt;
  const res::ActItem& item = shadow_act.actions[0][0].items[0];
  const auto image = ImageOf(shadow_sprite, item.image_type, 0);
  if (!image) return std::nullopt;

  const Matrix& m = view;
  const float px = object.position[0];
  const float pz = object.position[2];
  const float h = ground_height;
  const float x = (((h * m[3]) + (pz * m[6])) + (px * m[0])) + m[9];
  const float y = (((px * m[1]) + (h * m[4])) + (pz * m[7])) + m[10];
  float z = (((px * m[2]) + (h * m[5])) + (pz * m[8])) + m[11];
  if (!(z >= kNear)) z = kNear;
  const float inverse = 1.0f / z;
  const float sx = ((x * projection.scale_x) * inverse) + static_cast<float>(projection.center_x);
  const float sy = ((y * projection.scale_y) * inverse) + static_cast<float>(projection.center_y);
  const float pixel = (inverse * projection.scale_x) * kPixel;
  const float s = scale * object.shadow_scale;

  const std::int32_t far_y = (image->halved_y + 1) * image->height + item.y;
  const std::int32_t far_x = (image->halved_x + 1) * image->width + item.x;
  const float top = (((item.scale_y * static_cast<float>(item.y)) * s) * pixel) + sy;
  const float bottom = ((((static_cast<float>(far_y) * item.scale_y) * s) * pixel) + sy) + 1.0f;
  const float left = (((static_cast<float>(item.x) * item.scale_x) * s) * pixel) + sx;
  const float right = ((((static_cast<float>(far_x) * item.scale_x) * s) * pixel) + sx) + 1.0f;
  if (!Above(static_cast<float>(projection.height), top) || !Above(bottom, 0.0f) ||
      !Above(static_cast<float>(projection.width), left) || !Above(right, 0.0f))
    return std::nullopt;

  SpriteQuad q;
  const auto offset = static_cast<float>(object.offset_x);
  q.top = top;
  q.left = offset + left;
  q.bottom = bottom;
  q.right = offset + right;
  q.rhw_bottom = (1.0f / (z - ((bottom - sy) * scales.slope_up))) + kShadowBias;
  q.rhw_top = (1.0f / (z - ((top - sy) * scales.slope_up))) + kShadowBias;
  q.color = static_cast<std::uint32_t>(item.color[3]) << 24 |
            static_cast<std::uint32_t>(item.color[0]) << 16 |
            static_cast<std::uint32_t>(item.color[1]) << 8 | item.color[2];
  q.angle = static_cast<float>(item.angle);
  q.image_type = item.image_type;
  q.image = 0;
  q.flags = kOpaque;
  return q;
}

std::array<ScreenVertex, 4> SpriteQuadVertices(const SpriteQuad& quad,
                                               const SpriteTextureRect& rect) {
  const float range = kFar / (kFar - kNear);
  const auto depth = [&](float rhw) { return ((1.0f / rhw - kNear) * range) * rhw; };
  std::array<ScreenVertex, 4> v{};
  constexpr float kHalf = 0.5f;
  if (quad.angle == 0.0f || std::isnan(quad.angle)) {
    v[0].x = quad.left + kHalf;
    v[0].y = quad.top + kHalf;
    v[1].x = quad.right + kHalf;
    v[1].y = v[0].y;
    v[2].x = v[0].x;
    v[2].y = quad.bottom + kHalf;
    v[3].x = v[1].x;
    v[3].y = v[2].y;
  } else {
    const auto degrees = static_cast<std::int32_t>(quad.angle);
    const float c = core::CosDegrees(degrees);
    const float s = core::SinDegrees(degrees);
    const float hw = (quad.right - quad.left) * kHalf;
    const float hh = (quad.bottom - quad.top) * kHalf;
    const float cx = hw + quad.left;
    const float cy = hh + quad.top;
    const float a = -(hw * c);
    const float b = hw * c;
    const float hs = hw * s;
    const float cs = -hs;
    const float e = -(s * hh);
    const float f = s * hh;
    const float g = hh * c;
    const float hc = -g;
    v[0].x = ((a - e) + cx) + kHalf;
    v[0].y = ((hc + cs) + cy) + kHalf;
    v[1].x = ((b - e) + cx) + kHalf;
    v[1].y = ((hc + hs) + cy) + kHalf;
    v[2].x = ((a - f) + cx) + kHalf;
    v[2].y = ((g + cs) + cy) + kHalf;
    v[3].x = ((b - f) + cx) + kHalf;
    v[3].y = ((g + hs) + cy) + kHalf;
  }
  const float z_top = depth(quad.rhw_top);
  const float z_bottom = depth(quad.rhw_bottom);
  for (std::size_t i = 0; i < 4; ++i) {
    const bool top = i < 2;
    v[i].z = top ? z_top : z_bottom;
    v[i].rhw = top ? quad.rhw_top : quad.rhw_bottom;
    v[i].diffuse = quad.color;
    v[i].specular = 0xff000000;
    v[i].u = (i & 1) != 0 ? rect.u1 : rect.u0;
    v[i].v = top ? rect.v0 : rect.v1;
  }
  return v;
}

}  // namespace ragexe::render
