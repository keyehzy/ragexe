#include "world/server_moves.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

struct ServerMoveVector {
  std::array<std::uint32_t, 13> in;
  std::array<std::uint32_t, 16> out;
};

#include "server_move_vectors.inc"

void Push(std::vector<std::uint8_t>& packet, std::uint32_t word) {
  for (int k = 0; k < 4; ++k) packet.push_back(static_cast<std::uint8_t>(word >> (8 * k)));
}

// What the original's handler leaves for a vector: the clock, the ids asked for and the
// messages sent, as tools/server_move_vectors.py records them.
std::array<std::uint32_t, 16> Run(const ServerMoveVector& v) {
  const auto& in = v.in;
  const std::uint32_t kind = in[0];
  const std::uint32_t now = in[1];
  const bool found = in[4] != 0;
  ragexe::world::ServerClock clock{.offset = in[2], .early = static_cast<std::int32_t>(in[3])};
  std::vector<std::uint8_t> packet{static_cast<std::uint8_t>(kind), 0};
  std::vector<std::uint32_t> out;
  std::vector<std::uint32_t> asked;
  ragexe::world::MoveOrder order;
  bool sent = true;
  if (kind == 0x87) {
    Push(packet, in[6]);
    for (std::size_t k = 0; k < 6; ++k) packet.push_back(static_cast<std::uint8_t>(in[7 + k]));
    std::array<std::uint8_t, 12> bytes{};
    std::copy(packet.begin(), packet.end(), bytes.begin());
    order = ragexe::world::ReceiveOwnMove(clock, bytes, now);
  } else {
    Push(packet, in[5]);
    for (std::size_t k = 0; k < 6; ++k) packet.push_back(static_cast<std::uint8_t>(in[7 + k]));
    Push(packet, in[6]);
    std::array<std::uint8_t, 16> bytes{};
    std::copy(packet.begin(), packet.end(), bytes.begin());
    const ragexe::world::ActorMove move = ragexe::world::ReceiveActorMove(clock, bytes, now);
    asked.push_back(move.id);
    order = move.order;
    sent = found;
  }
  out.push_back(clock.offset);
  out.push_back(static_cast<std::uint32_t>(clock.early));
  out.push_back(static_cast<std::uint32_t>(asked.size()));
  out.insert(out.end(), asked.begin(), asked.end());
  out.push_back(sent ? 1 : 0);
  if (sent) {
    const auto u = [](std::int32_t x) { return static_cast<std::uint32_t>(x); };
    out.insert(out.end(), {0, 0, order.time, u(order.start_x), u(order.start_y), u(order.sub_x),
                           u(order.sub_y), u(order.goal_x), u(order.goal_y), 0, 0});
  }
  std::array<std::uint32_t, 16> got{};
  std::copy(out.begin(), out.end(), got.begin());
  return got;
}

}  // namespace

TEST_CASE("The server's moves are read as the original's CGameMode handlers read them", "[world]") {
  for (const ServerMoveVector& v : kServerMoves) {
    INFO("kind " << v.in[0] << ", now " << v.in[1] << ", time " << v.in[6]);
    CHECK(Run(v) == v.out);
  }
}

TEST_CASE("The server clock is the original's GetServerTime", "[world]") {
  // 00593200: timeGetTime + (-72 - offset).
  const ragexe::world::ServerClock clock{.offset = 1000};
  CHECK(clock.Time(5000) == 3928);
  CHECK(ragexe::world::ServerClock{}.Time(10) == 0xffffffc2u);
}
