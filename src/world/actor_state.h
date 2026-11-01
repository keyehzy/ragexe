#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "res/act.h"
#include "res/gat.h"
#include "world/actor_walk.h"
#include "world/path_finder.h"

namespace ragexe::world {

// A message an actor sends the game mode (CGameMode::SendMsg, vf05): its id and three values.
struct ModeMessage {
  std::int32_t id = 0;
  std::int32_t a = 0;
  std::int32_t b = 0;
  std::int32_t c = 0;
  bool operator==(const ModeMessage&) const = default;
};

// What the actors' states read beyond the actor (docs/world/states.md).
struct StateContext {
  const res::AttrMap* attr = nullptr;            // CWorld+0x40
  const res::Act* act = nullptr;                 // the actor's body, CRenderObject+0x9c
  float camera_yaw = 0.0f;                       // CGameMode+0xb8 (the CView) +0x20
  std::int32_t service_type = 0;                 // g_serviceType (0067c9d4)
  std::vector<ModeMessage>* messages = nullptr;  // what goes to the game mode, when kept
};

// CGameMode::GetAttrCell (00511d50): the attribute cell under world (x, z), floor(x / cell
// size + width / 2) and floor(z / cell size + height / 2), the halves C's.
std::array<std::int32_t, 2> AttrCellAt(const res::AttrMap& attr, float x, float z);

// IsSpecialActorIdOnKoreaService (004d8e10): on the Korean service (0), ids 0x2c8c7d to
// 0x2c8c80 and 0x2c8c82.
bool IsSpecialActorIdOnKoreaService(std::int32_t service_type, std::int32_t id);

// CGameActor::vf07 (00502ff0): CRenderObject::SetAction, then not pushed and +0x1ac -1.
void SetGameActorAction(GameActor& actor, const res::Act& act, std::int32_t action,
                        std::int32_t play_mode);

// CGameActor::SetState (005034e0, vf10) at timeGetTime `now`, for standing (0) and walking
// (1); the original's other states are not ported, and return false having done nothing.
// Nothing happens in state 3 or with +0xd0 set; leaving state 4 puts the actor on its target.
// Then the state's action and message, and the action's direction, the state and its time.
bool SetGameActorState(GameActor& actor, std::int32_t state, const StateContext& context,
                       std::uint32_t now);

// The order message 0 carries (CGameActor::vf02): when, and from which cell (with the
// sixteenths in it) to which.
struct MoveOrder {
  std::uint32_t time = 0;
  std::int32_t start_x = 0;
  std::int32_t start_y = 0;
  std::int32_t sub_x = 0;
  std::int32_t sub_y = 0;
  std::int32_t goal_x = 0;
  std::int32_t goal_y = 0;
};

// CGameActor::vf02's message 0 (00505dd3): the move time from the order, FindMovePath at
// the actor's speed; found, the goal kept and walking (segment 0), otherwise standing.
PathResult MoveGameActor(GameActor& actor, PathFinder& finder, const MoveOrder& order,
                         std::uint32_t server_time, const StateContext& context, std::uint32_t now);

}  // namespace ragexe::world
