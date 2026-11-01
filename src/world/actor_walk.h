#pragma once

#include <cstdint>

#include "render/actor.h"
#include "res/act.h"
#include "res/gat.h"
#include "world/path_finder.h"

namespace ragexe::world {

// The CGameActor fields the walk reads and writes (docs/world/walk.md).
struct GameActor {
  render::RenderObject object;
  std::int32_t state = 0;          // +0x58: 0 standing, 1 walking, docs/world/states.md
  std::int32_t goal_x = 0;         // +0xc0, the cell a move heads for
  std::int32_t goal_y = 0;         // +0xc4
  std::int32_t speed = 150;        // +0xc8, milliseconds a cell
  std::int32_t field_0xcc = 0;     // +0xcc
  std::int32_t field_0xd0 = 0;     // +0xd0: while set, SetState does nothing
  std::int32_t field_0xd8 = 0;     // +0xd8: while set, walking keeps the action it has
  MovePath path;                   // +0x170
  std::uint32_t move_time = 0;     // +0x18c, where FindMovePath times the path from
  bool pushed = false;             // +0x190: moved by message 0x13 without a speed; it
                                   // neither turns nor steps its frames
  std::int32_t segment = 0;        // +0x194, the step left, -1 on the last
  float travelled = 0.0f;          // +0x198, the distance walked, which steps the frames
  std::uint32_t processed_at = 0;  // +0x19c, timeGetTime at the last update
  std::uint32_t server_at = 0;     // +0x1a0, the server's time at the last update
  std::int32_t target_id = 0;      // +0x1a8, the id of the actor its action is aimed at
  float field_0x1ac = 0.0f;        // +0x1ac, -1 after vf07
  std::int32_t id = 0;             // +0x1b4, the server's id
  std::int32_t job = 0;            // +0x1b8
  std::int32_t window = 0;         // +0x1cc, a window of the actor's, closed when it walks
  std::uint32_t hold_until = 0;    // +0x1d0: held (+0x82) until timeGetTime passes it
  std::uint32_t pet_talk_at = 0;   // +0x1d4: a pet may talk 3 s after it, then it is 0
  std::int32_t field_0x1ec = 0;    // +0x1ec, from the records that bring it in
  std::int32_t field_0x1f0 = 0;    // +0x1f0
  std::int32_t field_0x1f4 = 0;    // +0x1f4
  std::int32_t body_state = 0;     // +0x218, the first status word (docs/world/arrival.md)
  std::uint32_t effect_state = 0;  // +0x21c, the second; bit 5 mounts jobs 7 and 14
  std::uint32_t health_state = 0;  // +0x220, the third
  std::int32_t field_0x224 = 0;    // +0x224 (SetField224, 00506be0)
  float target_x = 0.0f;           // +0x228, where state 4 puts the actor when it ends
  float target_z = 0.0f;           // +0x22c
  std::uint32_t fade_until = 0;    // +0x234: fading out until timeGetTime passes it
  std::int32_t guild = 0;          // +0x23c (vf39)
  std::int32_t emblem = 0;         // +0x240, the guild's emblem's version
};

// CGameActor::GetJob (00503490): the job, 7 and 14 mounted (bit 5 of +0x21c) as 13 and 21.
std::int32_t GameActorJob(const GameActor& actor);

// CGameActor::GetDirection (00502b00): which of eight directions a facing shows from the camera, as
// CGameObject::vf04 but rounded to the nearest for a player's standing, walking and sitting
// (jobs below 45, base actions 0, 8 and 16) and an NPC's standing (jobs 46 to 999, base
// action 0).
std::int32_t GameActorDirection(const GameActor& actor, float facing, float camera_yaw);

// CRenderObject::UpdateFrame (00500830, vf08) on an actor: render::StepActorFrame with the
// direction of GameActorDirection, as the actor's vtable gives vf04.
void UpdateGameActorFrame(GameActor& actor, const res::Act& act, float camera_yaw,
                          std::uint32_t now);

// The start of CGameActor::ProcessState (005038c0) in every state: the hold (+0x1d0) and the
// fade (UpdateFade, 00503830). False once the fade is over, when the update returns 0 at once.
bool BeginGameActorUpdate(GameActor& actor, std::uint32_t now);

// What CGameActor::ProcessState came to.
enum class WalkResult {
  kFaded,    // returned 0: the fade (+0x234) is over
  kWalking,  // returned 1
  kArrived,  // returned 1, at the path's end: the original calls SetState(0) to stand, which
             // the caller does
};

// CGameActor::ProcessState (005038c0) for an actor walking (state 1), at timeGetTime `now` and the
// server's time `server_time`: the hold and the fade, then the walker moved toward the
// step it heads for (MovePath::FindSegment), turned, its frame stepped by the distance
// walked (UpdateWalkFrame, 005031d0), and set on the ground.
WalkResult ProcessWalk(GameActor& actor, const res::AttrMap& attr, const res::Act& act,
                       float camera_yaw, std::uint32_t now, std::uint32_t server_time);

}  // namespace ragexe::world
