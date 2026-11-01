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
  const res::AttrMap* attr = nullptr;                   // CWorld+0x40
  const res::Act* act = nullptr;                        // the actor's body, CRenderObject+0x9c
  float camera_yaw = 0.0f;                              // CGameMode+0xb8 (the CView) +0x20
  std::int32_t service_type = 0;                        // g_serviceType (0067c9d4)
  std::vector<ModeMessage>* messages = nullptr;         // what goes to the game mode, when kept
  std::vector<std::int32_t>* closed_windows = nullptr;  // UIWindowMgr 004a90c0's, when kept
  std::int32_t* mode_field_0x2a0 = nullptr;  // CGameMode+0x2a0, which a player standing clears
};

// The CPc fields a player's states read and write beyond a CGameActor's.
struct PlayerActor : GameActor {
  std::int32_t field_0x218 = 0;  // +0x218: 1 or 2 keeps +0x82 when the player walks
  std::int32_t saved_frame = 0;  // +0x294: the frame standing goes back to
};

// CGameMode::GetAttrCell (00511d50): the attribute cell under world (x, z), floor(x / cell
// size + width / 2) and floor(z / cell size + height / 2), the halves C's.
std::array<std::int32_t, 2> AttrCellAt(const res::AttrMap& attr, float x, float z);

// A cell and where in it, in sixteenths of a cell.
struct CellPoint {
  std::int32_t cell_x = 0;
  std::int32_t cell_y = 0;
  std::int32_t sub_x = 0;
  std::int32_t sub_y = 0;
};

// CGameMode 00511c30: the cell under world (x, z) as AttrCellAt finds it, and the sixteenths
// of it, floor((width / 2 + x / cell size - cell) * 16) modulo 16, C's remainder.
CellPoint CellPointAt(const res::AttrMap& attr, float x, float z);

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

// CPc::SetState (00543020, vf10) for standing and walking, around CGameActor's: standing
// goes back to the saved frame, takes the ready stance (action 32) after states 2, 4 and 8
// or with +0x224 set, and clears CGameMode+0x2a0; walking clears +0xcc and, but for +0x218 1
// and 2, +0x82, closes the actor's window (message 0x53, CGameActor::SendMsg) and forgets
// the saved frame.
bool SetPlayerState(PlayerActor& player, std::int32_t state, const StateContext& context,
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

// CGameActor::SendMsg's message 0x13 (005062b3): a move from where the actor stands, at the
// server's time, to (goal_x, goal_y). A speed of 0 is 2 (0x37 with +0xd8) and pushes the
// actor; any other holds it (+0x82).
PathResult MoveGameActorTo(GameActor& actor, PathFinder& finder, std::int32_t goal_x,
                           std::int32_t goal_y, std::int32_t speed, std::uint32_t server_time,
                           const StateContext& context, std::uint32_t now);
PathResult MoveGameActorTo(PlayerActor& player, PathFinder& finder, std::int32_t goal_x,
                           std::int32_t goal_y, std::int32_t speed, std::uint32_t server_time,
                           const StateContext& context, std::uint32_t now);

// Message 0 to a player, whose vf10 is CPc::SetState.
PathResult MoveGameActor(PlayerActor& player, PathFinder& finder, const MoveOrder& order,
                         std::uint32_t server_time, const StateContext& context, std::uint32_t now);

}  // namespace ragexe::world
