#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "res/act.h"
#include "res/gat.h"
#include "world/actor_state.h"
#include "world/actor_walk.h"
#include "world/path_finder.h"

using ragexe::res::AttrMap;
using ragexe::world::GameActor;
using ragexe::world::MovePath;
using ragexe::world::PathFinder;
using ragexe::world::ProcessWalk;
using ragexe::world::WalkResult;

namespace {

struct WalkMapVector {
  const char* name;
  int width, height;
  bool hilly;
  const char* cells;
};

struct WalkQueryVector {
  int map;
  const char* query;
  const char* result;  // as ragres find-paths prints it
};

#include "walk_vectors.inc"

struct ActorDirectionVector {
  std::int32_t job;
  std::uint32_t effect;
  std::int32_t base;
  std::uint32_t facing, yaw;
  std::int32_t direction;
};

struct ActorFrameVector {
  std::int32_t job;
  std::uint32_t effect;
  std::int32_t base;
  std::uint32_t facing, yaw, delay, repeats;
  std::int32_t play;
  std::uint32_t start, now;
  int finished, held, frame, count;
  int action_after, frame_after, finished_after;
};

#include "actor_frame_vectors.inc"

struct StateQueryVector {
  int map;  // in kWalkMaps
  const char* query;
  const char* result;
};

struct AttrCellVector {
  int width, height;
  std::uint32_t x, z;
  int cell_x, cell_z;
};

#include "state_vectors.inc"

// tools/walk_vectors.py's heights for the hilly maps.
float Height(int x, int y, int corner) {
  return static_cast<float>((x * 7 + y * 13 + corner * 5) % 11 - 5);
}

AttrMap MakeMap(const WalkMapVector& v) {
  AttrMap map;
  map.major = 1;
  map.minor = 2;
  map.width = v.width;
  map.height = v.height;
  map.cells.resize(static_cast<std::size_t>(v.width) * static_cast<std::size_t>(v.height));
  for (std::size_t k = 0; k < map.cells.size(); ++k) {
    auto& cell = map.cells[k];
    switch (v.cells[k]) {
      case '#': cell.attribute = 1; break;
      case 'o': cell.attribute = 5; break;
      case '3': cell.attribute = 3; break;
      case '6': cell.attribute = 6; break;
      default: break;
    }
    if (v.hilly) {
      const int x = static_cast<int>(k % static_cast<std::size_t>(v.width));
      const int y = static_cast<int>(k / static_cast<std::size_t>(v.width));
      for (int c = 0; c < 4; ++c) cell.heights[static_cast<std::size_t>(c)] = Height(x, y, c);
    }
  }
  return map;
}

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

std::uint64_t Fold(std::uint64_t h, std::uint32_t v) {
  for (int k = 0; k < 4; ++k) h = (h ^ ((v >> (8 * k)) & 0xff)) * 0x100000001b3ull;
  return h;
}

std::string Search(PathFinder& finder, MovePath& path, std::istringstream& fields) {
  std::uint32_t time = 0;
  std::int32_t sx = 0, sy = 0, sub_x = 0, sub_y = 0, gx = 0, gy = 0, speed = 0;
  fields >> time >> sx >> sy >> sub_x >> sub_y >> gx >> gy >> speed;
  const auto r = finder.FindPath(time, sx, sy, sub_x, sub_y, gx, gy, speed, path);
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (const auto& s : path.steps) {
    h = Fold(h, static_cast<std::uint32_t>(s.x));
    h = Fold(h, static_cast<std::uint32_t>(s.y));
    h = Fold(h, static_cast<std::uint32_t>(s.dir));
    h = Fold(h, s.time);
  }
  static constexpr const char* kNames[] = {"found", "same", "none", "overflow"};
  char line[128];
  std::snprintf(line, sizeof line, "%s - %d %08x %08x %zu %016llx", kNames[static_cast<int>(r)],
                path.field_0x10, Bits(path.start_x), Bits(path.start_y), path.steps.size(),
                static_cast<unsigned long long>(h));
  return line;
}

std::string Walk(const AttrMap& map, const MovePath& path, std::istringstream& fields) {
  if (path.steps.size() < 2) return "-";
  std::vector<std::uint32_t> v;
  for (std::string token; fields >> token;)
    v.push_back(static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
  GameActor actor;
  actor.path = path;
  actor.state = 1;
  auto& o = actor.object;
  o.position = {std::bit_cast<float>(v[0]), 0.0f, std::bit_cast<float>(v[1])};
  o.facing = std::bit_cast<float>(v[2]);
  const float yaw = std::bit_cast<float>(v[3]);
  o.delay = std::bit_cast<float>(v[4]);
  actor.travelled = std::bit_cast<float>(v[5]);
  o.base_action = static_cast<std::int32_t>(v[6]);
  actor.job = static_cast<std::int32_t>(v[7]);
  actor.effect_state = v[8];
  actor.pushed = v[9] != 0;
  ragexe::res::Act act;
  act.actions.assign(64, std::vector<ragexe::res::ActFrame>(v[10]));
  actor.hold_until = v[11];
  actor.fade_until = v[12];
  o.color = v[13];
  o.held = v[14] != 0;
  actor.server_at = v[15];
  actor.processed_at = v[16];
  std::uint64_t h = 0xcbf29ce484222325ull;
  int updates = 0;
  const char* end = "walking";
  for (std::size_t k = 17; k + 1 < v.size(); k += 2) {
    const WalkResult r = ProcessWalk(actor, map, act, yaw, v[k + 1], v[k]);
    ++updates;
    for (const std::uint32_t x :
         {r == WalkResult::kFaded ? 0u : 1u, Bits(o.position[0]), Bits(o.position[1]),
          Bits(o.position[2]), Bits(o.facing), static_cast<std::uint32_t>(o.action),
          static_cast<std::uint32_t>(o.frame), static_cast<std::uint32_t>(actor.segment),
          Bits(actor.travelled), actor.processed_at, actor.server_at, o.color, o.held ? 1u : 0u,
          actor.pushed ? 1u : 0u, r == WalkResult::kArrived ? 0u : 0xffffffffu}) {
      h = Fold(h, x);
    }
    if (r == WalkResult::kFaded) {
      end = "faded";
      break;
    }
    if (r == WalkResult::kArrived) {
      end = "arrived";
      break;
    }
  }
  char line[160];
  std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %d %d %08x %016llx", end, updates,
                Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                o.action, o.frame, Bits(actor.travelled), static_cast<unsigned long long>(h));
  return line;
}

// Replays a G query as ragres find-paths does: message 0, then the walk to standing.
std::string Go(const AttrMap& map, PathFinder& finder, MovePath& path, std::istringstream& fields) {
  std::vector<std::uint32_t> v;
  for (std::string token; fields >> token;)
    v.push_back(static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
  constexpr std::size_t kFields = 39;
  ragexe::world::GameActor actor;
  actor.path = path;
  auto& o = actor.object;
  o.position = {std::bit_cast<float>(v[0]), 0.0f, std::bit_cast<float>(v[1])};
  o.facing = std::bit_cast<float>(v[2]);
  const float yaw = std::bit_cast<float>(v[3]);
  o.delay = std::bit_cast<float>(v[4]);
  actor.travelled = std::bit_cast<float>(v[5]);
  o.base_action = static_cast<std::int32_t>(v[6]);
  actor.job = static_cast<std::int32_t>(v[7]);
  actor.effect_state = v[8];
  actor.pushed = v[9] != 0;
  ragexe::res::Act act;
  act.actions.assign(64, std::vector<ragexe::res::ActFrame>(v[10]));
  actor.hold_until = v[11];
  actor.fade_until = v[12];
  o.color = v[13];
  o.held = v[14] != 0;
  actor.server_at = v[15];
  actor.processed_at = v[16];
  actor.state = static_cast<std::int32_t>(v[17]);
  actor.speed = static_cast<std::int32_t>(v[18]);
  actor.field_0xd0 = static_cast<std::int32_t>(v[19]);
  actor.field_0xd8 = static_cast<std::int32_t>(v[20]);
  act.delays.assign(9, 4.0f);
  act.delays[0] = std::bit_cast<float>(v[22]);
  act.delays[8] = std::bit_cast<float>(v[23]);
  act.delays.resize(v[37]);
  o.delay_scale = std::bit_cast<float>(v[24]);
  actor.target_x = std::bit_cast<float>(v[25]);
  actor.target_z = std::bit_cast<float>(v[26]);
  actor.id = static_cast<std::int32_t>(v[34]);
  o.repeats = std::bit_cast<float>(v[35]);
  o.finished = v[36] != 0;
  actor.segment = static_cast<std::int32_t>(v[38]);
  const ragexe::world::MoveOrder order{v[27],
                                       static_cast<std::int32_t>(v[28]),
                                       static_cast<std::int32_t>(v[29]),
                                       static_cast<std::int32_t>(v[30]),
                                       static_cast<std::int32_t>(v[31]),
                                       static_cast<std::int32_t>(v[32]),
                                       static_cast<std::int32_t>(v[33])};
  std::vector<ragexe::world::ModeMessage> messages;
  const ragexe::world::StateContext context{&map, &act, yaw, static_cast<std::int32_t>(v[21]),
                                            &messages};
  std::uint64_t h = 0xcbf29ce484222325ull;
  const auto fold = [&h](std::uint32_t x) {
    const std::uint8_t le[4] = {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(x >> 8),
                                static_cast<std::uint8_t>(x >> 16),
                                static_cast<std::uint8_t>(x >> 24)};
    for (const std::uint8_t b : le) h = (h ^ b) * 0x100000001b3ull;
  };
  // What the move, the walk and the states write, as tools/path_oracle.py's fields().
  const auto fold_fields = [&] {
    for (const std::uint32_t x : {static_cast<std::uint32_t>(actor.state),
                                  Bits(o.position[0]),
                                  Bits(o.position[1]),
                                  Bits(o.position[2]),
                                  Bits(o.facing),
                                  static_cast<std::uint32_t>(o.base_action),
                                  static_cast<std::uint32_t>(o.action),
                                  static_cast<std::uint32_t>(o.frame),
                                  Bits(o.delay),
                                  Bits(o.repeats),
                                  Bits(o.delay_scale),
                                  static_cast<std::uint32_t>(o.play_mode),
                                  o.start_time,
                                  Bits(actor.field_0x1ac),
                                  static_cast<std::uint32_t>(actor.goal_x),
                                  static_cast<std::uint32_t>(actor.goal_y),
                                  static_cast<std::uint32_t>(actor.segment),
                                  Bits(actor.travelled),
                                  actor.processed_at,
                                  actor.server_at,
                                  o.color,
                                  actor.move_time,
                                  o.finished ? 1u : 0u,
                                  o.held ? 1u : 0u,
                                  actor.pushed ? 1u : 0u}) {
      fold(x);
    }
  };
  const auto fold_messages = [&] {
    for (const auto& m : messages) {
      for (const std::int32_t x : {m.id, m.a, m.b, m.c}) fold(static_cast<std::uint32_t>(x));
    }
    messages.clear();
  };
  ragexe::world::MoveGameActor(actor, finder, order, v[kFields], context, v[kFields + 1]);
  std::vector<std::uint32_t> steps;
  for (const auto& step : actor.path.steps) {
    steps.push_back(static_cast<std::uint32_t>(step.x));
    steps.push_back(static_cast<std::uint32_t>(step.y));
    steps.push_back(static_cast<std::uint32_t>(step.dir));
    steps.push_back(step.time);
  }
  std::uint64_t digest = 0xcbf29ce484222325ull;
  for (const std::uint32_t x : steps) digest = Fold(digest, x);
  fold(2);
  fold_fields();
  fold(static_cast<std::uint32_t>(actor.path.steps.size()));
  fold(static_cast<std::uint32_t>(digest));
  fold(static_cast<std::uint32_t>(digest >> 32));
  fold_messages();
  int updates = 0;
  const char* end = actor.state == 1 ? "walking" : "standing";
  for (std::size_t k = kFields + 2; actor.state == 1 && k + 1 < v.size(); k += 2) {
    const auto r = ragexe::world::ProcessWalk(actor, map, act, yaw, v[k + 1], v[k]);
    if (r == ragexe::world::WalkResult::kArrived)
      ragexe::world::SetGameActorState(actor, 0, context, v[k + 1]);
    ++updates;
    fold(r == ragexe::world::WalkResult::kFaded ? 0u : 1u);
    fold_fields();
    fold_messages();
    if (r == ragexe::world::WalkResult::kFaded) {
      end = "faded";
      break;
    }
    if (actor.state != 1) {
      end = "arrived";
      break;
    }
  }
  path = actor.path;
  char line[160];
  std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %d %d %d %016llx", end, updates,
                Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                o.action, o.frame, actor.state, static_cast<unsigned long long>(h));
  return line;
}

}  // namespace

