#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/crt_rand.h"
#include "res/act.h"
#include "res/gat.h"
#include "world/actor_walk.h"
#include "world/path_finder.h"

namespace ragexe::world {

// A message an actor sends the game mode (CGameMode::SendMsg, vf05), or itself
// (CGameActor::SendMsg, vf02, from no sender): its id and three values.
struct ModeMessage {
  std::int32_t id = 0;
  std::int32_t a = 0;
  std::int32_t b = 0;
  std::int32_t c = 0;
  bool operator==(const ModeMessage&) const = default;
};

// A sound an actor plays (PlayWave, 004352a0): its file and where it is from the player, the
// height 0 and the rest 250, 40 and 1.
struct Sound {
  std::string_view name;
  float x = 0.0f;
  float z = 0.0f;
};

// An effect the game mode makes for an actor (a CMsgEffect: CGameMode::AddSpriteEffectByType,
// 005106a0, or AddEmotionEffect, 005111b0), as the actor's messages see it.
struct ActorEffect {
  bool emotion = false;   // made by AddEmotionEffect
  std::int32_t type = 0;  // AddSpriteEffectByType's type, which picks the sprite, or the emotion
  std::int32_t actor_id = 0;               // the actor's id (+0x1b4) when it was made
  const render::Vec3* position = nullptr;  // the actor's position, which it is drawn at
  float field_0x44 = 0.0f;                 // +0x44
};

// A message to an effect (its vf02): 0x32 from the actor, to follow it, or 0x35 from none, to
// end.
struct EffectMessage {
  std::int32_t effect = 0;  // its number
  std::int32_t id = 0;
  bool operator==(const EffectMessage&) const = default;
};

// The effects made for actors, which they name by number, their place in `made` from 1, and
// what they were sent, in order.
struct ActorEffects {
  std::vector<ActorEffect> made;
  std::vector<EffectMessage> messages;
};

// What a pet says when it talks (CGameActor::ProcessState, standing): ClientState's pet
// (+0x7d0) and the two lines of its tables the game mode hears, the first from 005aa3e0, the
// second from the table at +0x824 by 005a9f40's and 005a9fa0's levels.
struct PetTalk {
  std::int32_t pet_id = 0;
  std::int32_t first = 0;
  std::int32_t second = 0;
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
  const PetTalk* pet = nullptr;              // the player's pet, when there is one
  core::CrtRand* rand = nullptr;             // the C runtime's rand, which a pet's talk draws on
  const render::Vec3* target = nullptr;      // where CWorld::GetActorById finds the actor
                                             // +0x1a8 names, when it does
  std::vector<ModeMessage>* actor_messages = nullptr;  // what the actor sends itself, when kept
  std::vector<Sound>* sounds = nullptr;                // what it plays, when kept
  const render::Vec3* player = nullptr;  // CWorld+0x3c, the player, whom the sounds are heard by
  std::int32_t own_id = 0;               // ClientState+0xcdc, the player's id
  std::int32_t* ground_water_fade = nullptr;  // g_groundWaterFade (00679f9c)
  // The braces let contexts be brace-initialized short of these without
  // -Wmissing-field-initializers.
  // NOLINTBEGIN(readability-redundant-member-init)
  std::string_view server_name{};  // ClientState+0x4e4, the character server chosen at login
  std::function<bool(const std::string&)> resource_exists{};  // ResourceManager::Exists
                                                              // (004e0190); without it every
                                                              // file is there
  ActorEffects* effects{};  // the effects made; without it the messages that make and end
                            // them do nothing
  // NOLINTEND(readability-redundant-member-init)
};

// The CPc fields a player's states read and write beyond a CGameActor's.
struct PlayerActor : GameActor {
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

// CGameActor::SetState (005034e0, vf10) at timeGetTime `now`. Nothing happens in state 3 or
// with +0xd0 set, nor for 7 in state 6; leaving state 4 puts the actor on its target. Then
// the state's work: standing (0) and walking (1) their actions and messages; 5 action 0x18
// played once, clearing +0xcc and +0x82; 11 action 0x20 and 12 action 0x28 played once, 12
// reporting the cell; 6, 8, 10 and those past 13 none. Last the action's direction, the state
// and its time. States 2, 3, 4, 7 (but in state 6), 9 and 13 are not ported, and return false
// having done nothing.
bool SetGameActorState(GameActor& actor, std::int32_t state, const StateContext& context,
                       std::uint32_t now);

// CPc::SetState (00543020, vf10), around CGameActor's: standing goes back to the saved frame,
// takes the ready stance (action 32) after states 2, 4 and 8 or with +0x224 set, and clears
// CGameMode+0x2a0; walking clears +0xcc and, but for +0x218 1 and 2, +0x82, closes the
// actor's window (message 0x53, CGameActor::SendMsg) and forgets the saved frame. 6 reports
// the cell and plays action 0x10 once, finished at once, on the saved frame; 8 reports it and
// plays action 0x60 once, finished at once. The rest are CGameActor's, after leaving state 4
// puts the player on its target. States 2, 7 and 9 and CGameActor's unported are not ported.
bool SetPlayerState(PlayerActor& player, std::int32_t state, const StateContext& context,
                    std::uint32_t now);

// What an update of an actor's state came to.
enum class StateResult {
  kFaded,      // returned 0: the fade (+0x234) is over
  kDone,       // returned 1
  kNotPorted,  // a state whose update is not ported; nothing was done
};

// CGameActor::ProcessState (005038c0, vf09) in the actor's state, at timeGetTime `now` and the
// server's time. Walking is ProcessWalk, standing at the path's end. Otherwise the hold and the
// fade, then the state's work, and the times:
//  - 0: an action played once and finished stands again (SetState(0)), or the player's pet
//    (jobs 1000 to 0x57f, +0x1f4 20, its id the pet's) may talk: 3 s after +0x1d4, a
//    rand() % 100 below 25 sends the game mode 0xa1 with each of its lines, and +0x1d4 is
//    cleared. Then the frame (vf08).
//  - 2, 5, 7, 9 and 12: the frame and, the action finished, standing again; 5 and 7 clear
//    +0xcc first.
//  - 4: clears +0xcc, steps the frame, and slides toward the target (+0x228, +0x22c) by the
//    time since the last update over what is left of 200 ms from the state's start; the
//    action finished, it is on the target and stands. Set on the ground when it moved.
//  - 8: clears +0xcc, steps the frame, and faces the actor +0x1a8 names (context.target) on
//    every 34th tick since the state began.
//  - 10: clears +0xcc; 16 and 17: the frame; the rest nothing.
// States 3 and 13 are not ported.
StateResult ProcessGameActorState(GameActor& actor, const StateContext& context, std::uint32_t now,
                                  std::uint32_t server_time);

// CPc::ProcessState (00542eb0, vf09): the hold and the fade, then, without the times, 2 and 7
// the frame and, the action finished, standing again; 3 the frame, clearing +0x82 first; 6 the
// frame; 8 the frame and facing the target as CGameActor's state 8, keeping +0xcc. The rest
// are CGameActor's, standing again through CPc::SetState; 13 is not ported.
StateResult ProcessGameActorState(PlayerActor& player, const StateContext& context,
                                  std::uint32_t now, std::uint32_t server_time);

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
