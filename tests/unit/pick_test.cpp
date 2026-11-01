#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>

#include "world/actor_pick.h"

using ragexe::world::ActorPick;
using ragexe::world::ActorPickNode;
using ragexe::world::GameActor;
using ragexe::world::ItemPick;
using ragexe::world::PickPriority;
using ragexe::world::PickRecord;
using ragexe::world::SkillPick;

namespace {

struct PickVector {
  const char* object;  // tools/pick_vectors.py's fields
  const char* record;  // the record inserted, or -
};

struct PickOpVector {
  int script;
  const char* op;
  const char* result;
};

#include "pick_vectors.inc"

float Float(const std::string& token) {
  return std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(token, nullptr, 0)));
}

std::string Hex(float f) {
  char text[16];
  std::snprintf(text, sizeof text, "0x%08x", std::bit_cast<std::uint32_t>(f));
  return text;
}

std::string Text(const PickRecord& r) {
  return Hex(r.left) + ' ' + Hex(r.top) + ' ' + Hex(r.depth) + ' ' + Hex(r.right) + ' ' +
         Hex(r.bottom) + ' ' + Hex(r.depth_2) + ' ' + std::to_string(r.id) + ' ' +
         std::to_string(r.job) + ' ' + std::to_string(r.kind) + ' ' + std::to_string(r.value);
}

std::string Pick(const std::string& line) {
  std::istringstream in(line);
  std::string kind, player, ratio;
  int pickable = 0, shift = 0, aiming = 0, war = 0;
  long long id = 0, job = 0, effect = 0, f1f4 = 0, f224 = 0, guild = 0;
  in >> kind >> pickable >> id >> job >> effect >> f1f4 >> f224 >> guild >> shift >> aiming >>
      war >> player >> ratio;
  std::array<float, 4> bounds{};
  std::string token;
  for (float& b : bounds) {
    in >> token;
    b = Float(token);
  }
  in >> token;
  const float inverse_z = Float(token);
  if (!pickable) return "-";
  GameActor actor;
  actor.id = static_cast<std::int32_t>(id);
  actor.job = static_cast<std::int32_t>(job);
  actor.effect_state = static_cast<std::uint32_t>(effect);
  actor.field_0x1f4 = static_cast<std::int32_t>(f1f4);
  actor.field_0x224 = static_cast<std::int32_t>(f224);
  actor.guild = static_cast<std::int32_t>(guild);
  PickPriority priority;
  priority.shift = shift != 0;
  priority.aiming = aiming != 0;
  priority.guild_war = war != 0;
  if (player != "-") priority.player_guild = static_cast<std::int32_t>(std::stoll(player));
  const float width_ratio = Float(ratio);
  if (kind == "A") {
    const std::int32_t minimum = ragexe::world::PickMinimumSize(width_ratio, 40.0f);
    return Text(ActorPick(actor, bounds, inverse_z, priority, minimum));
  }
  const std::int32_t minimum = ragexe::world::PickMinimumSize(width_ratio, 34.0f);
  if (kind == "I") return Text(ItemPick(actor.id, bounds, inverse_z, minimum));
  return Text(SkillPick(actor.id, ragexe::world::GameActorJob(actor), bounds, inverse_z, minimum));
}

}  // namespace

TEST_CASE("Objects leave the pick records the original's vf05 does", "[pick]") {
  for (const PickVector& v : kPicks) {
    INFO(v.object);
    CHECK(Pick(v.object) == v.record);
  }
}

TEST_CASE("The pick tree files and finds records as CActorPickNode does", "[pick]") {
  int current = -1;
  ActorPickNode node;
  for (const PickOpVector& v : kPickOps) {
    if (v.script != current) {
      current = v.script;
      node = ActorPickNode();
    }
    INFO(v.script << ": " << v.op);
    std::istringstream in(v.op);
    char kind = 0;
    in >> kind;
    std::string got;
    if (kind == 'B') {
      int depth = 0;
      std::int32_t left = 0, top = 0, right = 0, bottom = 0;
      in >> depth >> left >> top >> right >> bottom;
      node.Build(depth, left, top, right, bottom);
    } else if (kind == 'C') {
      node.Clear();
    } else if (kind == 'I') {
      std::string t[6];
      PickRecord r;
      for (auto& s : t) in >> s;
      r.left = Float(t[0]);
      r.top = Float(t[1]);
      r.depth = Float(t[2]);
      r.right = Float(t[3]);
      r.bottom = Float(t[4]);
      r.depth_2 = Float(t[5]);
      in >> r.id >> r.job >> r.kind >> r.value;
      node.Insert(r);
    } else {
      std::string x, y;
      in >> x >> y;
      const PickRecord* found = node.Find(Float(x), Float(y));
      got = found == nullptr ? "-" : std::to_string(found->id);
    }
    CHECK(got == v.result);
  }
}
