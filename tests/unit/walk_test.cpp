#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "res/act.h"
#include "res/gat.h"
#include "world/actor_arrival.h"
#include "world/actor_effects.h"
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

struct CellPointVector {
  int width, height;
  std::uint32_t x, z;
  int cell_x, cell_z, sub_x, sub_z;
};

#include "actor_effect_vectors.inc"
#include "arrival_vectors.inc"
#include "death_vectors.inc"
#include "enter_vectors.inc"
#include "move_to_vectors.inc"
#include "player_state_vectors.inc"
#include "stand_vectors.inc"
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
  act.actions.assign(256, std::vector<ragexe::res::ActFrame>(v[10]));
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
std::string Go(const AttrMap& map, PathFinder& finder, MovePath& path, std::istringstream& fields,
               bool player, bool to = false, bool stand = false, bool enter = false,
               bool arrive = false, bool with_effects = false, bool die = false) {
  std::vector<std::uint32_t> v;
  for (std::string token; fields >> token;)
    v.push_back(static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
  const std::size_t base_fields = player ? 44 : 39;
  constexpr std::size_t kArriveFields = 12;
  constexpr std::size_t kRecordWords = 28;
  constexpr std::size_t kEffectFields = 3;
  constexpr std::size_t kDeathFields = 6;
  const std::size_t kFields = base_fields + (to ? 1 : 0) + (stand ? 7 : 0) + (enter ? 9 : 0) +
                              (arrive ? kArriveFields + kRecordWords : 0) +
                              (with_effects ? kEffectFields : 0) + (die ? kDeathFields : 0);
  // B, X and K: EFFECT_FIELDS, after ARRIVE_FIELDS for B.
  const std::size_t effect_at =
      base_fields + (arrive ? kArriveFields : 0) + (die ? kDeathFields : 0);
  const std::size_t record_at = base_fields + kArriveFields + (with_effects ? kEffectFields : 0);
  ragexe::world::PlayerActor actor;
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
  act.actions.assign(256, std::vector<ragexe::res::ActFrame>(v[10]));
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
  if (player) {
    actor.body_state = static_cast<std::int32_t>(v[39]);
    actor.saved_frame = static_cast<std::int32_t>(v[40]);
    actor.window = static_cast<std::int32_t>(v[41]);
    actor.field_0x224 = static_cast<std::int32_t>(v[42]);
    actor.field_0xcc = static_cast<std::int32_t>(v[43]);
  }
  const ragexe::world::MoveOrder order{v[27],
                                       static_cast<std::int32_t>(v[28]),
                                       static_cast<std::int32_t>(v[29]),
                                       static_cast<std::int32_t>(v[30]),
                                       static_cast<std::int32_t>(v[31]),
                                       static_cast<std::int32_t>(v[32]),
                                       static_cast<std::int32_t>(v[33])};
  // N and Y: srand's seed, the pet and its lines, +0x1d4, +0x54 and +0x1f4.
  ragexe::core::CrtRand rand(stand ? v[base_fields] : 1);
  ragexe::world::PetTalk pet;
  if (stand) {
    pet = {static_cast<std::int32_t>(v[base_fields + 1]),
           static_cast<std::int32_t>(v[base_fields + 2]),
           static_cast<std::int32_t>(v[base_fields + 3])};
    actor.pet_talk_at = v[base_fields + 4];
    o.play_mode = static_cast<std::int32_t>(v[base_fields + 5]);
    actor.field_0x1f4 = static_cast<std::int32_t>(v[base_fields + 6]);
  }
  // E and Q: whether to set the state and which, +0xcc, +0x1a8, whether the world has that
  // actor and where, +0x54 and +0x70.
  ragexe::render::Vec3 target{};
  if (enter) {
    actor.field_0xcc = static_cast<std::int32_t>(v[base_fields + 2]);
    actor.target_id = static_cast<std::int32_t>(v[base_fields + 3]);
    target = {std::bit_cast<float>(v[base_fields + 5]), 0.0f,
              std::bit_cast<float>(v[base_fields + 6])};
    o.play_mode = static_cast<std::int32_t>(v[base_fields + 7]);
    o.start_time = v[base_fields + 8];
  }
  std::vector<ragexe::world::ModeMessage> messages;
  std::vector<std::int32_t> windows;
  std::int32_t mode_field_0x2a0 = 0x5a5a5a5a;
  ragexe::world::StateContext context{&map,
                                      &act,
                                      yaw,
                                      static_cast<std::int32_t>(v[21]),
                                      &messages,
                                      &windows,
                                      &mode_field_0x2a0,
                                      stand ? &pet : nullptr,
                                      &rand,
                                      enter && v[base_fields + 4] != 0 ? &target : nullptr};
  // A: the status words, the four flags, the guild and its emblem before; the player's id,
  // whether the emblem's file is there, the world's player and where; then the record.
  std::vector<ragexe::world::ModeMessage> actor_messages;
  std::vector<ragexe::world::Sound> sounds;
  std::vector<std::string> asked;
  std::int32_t ground_water_fade = 0x5a5a5a5a;
  ragexe::render::Vec3 player_at{};
  std::array<std::int32_t, kRecordWords> record{};
  if (arrive) {
    const std::size_t b = base_fields;
    actor.body_state = static_cast<std::int32_t>(v[b + 2]);
    actor.health_state = v[b + 3];
    o.visible = (v[b + 4] & 1) != 0;
    o.body_shown = (v[b + 4] & 2) != 0;
    o.tinted = (v[b + 4] & 4) != 0;
    o.shadow = (v[b + 4] & 8) != 0;
    actor.guild = static_cast<std::int32_t>(v[b + 5]);
    actor.emblem = static_cast<std::int32_t>(v[b + 6]);
    context.own_id = static_cast<std::int32_t>(v[b + 7]);
    const bool exists = v[b + 8] != 0;
    context.resource_exists = [&asked, exists](const std::string& name) {
      asked.push_back(name);
      return exists;
    };
    player_at = {std::bit_cast<float>(v[b + 10]), 0.0f, std::bit_cast<float>(v[b + 11])};
    context.player = v[b + 9] != 0 ? &player_at : nullptr;
    context.actor_messages = with_effects ? nullptr : &actor_messages;
    context.sounds = &sounds;
    context.ground_water_fade = &ground_water_fade;
    context.server_name = "Chaos";
    for (std::size_t k = 0; k < kRecordWords; ++k)
      record[k] = static_cast<std::int32_t>(v[record_at + k]);
  }
  // The effects before: one in each slot of `slots`' bits (one for all with `same`), each in
  // the list in that order, then `listed` more.
  ragexe::world::ActorEffects effects;
  std::size_t made_before = 0;
  if (with_effects) {
    const std::uint32_t slots = v[effect_at];
    std::int32_t shared = 0;
    for (std::size_t k = 0; k < actor.effect_slots.size(); ++k) {
      if ((slots >> k & 1) == 0) continue;
      if (v[effect_at + 2] == 0 || shared == 0) {
        effects.made.emplace_back();
        shared = static_cast<std::int32_t>(effects.made.size());
        actor.effects.push_back(shared);
      }
      actor.effect_slots[k] = shared;
    }
    for (std::uint32_t k = 0; k < v[effect_at + 1]; ++k) {
      effects.made.emplace_back();
      actor.effects.push_back(static_cast<std::int32_t>(effects.made.size()));
    }
    made_before = effects.made.size();
    context.effects = &effects;
  }
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
  const auto fold_player = [&] {
    if (!player) return;
    for (const std::int32_t x :
         {mode_field_0x2a0, actor.window, actor.saved_frame, actor.field_0xcc})
      fold(static_cast<std::uint32_t>(x));
    for (const std::int32_t x : windows) fold(static_cast<std::uint32_t>(x));
    windows.clear();
  };
  const auto fold_messages = [&] {
    for (const auto& m : messages) {
      for (const std::int32_t x : {m.id, m.a, m.b, m.c}) fold(static_cast<std::uint32_t>(x));
    }
    messages.clear();
  };
  // The slots and the list by number, the effects made and what they were sent since, as
  // tools/path_oracle.py's effects().
  const auto fold_effects = [&] {
    if (!with_effects) return;
    for (const std::int32_t number : actor.effect_slots) fold(static_cast<std::uint32_t>(number));
    fold(static_cast<std::uint32_t>(actor.effects.size()));
    fold(static_cast<std::uint32_t>(actor.effects.size()));
    for (const std::int32_t number : actor.effects) fold(static_cast<std::uint32_t>(number));
    fold(static_cast<std::uint32_t>(effects.made.size()));
    for (std::size_t k = 0; k < effects.made.size(); ++k) {
      const auto& e = effects.made[k];
      for (const std::uint32_t x :
           {k < made_before ? 2u
            : e.emotion     ? 1u
                            : 0u,
            static_cast<std::uint32_t>(e.type), e.position == &o.position ? 1u : 0u,
            static_cast<std::uint32_t>(e.actor_id), Bits(e.field_0x44)}) {
        fold(x);
      }
    }
    fold(static_cast<std::uint32_t>(6 * effects.messages.size()));
    for (const auto& m : effects.messages) {
      for (const std::int32_t x : {m.effect, m.id == 0x32 ? 1 : 0, m.id, 0, 0, 0})
        fold(static_cast<std::uint32_t>(x));
    }
    effects.messages.clear();
  };
  // D and H: the death, by vf10 (0), 0xc or 0x1c, after +0x218, +0x20c, +0x210, a sprite or
  // none and +0xcc; then the updates, as tools/path_oracle.py's die().
  if (die) {
    auto& base = static_cast<ragexe::world::GameActor&>(actor);
    const std::uint32_t how = v[base_fields];
    actor.body_state = static_cast<std::int32_t>(v[base_fields + 1]);
    actor.field_0x20c = static_cast<std::int32_t>(v[base_fields + 2]);
    actor.field_0x210 = static_cast<std::int32_t>(v[base_fields + 3]);
    actor.field_0xcc = static_cast<std::int32_t>(v[base_fields + 5]);
    std::vector<ragexe::world::Corpse> corpses;
    context.corpses = &corpses;
    context.act_name = "poring.act";
    if (v[base_fields + 4] != 0) context.sprite_name = "poring.spr";
    const std::uint32_t at = v[kFields + 1];
    if (how == 0) {
      if (player)
        ragexe::world::SetPlayerState(actor, 3, context, at);
      else
        ragexe::world::SetGameActorState(base, 3, context, at);
    } else if (how == 0xc) {
      if (player)
        ragexe::world::KillGameActorAfterQueue(actor, context, at);
      else
        ragexe::world::KillGameActorAfterQueue(base, context, at);
    } else if (player) {
      ragexe::world::KillGameActor(actor, context, at);
    } else {
      ragexe::world::KillGameActor(base, context, at);
    }
    const auto fold_name = [&](std::string_view name) {
      if (name.data() == nullptr) {
        fold(0);
        fold(0);
        return;
      }
      std::uint64_t n = 0xcbf29ce484222325ull;
      for (const char c : name) n = (n ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
      fold(static_cast<std::uint32_t>(n));
      fold(static_cast<std::uint32_t>(n >> 32));
    };
    const auto fold_died = [&] {
      fold(static_cast<std::uint32_t>(actor.id));
      fold(static_cast<std::uint32_t>(actor.field_0x210));
      fold(o.depth_up ? 1u : 0u);
      fold(static_cast<std::uint32_t>(actor.field_0xcc));
      fold(static_cast<std::uint32_t>(10 * corpses.size()));
      for (const auto& c : corpses) {
        for (const float x : c.position) fold(Bits(x));
        fold_name(c.act);
        fold_name(c.sprite);
        fold(static_cast<std::uint32_t>(c.action));
        fold(static_cast<std::uint32_t>(c.frame));
        fold(Bits(c.facing));
      }
      corpses.clear();
    };
    fold(5);
    fold_fields();
    fold_messages();
    fold_player();
    fold_effects();
    fold_died();
    const std::int32_t state = actor.state;
    int updates = 0;
    const char* end = "staying";
    for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
      const auto r = player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                            : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
      if (r == ragexe::world::StateResult::kNotPorted) {
        end = "unported";
        break;
      }
      ++updates;
      const bool over =
          r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
      fold(over ? 0u : 1u);
      fold_fields();
      fold_messages();
      fold_player();
      fold_effects();
      fold_died();
      if (over) {
        end = r == ragexe::world::StateResult::kGone ? "gone" : "faded";
        break;
      }
      if (actor.state != state) {
        end = "changed";
        break;
      }
    }
    char line[160];
    std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %d %d %u %016llx", end, updates,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                  static_cast<unsigned long long>(h));
    return line;
  }
  if (with_effects && !arrive) {
    const std::size_t count = v[kFields];
    const std::uint32_t now = v[kFields + 3 * count + 2];
    for (std::size_t k = 0; k < count; ++k) {
      const auto message = static_cast<std::int32_t>(v[kFields + 1 + 3 * k]);
      const auto arg = static_cast<std::int32_t>(v[kFields + 2 + 3 * k]);
      const auto sender = static_cast<std::int32_t>(v[kFields + 3 + 3 * k]);
      if (player)
        ragexe::world::SendGameActorEffectMessage(actor, message, sender, arg, context, now);
      else
        ragexe::world::SendGameActorEffectMessage(static_cast<ragexe::world::GameActor&>(actor),
                                                  message, sender, arg, context, now);
      fold(static_cast<std::uint32_t>(message));
      fold_fields();
      fold_messages();
      fold_player();
      fold_effects();
    }
    char line[160];
    std::snprintf(line, sizeof line, "sent %zu %08x %08x %08x %08x %d %d %u %016llx", count,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                  static_cast<unsigned long long>(h));
    return line;
  }
  if (stand) {
    int updates = 0;
    const char* end = "standing";
    for (std::size_t k = kFields; k + 1 < v.size(); k += 2) {
      const auto r =
          player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                 : ragexe::world::ProcessGameActorState(
                       static_cast<ragexe::world::GameActor&>(actor), context, v[k + 1], v[k]);
      ++updates;
      fold(r == ragexe::world::StateResult::kFaded ? 0u : 1u);
      fold_fields();
      fold_messages();
      fold_player();
      fold(actor.pet_talk_at);
      if (r == ragexe::world::StateResult::kFaded) {
        end = "faded";
        break;
      }
    }
    char line[160];
    std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %d %d %d %016llx", end, updates,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, o.frame, actor.state, static_cast<unsigned long long>(h));
    return line;
  }
  if (arrive) {
    auto& base = static_cast<ragexe::world::GameActor&>(actor);
    const auto message = static_cast<std::int32_t>(v[base_fields]);
    const auto how = static_cast<std::int32_t>(v[base_fields + 1]);
    const auto& r = record;
    bool ported = true;
    if (message == 3 || message == 4) {
      const ragexe::world::StandingArrival arrival{r[0],
                                                   r[1],
                                                   r[2],
                                                   r[3],
                                                   r[4],
                                                   r[5],
                                                   r[6],
                                                   r[7],
                                                   r[8],
                                                   r[9],
                                                   r[13],
                                                   static_cast<std::uint32_t>(r[14]),
                                                   static_cast<std::uint32_t>(r[15]),
                                                   r[16],
                                                   r[17],
                                                   r[18],
                                                   r[19],
                                                   r[23],
                                                   r[24],
                                                   r[25],
                                                   r[26]};
      ported = ragexe::world::ArriveGameActor(base, message, arrival, how, context, v[kFields + 1]);
    } else {
      const ragexe::world::WalkingArrival arrival{
          r[0],
          r[1],
          r[2],
          {static_cast<std::uint32_t>(r[3]), r[4], r[5], r[6], r[7], r[8], r[9]},
          r[10],
          r[11],
          r[14],
          static_cast<std::uint32_t>(r[15]),
          static_cast<std::uint32_t>(r[16]),
          r[17],
          r[24],
          r[25],
          r[26],
          r[27]};
      ragexe::world::ArriveWalkingGameActor(base, finder, message, arrival, v[kFields], context,
                                            v[kFields + 1]);
    }
    // What arriving writes beyond the fields, and what it sent, played and asked for, as
    // tools/path_oracle.py's arrival().
    const auto fold_name = [&](std::string_view name) {
      std::uint64_t n = 0xcbf29ce484222325ull;
      for (const char c : name) n = (n ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
      fold(static_cast<std::uint32_t>(n));
      fold(static_cast<std::uint32_t>(n >> 32));
    };
    const auto fold_arrival = [&] {
      for (const std::uint32_t x :
           {static_cast<std::uint32_t>(actor.id), static_cast<std::uint32_t>(actor.field_0x1ec),
            static_cast<std::uint32_t>(actor.field_0x1f0),
            static_cast<std::uint32_t>(actor.field_0x1f4),
            static_cast<std::uint32_t>(actor.body_state), actor.effect_state, actor.health_state,
            static_cast<std::uint32_t>(actor.field_0x224), static_cast<std::uint32_t>(actor.guild),
            static_cast<std::uint32_t>(actor.emblem), static_cast<std::uint32_t>(actor.speed),
            o.visible ? 1u : 0u, o.body_shown ? 1u : 0u, o.tinted ? 1u : 0u, o.shadow ? 1u : 0u,
            static_cast<std::uint32_t>(ground_water_fade),
            static_cast<std::uint32_t>(mode_field_0x2a0)}) {
        fold(x);
      }
      fold(static_cast<std::uint32_t>(5 * actor_messages.size()));
      for (const auto& m : actor_messages) {
        for (const std::int32_t x : {0, m.id, m.a, m.b, m.c}) fold(static_cast<std::uint32_t>(x));
      }
      fold(static_cast<std::uint32_t>(8 * sounds.size()));
      for (const auto& sound : sounds) {
        fold_name(sound.name);
        for (const std::uint32_t x : {Bits(sound.x), 0u, Bits(sound.z), 250u, 40u, Bits(1.0f)})
          fold(x);
      }
      fold(static_cast<std::uint32_t>(2 * asked.size()));
      for (const auto& name : asked) fold_name(name);
      actor_messages.clear();
      sounds.clear();
      asked.clear();
    };
    std::uint64_t digest = 0xcbf29ce484222325ull;
    for (const auto& step : actor.path.steps) {
      for (const std::uint32_t x :
           {static_cast<std::uint32_t>(step.x), static_cast<std::uint32_t>(step.y),
            static_cast<std::uint32_t>(step.dir), step.time})
        digest = Fold(digest, x);
    }
    fold(4);
    fold_fields();
    fold(static_cast<std::uint32_t>(actor.path.steps.size()));
    fold(static_cast<std::uint32_t>(digest));
    fold(static_cast<std::uint32_t>(digest >> 32));
    fold_messages();
    fold_arrival();
    fold_effects();
    const std::int32_t state = actor.state;
    int updates = 0;
    const char* end = ported ? "staying" : "unported";
    for (std::size_t k = kFields + 2; ported && k + 1 < v.size(); k += 2) {
      const auto result = ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
      if (result == ragexe::world::StateResult::kNotPorted) {
        end = "unported";
        break;
      }
      ++updates;
      fold(result == ragexe::world::StateResult::kFaded ? 0u : 1u);
      fold_fields();
      fold_messages();
      fold_arrival();
      fold_effects();
      if (result == ragexe::world::StateResult::kFaded) {
        end = "faded";
        break;
      }
      if (actor.state != state) {
        end = "changed";
        break;
      }
    }
    path = actor.path;
    char line[160];
    std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %d %d %u %016llx", end, updates,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                  static_cast<unsigned long long>(h));
    return line;
  }
  if (enter) {
    auto& base = static_cast<ragexe::world::GameActor&>(actor);
    bool ported = true;
    if (v[base_fields] != 0) {
      const auto state = static_cast<std::int32_t>(v[base_fields + 1]);
      ported = player ? ragexe::world::SetPlayerState(actor, state, context, v[kFields + 1])
                      : ragexe::world::SetGameActorState(base, state, context, v[kFields + 1]);
    }
    fold(3);
    fold_fields();
    fold_messages();
    fold_player();
    fold(static_cast<std::uint32_t>(actor.field_0xcc));
    const std::int32_t state = actor.state;
    int updates = 0;
    const char* end = ported ? "staying" : "unported";
    for (std::size_t k = kFields + 2; ported && k + 1 < v.size(); k += 2) {
      const auto r = player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                            : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
      if (r == ragexe::world::StateResult::kNotPorted) {
        end = "unported";
        break;
      }
      ++updates;
      fold(r == ragexe::world::StateResult::kFaded ? 0u : 1u);
      fold_fields();
      fold_messages();
      fold_player();
      fold(static_cast<std::uint32_t>(actor.field_0xcc));
      if (r == ragexe::world::StateResult::kFaded) {
        end = "faded";
        break;
      }
      if (actor.state != state) {
        end = "changed";
        break;
      }
    }
    char line[160];
    std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %d %d %u %016llx", end, updates,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                  static_cast<unsigned long long>(h));
    return line;
  }
  const auto to_speed = static_cast<std::int32_t>(v[kFields - 1]);
  auto& base = static_cast<ragexe::world::GameActor&>(actor);
  if (to && player)
    ragexe::world::MoveGameActorTo(actor, finder, order.goal_x, order.goal_y, to_speed, v[kFields],
                                   context, v[kFields + 1]);
  else if (to)
    ragexe::world::MoveGameActorTo(base, finder, order.goal_x, order.goal_y, to_speed, v[kFields],
                                   context, v[kFields + 1]);
  else if (player)
    ragexe::world::MoveGameActor(actor, finder, order, v[kFields], context, v[kFields + 1]);
  else
    ragexe::world::MoveGameActor(base, finder, order, v[kFields], context, v[kFields + 1]);
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
  fold_player();
  int updates = 0;
  const char* end = actor.state == 1 ? "walking" : "standing";
  for (std::size_t k = kFields + 2; actor.state == 1 && k + 1 < v.size(); k += 2) {
    const auto r = player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                          : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
    ++updates;
    fold(r == ragexe::world::StateResult::kFaded ? 0u : 1u);
    fold_fields();
    fold_messages();
    fold_player();
    if (r == ragexe::world::StateResult::kFaded) {
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
        kind == 'F' ? Search(finder, path, fields) : Go(map, finder, path, fields, kind == 'P');
    CHECK(got == v.result);
  }
}

TEST_CASE("Players move, walk and stand as the original's message 0 and CPc's states", "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kPlayerStateQueries) {
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
        kind == 'F' ? Search(finder, path, fields) : Go(map, finder, path, fields, kind == 'P');
    CHECK(got == v.result);
  }
}

TEST_CASE("Actors and players move from where they stand as the original's message 0x13",
          "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kMoveToQueries) {
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
    const std::string got = kind == 'F' ? Search(finder, path, fields)
                                        : Go(map, finder, path, fields, kind == 'U', true);
    CHECK(got == v.result);
  }
}

TEST_CASE("The cell and sixteenths under a point are CGameMode 00511c30's", "[walk]") {
  for (const auto& v : kCellPoints) {
    INFO(v.width << "x" << v.height << " " << std::hex << v.x << " " << v.z);
    AttrMap map;
    map.width = v.width;
    map.height = v.height;
    const auto p =
        ragexe::world::CellPointAt(map, std::bit_cast<float>(v.x), std::bit_cast<float>(v.z));
    CHECK(p.cell_x == v.cell_x);
    CHECK(p.cell_y == v.cell_z);
    CHECK(p.sub_x == v.sub_x);
    CHECK(p.sub_y == v.sub_z);
  }
}

TEST_CASE("Actors and players stand as the original's ProcessState", "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kStandQueries) {
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
    const std::string got = kind == 'F' ? Search(finder, path, fields)
                                        : Go(map, finder, path, fields, kind == 'Y', false, true);
    CHECK(got == v.result);
  }
}

