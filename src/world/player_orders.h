#pragma once

#include <array>
#include <cstdint>

#include "world/actor_state.h"
#include "world/path_finder.h"

namespace ragexe::world {

// A message to the own player (CPlayer::SendMsg, 0054c5d0, docs/world/orders.md): its id and
// three words. Message 0's first word points at a move's order and 0x67's second at a skill's
// record, whose +4, +8 and +0xc are the skill, its range and its level.
struct OwnMessage {
  std::int32_t id = 0;
  std::int32_t a = 0;
  std::int32_t b = 0;
  std::int32_t c = 0;
  MoveOrder order{};
  std::array<std::int32_t, 3> object_skill{};
};

// CPlayer::SendMsg (0054c5d0, vf02): the messages the own player handles itself, at timeGetTime
// `now` and the server's time (docs/world/orders.md). Each is kept in context.actor_messages
// first, message 0 as 0 with 0s and its order in context.own_moves, 0x67 with its second word 0.
// Those it sends itself (0xd, 0x5b) are handled at once, in turn, and SetState's queued move
// through context.send_own. False for the others, which it leaves to CPc::SendMsg (0054ceb5).
bool SendOwnPlayerMessage(PlayerActor& player, PathFinder& finder, const OwnMessage& message,
                          const StateContext& context, std::uint32_t now,
                          std::uint32_t server_time);

}  // namespace ragexe::world
