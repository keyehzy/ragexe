#include "render/cursor.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

#include "core/angles.h"
#include "core/x87.h"

namespace ragexe::render {

namespace {

// Direct3D 7's render states, texture stage states and values these draws set.
constexpr std::uint32_t kZEnable = 7;
constexpr std::uint32_t kAlphaTestEnable = 15;
constexpr std::uint32_t kAlphaBlendEnable = 27;
constexpr std::uint32_t kColorOp = 1;
constexpr std::uint32_t kAlphaOp = 4;
constexpr std::uint32_t kMinFilter = 16;
constexpr std::uint32_t kMagFilter = 17;
constexpr std::uint32_t kDisable = 1;
constexpr std::uint32_t kModulate = 4;
constexpr std::uint32_t kPoint = 1;
constexpr std::uint32_t kLinear = 2;
constexpr std::uint32_t kTriangleStrip = 5;
constexpr std::uint32_t kTransformedFormat = 0x1c4;

constexpr float kHalfPixel = 0.5f;              // 005e25b8
constexpr std::uint32_t kNearZ = 0x358637bd;    // 1e-6
constexpr std::uint32_t kNearRhw = 0x3f7fffef;  // 0.999999
constexpr std::uint32_t kAlphaTestAbove = 0xff000000;
constexpr std::uint32_t kCursorColor = 0xfaffffff;
constexpr std::uint32_t kCursorUnit = 24;  // ms

// __ftol: truncation, and the integer indefinite outside the int range.
std::int32_t Ftol(float f) {
  if (std::isnan(f) || f <= -2147483904.0f || f >= 2147483648.0f)
    return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(f);
}

// CActRes::GetFrameCount (00421a80): 1 for an action of fewer than two frames.
std::uint32_t FrameCount(const res::Act& act, std::int32_t action) {
  if (action < 0 || static_cast<std::size_t>(action) >= act.actions.size()) return 1;
  const std::size_t n = act.actions[static_cast<std::size_t>(action)].size();
  return n < 2 ? 1 : static_cast<std::uint32_t>(n);
}

// The image an item names, as its record's width, height and halvings, or null.
struct Image {
  std::int32_t width = 0, height = 0, halved_x = 0, halved_y = 0;
};

bool ImageOf(const res::Sprite& sprite, std::int32_t type, std::int32_t index, Image& out) {
  if (index < 0) return false;
  const auto i = static_cast<std::size_t>(index);
  if (type == 0) {
    if (i >= sprite.images.size()) return false;
    const res::SpriteImage& im = sprite.images[i];
    out = {im.width, im.height, im.halved_x ? 1 : 0, im.halved_y ? 1 : 0};
    return true;
  }
  if (i >= sprite.images32.size()) return false;
  const res::SpriteImage32& im = sprite.images32[i];
  out = {im.width, im.height, im.halved_x ? 1 : 0, im.halved_y ? 1 : 0};
  return true;
}

void RenderState(ScreenSprites& out, std::uint32_t state, std::uint32_t value) {
  out.calls.push_back({SceneCall::Kind::kRenderState, -1, state, value, 0});
}

void StageState(ScreenSprites& out, std::int32_t stage, std::uint32_t state, std::uint32_t value) {
  out.calls.push_back({SceneCall::Kind::kStageState, stage, state, value, 0});
}

// An item's edge: the integer position (fild) times its scale rounded once, then the scale.
float Edge(std::int32_t position, float item_scale, float scale) {
  return core::RoundedProduct(item_scale, static_cast<double>(position)) * scale;
}

}  // namespace

void DrawActFrame(const ActFrameDraw& draw, const res::Act& act, const res::Sprite& sprite,
                  bool device_point_filter, const ActImageTextures& textures, BoundTextures& bound,
                  ScreenSprites& out) {
  RenderState(out, kAlphaTestEnable, draw.color > kAlphaTestAbove ? 1 : 0);
  RenderState(out, kAlphaBlendEnable, 1);
  RenderState(out, kZEnable, draw.z_test ? 1 : 0);
  const bool point = !device_point_filter && draw.point_filter;
  if (point) {
    StageState(out, 0, kMagFilter, kPoint);
    StageState(out, 0, kMinFilter, kPoint);
  }
  const res::ActFrame* frame = nullptr;
  if (draw.action >= 0 && static_cast<std::size_t>(draw.action) < act.actions.size()) {
    const auto& frames = act.actions[static_cast<std::size_t>(draw.action)];
    if (draw.frame >= 0 && static_cast<std::size_t>(draw.frame) < frames.size())
      frame = &frames[static_cast<std::size_t>(draw.frame)];
  }
  SpriteTextureRect rect;  // the cache's answer, kept from one 8-bit image to the next
  if (frame != nullptr) {
    for (const res::ActItem& item : frame->items) {
      Image image;
      if (!ImageOf(sprite, item.image_type, item.image, image)) continue;
      const float x0 = Edge(item.x, item.scale_x, draw.scale);
      const float y0 = Edge(item.y, item.scale_y, draw.scale);
      const float x1 =
          Edge((image.halved_x + 1) * image.width - 1 + item.x, item.scale_x, draw.scale);
      const float y1 =
          Edge((image.halved_y + 1) * image.height - 1 + item.y, item.scale_y, draw.scale);
      const ActImageTexture texture = textures(item.image_type, item.image);
      if (item.image_type == 0) rect = texture.rect;
      if (bound.stage0 != texture.texture) {
        out.calls.push_back({SceneCall::Kind::kTexture, 0, 0, texture.texture, 0});
        bound.stage0 = texture.texture;
      }
      StageState(out, 0, kAlphaOp, kModulate);
      StageState(out, 1, kColorOp, kDisable);

      std::array<ScreenVertex, 4> quad{};
      // fcomp against 0: a NaN compares equal too.
      if (draw.angle == 0.0f || std::isnan(draw.angle)) {
        const bool mirrored = (item.flags & 1) != 0;
        const float left = mirrored ? x1 : x0;
        const float right = mirrored ? x0 : x1;
        const float bx = core::RoundedSum(kHalfPixel, static_cast<double>(draw.x));
        const float by = core::RoundedSum(kHalfPixel, static_cast<double>(draw.y));
        quad[0].x = bx + left;
        quad[0].y = by + y0;
        quad[1].x = right + bx;
        quad[1].y = by + y0;
        quad[2].x = bx + left;
        quad[2].y = by + y1;
        quad[3].x = right + bx;
        quad[3].y = by + y1;
      } else {
        // The third corner is the bottom right and the fourth the bottom left, so the strip
        // folds over itself.
        const std::int32_t degrees = Ftol(draw.angle);
        const float c = core::CosDegrees(degrees);
        const float s = core::SinDegrees(degrees);
        const auto x = static_cast<float>(draw.x);
        const auto y = static_cast<float>(draw.y);
        quad[0].x = (((c * x0) - (s * y0)) + x) + kHalfPixel;
        quad[0].y = (((s * x0) + (c * y0)) + y) + kHalfPixel;
        quad[1].x = (((c * x1) - (s * y0)) + x) + kHalfPixel;
        quad[1].y = (((s * x1) + (c * y0)) + y) + kHalfPixel;
        quad[2].x = (((c * x1) - (s * y1)) + x) + kHalfPixel;
        quad[2].y = (((c * y1) + (s * x1)) + y) + kHalfPixel;
        quad[3].x = (((c * x0) - (s * y1)) + x) + kHalfPixel;
        quad[3].y = (((c * y1) + (s * x0)) + y) + kHalfPixel;
      }
      const std::array<std::array<float, 2>, 4> uv = {
          {{rect.u0, rect.v0}, {rect.u1, rect.v0}, {rect.u0, rect.v1}, {rect.u1, rect.v1}}};
      for (std::size_t k = 0; k < quad.size(); ++k) {
        quad[k].z = std::bit_cast<float>(kNearZ);
        quad[k].rhw = std::bit_cast<float>(kNearRhw);
        quad[k].diffuse = draw.color;
        quad[k].specular = 0xff000000;
        quad[k].u = uv[k][0];
        quad[k].v = uv[k][1];
      }
      out.calls.push_back({SceneCall::Kind::kDraw, -1, kTriangleStrip,
                           static_cast<std::uint32_t>(out.quads.size()), kTransformedFormat});
      out.quads.push_back(quad);
    }
  }
  if (point) {
    StageState(out, 0, kMagFilter, kLinear);
    StageState(out, 0, kMinFilter, kLinear);
  }
}

ActFrameDraw StepCursor(Cursor& cursor, const res::Act& act, std::uint32_t now, bool button_held,
                        std::int32_t mouse_x, std::int32_t mouse_y) {
  const std::uint32_t units = (now - cursor.start) / kCursorUnit;
  std::uint32_t step = 4;
  switch (cursor.action) {
    case 5: step = 20; break;
    case 7: step = 10; break;
    case 9: step = 1; break;
    case 10: step = 6; break;
    case 11: step = 3; break;
    default: break;
  }
  const std::uint32_t steps = units / step;
  const auto play_once = [&](std::int32_t count_action) {
    cursor.frame = static_cast<std::int32_t>(steps % FrameCount(act, count_action));
    if (steps / FrameCount(act, cursor.action) >= 1)
      cursor.frame = static_cast<std::int32_t>(FrameCount(act, cursor.action) - 1);
  };
  if (cursor.action == 1) {
    play_once(1);
  } else if (cursor.action == 2 || cursor.action == 9) {
    if (button_held) {
      play_once(cursor.action);
    } else {
      cursor.start = now;
      cursor.frame = 0;
    }
  } else {
    cursor.frame = static_cast<std::int32_t>(steps % FrameCount(act, cursor.action));
  }
  ActFrameDraw draw;
  draw.x = Ftol(core::RoundedSum(cursor.offset_x, static_cast<double>(mouse_x)));
  draw.y = Ftol(core::RoundedSum(cursor.offset_y, static_cast<double>(mouse_y)));
  draw.action = cursor.action;
  draw.frame = cursor.frame;
  draw.scale = 1.0f;
  draw.angle = 0.0f;
  draw.color = kCursorColor;
  draw.z_test = true;
  draw.point_filter = true;
  return draw;
}

}  // namespace ragexe::render
