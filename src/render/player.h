#pragma once

#include <array>
#include <cstdint>

#include "render/actor.h"
#include "res/act.h"
#include "res/imf.h"
#include "res/sprite.h"

namespace ragexe::render {

// A player character's layers (docs/render/player.md): the actions (CPc+0x2f8) and the
// sprite (+0x2e8) of each. A layer without both is not drawn.
enum PlayerLayerIndex : std::int32_t {
  kPlayerBody = 0,
  kPlayerHead = 1,
  kPlayerHeadgearTop = 2,  // 2 to 4: the headgear, drawn without writing depth
  kPlayerHeadgearMid = 3,
  kPlayerHeadgearBottom = 4,
  kPlayerWeapon = 5,  // 5 to 7: drawn only in the attack actions
  kPlayerWeaponGlow = 6,
  kPlayerShield = 7,
  kPlayerLayerCount = 8,
};

struct PlayerLayer {
  const res::Act* act = nullptr;
  const res::Sprite* sprite = nullptr;
};

// The head's offset from the body, from their frames' first anchors (006864f0, 006864f4).
// RenderActFrameItems keeps it between frames and between players: a head whose anchors
// do not match leaves it as it was, and the headgear adds it to their own.
struct PlayerHeadOffset {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

// CPc::RenderActFrameItems (00544be0): the layers in eight slots from 7 down to 0. Slots 7
// and 6 (with the actor turned away, slots 6 and 5, and slot 7 the shield) hold the
// .imf layer whose order at the action and frame is 1, then 0; the others hold layers in
// turn. Each layer's frame (the head's second item) is placed as DrawActorBody places an
// item, offset by the anchors, its rhw stepping nearer with each layer drawn; the quads
// come in the order drawn, each with its layer.
ActorBody DrawPlayer(const RenderObject& object,
                     const std::array<PlayerLayer, kPlayerLayerCount>& layers, const res::Imf& imf,
                     const Matrix& view, const Projection& projection, const SpriteScales& scales,
                     const std::array<float, 3>& ground_color, PlayerHeadOffset& head_offset);

// CImfRes::FindEntryWithValue (0042a220): the first layer whose order at the action and
// frame is value, or -1; the action and frame are checked against layer value's counts.
std::int32_t FindImfLayer(const res::Imf& imf, std::int32_t value, std::int32_t action,
                          std::int32_t frame);

}  // namespace ragexe::render
