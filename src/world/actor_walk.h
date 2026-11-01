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
  std::int32_t state = 0;          // +0x58: 1 walking
  MovePath path;                   // +0x170
  std::uint32_t move_time = 0;     // +0x18c, where FindMovePath times the path from
  bool pushed = false;             // +0x190: moved by message 0x13 without a speed; it
                                   // neither turns nor steps its frames
  std::int32_t segment = 0;        // +0x194, the step left, -1 on the last
  float travelled = 0.0f;          // +0x198, the distance walked, which steps the frames
  std::uint32_t processed_at = 0;  // +0x19c, timeGetTime at the last update
  std::uint32_t server_at = 0;     // +0x1a0, the server's time at the last update
  std::int32_t job = 0;            // +0x1b8
  std::uint32_t hold_until = 0;    // +0x1d0: held (+0x82) until timeGetTime passes it
  std::uint32_t effect_state = 0;  // +0x21c; bit 5 mounts jobs 7 and 14
  std::uint32_t fade_until = 0;    // +0x234: fading out until timeGetTime passes it
};

// CGameActor::GetJob (00503490): the job, 7 and 14 mounted (bit 5 of +0x21c) as 13 and 21.
std::int32_t GameActorJob(const GameActor& actor);

// CGameActor::GetDirection (00502b00): which of eight directions a facing shows from the camera, as
// CGameObject::vf04 but rounded to the nearest for a player's standing, walking and sitting
// (jobs below 45, base actions 0, 8 and 16) and an NPC's standing (jobs 46 to 999, base
// action 0).
std::int32_t GameActorDirection(const GameActor& actor, float facing, float camera_yaw);

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