TEST_CASE("Actors walk their paths as the original's CGameActor::ProcessState does", "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const WalkQueryVector& v : kWalkQueries) {
    if (v.map != current) {
      current = v.map;
      map = MakeMap(kWalkMaps[current]);
      finder = PathFinder();
      finder.SetAttrMap(&map);
      path = MovePath();
    }
    INFO(kWalkMaps[current].name << ": " << v.query);
    std::istringstream fields(v.query);
    char kind = 0;
    fields >> kind;
    const std::string got = kind == 'F' ? Search(finder, path, fields) : Walk(map, path, fields);
    CHECK(got == v.result);
  }
}

TEST_CASE("A walker's direction rounds to the nearest for players' and NPCs' standing", "[walk]") {
  GameActor actor;
  actor.object.base_action = 0;
  actor.job = 0;
  CHECK(ragexe::world::GameActorDirection(actor, -30.0f, 0.0f) == 1);
  CHECK(ragexe::world::GameActorDirection(actor, -340.0f, 0.0f) == 0);
  actor.object.base_action = 24;
  CHECK(ragexe::world::GameActorDirection(actor, -30.0f, 0.0f) == 0);
  actor.job = 7;
  actor.effect_state = 0x20;
  CHECK(ragexe::world::GameActorJob(actor) == 13);
  actor.job = 1001;
  actor.object.base_action = 0;
  CHECK(ragexe::world::GameActorDirection(actor, -30.0f, 0.0f) == 0);
}

