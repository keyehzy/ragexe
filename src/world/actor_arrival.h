#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "world/actor_state.h"
#include "world/actor_walk.h"
#include "world/path_finder.h"

namespace ragexe::world {

// The record messages 3 and 4 carry (CGameActor::SendMsg): an actor that comes into view
// standing. The offsets are the record's; CPc reads the ones between too.
struct StandingArrival {
  std::int32_t id = 0;             // +0, message 4's
  std::int32_t job = 0;            // +4, for vf13 (which CGameActor's ignores) and the
                                   // emblem of job 0x2d2
  std::int32_t job_detail = 0;     // +8, vf13's second argument
  std::int32_t cell_x = 0;         // +0xc
  std::int32_t cell_y = 0;         // +0x10
  std::int32_t direction = 0;      // +0x14: the facing is (direction + 4) × 45°
  std::int32_t speed = 0;          // +0x18, message 4's
  std::int32_t field_0x1ec = 0;    // +0x1c
  std::int32_t field_0x1f0 = 0;    // +0x20
  std::int32_t field_0x1f4 = 0;    // +0x24
  std::int32_t body_state = 0;     // +0x34, the status words
  std::uint32_t health_state = 0;  // +0x38
  std::uint32_t effect_state = 0;  // +0x3c
  std::int32_t field_0x224 = 0;    // +0x40, message 4's
  std::int32_t field_0x44 = 0;     // +0x44, the emblem of job 0x2d2
  std::int32_t field_0x48 = 0;     // +0x48, the high word of that job's guild
  std::int32_t field_0x4c = 0;     // +0x4c, its low word
  std::int32_t guild = 0;          // +0x5c, message 3's; message 4's for job 0x2d2
  std::int32_t emblem = 0;         // +0x60
  std::int32_t field_0x64 = 0;     // +0x64, vf37's arguments, message 4's
  std::int32_t field_0x68 = 0;     // +0x68
};

// The record messages 5 and 6 carry: an actor that comes into view walking, its move as
// message 0's.
struct WalkingArrival {
  std::int32_t id = 0;             // +0, message 6's
  std::int32_t job = 0;            // +4, for vf13
  std::int32_t job_detail = 0;     // +8
  MoveOrder order;                 // +0xc to +0x24: the move time, the start, its sixteenths
                                   // and the goal
  std::int32_t speed = 0;          // +0x28
  std::int32_t field_0x1f4 = 0;    // +0x2c
  std::int32_t body_state = 0;     // +0x38, the status words
  std::uint32_t health_state = 0;  // +0x3c
  std::uint32_t effect_state = 0;  // +0x40
  std::int32_t field_0x224 = 0;    // +0x44
  std::int32_t guild = 0;          // +0x60
  std::int32_t emblem = 0;         // +0x64
  std::int32_t field_0x68 = 0;     // +0x68, vf37's arguments
  std::int32_t field_0x6c = 0;     // +0x6c
};

// ClientState 00597950 asked about 7: whether the player sees hidden actors, on the Korean
// service (0) for the ids 0x186b4, 0x2933c9 and 0x326b70.
bool SeesHiddenActors(std::int32_t service_type, std::int32_t own_id);

// ClientState::GetEmblemFileName (0059c9f0): "<server>_<guild>_<version>.ebm" for a version
// above 0, otherwise "..\data\texture\effect\flagGraffiti<guild>.ebm".
std::string EmblemFileName(std::string_view server_name, std::int32_t guild, std::int32_t version);

// CGameActor::vf38 (00504390): for a guild above 0 with an emblem above 0 (or any, for job
// 0x2d2), and the actor's job below 0x16, 0x505 to 0x507 or 0x2d2 (GetJob's): the guild and
// the emblem kept (+0x23c, +0x240), and the game mode told (0x8b, the guild) when the emblem's
// file is not there.
void SetGameActorEmblem(GameActor& actor, std::int32_t guild, std::int32_t emblem,
                        const StateContext& context);

// CGameActor 00506c80 with its fourth argument 0, as messages 3 to 6 call it, at timeGetTime
// `now`: the three status words (+0x218, +0x21c, +0x220) and what changing them shows
// (docs/world/arrival.md): the actor hidden or shown, its tint, the messages it sends itself
// and the sounds it plays. The messages make and end its effects (SendGameActorEffectMessage).
void SetGameActorStatus(GameActor& actor, std::int32_t body_state, std::uint32_t effect_state,
                        std::uint32_t health_state, const StateContext& context, std::uint32_t now);

// CGameActor::SendMsg's messages 3 (0050597b) and 4 (00505a33), `how` the message's next
// argument: the actor stands on the record's cell, facing its direction, with its status and
// emblem. False for message 4 with `how` 2 and bit 6 of the effect state clear, which then
// launches effect 6 (not ported).
bool ArriveGameActor(GameActor& actor, std::int32_t message, StandingArrival record,
                     std::int32_t how, const StateContext& context, std::uint32_t now);

// Messages 5 (00505d11) and 6 (00505bc9): the record's status and emblem, then its move,
// FindMovePath from the start cell at the record's speed. 6 first puts the actor at the start
// and its sixteenths; 5 leaves it where it is. Found, the goal is kept and the actor walks
// (segment 0); not, 6 stands and 5 does nothing more.
PathResult ArriveWalkingGameActor(GameActor& actor, PathFinder& finder, std::int32_t message,
                                  WalkingArrival record, std::uint32_t server_time,
                                  const StateContext& context, std::uint32_t now);

}  // namespace ragexe::world
