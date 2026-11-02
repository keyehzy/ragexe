#include "world/actor_draw.h"

#include <cstdint>

#include "core/x87.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kPortal = 45;
constexpr std::int32_t kRepeater = 104;
constexpr std::int32_t kStill = 139;
constexpr std::int32_t kSized = 111;
constexpr float kPortalReach = 50.0f;  // 005e99c0
constexpr float kPortalShadow = 3.0f;  // 005e29d8
constexpr std::uint32_t kOpaque = 0xff000000;

NpcBounds Place(const GameActor& actor, const render::Matrix& view,
                const render::Projection& projection, const render::SpriteScales& scales) {
  const render::ScreenPlace place = render::PlaceOnScreen(actor.object.position, view, projection);
  NpcBounds out;
  out.inverse_z = place.inverse_z;
  out.screen_x = place.x;
  out.screen_y = place.y;
  out.pixel_scale = render::PixelScale(place.inverse_z, projection, scales);
  return out;
}

}  // namespace

NpcDrawing DrawNpc(const GameActor& actor, const render::Matrix& view,
                   const render::Projection& projection, const render::SpriteScales& scales,
                   bool effects_on) {
  const render::RenderObject& o = actor.object;
  NpcDrawing out;
  switch (GameActorJob(actor)) {
    case kPortal: {
      if (!o.body_shown) {
        out.point = true;
        break;
      }
      NpcBounds b = Place(actor, view, projection, scales);
      const float across = (b.pixel_scale * projection.width_ratio) * kPortalReach;
      const float down = (b.pixel_scale * projection.height_ratio) * kPortalReach;
      b.bounds = {b.screen_x - across, b.screen_y - down, across + b.screen_x, down + b.screen_y};
      out.bounds = b;
      if (!effects_on) out.shadow_scale = o.scale * kPortalShadow;
      break;
    }
    case kRepeater:
    case kStill: break;
    case kSized: {
      NpcBounds b = Place(actor, view, projection, scales);
      const std::int32_t half_width = actor.field_0x1ec / 2;
      const std::int32_t half_height = actor.field_0x1f0 / 2;
      // The left and top take the halves as fild loads them; the right takes the width again
      // from the float fst stored it as.
      const float left = core::RoundedProduct(b.pixel_scale, half_width) * projection.width_ratio;
      const float up = core::RoundedProduct(b.pixel_scale, half_height) * projection.height_ratio;
      const float right = (static_cast<float>(half_width) * b.pixel_scale) * projection.width_ratio;
      b.bounds = {b.screen_x - left, b.screen_y - up, right + b.screen_x, b.screen_y};
      out.bounds = b;
      break;
    }
    default:
      if (!o.visible) break;
      if ((o.color & kOpaque) == kOpaque) out.shadow_scale = o.scale;
      if (o.body_shown) {
        out.body = true;
      } else {
        out.point = true;
      }
      break;
  }
  return out;
}

}  // namespace ragexe::world