// From the original in the emulation oracle: __ftol keeps the low dword of its 64-bit
// result, so an infinite frame count (a delay of 0) gives frame 0.
TEST_CASE("A walker with no delay stays on frame 0", "[walk]") {
  AttrMap map;
  map.width = 12;
  map.height = 10;
  map.cells.resize(120);
  PathFinder finder;
  finder.SetAttrMap(&map);
  GameActor actor;
  REQUIRE(finder.FindPath(1000, 1, 1, 8, 8, 4, 1, 150, actor.path) ==
          ragexe::world::PathResult::kFound);
  actor.object.position = {-22.5f, 0.0f, -17.5f};
  actor.object.delay = 0.0f;
  actor.object.base_action = 24;
  actor.job = 1001;
  actor.server_at = 1000;
  ragexe::res::Act act;
  act.actions.assign(64, std::vector<ragexe::res::ActFrame>(13));
  CHECK(ProcessWalk(actor, map, act, 0.0f, 1172, 1100) == WalkResult::kWalking);
  CHECK(ProcessWalk(actor, map, act, 0.0f, 1272, 1200) == WalkResult::kWalking);
  CHECK(actor.object.action == 29);
  CHECK(actor.object.frame == 0);
}

TEST_CASE("An actor's direction is CGameActor::GetDirection's", "[walk]") {
  for (const auto& v : kActorDirections) {
    INFO(v.job << " " << v.base << " " << std::hex << v.facing << " " << v.yaw);
    GameActor actor;
    actor.job = v.job;
    actor.effect_state = v.effect;
    actor.object.base_action = v.base;
    CHECK(ragexe::world::GameActorDirection(actor, std::bit_cast<float>(v.facing),
                                            std::bit_cast<float>(v.yaw)) == v.direction);
  }
}

