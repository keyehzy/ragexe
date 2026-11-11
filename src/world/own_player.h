#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "world/actor_state.h"
#include "world/player_look.h"

namespace ragexe::world {

// What CPlayer's reset (0054b4a0) takes of ClientState (00686670) and the globals: the player as
// the server last described it (docs/world/own.md).
struct OwnSession {
  std::int32_t cell_x = 0;  // +0x620 and +0x624, the cell it enters the map on
  std::int32_t cell_y = 0;
  std::int32_t direction = 0;              // +0x628: the facing is (direction + 4) × 45°
  std::int32_t id = 0;                     // +0xcdc
  std::int32_t speed = 0;                  // +0xd94 (00687404), milliseconds a cell
  std::int32_t job = 0;                    // +0xcec
  std::int32_t sex = 0;                    // +0x18d0, which 005a4bd0 copies to +0x5b8
  std::int32_t hair = 0;                   // +0x5f4
  std::int32_t weapon = 0;                 // +0x5f8, a kind
  std::int32_t shield = 0;                 // +0x5fc, a kind
  std::array<std::int32_t, 3> headgear{};  // +0x600, +0x604 and +0x608: the top, middle and
                                           // bottom
  std::int32_t body_color = 0;             // +0x60c, the clothes' colour
  std::int32_t hair_color = 0;             // +0x610
  std::int32_t body_state = 0;             // +0x614, the status words (docs/world/arrival.md)
  std::uint32_t health_state = 0;          // +0x618
  std::uint32_t effect_state = 0;          // +0x61c
  std::int32_t guild = 0;                  // 0067cb38 (packets 0x0150, 0x015c, 0x016c, 0x01b6)
  std::int32_t emblem = 0;                 // 0067cb3c, its emblem's version (packet 0x0150)
};

// ClientState 0059d2e0: the job vf14 is given, 7 and 14 mounted (bit 5 of +0x61c) as 13 and 21.
std::int32_t OwnDressJob(const OwnSession& session);

// What vf14 (00542c50) dresses the own player in: OwnDressJob, the sex and +0x5f4 to +0x610.
PlayerDress OwnDress(const OwnSession& session);

// CPlayer 0054cf80: for each of the player's statuses (context.own_statuses) in order, the
// effect it shows, launched on the player (LaunchGameActorEffect): 2, 0x19, 0x1f and 0x44 the
// after-images (0xa6), each that is made sent message 0x2c with 0xffff and a colour (0x82, 0x80,
// 0xa9, 0xfa); 0x56 effect 0xcb and 0x57 0xfe. The rest show none.
void LaunchOwnStatusEffects(PlayerActor& player, const StateContext& context);

// CPlayer's constructor (0054b1d0): CPc's (MakePlayerActor), then its reset (0054b4a0) at
// timeGetTime `now`:
//  - in the centre of its cell (CGameMode::GetCellCentre) on the ground, its goal (+0x308,
//    +0x30c) the attribute cell under it, facing its direction; the id, the speed, the guild and
//    its emblem;
//  - its own fields (OwnPlayer) at their first values, +0x310, +0x338 and +0x33c 1200 ms before
//    now; pickable; its point on screen (+0x90, +0x94) the renderer's origin (+0x2c, +0x30);
//  - a CPlayer, which reports neither its cell nor its walking (GameActor::reports false);
//  - CPlayer::SetState(0), the body's actions (context.act) still those CRenderObject's
//    constructor named (NPC\ and job 46's name, 00686dc8 +0xb8): the action's delay is theirs;
//  - the sex (CGameActor::SetSex); vf14 with OwnDress, the status words the session's
//    meanwhile and then back; then SetStatus (00506c80) with them, announcing nothing;
//  - message 0x6d with 0xa4 to itself (vf02), the status icons' effect (0xa4) launched, its
//    number kept (+0x3d8), and LaunchOwnStatusEffects.
// 006864f8, which nothing reads, is cleared.
PlayerActor MakeOwnPlayer(const OwnSession& session, std::array<std::int32_t, 2> screen_origin,
                          const StateContext& context, std::uint32_t now,
                          std::uint32_t server_time);

}  // namespace ragexe::world
