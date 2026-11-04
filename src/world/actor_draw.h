#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "render/actor.h"
#include "render/camera.h"
#include "render/matrix.h"
#include "world/actor_walk.h"

namespace ragexe::world {

// The bounds an NPC's drawing hands vf05 (CGameActor::OnScreenBounds, ActorPick) without
// drawing a sprite, and the pixel scale it leaves at +0x48 (render::PixelScale).
struct NpcBounds {
  std::array<float, 4> bounds{};  // left, top, right, bottom
  float inverse_z = 0.0f;
  float screen_x = 0.0f;  // the point on screen, which vf05 passes on to vf06
  float screen_y = 0.0f;
  float pixel_scale = 0.0f;
};

// What CNpc::Render (00540610, vf03) does, in the order it does it: the bounds to vf05, the
// shadow (CRenderObject::DrawShadow, 00501550) at a scale, then the body (DrawBody, 00500f70)
// or only the point on screen (ProjectToScreen, 00500ed0).
struct NpcDrawing {
  std::optional<NpcBounds> bounds;
  std::optional<float> shadow_scale;
  bool body = false;
  bool point = false;
};

// CNpc::Render, by the job (GameActorJob, through the byte table 005408e4):
// - 45: with the body shown (+0x85), bounds 50 sprite pixels (005e99c0) about the point each
//   way, and with effects off (00687ea8) the shadow at three times (005e29d8) the scale; without
//   it the point;
// - 104 and 139: nothing;
// - 111: bounds of +0x1ec by +0x1f0 sprite pixels (each halved toward 0) above the point;
// - the others: nothing unless shown (+0x84); then the shadow at the scale when opaque (alpha
//   255), and the body when shown, the point otherwise.
// mg::CMGNpc and mg::mgType1::CMonster share it (vtables 005e8f54, 005e91d8).
NpcDrawing DrawNpc(const GameActor& actor, const render::Matrix& view,
                   const render::Projection& projection, const render::SpriteScales& scales,
                   bool effects_on);

// What CPc::Render (00544380, vf03) does, in the order it does it.
struct PcDrawing {
  std::optional<float> shadow_scale;  // the shadow (DrawShadow) at the object's scale (+0x3c)
  bool layers = false;                // the layers (CPc::RenderActFrameItems, render::DrawPlayer)
  bool special = false;  // 00545540, the Korean service's drawing for a few ids, not ported
  bool point = false;    // only the point on screen (ProjectToScreen)
};

// CPc::Render: nothing but the point unless shown (+0x84); then the shadow unless it is dead
// or sitting (+0x58 3 or 6), whatever its alpha, and with the body shown (+0x85) the layers,
// or 00545540 for IsSpecialActorIdOnKoreaService's ids; the point otherwise. Its DrawBody with
// +0x2c8 set is left out: a CPc's constructor clears +0x2c8 (005422dc), and only CGrannyPc's
// sets it.
PcDrawing DrawPc(const GameActor& actor, std::int32_t service_type);

}  // namespace ragexe::world
