#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "res/act.h"
#include "res/gat.h"
#include "res/item_tables.h"
#include "world/actor_arrival.h"
#include "world/actor_attack.h"
#include "world/actor_effects.h"
#include "world/actor_hit.h"
#include "world/actor_state.h"
#include "world/actor_update.h"
#include "world/actor_walk.h"
#include "world/actor_window.h"
#include "world/ground_item.h"
#include "world/own_player.h"
#include "world/path_finder.h"
#include "world/player_look.h"
#include "world/player_orders.h"
#include "world/player_pursuit.h"
#include "world/world_objects.h"

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

struct WeaponItemVector {
  int job, sex;
  std::uint32_t word;  // the item's id, the second hand's in the high word
  int exists;          // whether the item's own sprite is there
  std::uint32_t weapon;
  // FNV-1a of layer 5's actions and sprite, then layer 6's.
  std::uint64_t act5, sprite5, act6, sprite6;
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

struct WeaponKindVector {
  int id, kind;
};

struct HitSoundVector {
  int player;  // 1 ClientState 00599a00 by the job, 0 00599df0 by the weapon's kind
  int value;
  std::uint32_t seed;
  const char* name;
};

struct LaunchVector {
  int id;
  int effects_on;
  int made;
  int field_0xa8;
};

struct AttackChoiceVector {
  int job, field_0x1bc, weapon;
  int second;
  std::uint32_t frame, second_frame;
};

#include "actor_effect_vectors.inc"
#include "arrival_vectors.inc"
#include "attack_vectors.inc"
#include "cast_vectors.inc"
#include "death_vectors.inc"
#include "enter_vectors.inc"
#include "item_vectors.inc"
#include "land_vectors.inc"
#include "list_vectors.inc"
#include "look_vectors.inc"
#include "move_to_vectors.inc"
#include "own_orders_vectors.inc"
#include "own_update_vectors.inc"
#include "player_state_vectors.inc"
#include "stand_vectors.inc"
#include "state_vectors.inc"
#include "update_vectors.inc"

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

// What made an effect, as tools/path_oracle.py names it: 0 AddSpriteEffectByType, 1
// AddEmotionEffect, 3 ShowNumberEffect, 4 AddMsgSpriteEffect (2 is an effect made before).
std::uint32_t MakerCode(ragexe::world::EffectMaker maker) {
  switch (maker) {
    case ragexe::world::EffectMaker::kSprite: return 0;
    case ragexe::world::EffectMaker::kEmotion: return 1;
    case ragexe::world::EffectMaker::kNumber: return 3;
    case ragexe::world::EffectMaker::kMsgSprite: return 4;
  }
  return 0;
}

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
               bool arrive = false, bool with_effects = false, bool die = false,
               bool attack = false, bool land = false, bool cast = false, bool update = false,
               bool own = false, bool orders = false, bool look = false) {
  std::vector<std::uint32_t> v;
  for (std::string token; fields >> token;)
    v.push_back(static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
  const std::size_t base_fields = player ? 44 : 39;
  constexpr std::size_t kArriveFields = 12;
  constexpr std::size_t kRecordWords = 28;
  constexpr std::size_t kEffectFields = 3;
  constexpr std::size_t kDeathFields = 6;
  constexpr std::size_t kAttackFields = 17 + 10;  // and the record
  constexpr std::size_t kLandFields = 27;         // then 10 words a hit
  constexpr std::size_t kCastFields = 21;
  constexpr std::size_t kUpdateFields = 30;
  constexpr std::size_t kOwnFields = 32;
  constexpr std::size_t kOrderFields = 7;
  constexpr std::size_t kLookFields = 21;
  const std::size_t hit_words = land ? 10 * std::size_t{v[base_fields + kLandFields - 1]} : 0;
  const std::size_t kFields = base_fields + (to ? 1 : 0) + (stand ? 7 : 0) + (enter ? 9 : 0) +
                              (arrive ? kArriveFields + kRecordWords : 0) +
                              (with_effects ? kEffectFields : 0) + (die ? kDeathFields : 0) +
                              (attack ? kAttackFields : 0) + (land ? kLandFields + hit_words : 0) +
                              (cast ? kCastFields : 0) + (update ? kUpdateFields : 0) +
                              (own ? kOwnFields : 0) + (orders ? kOrderFields : 0) +
                              (look ? kArriveFields + kLookFields : 0);
  // B, X and K: EFFECT_FIELDS, after ARRIVE_FIELDS for B.
  const std::size_t effect_at =
      base_fields + (arrive || look ? kArriveFields : 0) + (die ? kDeathFields : 0);
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
    actor.cast_gauge = static_cast<std::int32_t>(v[41]);
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
  if (arrive || look) {
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
    context.actor_messages = with_effects && !look ? nullptr : &actor_messages;
    context.sounds = &sounds;
    context.ground_water_fade = &ground_water_fade;
    context.server_name = "Chaos";
    for (std::size_t k = 0; arrive && k < kRecordWords; ++k)
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
         {mode_field_0x2a0, actor.cast_gauge, actor.saved_frame, actor.field_0xcc})
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
           {k < made_before ? 2u : MakerCode(e.maker), static_cast<std::uint32_t>(e.type),
            e.position == &o.position ? 1u : 0u, static_cast<std::uint32_t>(e.actor_id),
            Bits(e.field_0x44)}) {
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
  // V and J: by `how`, vf10's state, message 0xb with the record or 0x1b with it as a hit; the
  // actor the world finds and the player, each another or the actor itself; the act's frames'
  // events and the hits queued before; then the updates, as tools/path_oracle.py's attack().
  if (attack) {
    auto& base = static_cast<ragexe::world::GameActor&>(actor);
    const std::size_t b = base_fields;
    const std::uint32_t how = v[b];
    const std::uint32_t enter_state = v[b + 1];
    actor.field_0xcc = static_cast<std::int32_t>(v[b + 2]);
    actor.target_id = static_cast<std::int32_t>(v[b + 3]);
    ragexe::world::GameActor foe;
    foe.object.position = {std::bit_cast<float>(v[b + 5]), 0.0f, std::bit_cast<float>(v[b + 6])};
    context.own_id = static_cast<std::int32_t>(v[b + 7]);
    ragexe::world::GameActor pc;
    pc.object.position = {std::bit_cast<float>(v[b + 9]), 0.0f, std::bit_cast<float>(v[b + 10])};
    actor.field_0x1bc = static_cast<std::int32_t>(v[b + 11]);
    actor.weapon = static_cast<std::int32_t>(v[b + 12]);
    const std::uint32_t events = v[b + 13];
    o.play_mode = static_cast<std::int32_t>(v[b + 14]);
    o.start_time = v[b + 15];
    if (!player) actor.body_state = 0;
    // Each frame's event by 3 bits of `events`, the first 10 frames: 1 to 4 the act's four,
    // 5 one past them, 6 0x7fffffff, the rest none.
    act.events = {"atk", "atk2", "at", ""};
    for (auto& frames : act.actions) {
      for (std::size_t k = 0; k < frames.size() && k < 10; ++k) {
        const std::uint32_t code = events >> (3 * k) & 7;
        frames[k].event = code == 0 || code == 7 ? -1
                          : code == 5            ? 4
                          : code == 6            ? 0x7fffffff
                                                 : static_cast<std::int32_t>(code) - 1;
      }
    }
    const auto hit_of = [](const std::uint32_t* w) {
      return ragexe::world::QueuedHit{static_cast<std::int32_t>(w[0]),
                                      static_cast<std::int32_t>(w[1]),
                                      w[2],
                                      static_cast<std::int32_t>(w[3]),
                                      static_cast<std::int32_t>(w[4]),
                                      static_cast<std::int32_t>(w[5]),
                                      static_cast<std::int32_t>(w[6]),
                                      static_cast<std::int32_t>(w[7]),
                                      static_cast<std::int32_t>(w[8]),
                                      std::bit_cast<float>(w[9])};
    };
    for (ragexe::world::GameActor* at : {&base, &foe, &pc}) {
      for (std::uint32_t k = 0; k < v[b + 16]; ++k) {
        std::uint32_t w[10];
        for (std::uint32_t j = 0; j < 10; ++j) w[j] = (k + 1) << 16 | j;
        at->hits.push_back(hit_of(w));
      }
    }
    const std::uint32_t found = v[b + 4];
    const std::uint32_t player_is = v[b + 8];
    context.target_actor = found == 1 ? &foe : found == 2 ? &base : nullptr;
    context.player_actor = player_is == 1 ? &pc : player_is == 2 ? &base : nullptr;
    context.target =
        context.target_actor != nullptr ? &context.target_actor->object.position : nullptr;
    context.player =
        context.player_actor != nullptr ? &context.player_actor->object.position : nullptr;
    std::vector<ragexe::world::Arrow> arrows;
    std::vector<std::int32_t> launched;
    context.arrows = &arrows;
    context.launched = &launched;
    const std::uint32_t* record = &v[b + 17];
    const std::uint32_t at = v[kFields + 1];
    std::uint32_t logged = 0;
    if (how == 0) {
      if (player)
        ragexe::world::SetPlayerState(actor, static_cast<std::int32_t>(enter_state), context, at);
      else
        ragexe::world::SetGameActorState(base, static_cast<std::int32_t>(enter_state), context, at);
    } else if (how == 0x1b) {
      ragexe::world::QueueGameActorHit(base, hit_of(record),
                                       static_cast<std::int32_t>(enter_state));
    } else {
      const ragexe::world::TargetAction action{
          static_cast<std::int32_t>(record[0]), static_cast<std::int32_t>(record[1]),
          static_cast<std::int32_t>(record[2]), static_cast<std::int32_t>(record[3]),
          static_cast<std::int32_t>(record[5]), static_cast<std::int32_t>(record[6]),
          static_cast<std::int32_t>(record[7]), static_cast<std::int32_t>(record[8]),
          static_cast<std::int32_t>(record[9])};
      const bool known = player ? ragexe::world::ActOnTarget(actor, action, context, at)
                                : ragexe::world::ActOnTarget(base, action, context, at);
      if (!known) logged = 1;
    }
    // +0x1a8, +0xcc, and what was logged, launched and shot since, and the three queues, as
    // tools/path_oracle.py's attacked().
    const auto fold_attacked = [&] {
      fold(static_cast<std::uint32_t>(actor.target_id));
      fold(static_cast<std::uint32_t>(actor.field_0xcc));
      fold(logged);
      logged = 0;
      fold(static_cast<std::uint32_t>(launched.size()));
      for (const std::int32_t id : launched) fold(static_cast<std::uint32_t>(id));
      launched.clear();
      fold(static_cast<std::uint32_t>(3 * arrows.size()));
      for (const auto& arrow : arrows) {
        fold(arrow.from == &o.position ? 1u : 0u);
        fold(static_cast<std::uint32_t>(arrow.target_id));
        fold(Bits(arrow.speed));
      }
      arrows.clear();
      for (const ragexe::world::GameActor* queue : {&base, &foe, &pc}) {
        fold(static_cast<std::uint32_t>(queue->hits.size()));
        for (const auto& hit : queue->hits) {
          for (const std::uint32_t x :
               {static_cast<std::uint32_t>(hit.message), static_cast<std::uint32_t>(hit.damage),
                hit.time, static_cast<std::uint32_t>(hit.attacker_id),
                static_cast<std::uint32_t>(hit.cell_x), static_cast<std::uint32_t>(hit.cell_y),
                static_cast<std::uint32_t>(hit.field_0x18),
                static_cast<std::uint32_t>(hit.field_0x1c),
                static_cast<std::uint32_t>(hit.field_0x20), Bits(hit.field_0x24)}) {
            fold(x);
          }
        }
      }
    };
    fold(6);
    fold_fields();
    fold_messages();
    fold_player();
    fold_attacked();
    const std::int32_t state = actor.state;
    int updates = 0;
    const char* end = "staying";
    for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
      const auto r = player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                            : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
      ++updates;
      const bool over =
          r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
      fold(over ? 0u : 1u);
      fold_fields();
      fold_messages();
      fold_player();
      fold_attacked();
      if (over) {
        end = "faded";
        break;
      }
      if (actor.state != state) {
        end = "changed";
        break;
      }
    }
    char line[160];
    std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %d %u %u %016llx", end, updates,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, static_cast<std::uint32_t>(o.frame),
                  static_cast<std::uint32_t>(actor.state), static_cast<unsigned long long>(h));
    return line;
  }
  // C and R: the message (0x52, 0x53, 0x21 or 0x36) and the state; +0xcc, the windows and
  // their times, the point on screen, +0x48 and the screen's size; then the updates, each
  // 00503210 then ProcessState, as tools/path_oracle.py's cast().
  if (cast) {
    auto& base = static_cast<ragexe::world::GameActor&>(actor);
    const std::size_t b = base_fields;
    const std::uint32_t how = v[b];
    const auto duration = static_cast<std::int32_t>(v[b + 1]);
    const std::uint32_t enter_state = v[b + 2];
    actor.field_0xcc = static_cast<std::int32_t>(v[b + 3]);
    ragexe::world::ActorWindows actor_windows;
    for (std::int32_t* window :
         {&actor.balloon, &actor.chat_title, &actor.shop_title, &actor.cast_gauge}) {
      const auto k = static_cast<std::uint32_t>(window - &actor.balloon);
      if ((v[b + 4] >> k & 1) == 0) continue;
      actor_windows.sizes.push_back(k == 0   ? std::array{static_cast<std::int32_t>(v[b + 5]),
                                                          static_cast<std::int32_t>(v[b + 6])}
                                    : k == 3 ? std::array{0x3c, 6}
                                             : std::array{0x8c, 0x22});
      *window = static_cast<std::int32_t>(actor_windows.sizes.size());
    }
    actor.balloon_width = static_cast<std::int32_t>(v[b + 7]);
    actor.balloon_height = static_cast<std::int32_t>(v[b + 8]);
    actor.balloon_at = v[b + 9];
    actor.cast_end = v[b + 10];
    actor.cast_start = v[b + 11];
    o.screen_x = static_cast<std::int32_t>(v[b + 12]);
    o.screen_y = static_cast<std::int32_t>(v[b + 13]);
    o.pixel_scale = std::bit_cast<float>(v[b + 14]);
    context.windows = &actor_windows;
    context.screen_width = static_cast<std::int32_t>(v[b + 15]);
    context.screen_height = static_cast<std::int32_t>(v[b + 16]);
    actor.field_0x1bc = static_cast<std::int32_t>(v[b + 17]);
    actor.weapon = static_cast<std::int32_t>(v[b + 18]);
    o.play_mode = static_cast<std::int32_t>(v[b + 19]);
    o.start_time = v[b + 20];
    if (!player) actor.body_state = 0;
    // The windows by number, the cast's times, +0x1a4 and +0xcc; for a player CGameMode+0x2a0
    // and +0x294; what the windows were told and which were queued for removal since, as
    // tools/path_oracle.py's casted().
    const auto fold_cast = [&] {
      for (const std::int32_t x :
           {actor.balloon, actor.chat_title, actor.shop_title, actor.cast_gauge,
            static_cast<std::int32_t>(actor.balloon_at), static_cast<std::int32_t>(actor.cast_end),
            static_cast<std::int32_t>(actor.cast_start), actor.field_0xcc})
        fold(static_cast<std::uint32_t>(x));
      if (player) {
        fold(static_cast<std::uint32_t>(mode_field_0x2a0));
        fold(static_cast<std::uint32_t>(actor.saved_frame));
      }
      fold(static_cast<std::uint32_t>(4 * actor_windows.calls.size()));
      for (const auto& call : actor_windows.calls) {
        for (const std::int32_t x :
             {static_cast<std::int32_t>(call.kind), call.window, call.a, call.b})
          fold(static_cast<std::uint32_t>(x));
      }
      actor_windows.calls.clear();
      fold(static_cast<std::uint32_t>(windows.size()));
      for (const std::int32_t x : windows) fold(static_cast<std::uint32_t>(x));
      windows.clear();
    };
    const std::uint32_t at = v[kFields + 1];
    if (how == 0x52)
      ragexe::world::StartGameActorCast(base, duration, context, at);
    else if (how != 0)
      ragexe::world::CloseGameActorWindow(base, static_cast<std::int32_t>(how), context);
    if (enter_state != 0) {
      if (player)
        ragexe::world::SetPlayerState(actor, static_cast<std::int32_t>(enter_state), context, at);
      else
        ragexe::world::SetGameActorState(base, static_cast<std::int32_t>(enter_state), context, at);
    }
    fold(8);
    fold_fields();
    fold_messages();
    fold_cast();
    int updates = 0;
    const char* end = "staying";
    for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
      if (player)
        ragexe::world::StepGameActorWindows(actor, context, v[k + 1]);
      else
        ragexe::world::StepGameActorWindows(base, context, v[k + 1]);
      fold(9);
      fold_fields();
      fold_messages();
      fold_cast();
      const auto r = player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                            : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
      ++updates;
      const bool over =
          r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
      fold(over ? 0u : 1u);
      fold_fields();
      fold_messages();
      fold_cast();
      if (over) {
        end = "faded";
        break;
      }
      if (actor.state != 13 && actor.balloon == 0 && actor.chat_title == 0 &&
          actor.shop_title == 0 && actor.cast_gauge == 0) {
        end = "settled";
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
  // o and m: the CPlayer's fields, the ping, the attack range, the player's id, what CWorld finds
  // of the one pursued (found's bits) and where it stands, its state, cell, +0x238, job and
  // +0x21c, and CGameMode+0x198's list before, as tools/path_oracle.py's ready_own().
  ragexe::world::GameActor pursued;
  std::vector<std::int32_t> picked;
  std::vector<ragexe::world::MoveOrder> own_moves;
  if (own) {
    const std::size_t w = base_fields + (update ? kUpdateFields : 0);
    const auto i = [&](std::size_t k) { return static_cast<std::int32_t>(v[w + k]); };
    ragexe::world::OwnPlayer& p = actor.own.emplace();
    actor.reports = false;
    p.goal_x = i(0);
    p.goal_y = i(1);
    p.acted_at = v[w + 2];
    p.premove_at = v[w + 3];
    p.pursuit = i(4);
    p.premove = i(5);
    p.attack_mode = i(6);
    p.acted = i(7);
    p.move_queued = i(8);
    p.target_id = i(9);
    p.acts = i(10);
    p.asked_at = v[w + 11];
    p.skill = i(12);
    p.range = i(13);
    p.cell_x = i(14);
    p.cell_y = i(15);
    p.queued_x = i(16);
    p.queued_y = i(17);
    context.ping = v[w + 18];
    context.attack_range = i(19);
    context.own_id = i(20);
    const std::uint32_t found = v[w + 21];
    pursued.object.position = {std::bit_cast<float>(v[w + 22]), std::bit_cast<float>(v[w + 23]),
                               std::bit_cast<float>(v[w + 24])};
    pursued.state = i(25);
    pursued.goal_x = i(26);
    pursued.goal_y = i(27);
    pursued.model_kind = i(28);
    pursued.job = i(29);
    pursued.effect_state = v[w + 30];
    if ((found & 1) != 0) {
      context.target_actor = &pursued;
      context.target = &pursued.object.position;
    }
    if ((found & 2) != 0) context.item = &pursued.object.position;
    if ((found & 4) != 0) context.skill_object = &pursued;
    if (v[w + 31] != 0) picked.push_back(p.target_id + (v[w + 31] == 2 ? 1 : 0));
    context.picked = &picked;
    context.own_moves = &own_moves;
    context.actor_messages = &actor_messages;
  }
  // What the own player's update writes beyond p's, as tools/path_oracle.py's own().
  const auto fold_own = [&] {
    if (!own) return;
    const ragexe::world::OwnPlayer& p = *actor.own;
    for (const std::int32_t x :
         {p.goal_x, p.goal_y, static_cast<std::int32_t>(p.acted_at),
          static_cast<std::int32_t>(p.premove_at), p.pursuit, p.premove, p.attack_mode, p.acted,
          p.move_queued, p.target_id, p.acts, static_cast<std::int32_t>(p.asked_at), p.skill,
          p.range, p.cell_x, p.cell_y, p.queued_x, p.queued_y})
      fold(static_cast<std::uint32_t>(x));
    fold(static_cast<std::uint32_t>(4 * actor_messages.size()));
    for (const auto& m : actor_messages) {
      for (const std::int32_t x : {m.id, m.a, m.b, m.c}) fold(static_cast<std::uint32_t>(x));
    }
    actor_messages.clear();
    fold(static_cast<std::uint32_t>(7 * own_moves.size()));
    for (const auto& m : own_moves) {
      for (const std::int32_t x : {static_cast<std::int32_t>(m.time), m.start_x, m.start_y, m.sub_x,
                                   m.sub_y, m.goal_x, m.goal_y})
        fold(static_cast<std::uint32_t>(x));
    }
    own_moves.clear();
    fold(static_cast<std::uint32_t>(picked.size()));
    fold(static_cast<std::uint32_t>(std::min<std::size_t>(picked.size(), 16)));
    for (std::size_t k = 0; k < picked.size() && k < 16; ++k)
      fold(static_cast<std::uint32_t>(picked[k]));
  };
  // m: after the own fields +0x328, +0x33c and +0x34c, the player's speed, CGameMode+0x364, and
  // the one pursued's speed and +0x1f4, the messages through CPlayer::SendMsg, as
  // tools/path_oracle.py's orders().
  if (orders) {
    const std::size_t r = base_fields + kOwnFields;
    ragexe::world::OwnPlayer& p = *actor.own;
    p.walk_asked = static_cast<std::int32_t>(v[r]);
    p.walk_asked_at = v[r + 1];
    p.level = static_cast<std::int32_t>(v[r + 2]);
    context.own_speed = static_cast<std::int32_t>(v[r + 3]);
    context.skill_range = static_cast<std::int32_t>(v[r + 4]);
    pursued.speed = static_cast<std::int32_t>(v[r + 5]);
    pursued.field_0x1f4 = static_cast<std::int32_t>(v[r + 6]);
    std::vector<ragexe::world::CellSkill> cell_skills;
    context.cell_skills = &cell_skills;
    std::uint32_t tick = 0;
    std::uint32_t server = 0;
    context.send_own = [&](std::int32_t id, std::int32_t a, std::int32_t b) {
      ragexe::world::SendOwnPlayerMessage(actor, finder, {id, a, b}, context, tick, server);
    };
    const std::uint32_t count = v[kFields];
    constexpr std::size_t kRecord = 13;
    REQUIRE(v.size() >= kFields + 1 + kRecord * std::size_t{count});
    for (std::uint32_t n = 0; n < count; ++n) {
      const std::size_t k = kFields + 1 + kRecord * n;
      const auto i = [&](std::size_t j) { return static_cast<std::int32_t>(v[k + j]); };
      ragexe::world::OwnMessage message{i(0), i(1), i(2), i(3)};
      message.order = {v[k + 4], i(5), i(6), i(7), i(8), i(9), i(10)};
      message.object_skill = {i(5), i(6), i(7)};
      server = v[k + 11];
      tick = v[k + 12];
      ragexe::world::SendOwnPlayerMessage(actor, finder, message, context, tick, server);
      std::uint64_t digest = 0xcbf29ce484222325ull;
      for (const auto& step : actor.path.steps) {
        for (const std::uint32_t x :
             {static_cast<std::uint32_t>(step.x), static_cast<std::uint32_t>(step.y),
              static_cast<std::uint32_t>(step.dir), step.time})
          digest = Fold(digest, x);
      }
      fold(v[k]);
      fold_fields();
      fold(static_cast<std::uint32_t>(actor.path.steps.size()));
      fold(static_cast<std::uint32_t>(digest));
      fold(static_cast<std::uint32_t>(digest >> 32));
      fold_messages();
      fold_player();
      fold_own();
      for (const std::int32_t x :
           {p.walk_asked, static_cast<std::int32_t>(p.walk_asked_at), p.level})
        fold(static_cast<std::uint32_t>(x));
      fold(static_cast<std::uint32_t>(cell_skills.size()));
      for (const auto& asked_for : cell_skills) {
        if (asked_for.window) {
          for (const std::int32_t x :
               {0x54, 0, 0x44, asked_for.skill, asked_for.x, asked_for.y, asked_for.level, 0, 0})
            fold(static_cast<std::uint32_t>(x));
        } else {
          for (const std::int32_t x :
               {0x4d, asked_for.skill, asked_for.x, asked_for.y, asked_for.level, 0, 0})
            fold(static_cast<std::uint32_t>(x));
        }
      }
      cell_skills.clear();
    }
    path = actor.path;
    char line[160];
    std::snprintf(line, sizeof line, "sent %u %08x %08x %08x %08x %d %d %u %016llx", count,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                  static_cast<unsigned long long>(h));
    return line;
  }
  // u and p: the actor's whole update (CNpc::Update, CPc::Update) after +0x230, the key colour,
  // +0x87, +0x1ec, +0x290, +0x1f4, +0x218, effects on, the act's frames' events, +0x2c, +0x30,
  // +0x28, the world's player, the gauges, the point on screen, +0x48, the screen's height, the
  // cast's times, +0xcc, +0x1bc, the weapon, +0x54 and +0x70, as tools/path_oracle.py's
  // update().
  if (update) {
    auto& base = static_cast<ragexe::world::GameActor&>(actor);
    const std::size_t b = base_fields;
    actor.sight_ticks = v[b];
    o.keyed = v[b + 1] != 0;
    o.keyed_at = v[b + 2];
    o.key_color = v[b + 3];
    o.tinted = v[b + 4] != 0;
    actor.field_0x1ec = static_cast<std::int32_t>(v[b + 5]);
    actor.repeated_at = v[b + 6];
    actor.field_0x1f4 = static_cast<std::int32_t>(v[b + 7]);
    actor.body_state = static_cast<std::int32_t>(v[b + 8]);
    context.effects_on = v[b + 9] != 0;
    const std::uint32_t events = v[b + 10];
    o.sounded_action = static_cast<std::int32_t>(v[b + 11]);
    o.sounded_frame = static_cast<std::int32_t>(v[b + 12]);
    o.frame = static_cast<std::int32_t>(v[b + 13]);
    player_at = {std::bit_cast<float>(v[b + 14]), 0.0f, std::bit_cast<float>(v[b + 15])};
    context.player = &player_at;
    ragexe::world::ActorWindows actor_windows;
    actor.cast_gauge = 0;
    if (v[b + 21] != 0) {
      actor_windows.sizes.push_back({0x3c, 6});
      actor.cast_gauge = static_cast<std::int32_t>(actor_windows.sizes.size());
    }
    if (player && v[b + 16] != 0) {
      actor_windows.sizes.push_back({0, 0});
      actor.gauge = static_cast<std::int32_t>(actor_windows.sizes.size());
    }
    o.screen_x = static_cast<std::int32_t>(v[b + 17]);
    o.screen_y = static_cast<std::int32_t>(v[b + 18]);
    o.pixel_scale = std::bit_cast<float>(v[b + 19]);
    context.windows = &actor_windows;
    context.screen_height = static_cast<std::int32_t>(v[b + 20]);
    actor.cast_end = v[b + 22];
    actor.cast_start = v[b + 23];
    actor.field_0xcc = static_cast<std::int32_t>(v[b + 24]);
    actor.field_0x1bc = static_cast<std::int32_t>(v[b + 25]);
    actor.weapon = static_cast<std::int32_t>(v[b + 26]);
    o.play_mode = static_cast<std::int32_t>(v[b + 27]);
    o.start_time = v[b + 28];
    o.action = static_cast<std::int32_t>(v[b + 29]);
    // Each frame's event by 3 bits of `events`, the first 10 frames, as the attacks' but named
    // for sounds.
    act.events = {"foot.wav", "a.wav.b", "a.wa", "X.WAV"};
    for (auto& frames : act.actions) {
      for (std::size_t k = 0; k < frames.size() && k < 10; ++k) {
        const std::uint32_t code = events >> (3 * k) & 7;
        frames[k].event = code == 0 || code == 7 ? -1
                          : code == 5            ? 4
                          : code == 6            ? 0x7fffffff
                                                 : static_cast<std::int32_t>(code) - 1;
      }
    }
    std::vector<std::int32_t> launched;
    std::vector<ragexe::world::EffectMessage> launched_messages;
    context.launched = &launched;
    context.launched_messages = &launched_messages;
    context.sounds = &sounds;
    // What the whole update writes beyond the fields, as tools/path_oracle.py's updated().
    const auto fold_updated = [&] {
      for (const std::uint32_t x :
           {actor.sight_ticks, actor.repeated_at, o.key_color, o.keyed_at,
            static_cast<std::uint32_t>(o.sounded_action),
            static_cast<std::uint32_t>(o.sounded_frame), actor.cast_end, actor.cast_start,
            static_cast<std::uint32_t>(actor.field_0xcc), o.keyed ? 1u : 0u,
            static_cast<std::uint32_t>(actor.cast_gauge)})
        fold(x);
      if (player) {
        fold(static_cast<std::uint32_t>(actor.gauge));
        fold(static_cast<std::uint32_t>(mode_field_0x2a0));
        fold(static_cast<std::uint32_t>(actor.saved_frame));
      }
      fold(static_cast<std::uint32_t>(8 * sounds.size()));
      for (const auto& sound : sounds) {
        std::uint64_t n = 0xcbf29ce484222325ull;
        for (const char c : sound.name) n = (n ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
        fold(static_cast<std::uint32_t>(n));
        fold(static_cast<std::uint32_t>(n >> 32));
        for (const std::uint32_t x : {Bits(sound.x), 0u, Bits(sound.z), 250u, 40u, Bits(1.0f)})
          fold(x);
      }
      sounds.clear();
      fold(static_cast<std::uint32_t>(launched.size()));
      for (const std::int32_t id : launched) fold(static_cast<std::uint32_t>(id));
      launched.clear();
      fold(static_cast<std::uint32_t>(6 * launched_messages.size()));
      for (const auto& m : launched_messages) {
        for (const std::int32_t x : {m.effect, 0, m.id, m.a, m.b, m.c})
          fold(static_cast<std::uint32_t>(x));
      }
      launched_messages.clear();
      fold(static_cast<std::uint32_t>(4 * actor_windows.calls.size()));
      for (const auto& call : actor_windows.calls) {
        for (const std::int32_t x :
             {static_cast<std::int32_t>(call.kind), call.window, call.a, call.b})
          fold(static_cast<std::uint32_t>(x));
      }
      actor_windows.calls.clear();
      fold(static_cast<std::uint32_t>(windows.size()));
      for (const std::int32_t x : windows) fold(static_cast<std::uint32_t>(x));
      windows.clear();
    };
    int updates = 0;
    const char* end = "staying";
    for (std::size_t k = kFields; k + 1 < v.size(); k += 2) {
      const auto r = own      ? ragexe::world::UpdateOwnPlayer(actor, context, v[k + 1], v[k])
                     : player ? ragexe::world::UpdatePlayer(actor, context, v[k + 1], v[k])
                              : ragexe::world::UpdateNpc(base, context, v[k + 1], v[k]);
      ++updates;
      const bool over =
          r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
      fold(over ? 0u : 1u);
      fold_fields();
      fold_messages();
      fold_updated();
      fold_own();
      if (over) {
        end = "faded";
        break;
      }
    }
    char line[160];
    std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %u %u %u %016llx", end, updates,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  static_cast<std::uint32_t>(o.action), static_cast<std::uint32_t>(o.frame),
                  static_cast<std::uint32_t>(actor.state), static_cast<unsigned long long>(h));
    return line;
  }
  // L and O: the hits, queued and delivered or the first sent; the attacker, another or the
  // player or the actor itself; the player's statuses, the guild war, effects on, the msg
  // sprites, the effects listed before; then the updates, each delivering what is due, as
  // tools/path_oracle.py's land().
  if (land) {
    auto& base = static_cast<ragexe::world::GameActor&>(actor);
    const std::size_t b = base_fields;
    const std::uint32_t how = v[b];
    const std::uint32_t attacker = v[b + 1];
    const std::uint32_t pcs = v[b + 2];
    context.own_id = static_cast<std::int32_t>(v[b + 6]);
    ragexe::world::GameActor foe;
    ragexe::world::GameActor pc;
    for (ragexe::world::GameActor* other : {&foe, &pc}) {
      other->job = static_cast<std::int32_t>(v[b + 3]);
      other->effect_state = v[b + 4];
    }
    foe.id = 0x5a5a0001;
    pc.id = 0x5a5a0002;
    foe.object.position = {std::bit_cast<float>(v[b + 22]), 0.0f, std::bit_cast<float>(v[b + 23])};
    pc.object.position = {std::bit_cast<float>(v[b + 24]), 0.0f, std::bit_cast<float>(v[b + 25])};
    const auto weapon_of = [&](std::uint32_t bit) {
      return (pcs & bit) != 0 ? static_cast<std::int32_t>(v[b + 5]) : -1;
    };
    std::vector<std::int32_t> statuses;
    if ((v[b + 7] & 2) != 0) statuses.push_back(5);
    if ((v[b + 7] & 1) != 0) statuses.push_back(0x13);
    context.own_statuses = &statuses;
    context.guild_war_map = v[b + 8] != 0;
    context.effects_on = v[b + 9] != 0;
    context.msg_sprites = v[b + 10] != 0;
    rand.Seed(v[b + 11]);
    actor.field_0x210 = static_cast<std::int32_t>(v[b + 12]);
    const std::uint32_t events = v[b + 16];
    o.play_mode = static_cast<std::int32_t>(v[b + 17]);
    o.start_time = v[b + 18];
    actor.target_id = static_cast<std::int32_t>(v[b + 19]);
    actor.weapon = static_cast<std::int32_t>(v[b + 20]);
    actor.field_0x1bc = static_cast<std::int32_t>(v[b + 21]);
    actor.target_x = 0.0f;
    actor.target_z = 0.0f;
    if (!player) actor.body_state = 0;
    const std::int32_t own_weapon = player ? actor.weapon : -1;
    context.target_actor = attacker == 1 ? &foe : attacker == 3 ? &base : nullptr;
    context.target_weapon = attacker == 3 ? own_weapon : weapon_of(1);
    context.player_actor = attacker == 4 ? &base : &pc;
    context.player_weapon = attacker == 4 ? own_weapon : weapon_of(2);
    context.target =
        context.target_actor != nullptr ? &context.target_actor->object.position : nullptr;
    context.player = &context.player_actor->object.position;
    // The effects before: `listed` numbers, of type 0x16 by `kinds`' bits, the first in the
    // slot `slot` below 11.
    ragexe::world::ActorEffects effects;
    for (std::uint32_t k = 0; k < v[b + 13]; ++k) {
      ragexe::world::ActorEffect e;
      if ((v[b + 14] >> k & 1) != 0) {
        e.maker = ragexe::world::EffectMaker::kNumber;
        e.type = 0x16;
      }
      effects.made.push_back(e);
      const auto number = static_cast<std::int32_t>(effects.made.size());
      actor.effects.push_back(number);
      if (k == 0 && v[b + 15] < actor.effect_slots.size()) actor.effect_slots[v[b + 15]] = number;
    }
    const std::size_t made_before = effects.made.size();
    context.effects = &effects;
    act.events = {"atk", "atk2", "at", ""};
    for (auto& frames : act.actions) {
      for (std::size_t k = 0; k < frames.size() && k < 10; ++k) {
        const std::uint32_t code = events >> (3 * k) & 7;
        frames[k].event = code == 0 || code == 7 ? -1
                          : code == 5            ? 4
                          : code == 6            ? 0x7fffffff
                                                 : static_cast<std::int32_t>(code) - 1;
      }
    }
    std::vector<ragexe::world::Arrow> arrows;
    std::vector<std::int32_t> launched;
    std::vector<ragexe::world::EffectMessage> launched_messages;
    context.arrows = &arrows;
    context.launched = &launched;
    context.launched_messages = &launched_messages;
    context.sounds = &sounds;
    ragexe::world::QueuedHit first;
    for (std::size_t k = 0; k < v[b + 26]; ++k) {
      const std::uint32_t* w = &v[b + kLandFields + 10 * k];
      const ragexe::world::QueuedHit hit{static_cast<std::int32_t>(w[0]),
                                         static_cast<std::int32_t>(w[1]),
                                         w[2],
                                         static_cast<std::int32_t>(w[3]),
                                         static_cast<std::int32_t>(w[4]),
                                         static_cast<std::int32_t>(w[5]),
                                         static_cast<std::int32_t>(w[6]),
                                         static_cast<std::int32_t>(w[7]),
                                         static_cast<std::int32_t>(w[8]),
                                         std::bit_cast<float>(w[9])};
      if (k == 0 && how != 0) {
        first = hit;
      } else {
        actor.hits.push_back(hit);
      }
    }
    const std::uint32_t at = v[kFields + 1];
    if (how == 0x6c) {
      if (player)
        ragexe::world::RestartGameActorAttack(actor, context, at);
      else
        ragexe::world::RestartGameActorAttack(base, context, at);
    } else if (how != 0) {
      if (player)
        ragexe::world::LandGameActorHit(actor, static_cast<std::int32_t>(how), first, context, at);
      else
        ragexe::world::LandGameActorHit(base, static_cast<std::int32_t>(how), first, context, at);
    }
    const auto where = [&](const ragexe::render::Vec3* p) {
      return p == &o.position ? 1u : p == &foe.object.position ? 2u : 3u;
    };
    const auto who = [&](const ragexe::world::GameActor* sender) {
      return sender == nullptr ? 0u : sender == &base ? 1u : sender == &foe ? 2u : 3u;
    };
    const auto fold_message = [&](const ragexe::world::EffectMessage& m) {
      for (const std::uint32_t x :
           {static_cast<std::uint32_t>(m.effect), who(m.sender), static_cast<std::uint32_t>(m.id),
            m.at != nullptr ? where(m.at) : static_cast<std::uint32_t>(m.a),
            static_cast<std::uint32_t>(m.b), static_cast<std::uint32_t>(m.c)})
        fold(x);
    };
    const auto fold_list = [&](const ragexe::world::GameActor& a) {
      fold(static_cast<std::uint32_t>(a.effects.size()));
      for (const std::int32_t number : a.effects) fold(static_cast<std::uint32_t>(number));
    };
    // What a hit's landing writes beyond the fields, and the queue; what was played,
    // launched and sent to it since; the slots, the three lists, every effect and what they
    // were sent and which the world deleted since, as tools/path_oracle.py's landed().
    const auto fold_landed = [&] {
      for (const std::uint32_t x :
           {o.keyed_at, static_cast<std::uint32_t>(actor.field_0xa8), Bits(actor.target_x),
            Bits(actor.target_z), static_cast<std::uint32_t>(actor.field_0x210),
            static_cast<std::uint32_t>(actor.target_id),
            static_cast<std::uint32_t>(actor.field_0xcc), o.keyed ? 1u : 0u,
            static_cast<std::uint32_t>(actor.hits.size())})
        fold(x);
      for (const auto& hit : actor.hits) {
        for (const std::uint32_t x :
             {static_cast<std::uint32_t>(hit.message), static_cast<std::uint32_t>(hit.damage),
              hit.time, static_cast<std::uint32_t>(hit.attacker_id),
              static_cast<std::uint32_t>(hit.cell_x), static_cast<std::uint32_t>(hit.cell_y),
              static_cast<std::uint32_t>(hit.field_0x18),
              static_cast<std::uint32_t>(hit.field_0x1c),
              static_cast<std::uint32_t>(hit.field_0x20), Bits(hit.field_0x24)}) {
          fold(x);
        }
      }
      fold(static_cast<std::uint32_t>(8 * sounds.size()));
      for (const auto& sound : sounds) {
        std::uint64_t n = 0xcbf29ce484222325ull;
        for (const char c : sound.name) n = (n ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
        fold(static_cast<std::uint32_t>(n));
        fold(static_cast<std::uint32_t>(n >> 32));
        for (const std::uint32_t x : {Bits(sound.x), 0u, Bits(sound.z), 250u, 40u, Bits(1.0f)})
          fold(x);
      }
      sounds.clear();
      fold(static_cast<std::uint32_t>(launched.size()));
      for (const std::int32_t id : launched) fold(static_cast<std::uint32_t>(id));
      launched.clear();
      fold(static_cast<std::uint32_t>(6 * launched_messages.size()));
      for (const auto& m : launched_messages) fold_message(m);
      launched_messages.clear();
      for (const std::int32_t number : actor.effect_slots) fold(static_cast<std::uint32_t>(number));
      fold_list(base);
      fold_list(foe);
      fold_list(pc);
      fold(static_cast<std::uint32_t>(effects.made.size()));
      for (std::size_t k = 0; k < effects.made.size(); ++k) {
        const auto& e = effects.made[k];
        const bool before = k < made_before;
        const bool number = e.maker == ragexe::world::EffectMaker::kNumber;
        const bool sprite = e.maker == ragexe::world::EffectMaker::kSprite ||
                            e.maker == ragexe::world::EffectMaker::kEmotion;
        for (const std::uint32_t x :
             {before ? 2u : MakerCode(e.maker),
              !before && sprite ? static_cast<std::uint32_t>(e.type) : 0u,
              before || number ? 0u : where(e.position),
              before || number ? 0u : static_cast<std::uint32_t>(e.actor_id), Bits(e.field_0x44),
              Bits(e.field_0x3c), e.color, number ? static_cast<std::uint32_t>(e.type) : 0u}) {
          fold(x);
        }
      }
      fold(static_cast<std::uint32_t>(6 * effects.messages.size()));
      for (const auto& m : effects.messages) fold_message(m);
      effects.messages.clear();
      fold(static_cast<std::uint32_t>(effects.deleted.size()));
      for (const std::int32_t number : effects.deleted) fold(static_cast<std::uint32_t>(number));
      effects.deleted.clear();
    };
    fold(7);
    fold_fields();
    fold_messages();
    fold_player();
    fold_landed();
    int updates = 0;
    const char* end = "staying";
    for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
      if (player)
        ragexe::world::DeliverGameActorHits(actor, context, v[k + 1]);
      else
        ragexe::world::DeliverGameActorHits(base, context, v[k + 1]);
      const auto r = player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                            : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
      ++updates;
      const bool over =
          r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
      fold(over ? 0u : 1u);
      fold_fields();
      fold_messages();
      fold_player();
      fold_landed();
      if (over) {
        end = "faded";
        break;
      }
    }
    char line[160];
    std::snprintf(line, sizeof line, "%s %d %08x %08x %08x %08x %d %u %u %016llx", end, updates,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, static_cast<std::uint32_t>(o.frame),
                  static_cast<std::uint32_t>(actor.state), static_cast<unsigned long long>(h));
    return line;
  }
  // D and H: the death, by vf10 (0), 0xc or 0x1c, after +0x218, +0x20c, +0x210, a sprite or
  // none and +0xcc; then the updates, as tools/path_oracle.py's die().
  if (die) {
    auto& base = static_cast<ragexe::world::GameActor&>(actor);
    const std::uint32_t how = v[base_fields];
    actor.body_state = static_cast<std::int32_t>(v[base_fields + 1]);
    actor.hits.resize(v[base_fields + 2]);
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
  // c: the look before, the gauge, the screen, the one administrator, effects on and msg
  // sprites, then the messages through CPc::SendMsg, as tools/path_oracle.py's looks().
  if (look) {
    const std::size_t l = base_fields + kArriveFields + kEffectFields;
    const auto i = [&](std::size_t j) { return static_cast<std::int32_t>(v[l + j]); };
    ragexe::world::PlayerLook& dressed = actor.look;
    actor.field_0x1bc = i(0);
    dressed.hair = i(1);
    dressed.hair_color = i(2);
    actor.weapon = i(3);
    dressed.headgear = {i(4), i(5), i(6)};
    dressed.shield = i(7);
    dressed.body_color = i(8);
    actor.field_0x28c = i(9);
    actor.repeated_at = v[l + 10];
    actor.field_0x2e0 = i(11);
    actor.field_0x2e4 = i(12);
    actor.field_0x1f4 = i(13);
    actor.cast_gauge = 0;
    ragexe::world::ActorWindows made_windows;
    if (i(14) != 0) {
      made_windows.sizes.push_back({0, 0});
      actor.gauge = 1;
    }
    o.screen_x = i(15);
    o.screen_y = i(16);
    context.screen_height = i(17);
    std::vector<std::int32_t> admins;
    if (i(18) != 0) admins.push_back(i(18));
    context.admins = &admins;
    context.effects_on = i(19) != 0;
    context.msg_sprites = i(20) != 0;
    context.windows = &made_windows;
    std::vector<std::int32_t> launched;
    context.launched = &launched;
    const std::uint32_t count = v[kFields];
    constexpr std::size_t kRecord = 3 + kRecordWords + 2;
    REQUIRE(v.size() >= kFields + 1 + kRecord * std::size_t{count});
    const auto fold_name = [&](std::string_view name) {
      std::uint64_t n = 0xcbf29ce484222325ull;
      for (const char c : name) n = (n ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
      fold(static_cast<std::uint32_t>(n));
      fold(static_cast<std::uint32_t>(n >> 32));
    };
    // What the messages write beyond the fields and what they sent, played, asked for,
    // launched, made, deleted and did to the windows, as tools/path_oracle.py's looked().
    const auto fold_looked = [&] {
      for (const std::int32_t x : {actor.id,
                                   actor.job,
                                   actor.field_0x1bc,
                                   dressed.hair,
                                   dressed.hair_color,
                                   actor.weapon,
                                   dressed.headgear[0],
                                   dressed.headgear[1],
                                   dressed.headgear[2],
                                   dressed.shield,
                                   dressed.body_color,
                                   actor.field_0x28c,
                                   static_cast<std::int32_t>(actor.repeated_at),
                                   actor.field_0x2e0,
                                   actor.field_0x2e4,
                                   static_cast<std::int32_t>(Bits(o.shadow_scale)),
                                   actor.saved_frame,
                                   actor.speed,
                                   actor.field_0x1ec,
                                   actor.field_0x1f0,
                                   actor.field_0x1f4,
                                   actor.body_state,
                                   static_cast<std::int32_t>(actor.effect_state),
                                   static_cast<std::int32_t>(actor.health_state),
                                   actor.field_0x224,
                                   actor.guild,
                                   actor.emblem})
        fold(static_cast<std::uint32_t>(x));
      for (const bool x : {o.visible, o.body_shown, o.tinted, o.shadow}) fold(x ? 1u : 0u);
      fold(static_cast<std::uint32_t>(ground_water_fade));
      fold(static_cast<std::uint32_t>(mode_field_0x2a0));
      for (const auto& layer : dressed.layers) {
        fold_name(layer.act);
        fold_name(layer.sprite);
      }
      for (const std::string& name : {dressed.imf, dressed.head_palette, dressed.body_palette,
                                      dressed.body.act, dressed.body.sprite})
        fold_name(name);
      fold(static_cast<std::uint32_t>(actor.gauge));
      fold(static_cast<std::uint32_t>(4 * actor_messages.size()));
      for (const auto& m : actor_messages) {
        for (const std::int32_t x : {m.id, m.a, m.b, m.c}) fold(static_cast<std::uint32_t>(x));
      }
      actor_messages.clear();
      fold(static_cast<std::uint32_t>(8 * sounds.size()));
      for (const auto& sound : sounds) {
        fold_name(sound.name);
        for (const std::uint32_t x : {Bits(sound.x), 0u, Bits(sound.z), 250u, 40u, Bits(1.0f)})
          fold(x);
      }
      sounds.clear();
      fold(static_cast<std::uint32_t>(2 * asked.size()));
      for (const auto& name : asked) fold_name(name);
      asked.clear();
      fold(static_cast<std::uint32_t>(launched.size()));
      for (const std::int32_t id : launched) fold(static_cast<std::uint32_t>(id));
      launched.clear();
      fold_effects();
      fold(static_cast<std::uint32_t>(effects.deleted.size()));
      for (const std::int32_t number : effects.deleted) fold(static_cast<std::uint32_t>(number));
      effects.deleted.clear();
      fold(static_cast<std::uint32_t>(4 * made_windows.calls.size()));
      for (const auto& call : made_windows.calls) {
        for (const std::int32_t x :
             {static_cast<std::int32_t>(call.kind), call.window, call.a, call.b})
          fold(static_cast<std::uint32_t>(x));
      }
      made_windows.calls.clear();
      fold(static_cast<std::uint32_t>(windows.size()));
      for (const std::int32_t window : windows) fold(static_cast<std::uint32_t>(window));
      windows.clear();
    };
    for (std::uint32_t n = 0; n < count; ++n) {
      const std::size_t k = kFields + 1 + kRecord * n;
      const auto w = [&](std::size_t j) { return static_cast<std::int32_t>(v[k + j]); };
      const std::int32_t id = w(0);
      const std::uint32_t server = v[k + 3 + kRecordWords];
      const std::uint32_t tick = v[k + 4 + kRecordWords];
      const auto r = [&](std::size_t j) { return w(3 + j); };
      const auto u = [&](std::size_t j) { return static_cast<std::uint32_t>(w(3 + j)); };
      if (id == 3 || id == 4 || id == 0x79 || id == 0x7c) {
        const ragexe::world::PlayerStandingArrival arrival{
            r(0),  r(1),  r(2),  r(3),  r(4),  r(5),  r(6),  r(7),  r(8),
            r(9),  r(10), r(11), r(12), r(13), u(14), u(15), r(16), r(17),
            r(18), r(19), r(20), r(21), r(22), r(23), r(24), r(25), r(26)};
        ragexe::world::ArrivePlayer(actor, id, arrival, w(2), context, tick);
      } else if (id == 5 || id == 6 || id == 0x7a || id == 0x7b) {
        const ragexe::world::PlayerWalkingArrival arrival{
            r(0),  r(1),  r(2),  {u(3), r(4), r(5), r(6), r(7), r(8), r(9)},
            r(10), r(11), r(12), r(13),
            r(14), u(15), u(16), r(17),
            r(18), r(19), r(20), r(21),
            r(22), r(24), r(25), r(26),
            r(27)};
        ragexe::world::ArriveWalkingPlayer(actor, finder, id, arrival, server, context, tick);
      } else {
        ragexe::world::SendPlayerMessage(actor, id, w(1), w(2), context, tick);
      }
      std::uint64_t digest = 0xcbf29ce484222325ull;
      for (const auto& step : actor.path.steps) {
        for (const std::uint32_t x :
             {static_cast<std::uint32_t>(step.x), static_cast<std::uint32_t>(step.y),
              static_cast<std::uint32_t>(step.dir), step.time})
          digest = Fold(digest, x);
      }
      fold(static_cast<std::uint32_t>(id));
      fold_fields();
      fold(static_cast<std::uint32_t>(actor.path.steps.size()));
      fold(static_cast<std::uint32_t>(digest));
      fold(static_cast<std::uint32_t>(digest >> 32));
      fold_messages();
      fold_looked();
    }
    path = actor.path;
    char line[160];
    std::snprintf(line, sizeof line, "sent %u %08x %08x %08x %08x %d %u %u %016llx", count,
                  Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]), Bits(o.facing),
                  o.action, static_cast<std::uint32_t>(o.frame),
                  static_cast<std::uint32_t>(actor.state), static_cast<unsigned long long>(h));
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
    if (v[base_fields] != 0) {
      const auto state = static_cast<std::int32_t>(v[base_fields + 1]);
      if (player)
        ragexe::world::SetPlayerState(actor, state, context, v[kFields + 1]);
      else
        ragexe::world::SetGameActorState(base, state, context, v[kFields + 1]);
    }
    fold(3);
    fold_fields();
    fold_messages();
    fold_player();
    fold(static_cast<std::uint32_t>(actor.field_0xcc));
    const std::int32_t state = actor.state;
    int updates = 0;
    const char* end = "staying";
    for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
      const auto r = player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                            : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
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

// The FNV-1a of a name's bytes, folded as its two halves.
std::uint64_t FoldName(std::uint64_t h, std::string_view name) {
  std::uint64_t n = 0xcbf29ce484222325ull;
  for (const char c : name) n = (n ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
  return Fold(Fold(h, static_cast<std::uint32_t>(n)), static_cast<std::uint32_t>(n >> 32));
}

// tools/path_oracle.py's i: a CItem made, sent a message, updated and drawn.
std::string DropItem(const AttrMap& map, std::istringstream& fields) {
  const auto bytes = [](const std::string& hex) {
    std::string out;
    if (hex == "-") return out;
    for (std::size_t k = 0; k + 1 < hex.size(); k += 2)
      out.push_back(static_cast<char>(std::stoi(hex.substr(k, 2), nullptr, 16)));
    return out;
  };
  std::string name;
  fields >> name;
  std::uint32_t v[13] = {};
  for (std::uint32_t& x : v) {
    std::string token;
    fields >> token;
    x = static_cast<std::uint32_t>(std::stoll(token, nullptr, 0));
  }
  constexpr const char* kTables[] = {"IdNum2ItemDisplayNameTable.txt",
                                     "Num2ItemDisplayNameTable.txt", "IdNum2ItemResNameTable.txt",
                                     "Num2ItemResNameTable.txt"};
  std::map<std::string, std::string, std::less<>> tables;
  for (const char* table : kTables) {
    std::string hex;
    fields >> hex;
    if (const std::string text = bytes(hex); !text.empty()) tables.emplace(table, text);
  }
  std::string format;
  fields >> format;
  format = bytes(format);
  ragexe::res::ItemTables items;
  std::string error;
  const auto read = [&tables](std::string_view n) -> std::optional<std::vector<std::uint8_t>> {
    const auto found = tables.find(n);
    if (found == tables.end()) return std::nullopt;
    return std::vector<std::uint8_t>(found->second.begin(), found->second.end());
  };
  REQUIRE(ragexe::res::LoadItemTables(read, {}, items, &error));
  ragexe::world::GroundItem item = ragexe::world::MakeGroundItem(v[11]);
  auto& o = item.object;
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (const std::uint32_t x :
       {Bits(o.scale), o.key_color, o.keyed_at, o.depth_up ? 1u : 0u, o.keyed ? 1u : 0u,
        o.visible ? 1u : 0u, o.shadow ? 1u : 0u, o.pickable ? 1u : 0u})
    h = Fold(h, x);
  o.visible = v[9] != 0;
  o.scale = std::bit_cast<float>(v[10]);
  ragexe::world::GroundItemArrival record;
  record.name = bytes(name).substr(0, 0x1f);
  record.name = record.name.substr(0, record.name.find('\0'));
  record.id = static_cast<std::int32_t>(v[1]);
  record.cell_x = static_cast<std::int32_t>(v[2]);
  record.cell_y = static_cast<std::int32_t>(v[3]);
  record.sub_x = static_cast<std::int32_t>(v[4]);
  record.sub_y = static_cast<std::int32_t>(v[5]);
  record.dropped = static_cast<std::int32_t>(v[6]);
  record.count = static_cast<std::int32_t>(v[7]);
  record.identified = (v[8] & 0xff) != 0;
  if (v[0] == 4) ragexe::world::ArriveGroundItem(item, record, map, {&items, format}, v[12]);
  for (const std::uint32_t x : {static_cast<std::uint32_t>(item.id), Bits(o.position[0]),
                                Bits(o.position[1]), Bits(o.position[2]), item.falling ? 1u : 0u,
                                Bits(item.fall_speed), Bits(item.drop_height), o.start_time})
    h = Fold(h, x);
  h = FoldName(FoldName(FoldName(h, item.label), item.sprite_name), item.act_name);
  int updates = 0;
  for (std::string token; fields >> token;) {
    ragexe::world::UpdateGroundItem(item, map,
                                    static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
    ++updates;
    for (const std::uint32_t x :
         {1u, Bits(o.position[1]), item.falling ? 1u : 0u, o.keyed ? 1u : 0u})
      h = Fold(h, x);
  }
  if (const std::optional<float> shadow = ragexe::world::GroundItemShadowScale(item)) {
    for (const std::uint32_t x : {3u, 1u, Bits(*shadow), 2u}) h = Fold(h, x);
  } else {
    h = Fold(h, 0);
  }
  char line[96];
  std::snprintf(line, sizeof line, "%s %d %08x %d %016llx", item.falling ? "falling" : "lying",
                updates, Bits(o.position[1]), o.keyed ? 1 : 0, static_cast<unsigned long long>(h));
  return line;
}

// tools/path_oracle.py's k: a CCorpse made by CGameMode::AddCorpse, then faded.
std::string FadeCorpse(std::istringstream& fields) {
  std::string names[2];
  for (std::string& name : names) {
    std::string hex;
    fields >> hex;
    if (hex == "-") continue;
    for (std::size_t k = 0; k + 1 < hex.size(); k += 2)
      name.push_back(static_cast<char>(std::stoi(hex.substr(k, 2), nullptr, 16)));
  }
  std::vector<std::uint32_t> v;
  for (std::string token; fields >> token;)
    v.push_back(static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
  REQUIRE(v.size() >= 9);
  ragexe::world::Corpse left;
  left.position = {std::bit_cast<float>(v[0]), std::bit_cast<float>(v[1]),
                   std::bit_cast<float>(v[2])};
  left.act = names[0];
  left.sprite = names[1];
  left.action = static_cast<std::int32_t>(v[3]);
  left.frame = static_cast<std::int32_t>(v[4]);
  left.facing = std::bit_cast<float>(v[5]);
  ragexe::res::Act act;
  act.actions.assign(256, std::vector<ragexe::res::ActFrame>(v[7]));
  ragexe::world::CorpseObject corpse = ragexe::world::MakeCorpse(left, v[8]);
  const auto& o = corpse.object;
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (const std::uint32_t x :
       {o.keyed_at, o.start_time, o.depth_up ? 1u : 0u, o.visible ? 1u : 0u, 1u,
        Bits(o.position[0]), Bits(o.position[1]), Bits(o.position[2]),
        static_cast<std::uint32_t>(o.base_action), static_cast<std::uint32_t>(o.action),
        static_cast<std::uint32_t>(o.frame), Bits(o.facing)})
    h = Fold(h, x);
  h = FoldName(FoldName(h, corpse.act_name), corpse.sprite_name);
  int updates = 0;
  bool kept = true;
  for (std::size_t k = 9; k < v.size() && kept; ++k) {
    kept = ragexe::world::UpdateCorpse(corpse, act, std::bit_cast<float>(v[6]), v[k]);
    ++updates;
    for (const std::uint32_t x : {kept ? 1u : 0u, static_cast<std::uint32_t>(o.action),
                                  static_cast<std::uint32_t>(o.frame), o.color})
      h = Fold(h, x);
  }
  char line[64];
  std::snprintf(line, sizeof line, "%s %d %08x %016llx", kept ? "fading" : "gone", updates, o.color,
                static_cast<unsigned long long>(h));
  return line;
}

// tools/path_oracle.py's l: the world's lists, op by op.
std::string KeepLists(const AttrMap& map, std::istringstream& fields) {
  constexpr const char* kTables[] = {"IdNum2ItemDisplayNameTable.txt",
                                     "Num2ItemDisplayNameTable.txt", "IdNum2ItemResNameTable.txt",
                                     "Num2ItemResNameTable.txt"};
  std::map<std::string, std::vector<std::uint8_t>, std::less<>> tables;
  for (const char* table : kTables) {
    std::string hex;
    fields >> hex;
    std::vector<std::uint8_t> text;
    for (std::size_t k = 0; hex != "-" && k + 1 < hex.size(); k += 2)
      text.push_back(static_cast<std::uint8_t>(std::stoi(hex.substr(k, 2), nullptr, 16)));
    if (!text.empty()) tables.emplace(table, std::move(text));
  }
  std::vector<std::uint32_t> v;
  for (std::string token; fields >> token;)
    v.push_back(static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
  ragexe::res::ItemTables items;
  std::string error;
  REQUIRE(ragexe::res::LoadItemTables(
      [&tables](std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
        const auto found = tables.find(name);
        if (found == tables.end()) return std::nullopt;
        return found->second;
      },
      {}, items, &error));
  ragexe::world::WorldObjects world;
  std::vector<std::int32_t> picked;
  std::map<const void*, std::uint32_t> numbers;
  std::map<const void*, std::uint32_t> lives;
  const auto address = [](const ragexe::world::WorldObject& object) {
    return std::visit([](const auto& x) -> const void* { return &x; }, object);
  };
  const auto number = [&numbers](const void* at) -> std::uint32_t {
    const auto found = numbers.find(at);
    return found == numbers.end() ? 0 : found->second;
  };
  const auto listed = [&] {
    std::vector<std::uint32_t> out;
    for (const auto& object : world.objects()) out.push_back(number(address(*object)));
    return out;
  };
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (std::size_t k = 0; k < v.size();) {
    const std::uint32_t op = v[k];
    const std::vector<std::uint32_t> before = listed();
    std::uint32_t result = 0;
    std::uint64_t names = h;
    bool named = false;
    const auto id = [&v, k] { return static_cast<std::int32_t>(v[k + 1]); };
    if (op == 0) {
      const void* made = nullptr;
      if (v[k + 1] == 2) {
        made = &world.CreatePlayer({});
      } else if (v[k + 1] == 1) {
        GameActor actor;
        actor.id = static_cast<std::int32_t>(v[k + 2]);
        made = &world.AddActor(std::move(actor));
      } else {
        made = address(world.Add(ragexe::world::CorpseObject{}));
      }
      numbers[made] = static_cast<std::uint32_t>(numbers.size() + 1);
      lives[made] = v[k + 3];
      k += 4;
    } else if (op == 1) {
      const bool dropped = v[k + 1] != 0;
      std::array<std::uint8_t, ragexe::world::kItemPacketSize> packet{};
      const auto put = [&packet](std::size_t at, std::uint32_t x, std::size_t n) {
        for (std::size_t b = 0; b < n; ++b)
          packet[at + b] = static_cast<std::uint8_t>(x >> (8 * b));
      };
      put(0, dropped ? 0x9e : 0x9d, 2);
      put(2, v[k + 2], 4);
      put(6, v[k + 3], 2);
      put(8, v[k + 4], 1);
      put(9, v[k + 5], 2);
      put(0xb, v[k + 6], 2);
      put(dropped ? 0xd : 0xf, v[k + 7], 1);
      put(dropped ? 0xe : 0x10, v[k + 8], 1);
      put(dropped ? 0xf : 0xd, v[k + 9], 2);
      const ragexe::world::GroundItemArrival record =
          dropped ? ragexe::world::ItemDropped(packet) : ragexe::world::ItemInView(packet);
      const bool fresh = world.FindItem(record.id) == nullptr;
      const ragexe::world::GroundItem& item =
          ragexe::world::ShowGroundItem(world, record, map, {&items, "%s: %d ea"}, v[k + 10]);
      if (fresh) numbers[&item] = static_cast<std::uint32_t>(numbers.size() + 1);
      result = number(world.FindItem(record.id));
      names = FoldName(FoldName(FoldName(h, item.label), item.sprite_name), item.act_name);
      named = true;
      k += 11;
    } else if (op == 2) {
      ragexe::world::RemoveGroundItem(world, &picked, id());
      k += 2;
    } else if (op == 3) {
      picked.push_back(id());
      k += 2;
    } else if (op == 4) {
      result = world.DeleteActor(id()) ? 1 : 0;
      k += 2;
    } else if (op == 5 || op == 6) {
      result = op == 5 ? number(world.FindActor(id())) : number(world.FindItem(id()));
      k += 2;
    } else if (op == 7) {
      const std::uint32_t at = v[k + 1];
      world.Update([&](ragexe::world::WorldObject& object) {
        if (auto* item = std::get_if<ragexe::world::GroundItem>(&object)) {
          ragexe::world::UpdateGroundItem(*item, map, at);
          return true;
        }
        std::uint32_t& left = lives[address(object)];
        if (left == 0) return false;
        --left;
        return true;
      });
      k += 2;
    } else {
      world.Clear();
      k += 1;
    }
    if (named) h = names;
    const std::vector<std::uint32_t> after = listed();
    std::vector<std::uint32_t> deleted;
    for (const std::uint32_t n : before)
      if (std::ranges::find(after, n) == after.end()) deleted.push_back(n);
    std::vector<std::uint32_t> words = {op, result, static_cast<std::uint32_t>(deleted.size())};
    words.insert(words.end(), deleted.begin(), deleted.end());
    words.push_back(static_cast<std::uint32_t>(after.size()));
    words.insert(words.end(), after.begin(), after.end());
    words.push_back(static_cast<std::uint32_t>(world.actors().size()));
    for (const GameActor* actor : world.actors()) words.push_back(number(actor));
    words.push_back(static_cast<std::uint32_t>(world.items().size()));
    for (const ragexe::world::GroundItem* item : world.items()) words.push_back(number(item));
    words.push_back(number(world.player()));
    words.push_back(static_cast<std::uint32_t>(picked.size()));
    words.push_back(static_cast<std::uint32_t>(picked.size()));
    for (const std::int32_t picked_id : picked)
      words.push_back(static_cast<std::uint32_t>(picked_id));
    for (const ragexe::world::GroundItem* item : world.items()) {
      const auto& o = item->object;
      words.insert(words.end(), {static_cast<std::uint32_t>(item->id), Bits(o.position[0]),
                                 Bits(o.position[1]), Bits(o.position[2]), item->falling ? 1u : 0u,
                                 o.keyed_at, o.start_time, o.keyed ? 1u : 0u});
    }
    for (const std::uint32_t x : words) h = Fold(h, x);
  }
  char line[64];
  std::snprintf(line, sizeof line, "%zu %zu %zu %016llx", world.objects().size(),
                world.actors().size(), world.items().size(), static_cast<unsigned long long>(h));
  return line;
}

struct ActorMakeVector {
  std::uint32_t kind;  // 0 CNpc, 1 CPc
  std::uint32_t now;
  std::uint32_t server_offset;
  std::size_t count;
  const char* set;
  std::array<std::uint32_t, 98> values;
};

#include "actor_make_vectors.inc"

std::uint32_t Word(float f) { return std::bit_cast<std::uint32_t>(f); }
std::uint32_t Word(bool b) { return b ? 1u : 0u; }
std::uint32_t Word(std::int32_t i) { return static_cast<std::uint32_t>(i); }
std::uint32_t Word(std::uint32_t u) { return u; }

// The port's field at the original's offset in a CGameActor, or a CPc when `player` is given.
std::optional<std::uint32_t> ActorWord(const GameActor& a, const ragexe::world::PlayerActor* player,
                                       std::uint32_t offset) {
  const ragexe::render::RenderObject& o = a.object;
  if (offset >= 0x244 && offset <= 0x26c && offset % 4 == 0)
    return Word(a.effect_slots[(offset - 0x244) / 4]);
  switch (offset) {
    case 0x4: return Word(o.position[0]);
    case 0x8: return Word(o.position[1]);
    case 0xc: return Word(o.position[2]);
    case 0x20: return Word(o.base_action);
    case 0x24: return Word(o.action);
    case 0x28: return Word(o.frame);
    case 0x2c: return Word(o.sounded_action);
    case 0x30: return Word(o.sounded_frame);
    case 0x38: return Word(o.facing);
    case 0x3c: return Word(o.scale);
    case 0x40: return Word(o.shadow_scale);
    case 0x44: return Word(o.delay);
    case 0x48: return Word(o.pixel_scale);
    case 0x4c: return Word(o.repeats);
    case 0x50: return Word(o.delay_scale);
    case 0x54: return Word(o.play_mode);
    case 0x58: return Word(a.state);
    case 0x5c: return Word(o.offset_x);
    case 0x60: return Word(o.angle);
    case 0x64: return Word(o.depth_index);
    case 0x68: return Word(o.key_color);
    case 0x6c: return Word(o.keyed_at);
    case 0x70: return Word(o.start_time);
    case 0x7c: return Word(o.color);
    case 0x80: return Word(o.depth_up);
    case 0x81: return Word(o.finished);
    case 0x82: return Word(o.held);
    case 0x83: return Word(o.keyed);
    case 0x84: return Word(o.visible);
    case 0x85: return Word(o.body_shown);
    case 0x86: return Word(o.flat);
    case 0x87: return Word(o.tinted);
    case 0x88: return Word(o.shadow);
    case 0x89: return Word(o.pickable);
    case 0x90: return Word(o.screen_x);
    case 0x94: return Word(o.screen_y);
    case 0xa8: return Word(a.field_0xa8);
    case 0xc0: return Word(a.goal_x);
    case 0xc4: return Word(a.goal_y);
    case 0xc8: return Word(a.speed);
    case 0xcc: return Word(a.field_0xcc);
    case 0xd0: return Word(a.field_0xd0);
    case 0xd8: return Word(a.field_0xd8);
    case 0x18c: return Word(a.move_time);
    case 0x190: return Word(a.pushed);
    case 0x194: return Word(a.segment);
    case 0x198: return Word(a.travelled);
    case 0x19c: return Word(a.processed_at);
    case 0x1a0: return Word(a.server_at);
    case 0x1a4: return Word(a.balloon_at);
    case 0x1a8: return Word(a.target_id);
    case 0x1ac: return Word(a.field_0x1ac);
    case 0x1b4: return Word(a.id);
    case 0x1b8: return Word(a.job);
    case 0x1bc: return Word(a.field_0x1bc);
    case 0x1c0: return Word(a.balloon);
    case 0x1c4: return Word(a.chat_title);
    case 0x1c8: return Word(a.shop_title);
    case 0x1cc: return Word(a.cast_gauge);
    case 0x1d0: return Word(a.hold_until);
    case 0x1d4: return Word(a.pet_talk_at);
    case 0x1d8: return Word(a.cast_end);
    case 0x1dc: return Word(a.cast_start);
    case 0x1e0: return Word(a.balloon_width);
    case 0x1e4: return Word(a.balloon_height);
    case 0x1ec: return Word(a.field_0x1ec);
    case 0x1f0: return Word(a.field_0x1f0);
    case 0x1f4: return Word(a.field_0x1f4);
    case 0x210: return Word(a.field_0x210);
    case 0x218: return Word(a.body_state);
    case 0x21c: return Word(a.effect_state);
    case 0x220: return Word(a.health_state);
    case 0x224: return Word(a.field_0x224);
    case 0x228: return Word(a.target_x);
    case 0x22c: return Word(a.target_z);
    case 0x230: return Word(a.sight_ticks);
    case 0x234: return Word(a.fade_until);
    case 0x238: return Word(a.model_kind);
    case 0x23c: return Word(a.guild);
    case 0x240: return Word(a.emblem);
    case 0x290: return Word(a.repeated_at);
    default: break;
  }
  if (player == nullptr) return std::nullopt;
  switch (offset) {
    case 0x28c: return Word(player->field_0x28c);
    case 0x294: return Word(player->saved_frame);
    case 0x2a0: return Word(player->weapon);
    case 0x2dc: return Word(player->gauge);
    case 0x2e0: return Word(player->field_0x2e0);
    case 0x2e4: return Word(player->field_0x2e4);
    default: return std::nullopt;
  }
}

struct NpcStateVector {
  std::array<std::uint32_t, 37> in;
  std::array<std::uint32_t, 48> out;
};

#include "npc_state_vectors.inc"

// tools/npc_state_vectors.py's body: a delay for each action to 0x47.
ragexe::res::Act NpcStateAct(std::size_t frames) {
  ragexe::res::Act act;
  act.actions.assign(256, std::vector<ragexe::res::ActFrame>(frames));
  for (std::int32_t a = 0; a < 0x48; ++a)
    act.delays.push_back(1.0f + static_cast<float>(a % 7) * 0.75f);
  return act;
}

// CNpc::SetState on a CNpc made at the vector's time and given its fields, as the vector's words.
std::array<std::uint32_t, 48> RunNpcState(const NpcStateVector& v) {
  const auto& in = v.in;
  const auto i = [&](std::size_t k) { return static_cast<std::int32_t>(in[k]); };
  const auto f = [&](std::size_t k) { return std::bit_cast<float>(in[k]); };
  GameActor npc = ragexe::world::MakeNpc(in[35], in[35] - 72);
  ragexe::render::RenderObject& o = npc.object;
  npc.job = i(0);
  npc.state = i(1);
  npc.body_state = i(3);
  npc.field_0x224 = i(4);
  npc.field_0x1ec = i(5);
  npc.field_0x1f0 = i(6);
  npc.portal_effect = i(7);
  o.body_shown = in[8] != 0;
  o.action = i(9);
  o.base_action = i(10);
  o.frame = i(11);
  o.facing = f(12);
  o.position = {f(14), f(15), f(16)};
  npc.target_x = f(17);
  npc.target_z = f(18);
  npc.id = i(19);
  npc.target_id = i(20);
  o.delay = f(21);
  npc.effect_state = in[26];
  o.finished = in[27] != 0;
  o.held = in[28] != 0;
  o.pickable = in[29] != 0;
  o.depth_up = in[30] != 0;
  o.play_mode = i(31);
  o.start_time = in[32];
  npc.pet_talk_at = in[33];
  npc.cast_gauge = i(34);
  AttrMap map;
  map.width = 40;
  map.height = 30;
  const ragexe::res::Act act = NpcStateAct(in[22]);
  const ragexe::world::PetTalk pet{i(23), 0, 0, i(24), i(25)};
  std::vector<ragexe::world::ModeMessage> messages, own;
  std::vector<std::int32_t> launched;
  std::vector<ragexe::world::Arrow> arrows;
  std::vector<ragexe::world::MandraAttack> mandra;
  ragexe::world::StateContext context{};
  context.attr = &map;
  context.act = &act;
  context.camera_yaw = f(13);
  context.service_type = 1;
  context.messages = &messages;
  context.actor_messages = &own;
  context.pet = &pet;
  context.launched = &launched;
  context.arrows = &arrows;
  context.mandra_attacks = &mandra;
  ragexe::world::SetGameActorState(npc, i(2), context, in[36]);
  std::vector<std::uint32_t> got;
  for (const float x : {o.position[0], o.position[1], o.position[2]}) got.push_back(Word(x));
  for (const std::int32_t x : {o.base_action, o.action, o.frame, o.play_mode})
    got.push_back(Word(x));
  for (const float x : {o.delay, o.repeats, o.delay_scale}) got.push_back(Word(x));
  got.push_back(Word(npc.state));
  got.push_back(o.start_time);
  got.push_back(npc.pet_talk_at);
  for (const std::int32_t x : {npc.field_0x224, npc.field_0x1ec, npc.field_0x1f0})
    got.push_back(Word(x));
  for (const float x : npc.portal_area) got.push_back(Word(x));
  got.push_back(Word(npc.portal_effect));
  got.push_back(Word(npc.field_0x1ac));
  got.push_back(Word(npc.cast_gauge));
  for (const bool x : {o.depth_up, o.finished, o.held, o.pickable, npc.pushed})
    got.push_back(Word(x));
  got.push_back(static_cast<std::uint32_t>(messages.size()));
  for (const auto& m : messages)
    for (const std::int32_t x : {m.id, m.a, m.b, m.c}) got.push_back(Word(x));
  got.push_back(static_cast<std::uint32_t>(own.size()));
  for (const auto& m : own) got.push_back(Word(m.id));
  got.push_back(static_cast<std::uint32_t>(launched.size()));
  for (const std::int32_t x : launched) got.push_back(Word(x));
  got.push_back(static_cast<std::uint32_t>(arrows.size()));
  for (const auto& a : arrows) {
    got.push_back(Word(a.target_id));
    got.push_back(Word(a.speed));
  }
  got.push_back(static_cast<std::uint32_t>(mandra.size()));
  for (const auto& m : mandra) {
    got.push_back(Word(m.target_id));
    got.push_back(Word(m.job));
  }
  std::array<std::uint32_t, 48> out{};
  REQUIRE(got.size() <= out.size());
  std::copy(got.begin(), got.end(), out.begin());
  return out;
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

TEST_CASE("Actors and players act on targets as the original's messages 0xb and 0x1b and states",
          "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kAttackQueries) {
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
                                        : Go(map, finder, path, fields, kind == 'J', false, false,
                                             false, false, false, false, true);
    CHECK(got == v.result);
  }
}

TEST_CASE("Hits land on actors and players as the original's messages and 00503f80 deliver them",
          "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kLandQueries) {
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
                                        : Go(map, finder, path, fields, kind == 'O', false, false,
                                             false, false, false, false, false, true);
    CHECK(got == v.result);
  }
}

TEST_CASE(
    "Actors and players cast, and their windows follow them, as the original's message "
    "0x52, state 13 and 00503210",
    "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kCastQueries) {
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
                                        : Go(map, finder, path, fields, kind == 'R', false, false,
                                             false, false, false, false, false, false, true);
    CHECK(got == v.result);
  }
}

TEST_CASE(
    "Actors and players update whole, as the original's CNpc::Update and CPc::Update: Sight, the "
    "key colour, the colour, the frames' sounds and the gauge",
    "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kUpdateQueries) {
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
                                        : Go(map, finder, path, fields, kind == 'p', false, false,
                                             false, false, false, false, false, false, false, true);
    CHECK(got == v.result);
  }
}

TEST_CASE(
    "The own player updates whole, as the original's CPlayer::Update: PreMove and the pursuit of "
    "its target",
    "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kOwnUpdateQueries) {
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
                                        : Go(map, finder, path, fields, true, false, false, false,
                                             false, false, false, false, false, false, true, true);
    CHECK(got == v.result);
  }
}

TEST_CASE(
    "The own player's messages are the original's CPlayer::SendMsg: its orders, the look, the "
    "walk asked and the moves",
    "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kOwnOrderQueries) {
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
    const std::string got = Go(map, finder, path, fields, true, false, false, false, false, false,
                               false, false, false, false, false, true, true);
    CHECK(got == v.result);
  }
}

TEST_CASE(
    "The players' own messages are the original's CPc::SendMsg: the arrivals and the look, the "
    "gauge and the rest",
    "[walk]") {
  int current = -1;
  AttrMap map;
  PathFinder finder;
  MovePath path;
  for (const StateQueryVector& v : kLookQueries) {
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
    const std::string got = Go(map, finder, path, fields, true, false, false, false, false, true,
                               false, false, false, false, false, false, false, true);
    CHECK(got == v.result);
  }
}

TEST_CASE("A player's weapon by item ids is CPc::SetWeapon's, one in each hand or its own sprite",
          "[walk]") {
  const auto fnv = [](const std::string& name) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const char c : name) h = (h ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
    return h;
  };
  for (const WeaponItemVector& v : kWeaponItems) {
    INFO(v.job << " " << v.sex << " " << v.word << " " << v.exists);
    ragexe::world::PlayerActor player;
    player.job = v.job;
    player.field_0x1bc = v.sex;
    ragexe::world::StateContext context;
    context.resource_exists = [&v](const std::string&) { return v.exists != 0; };
    ragexe::world::SetPlayerWeapon(player, static_cast<std::int32_t>(v.word), context);
    CHECK(static_cast<std::uint32_t>(player.weapon) == v.weapon);
    const auto& layers = player.look.layers;
    CHECK(fnv(layers[5].act) == v.act5);
    CHECK(fnv(layers[5].sprite) == v.sprite5);
    CHECK(fnv(layers[6].act) == v.act6);
    CHECK(fnv(layers[6].sprite) == v.sprite6);
  }
}

TEST_CASE("The sounds of hits are ClientState 00599a00's and 00599df0's", "[walk]") {
  for (const HitSoundVector& v : kHitSounds) {
    INFO(v.player << " " << v.value << " " << v.seed);
    ragexe::core::CrtRand rand(v.seed);
    const std::string_view got = v.player != 0 ? ragexe::world::PlayerHitSound(v.value, rand)
                                               : ragexe::world::WeaponHitSound(v.value, rand);
    CHECK(got == v.name);
  }
}

TEST_CASE("CAbleToMakeEffect::LaunchEffect makes its own few, and the rest with effects on",
          "[walk]") {
  for (const LaunchVector& v : kLaunches) {
    INFO(v.id << " " << v.effects_on);
    ragexe::world::GameActor actor;
    std::vector<std::int32_t> launched;
    ragexe::world::StateContext context;
    context.launched = &launched;
    context.effects_on = v.effects_on != 0;
    const std::int32_t made = ragexe::world::LaunchGameActorEffect(actor, v.id, context);
    CHECK((made != 0 ? 1 : 0) == v.made);
    CHECK(ragexe::world::LaunchesEffect(v.id, v.effects_on != 0) == (v.made != 0));
    CHECK(actor.field_0xa8 == v.field_0xa8);
    CHECK(launched == std::vector<std::int32_t>{v.id});
  }
}

TEST_CASE("Weapons' kinds are ClientState::MapRangedIdToKind's", "[walk]") {
  for (const WeaponKindVector& v : kWeaponKinds) {
    INFO(v.id);
    CHECK(ragexe::world::WeaponKind(v.id) == v.kind);
  }
}

TEST_CASE("A player's attack takes ClientState 0059a440's action and 0059a3e0's frame", "[walk]") {
  for (const AttackChoiceVector& v : kAttackChoices) {
    INFO(v.job << " " << v.field_0x1bc << " " << v.weapon);
    CHECK(ragexe::world::AttacksWithSecondAction(v.job, v.field_0x1bc, v.weapon) ==
          (v.second != 0));
    CHECK(Bits(ragexe::world::PlayerAttackFrame(v.job, v.field_0x1bc, v.weapon, false)) == v.frame);
    CHECK(Bits(ragexe::world::PlayerAttackFrame(v.job, v.field_0x1bc, v.weapon, true)) ==
          v.second_frame);
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

TEST_CASE(
    "Items on the ground and corpses are the original's CItem and CCorpse: the drop, the label, "
    "the blink and the fade",
    "[walk]") {
  int current = -1;
  AttrMap map;
  for (const StateQueryVector& v : kItemQueries) {
    if (v.map != current) {
      current = v.map;
      if (current < 3) {
        map = MakeMap(kWalkMaps[current]);
      } else {
        map = AttrMap();
        map.width = map.height = 4;
        map.cells.resize(16);
        for (auto& cell : map.cells) cell.heights.fill(kDeepGround);
      }
    }
    INFO(current << ": " << v.query);
    std::istringstream fields(v.query);
    char kind = 0;
    fields >> kind;
    const std::string got = kind == 'i' ? DropItem(map, fields) : FadeCorpse(fields);
    CHECK(got == v.result);
  }
}

TEST_CASE(
    "The world's lists are the original's CWorld's: updates, finds, deletes and the item packets",
    "[walk]") {
  int current = -1;
  AttrMap map;
  for (const StateQueryVector& v : kListQueries) {
    if (v.map != current) {
      current = v.map;
      map = MakeMap(kWalkMaps[current]);
    }
    INFO(current << ": " << v.query);
    std::istringstream fields(v.query);
    char kind = 0;
    fields >> kind;
    CHECK(KeepLists(map, fields) == v.result);
  }
}

TEST_CASE("Objects that join the world's list during an update are updated in that update",
          "[walk]") {
  // CWorld::UpdateGameObjects reads the next node after each update (004c4aa1), so a corpse an
  // actor leaves at the end is reached in the same pass.
  ragexe::world::WorldObjects world;
  GameActor actor;
  actor.id = 1;
  world.AddActor(std::move(actor));
  std::vector<std::size_t> updated;
  world.Update([&](ragexe::world::WorldObject& object) {
    updated.push_back(object.index());
    if (std::holds_alternative<GameActor>(object)) {
      world.Add(ragexe::world::CorpseObject{});
      return false;
    }
    return true;
  });
  CHECK(updated == std::vector<std::size_t>{0, 3});
  CHECK(world.objects().size() == 1);
  CHECK(world.actors().empty());
}

TEST_CASE("The own player let go by its update is no longer the world's player", "[walk]") {
  ragexe::world::WorldObjects world;
  world.CreatePlayer({});
  CHECK(world.player() != nullptr);
  CHECK_FALSE(world.DeleteActor(0));
  world.Update([](ragexe::world::WorldObject&) { return false; });
  CHECK(world.player() == nullptr);
  CHECK(world.objects().empty());
}

TEST_CASE("An item sent message 4 again falls only if this one drops it", "[walk]") {
  // CItem::SendMsg clears +0xb4 for a record that does not drop the item (0053b0d0).
  AttrMap map;
  map.width = map.height = 2;
  map.cells.resize(4);
  const ragexe::res::ItemTables tables;
  ragexe::world::GroundItem item = ragexe::world::MakeGroundItem(0);
  ragexe::world::GroundItemArrival record;
  record.dropped = 1;
  ragexe::world::ArriveGroundItem(item, record, map, {&tables, ""}, 0);
  CHECK(item.falling);
  record.dropped = 0;
  ragexe::world::ArriveGroundItem(item, record, map, {&tables, ""}, 0);
  CHECK_FALSE(item.falling);
  CHECK(item.object.position[1] == 0.0f);
}

TEST_CASE("Actors start as the original's CNpc and CPc constructors leave them", "[walk]") {
  for (const ActorMakeVector& v : kActorMakes) {
    const std::uint32_t server_time = v.now - 72 - v.server_offset;
    const ragexe::world::PlayerActor player = ragexe::world::MakePlayerActor(v.now, server_time);
    const GameActor npc = ragexe::world::MakeNpc(v.now, server_time);
    const GameActor& actor = v.kind == 1 ? static_cast<const GameActor&>(player) : npc;
    for (std::size_t k = 0; k < v.count; ++k) {
      if (v.set[k] != '1') continue;
      INFO("kind " << v.kind << " at " << v.now << ", offset 0x" << std::hex
                   << kActorMakeOffsets[k]);
      const std::optional<std::uint32_t> got =
          ActorWord(actor, v.kind == 1 ? &player : nullptr, kActorMakeOffsets[k]);
      REQUIRE(got.has_value());
      CHECK(*got == v.values[k]);
    }
  }
}

TEST_CASE("NPCs and monsters change state as the original's CNpc::SetState", "[walk]") {
  for (std::size_t k = 0; k < std::size(kNpcStates); ++k) {
    const NpcStateVector& v = kNpcStates[k];
    INFO("vector " << k << ": job " << v.in[0] << ", state " << static_cast<std::int32_t>(v.in[2])
                   << " from " << v.in[1]);
    CHECK(RunNpcState(v) == v.out);
  }
}

TEST_CASE("A pet's lines are ClientState 005a9f40's and 005aa3e0's", "[walk]") {
  CHECK(ragexe::world::PetLevel(10) == 0);
  CHECK(ragexe::world::PetLevel(11) == 1);
  CHECK(ragexe::world::PetLevel(100) == 4);
  CHECK(ragexe::world::PetLevel(101) == 0);
  CHECK(ragexe::world::PetLine(1002, 7, 2, 1) == 2 + 10 * (7 + 10 * 1002 - 10000) + 50000);
  CHECK(ragexe::world::PetLine(1002, 7, 2, 0) == 2 + 10 * (7 + 10 * 1002 - 10000) + 90000);
}

// CNpc's vf11 (00540210, its byte table 00540280): jobs 0x2d, 0x68, 0x6f and 0x8b report
// nothing; the others, CGameActor's message 0xc with the cell.
TEST_CASE("Four NPC jobs report no cell", "[walk]") {
  AttrMap map;
  map.width = 40;
  map.height = 30;
  for (const std::int32_t job : {0x2c, 0x2d, 0x2e, 0x68, 0x6f, 0x8a, 0x8b, 0x8c, 1002}) {
    GameActor npc = ragexe::world::MakeNpc(0, 0);
    npc.job = job;
    std::vector<ragexe::world::ModeMessage> messages;
    ragexe::world::StateContext context{};
    context.attr = &map;
    context.messages = &messages;
    ragexe::world::ReportGameActorCell(npc, context);
    const bool silent = job == 0x2d || job == 0x68 || job == 0x6f || job == 0x8b;
    INFO("job " << job);
    CHECK(messages.size() == (silent ? 0u : 1u));
  }
}

struct OwnMakeVector {
  std::array<std::uint32_t, 29> in;
  std::array<std::uint32_t, 256> out;
};

#include "own_make_vectors.inc"

// CPlayer's constructor on the vector's map and session, as the vector's words.
std::vector<std::uint32_t> RunOwnMake(const OwnMakeVector& v) {
  const auto& in = v.in;
  const auto i = [&](std::size_t k) { return static_cast<std::int32_t>(in[k]); };
  const AttrMap map = MakeMap(kWalkMaps[in[0]]);
  ragexe::world::OwnSession session;
  session.cell_x = i(2);
  session.cell_y = i(3);
  session.direction = i(4);
  session.id = i(5);
  session.speed = i(6);
  session.job = i(7);
  session.sex = i(8);
  session.hair = i(9);
  session.weapon = i(10);
  session.shield = i(11);
  session.headgear = {i(12), i(13), i(14)};
  session.body_color = i(15);
  session.hair_color = i(16);
  session.body_state = i(17);
  session.health_state = in[18];
  session.effect_state = in[19];
  session.guild = i(20);
  session.emblem = i(21);
  const auto first = in.begin() + 26;
  const std::vector<std::int32_t> statuses(first, first + in[25]);
  const ragexe::res::Act act = NpcStateAct(1);
  std::vector<ragexe::world::ModeMessage> messages, own;
  std::vector<std::int32_t> launched;
  std::vector<ragexe::world::EffectMessage> sent;
  std::int32_t field_0x2a0 = 0x5a5a5a5a;
  ragexe::world::StateContext context{};
  context.attr = &map;
  context.act = &act;
  context.service_type = 1;
  context.messages = &messages;
  context.actor_messages = &own;
  context.launched = &launched;
  context.launched_messages = &sent;
  context.effects_on = in[24] != 0;
  context.own_statuses = &statuses;
  context.mode_field_0x2a0 = &field_0x2a0;
  context.own_id = session.id;
  const ragexe::world::PlayerActor p =
      ragexe::world::MakeOwnPlayer(session, {i(22), i(23)}, context, in[1], in[1] - 72);
  const ragexe::render::RenderObject& o = p.object;
  std::vector<std::uint32_t> got;
  for (const float x : {o.position[0], o.position[1], o.position[2], o.facing})
    got.push_back(Word(x));
  for (const std::int32_t x :
       {p.id, p.guild, p.emblem, p.segment, p.speed, p.state, o.base_action, o.action, o.frame})
    got.push_back(Word(x));
  got.push_back(Word(o.delay));
  got.push_back(Word(o.play_mode));
  got.push_back(o.start_time);
  got.push_back(Word(p.field_0x1bc));
  got.push_back(Word(p.body_state));
  got.push_back(p.effect_state);
  got.push_back(p.health_state);
  got.push_back(o.color);
  got.push_back(Word(o.screen_x));
  got.push_back(Word(o.screen_y));
  for (const bool x : {o.finished, o.held, o.visible, o.body_shown, o.tinted, o.shadow, o.pickable})
    got.push_back(Word(x));
  const ragexe::world::OwnPlayer& w = *p.own;
  for (const std::int32_t x : {w.goal_x, w.goal_y}) got.push_back(Word(x));
  got.push_back(w.acted_at);
  got.push_back(w.premove_at);
  for (const std::int32_t x : {w.pursuit, w.premove, w.attack_mode, w.acted, w.walk_asked,
                               w.move_queued, w.target_id, w.acts})
    got.push_back(Word(x));
  got.push_back(w.asked_at);
  got.push_back(w.walk_asked_at);
  for (const std::int32_t x : {w.skill, w.range, w.level, w.cell_x, w.cell_y, w.queued_x,
                               w.queued_y, w.status_icons, field_0x2a0})
    got.push_back(Word(x));
  const ragexe::world::PlayerDress d = ragexe::world::OwnDress(session);
  for (const std::int32_t x : {d.job, d.sex, d.hair, d.weapon, d.headgear[0], d.headgear[1],
                               d.headgear[2], d.shield, d.hair_color, d.body_color})
    got.push_back(Word(x));
  got.push_back(static_cast<std::uint32_t>(messages.size()));
  for (const auto& m : messages)
    for (const std::int32_t x : {m.id, m.a, m.b, m.c}) got.push_back(Word(x));
  got.push_back(static_cast<std::uint32_t>(own.size()));
  for (const auto& m : own)
    for (const std::int32_t x : {m.id, m.a, m.b, m.c}) got.push_back(Word(x));
  got.push_back(static_cast<std::uint32_t>(launched.size()));
  for (const std::int32_t x : launched) got.push_back(Word(x));
  got.push_back(static_cast<std::uint32_t>(sent.size()));
  for (const auto& m : sent)
    for (const std::int32_t x : {m.effect, m.id, m.a, m.b, m.c}) got.push_back(Word(x));
  got.resize(v.out.size());
  return got;
}

TEST_CASE("The own player is made as the original's CPlayer constructor", "[walk]") {
  for (const OwnMakeVector& v : kOwnMakes) {
    INFO("map " << v.in[0] << " job " << v.in[7] << " effect " << v.in[19] << " body " << v.in[17]
                << " health " << v.in[18]);
    std::vector<std::uint32_t> got = RunOwnMake(v);
    std::vector<std::uint32_t> want(v.out.begin(), v.out.end());
    // +0x1bc: vf14, which the vectors stand in for, makes a bard male and a dancer female
    // (CPc::UpdateBody, look_vectors.inc's).
    if (v.in[7] == 0x13 || v.in[7] == 0x14) got[16] = want[16] = 0;
    CHECK(got == want);
  }
}
