#pragma once

#include <array>
#include <cstdint>

#include "world/actor_state.h"
#include "world/path_finder.h"

namespace ragexe::world {

// What CPc's vf14 (00542c50) and vf15 (00542d20) dress a player in (docs/world/look.md).
struct PlayerDress {
  std::int32_t job = 0;
  std::int32_t sex = 0;
  std::int32_t hair = 0;
  std::int32_t weapon = 0;  // vf14's a kind; vf15's an item's id in the low word and the shield's
                            // in the high word
  std::array<std::int32_t, 3> headgear{};  // the top, middle and bottom
  std::int32_t shield = 0;                 // vf14's; vf15 takes it from the weapon's word
  std::int32_t hair_color = 0;
  std::int32_t body_color = 0;
};

// The record of CPc::SendMsg's messages 3, 4, 0x79 and 0x7c: a player who comes into view
// standing. The offsets are the record's.
struct PlayerStandingArrival {
  std::int32_t id = 0;               // +0, 4's and 0x79's
  std::int32_t job = 0;              // +4
  std::int32_t sex = 0;              // +8
  std::int32_t cell_x = 0;           // +0xc
  std::int32_t cell_y = 0;           // +0x10
  std::int32_t direction = 0;        // +0x14: the facing is (direction + 4) × 45°
  std::int32_t speed = 0;            // +0x18, 4's and 0x79's
  std::int32_t field_0x1ec = 0;      // +0x1c
  std::int32_t field_0x1f0 = 0;      // +0x20
  std::int32_t hair = 0;             // +0x24, also to +0x1f4
  std::int32_t weapon = 0;           // +0x28
  std::int32_t headgear_top = 0;     // +0x2c
  std::int32_t pc_state = 0;         // +0x30: 0 standing, 1 dead, 2 sitting
  std::int32_t body_state = 0;       // +0x34, the status words
  std::uint32_t health_state = 0;    // +0x38
  std::uint32_t effect_state = 0;    // +0x3c
  std::int32_t field_0x224 = 0;      // +0x40
  std::int32_t shield = 0;           // +0x44
  std::int32_t headgear_mid = 0;     // +0x48
  std::int32_t headgear_bottom = 0;  // +0x4c
  std::int32_t hair_color = 0;       // +0x50
  std::int32_t body_color = 0;       // +0x54
  std::int32_t head = 0;             // +0x58, to +0x294
  std::int32_t guild = 0;            // +0x5c
  std::int32_t emblem = 0;           // +0x60
  std::int32_t field_0x28c = 0;      // +0x64 and +0x68, vf37's arguments
  std::int32_t field_0x290 = 0;
};

// The record of messages 5, 6, 0x7a and 0x7b: a player who comes into view walking, its move as
// message 0's.
struct PlayerWalkingArrival {
  std::int32_t id = 0;               // +0
  std::int32_t job = 0;              // +4
  std::int32_t sex = 0;              // +8
  MoveOrder order;                   // +0xc to +0x24
  std::int32_t speed = 0;            // +0x28
  std::int32_t hair = 0;             // +0x2c
  std::int32_t weapon = 0;           // +0x30
  std::int32_t headgear_top = 0;     // +0x34
  std::int32_t body_state = 0;       // +0x38, the status words
  std::uint32_t health_state = 0;    // +0x3c
  std::uint32_t effect_state = 0;    // +0x40
  std::int32_t field_0x224 = 0;      // +0x44
  std::int32_t shield = 0;           // +0x48
  std::int32_t headgear_mid = 0;     // +0x4c
  std::int32_t headgear_bottom = 0;  // +0x50
  std::int32_t hair_color = 0;       // +0x54
  std::int32_t body_color = 0;       // +0x58
  std::int32_t guild = 0;            // +0x60
  std::int32_t emblem = 0;           // +0x64
  std::int32_t field_0x28c = 0;      // +0x68 and +0x6c, vf37's arguments
  std::int32_t field_0x290 = 0;
};

// CPc::ClassifyIdPair (00546a30): the kind two weapons in the hands show, by their ids' ranges
// (0x44c to 0x47d, 0x4b0 to 0x4e1 and 0x514 to 0x545): 0x11 to 0x16 for two, 1, 2 or 6 for
// one, 0 for neither; with no first, the second alone.
std::int32_t TwoWeaponsKind(std::int32_t first, std::int32_t second);

// vf14, or with by_item vf15: the sex (CGameActor 00503020), the job (00503440) and the rest of
// the dress, then every layer named again, the .imf and the palettes.
void DressPlayer(PlayerActor& player, const PlayerDress& dress, bool by_item,
                 const StateContext& context);

// CPc::SetWeapon (vf20, 005464b0), the weapon by an item's id, of the second hand's in the high
// word: one in each hand shows their kind; one with its own sprite (ResourceManager::Exists) shows
// it; otherwise its kind, if the job holds it so. Layer 6 the kind's glow.
void SetPlayerWeapon(PlayerActor& player, std::int32_t id, const StateContext& context);

// CPc::SendMsg (00543540, vf02)'s messages 3, 4, 0x79 and 0x7c, `how` the message's next word:
// the player stands on the record's cell, facing its direction, dressed (0x79 and 0x7c by item
// ids), with its status and emblem, then stands, dies (state 3) or sits (6) by the record's
// state. 4 and 0x79 also take the id and the speed, and with `how` 2 launch effect 6 unless
// hidden from all.
void ArrivePlayer(PlayerActor& player, std::int32_t message, const PlayerStandingArrival& record,
                  std::int32_t how, const StateContext& context, std::uint32_t now);

// Messages 5, 6, 0x7a and 0x7b: the id, speed and move time, dressed (0x7a and 0x7b by item
// ids), the status and emblem, then the move from the start cell. 6 and 0x7a first put the
// player at the start and its sixteenths and stand it when no path is found.
PathResult ArriveWalkingPlayer(PlayerActor& player, PathFinder& finder, std::int32_t message,
                               const PlayerWalkingArrival& record, std::uint32_t server_time,
                               const StateContext& context, std::uint32_t now);

// The rest of CPc::SendMsg's own, with its first two words: 0x22 makes the gauge (UIPcGage) and
// fills its first bar, 0x23 closes it, 0x4d is nothing, 0x64 sets the frame while standing or
// sitting, 0x69 and 0x6a dress the player again as it is, 0x72 keeps its words. False for the
// arrivals and the messages it leaves to CGameActor::SendMsg.
bool SendPlayerMessage(PlayerActor& player, std::int32_t message, std::int32_t a, std::int32_t b,
                       const StateContext& context, std::uint32_t now);

}  // namespace ragexe::world