TEST_CASE("An actor's frame steps as CRenderObject::UpdateFrame on a CGameActor", "[walk]") {
  for (const auto& v : kActorFrames) {
    INFO(v.job << " " << v.base << " " << std::hex << v.facing << " " << v.now);
    GameActor actor;
    actor.job = v.job;
    actor.effect_state = v.effect;
    auto& o = actor.object;
    o.base_action = v.base;
    o.facing = std::bit_cast<float>(v.facing);
    o.delay = std::bit_cast<float>(v.delay);
    o.repeats = std::bit_cast<float>(v.repeats);
    o.play_mode = v.play;
    o.start_time = v.start;
    o.finished = v.finished != 0;
    o.held = v.held != 0;
    o.frame = v.frame;
    ragexe::res::Act act;
    // A turn of just 337.5 rounds to direction 8, past the eight.
    act.actions.assign(static_cast<std::size_t>(v.base) + 9,
                       std::vector<ragexe::res::ActFrame>(static_cast<std::size_t>(v.count)));
    ragexe::world::UpdateGameActorFrame(actor, act, std::bit_cast<float>(v.yaw), v.now);
    CHECK(o.action == v.action_after);
    CHECK(o.frame == v.frame_after);
    CHECK(o.finished == (v.finished_after != 0));
  }
}

TEST_CASE("Actors move, walk and stand as the original's message 0 and states", "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kStateQueries) {
    if (v.map != current) {
      current = v.map;
      map = MakeMap(kWalkMaps[current]);
      finder = PathFinder();
      finder.SetAttrMap(&map);
      path = MovePath();
    }
    INFO(kWalkMaps[current].name << ": " << v.query);
    std::istringstream fields(v.query);
    char kind = 0;
    fields >> kind;
    const std::string got =
        kind == 'F' ? Search(finder, path, fields) : Go(map, finder, path, fields);
    CHECK(got == v.result);
  }
}

TEST_CASE("The cell under a point is CGameMode::GetAttrCell's", "[walk]") {
  for (const auto& v : kAttrCells) {
    INFO(v.width << "x" << v.height << " " << std::hex << v.x << " " << v.z);
    AttrMap map;
    map.width = v.width;
    map.height = v.height;
    const auto cell =
        ragexe::world::AttrCellAt(map, std::bit_cast<float>(v.x), std::bit_cast<float>(v.z));
    CHECK(cell[0] == v.cell_x);
    CHECK(cell[1] == v.cell_z);
  }
}
