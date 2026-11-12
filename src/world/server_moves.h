#pragma once

#include <array>
#include <cstdint>

#include "world/actor_state.h"

namespace ragexe::world {

// ClientState's server clock (00686670 +0x504 and +0x51c, docs/world/server.md): the server's
// time as the client reckons it from timeGetTime.
struct ServerClock {
  std::uint32_t offset = 0;  // +0x504: timeGetTime less the server's time, less 72 ms
  std::int32_t early = 0;    // +0x51c: times in a row that came over 72 ms behind the clock

  // GetServerTime (00593200): `now` less 72 ms and the offset.
  [[nodiscard]] std::uint32_t Time(std::uint32_t now) const;
  // SetServerTime (005931d0): the offset that makes the clock 72 ms behind `time` at `now`;
  // the count of early times cleared.
  void Set(std::uint32_t time, std::uint32_t now);
  // SyncServerTime (00593170), with the time a packet carries: one past where the clock will be
  // in 72 ms sets it there; one over 72 ms behind that counts, and the 4th in a row sets the clock
  // back 72 ms; any other clears the count. Unsigned, wrapping.
  void Sync(std::uint32_t time, std::uint32_t now);
};

// The two cells and the sixteenths a move packet packs into 6 bytes, 10 bits a coordinate:
// the start's x and y, the goal's, then the sixteenths' x and y, 4 bits each.
MoveOrder UnpackMoveCells(const std::array<std::uint8_t, 6>& cells, std::uint32_t time);

// Packet 0x0087, the own player's move (12 bytes): its time at +2, its cells at +6.
MoveOrder ReadOwnMove(const std::array<std::uint8_t, 12>& packet);

// Packet 0x0086, another actor's move (16 bytes): the actor's id at +2, its cells at +6, its
// time at +0xc.
struct ActorMove {
  std::uint32_t id = 0;
  MoveOrder order;
};
ActorMove ReadActorMove(const std::array<std::uint8_t, 16>& packet);

// CGameMode::HandlePacket_0087 (00526d60) at timeGetTime `now`: the clock synced with the move's
// time; the order is then message 0's to the own player (CWorld+0x3c, vf02, with no sender and
// no more words).
MoveOrder ReceiveOwnMove(ServerClock& clock, const std::array<std::uint8_t, 12>& packet,
                         std::uint32_t now);

// CGameMode::HandlePacket_0086 (00526ca0) at `now`: the clock synced with the move's time; the
// order is then message 0's to the actor with the id, if CWorld::GetActorById (004c4cf0) finds
// one.
ActorMove ReceiveActorMove(ServerClock& clock, const std::array<std::uint8_t, 16>& packet,
                           std::uint32_t now);

}  // namespace ragexe::world
