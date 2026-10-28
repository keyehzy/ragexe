#include "render/player.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

namespace ragexe::render {

namespace {

constexpr float kNear = 10.0f;                                    // 005e9b8c
constexpr float kPixel = std::bit_cast<float>(0x3e124925u);       // 005e9564, 1/7
constexpr float kInverse255 = std::bit_cast<float>(0x3b808081u);  // 005e88c8
constexpr float kChannel = 255.0f;                                // 005e26a8, -005e9c38
constexpr float kFlatLayers = 20.0f;                              // 005e9560
constexpr std::uint32_t kUnkeyed = 0xffff0000;
constexpr std::uint32_t kOpaque = 0x101;
constexpr std::uint32_t kTranslucent = 1;
constexpr std::int32_t kSlots = 8;

// ActFrame::GetItem (00420520): out of range, the static item at 00605b70, which shows
// image 0 at the origin.
res::ActItem ItemAt(const res::ActFrame& frame, std::size_t index) {
  if (index < frame.items.size()) return frame.items[index];
  res::ActItem item;
  item.image = 0;
  return item;
}

const res::ActFrame* FrameAt(const res::Act* act, std::int32_t action, std::int32_t frame) {
  if (act == nullptr || action < 0 || frame < 0) return nullptr;
  const auto a = static_cast<std::size_t>(action);
  const auto f = static_cast<std::size_t>(frame);
  if (a >= act->actions.size() || f >= act->actions[a].size()) return nullptr;
  return &act->actions[a][f];
}

std::int32_t FrameCount(const res::Act& act, std::int32_t action) {
  if (action < 0 || static_cast<std::size_t>(action) >= act.actions.size()) return 1;
  const std::size_t n = act.actions[static_cast<std::size_t>(action)].size();
  return n < 2 ? 1 : static_cast<std::int32_t>(n);
}

// The difference between the first anchors of two frames, when both have one and they are
// of the same kind.
bool AnchorOffset(const res::ActFrame* to, const res::ActFrame& from, std::int32_t& dx,
                  std::int32_t& dy) {
  if (to == nullptr || to->anchors.empty() || from.anchors.empty()) return false;
  const res::ActAnchor& a = to->anchors[0];
  const res::ActAnchor& b = from.anchors[0];
  if (a.kind != b.kind) return false;
  dx = a.x - b.x;
  dy = a.y - b.y;
  return true;
}

std::int32_t Ftol(float f) {
  if (std::isnan(f) || f <= -2147483904.0f || f >= 2147483648.0f)
    return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(f);
}

struct ImageSize {
  std::int32_t width = 0, height = 0, halved_x = 0, halved_y = 0;
};

bool ImageOf(const res::Sprite& sprite, std::int32_t type, std::int32_t index, ImageSize& out) {
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

bool Above(float a, float b) { return a > b; }

}  // namespace

std::int32_t FindImfLayer(const res::Imf& imf, std::int32_t value, std::int32_t action,
                          std::int32_t frame) {
  if (value < 0 || action < 0 || frame < 0) return -1;
  const auto in = [&](std::size_t layer) {
    const auto& actions = imf.layers[layer];
    return static_cast<std::size_t>(action) < actions.size() &&
           static_cast<std::size_t>(frame) < actions[static_cast<std::size_t>(action)].size();
  };
  if (static_cast<std::size_t>(value) >= imf.layers.size() || !in(static_cast<std::size_t>(value)))
    return -1;
  for (std::size_t layer = 0; layer < imf.layers.size(); ++layer) {
    if (in(layer) &&
        imf.layers[layer][static_cast<std::size_t>(action)][static_cast<std::size_t>(frame)]
                .order == value)
      return static_cast<std::int32_t>(layer);
  }
  return -1;
}

ActorBody DrawPlayer(const RenderObject& object,
                     const std::array<PlayerLayer, kPlayerLayerCount>& layers, const res::Imf& imf,
                     const Matrix& view, const Projection& projection, const SpriteScales& scales,
                     const std::array<float, 3>& ground_color, PlayerHeadOffset& head_offset) {
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
      ((projection.width_ratio * static_cast<float>(object.offset_x)) * object.scale) *
      out.pixel_scale;
  const float bias = out.pixel_scale * scales.depth_bias;
  const float alpha = static_cast<float>(object.color >> 24) * kInverse255;
  const float red = (static_cast<float>(object.color >> 16 & 0xff) * kInverse255) * ground_color[0];
  const float green =
      (static_cast<float>(object.color >> 8 & 0xff) * kInverse255) * ground_color[1];
  const float blue = (static_cast<float>(object.color & 0xff) * kInverse255) * ground_color[2];
  const auto width = static_cast<float>(projection.width);
  const auto height = static_cast<float>(projection.height);
  const float scale = object.scale;

  bool head_seen = false;
  std::int32_t body_moves = 0;
  std::int32_t drawn = 0;
  for (std::int32_t slot = kSlots - 1; slot >= 0; --slot) {
    const std::int32_t direction = object.action - object.base_action;
    std::int32_t layer = 0;
    if (direction >= 2 && direction <= 5) {
      if (slot == 7) {
        layer = 7;
      } else if (slot >= 5 && slot <= 6) {
        layer = FindImfLayer(imf, slot - 5, object.action, object.frame);
      } else {
        layer = 6 - slot;
      }
    } else if (slot >= 6) {
      layer = FindImfLayer(imf, slot - 6, object.action, object.frame);
    } else {
      layer = 7 - slot;
    }
    std::int32_t v = layer;
    if (!head_seen) head_seen = layer == 1;
    if (head_seen && layer == 0) {
      ++body_moves;
      v = 2;
    }
    if (body_moves == 1 && layer == 2) {
      body_moves = 2;
      v = 0;
    } else if (v < 0 || v > 7) {
      v = 0;
    } else if (v == 2) {
      v = 4;
    } else if (v == 4) {
      v = 2;
    }

    const PlayerLayer& l = layers[static_cast<std::size_t>(v)];
    if (l.act == nullptr || l.sprite == nullptr) continue;
    const std::int32_t action = object.action;
    const bool attacking = (action >= 0x20 && action <= 0x27) || (action >= 0x50 && action <= 0x5f);
    if (!attacking && v >= 5) continue;
    if (object.frame >= FrameCount(*l.act, action)) continue;
    const res::ActFrame* frame = FrameAt(l.act, action, object.frame);
    if (frame == nullptr) continue;
    const res::ActItem item = ItemAt(*frame, v == 1 ? 1 : 0);
    if (item.image == -1) continue;

    const std::int32_t a =
        Ftol(((static_cast<float>(item.color[3]) * kInverse255) * alpha) * -kChannel);
    const std::int32_t r =
        Ftol(((static_cast<float>(item.color[0]) * kInverse255) * red) * -kChannel);
    const std::int32_t g =
        Ftol(((static_cast<float>(item.color[1]) * kInverse255) * green) * -kChannel);
    const std::int32_t b =
        Ftol(((static_cast<float>(item.color[2]) * kInverse255) * blue) * kChannel);
    const std::uint32_t high =
        ((static_cast<std::uint32_t>(a) * 256u + static_cast<std::uint32_t>(r)) * 256u +
         static_cast<std::uint32_t>(g)) *
        256u;
    std::uint32_t color = static_cast<std::uint32_t>(b) - high;

    ImageSize image;
    if (!ImageOf(*l.sprite, item.image_type, item.image, image)) continue;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    if (v == kPlayerHead) {
      if (AnchorOffset(FrameAt(layers[kPlayerBody].act, action, object.frame), *frame, dx, dy))
        head_offset = {dx, dy};
    } else if (v >= kPlayerHeadgearTop && v <= kPlayerHeadgearBottom) {
      if (!AnchorOffset(FrameAt(layers[kPlayerHead].act, action, object.frame), *frame, dx, dy)) {
        dx = 0;
        dy = 0;
      }
      dx += head_offset.x;
      dy += head_offset.y;
    }

    const std::int32_t near_y = item.y + dy;
    const std::int32_t far_y = (image.halved_y + 1) * image.height + item.y + dy;
    const std::int32_t near_x = item.x + dx;
    const std::int32_t far_x = (image.halved_x + 1) * image.width + item.x + dx;
    const float top = (((static_cast<float>(near_y) * item.scale_y) * scale) * pixel) + sy;
    const float bottom =
        ((((static_cast<float>(far_y) * item.scale_y) * scale) * pixel) + sy) + 1.0f;
    const float near_edge = (((static_cast<float>(near_x) * item.scale_x) * scale) * pixel) + sx;
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
      const float depth = (((static_cast<float>(slot) * scales.layer_step) + (1.0f / kNear)) -
                           (scales.layer_step * kFlatLayers)) +
                          (static_cast<float>(object.depth_index) * scales.layer_step);
      q.rhw_bottom = depth;
      q.rhw_top = depth;
    } else {
      const float step = static_cast<float>(object.depth_index + drawn + 3) * scales.layer_step;
      const auto rhw = [&](float edge) {
        const float d = object.depth_up ? z - ((edge - sy) * scales.slope_up)
                                        : ((edge - sy) * scales.slope_down) + z;
        return ((1.0f / d) + step) + bias;
      };
      q.rhw_bottom = rhw(bottom);
      q.rhw_top = rhw(top);
    }
    ++drawn;
    if (object.keyed && object.key_color != kUnkeyed) color = object.key_color;
    q.top = top;
    q.left = left + offset;
    q.bottom = bottom;
    q.right = right + offset;
    q.color = color;
    q.angle = static_cast<float>(item.angle + object.angle);
    q.image_type = item.image_type;
    q.image = item.image;
    q.layer = v;
    q.flags = v >= kPlayerHeadgearTop && v <= kPlayerHeadgearBottom ? kTranslucent : kOpaque;
    out.quads.push_back(q);
  }
  return out;
}

}  // namespace ragexe::render
