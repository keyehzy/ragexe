#include "world/actor_draw.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "render/camera.h"

using ragexe::world::DrawNpc;
using ragexe::world::GameActor;
using ragexe::world::NpcDrawing;

namespace {

struct NpcDrawVector {
  std::int32_t width, height;
  std::uint32_t view[12];
  std::uint32_t size;
  std::uint32_t position[3];
  std::int32_t job;
  std::uint32_t effect_state, scale, color;
  int visible, body_shown;
  std::int32_t field_0x1ec, field_0x1f0;
  int effects_on;
  std::uint32_t calls[10];
  int call_words;
  std::uint32_t pixel_scale;
};

#include "npc_draw_vectors.inc"

constexpr std::uint32_t kUnset = 0x11111111;

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }

// The calls as the original makes them: the bounds to vf05, the shadow, then the body or the
// point.
std::vector<std::uint32_t> Calls(const NpcDrawing& d) {
  std::vector<std::uint32_t> out;
  if (d.bounds) {
    out.push_back(5);
    for (const float f : d.bounds->bounds) out.push_back(B(f));
    out.insert(out.end(), {B(d.bounds->inverse_z), B(d.bounds->screen_x), B(d.bounds->screen_y)});
  }
  if (d.shadow_scale) out.insert(out.end(), {1, B(*d.shadow_scale)});
  if (d.body) out.push_back(2);
  if (d.point) out.push_back(3);
  return out;
}

}  // namespace

TEST_CASE("An NPC's drawing is the original's CNpc::Render", "[actor]") {
  for (const NpcDrawVector& v : kNpcDraws) {
    GameActor actor;
    actor.object.position = {F(v.position[0]), F(v.position[1]), F(v.position[2])};
    actor.job = v.job;
    actor.effect_state = v.effect_state;
    actor.object.scale = F(v.scale);
    actor.object.color = v.color;
    actor.object.visible = v.visible != 0;
    actor.object.body_shown = v.body_shown != 0;
    actor.field_0x1ec = v.field_0x1ec;
    actor.field_0x1f0 = v.field_0x1f0;
    ragexe::render::Matrix view;
    for (int k = 0; k < 12; ++k) view[k] = F(v.view[k]);
    ragexe::render::SpriteScales scales;
    scales.size = F(v.size);
    const NpcDrawing d =
        DrawNpc(actor, view, ragexe::render::SetSize(v.width, v.height), scales, v.effects_on != 0);
    INFO("job " << v.job << ", " << v.width << " x " << v.height << ", +0x1ec " << v.field_0x1ec);
    CHECK(Calls(d) == std::vector<std::uint32_t>(v.calls, v.calls + v.call_words));
    CHECK((d.bounds ? B(d.bounds->pixel_scale) : kUnset) == v.pixel_scale);
  }
}