TEST_CASE(
    "Actors and players take and keep their states as the original's SetState and ProcessState",
    "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kEnterQueries) {
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
    const std::string got = kind == 'F'
                                ? Search(finder, path, fields)
                                : Go(map, finder, path, fields, kind == 'Q', false, false, true);
    CHECK(got == v.result);
  }
}

TEST_CASE("Actors come into view as the original's messages 3 to 6 bring them", "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kArrivalQueries) {
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
        kind == 'F' ? Search(finder, path, fields)
                    : Go(map, finder, path, fields, false, false, false, false, kind == 'A');
    CHECK(got == v.result);
  }
}

TEST_CASE("Actors' effects are made and ended as the original's messages make and end them",
          "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kActorEffectQueries) {
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
    const std::string got = kind == 'F' ? Search(finder, path, fields)
                                        : Go(map, finder, path, fields, kind == 'K', false, false,
                                             false, kind == 'B', true);
    CHECK(got == v.result);
  }
}

TEST_CASE("Actors and players die as the original's SetState(3), messages 0xc and 0x1c and state 3",
          "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kDeathQueries) {
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
    const std::string got = kind == 'F' ? Search(finder, path, fields)
                                        : Go(map, finder, path, fields, kind == 'H', false, false,
                                             false, false, true, true);
    CHECK(got == v.result);
  }
}

TEST_CASE("An emblem's file is named as ClientState::GetEmblemFileName names it", "[walk]") {
  CHECK(ragexe::world::EmblemFileName("Chaos", 12, 3) == "Chaos_12_3.ebm");
  CHECK(ragexe::world::EmblemFileName("Chaos", 12, 0) ==
        "..\\data\\texture\\effect\\flagGraffiti12.ebm");
  CHECK(ragexe::world::EmblemFileName("", -1, -1) ==
        "..\\data\\texture\\effect\\flagGraffiti-1.ebm");
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
