#include "world/server_moves.h"

#include <cstddef>

namespace ragexe::world {

namespace {

constexpr std::uint32_t kLag = 72;            // 00593214: 0xffffffb8
constexpr std::uint32_t kEarlyBy = 2 * kLag;  // 00593192: 0x90
constexpr std::int32_t kEarlyTimesToSet = 4;  // 005931ab

std::uint32_t Word(const std::uint8_t* p) {
  return std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 | std::uint32_t{p[2]} << 16 |
         std::uint32_t{p[3]} << 24;
}

}  // namespace

std::uint32_t ServerClock::Time(std::uint32_t now) const { return now - kLag - offset; }

void ServerClock::Set(std::uint32_t time, std::uint32_t now) {
  early = 0;
  offset = now - time;
}

void ServerClock::Sync(std::uint32_t time, std::uint32_t now) {
  const std::uint32_t ahead = Time(now) + kLag;
  if (ahead < time) {
    Set(time, now);
  } else if (ahead > time + kEarlyBy) {
    // 005931a2: inc, wrapping, then a signed comparison.
    early = static_cast<std::int32_t>(static_cast<std::uint32_t>(early) + 1u);
    if (early >= kEarlyTimesToSet) Set(ahead - 2 * kLag, now);
  } else {
    early = 0;
  }
}

MoveOrder UnpackMoveCells(const std::array<std::uint8_t, 6>& cells, std::uint32_t time) {
  const auto b = [&](std::size_t k) { return std::int32_t{cells[k]}; };
  return {.time = time,
          .start_x = b(0) << 2 | b(1) >> 6,
          .start_y = (b(1) & 0x3f) << 4 | b(2) >> 4,
          .sub_x = b(5) >> 4,
          .sub_y = b(5) & 0xf,
          .goal_x = (b(2) & 0xf) << 6 | b(3) >> 2,
          .goal_y = (b(3) & 3) << 8 | b(4)};
}

MoveOrder ReadOwnMove(const std::array<std::uint8_t, 12>& packet) {
  return UnpackMoveCells({packet[6], packet[7], packet[8], packet[9], packet[10], packet[11]},
                         Word(&packet[2]));
}

ActorMove ReadActorMove(const std::array<std::uint8_t, 16>& packet) {
  return {
      .id = Word(&packet[2]),
      .order = UnpackMoveCells({packet[6], packet[7], packet[8], packet[9], packet[10], packet[11]},
                               Word(&packet[12]))};
}

MoveOrder ReceiveOwnMove(ServerClock& clock, const std::array<std::uint8_t, 12>& packet,
                         std::uint32_t now) {
  const MoveOrder order = ReadOwnMove(packet);
  clock.Sync(order.time, now);
  return order;
}

ActorMove ReceiveActorMove(ServerClock& clock, const std::array<std::uint8_t, 16>& packet,
                           std::uint32_t now) {
  const ActorMove move = ReadActorMove(packet);
  clock.Sync(move.order.time, now);
  return move;
}

}  // namespace ragexe::world
