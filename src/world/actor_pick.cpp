#include "world/actor_pick.h"

#include <cmath>
#include <cstddef>

namespace ragexe::world {

namespace {

constexpr float kItemBias = 300.0f;   // 005e2738
constexpr float kSkillBias = 350.0f;  // 005ea084

// __ftol (005c2084): fistp qword toward zero, then the low dword. NaN and values beyond 2^63
// give the integer indefinite 0x8000000000000000, whose low dword is 0.
std::int32_t Ftol(float f) {
  if (std::isnan(f) || f >= 9223372036854775808.0f || f < -9223372036854775808.0f) return 0;
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(f)));
}

// The rectangle from the bounds: left and right ordered by fcom (an unordered pair keeps
// them), the top and bottom as they are, the depth the bias and the point's 1/z.
PickRecord Rectangle(const std::array<float, 4>& bounds, float inverse_z, float bias) {
  PickRecord r;
  const bool swapped = bounds[0] > bounds[2];
  r.left = swapped ? bounds[2] : bounds[0];
  r.top = bounds[1];
  r.depth = bias + inverse_z;
  r.right = swapped ? bounds[0] : bounds[2];
  r.bottom = bounds[3];
  r.depth_2 = bias + inverse_z;
  return r;
}

// Half the shortfall, the difference wrapping as on x86 and halved toward zero.
float HalfShortfall(std::int32_t minimum, std::int32_t size) {
  const auto shortfall = static_cast<std::int32_t>(static_cast<std::uint32_t>(minimum) -
                                                   static_cast<std::uint32_t>(size));
  const std::int32_t half = shortfall / 2;
  return static_cast<float>(half);
}

// Each side short of the minimum grows by half the shortfall at both ends.
void Widen(PickRecord& r, std::int32_t minimum) {
  const std::int32_t width = Ftol(r.right - r.left);
  if (width < minimum) {
    const float half = HalfShortfall(minimum, width);
    r.right = half + r.right;
    r.left = r.left - half;
  }
  const std::int32_t height = Ftol(r.bottom - r.top);
  if (height < minimum) {
    const float half = HalfShortfall(minimum, height);
    r.bottom = half + r.bottom;
    r.top = r.top - half;
  }
}

// BoxContainsPointXY (004ce4a0): strictly inside.
bool Contains(const PickRecord& r, float x, float y) {
  return x > r.left && x < r.right && y > r.top && y < r.bottom;
}

// 004ce820: a child's rectangle, split at half the right and the bottom.
std::array<std::int32_t, 4> ChildRect(const std::array<std::int32_t, 4>& rect, int quadrant) {
  auto [left, top, right, bottom] = rect;
  switch (quadrant) {
    case 0:
      right = right / 2;
      bottom = bottom / 2;
      break;
    case 1:
      left = right / 2 + 1;
      bottom = bottom / 2;
      break;
    case 2:
      left = right / 2 + 1;
      top = bottom / 2 + 1;
      break;
    default:
      right = right / 2;
      top = bottom / 2 + 1;
      break;
  }
  return {left, top, right, bottom};
}

}  // namespace

std::int32_t PickMinimumSize(float width_ratio, float side) { return Ftol(width_ratio * side); }

float ActorPickBias(const GameActor& actor, const PickPriority& priority) {
  // Players first or monsters first: Shift and an aimed skill each turn it to players.
  const bool players_first = priority.shift != priority.aiming;
  const std::int32_t job = GameActorJob(actor);
  if (job < 45) {
    if (priority.guild_war) {
      // ClientState 005aa680: the player's guild, when there is a player and a guild.
      const bool same_guild = actor.guild != 0 && priority.player_guild.has_value() &&
                              *priority.player_guild == actor.guild;
      return same_guild == players_first ? 400.0f : 100.0f;
    }
    return players_first ? 400.0f : 100.0f;
  }
  if (job < 1000) return players_first ? 100.0f : 200.0f;
  if (job == 1000) return 0.0f;
  if (actor.field_0x1f4 == 20) return 50.0f;
  return players_first ? 200.0f : 400.0f;
}

PickRecord ActorPick(const GameActor& actor, const std::array<float, 4>& bounds, float inverse_z,
                     const PickPriority& priority, std::int32_t minimum) {
  PickRecord r = Rectangle(bounds, inverse_z, ActorPickBias(actor, priority));
  r.id = actor.id;
  r.job = GameActorJob(actor);
  r.value = actor.field_0x224;
  r.kind = r.job > 1000 && actor.field_0x1f4 == 20 ? kPickMonster20 : kPickActor;
  Widen(r, minimum);
  return r;
}

PickRecord ItemPick(std::int32_t id, const std::array<float, 4>& bounds, float inverse_z,
                    std::int32_t minimum) {
  PickRecord r = Rectangle(bounds, inverse_z, kItemBias);
  r.id = id;
  r.job = 0x580;
  r.kind = kPickItem;
  Widen(r, minimum);
  return r;
}

PickRecord SkillPick(std::int32_t id, std::int32_t job, const std::array<float, 4>& bounds,
                     float inverse_z, std::int32_t minimum) {
  PickRecord r = Rectangle(bounds, inverse_z, kSkillBias);
  r.id = id;
  r.job = job;
  r.kind = kPickSkill;
  Widen(r, minimum);
  return r;
}

void ActorPickNode::Build(int depth, std::int32_t left, std::int32_t top, std::int32_t right,
                          std::int32_t bottom) {
  Clear();
  rect_ = {left, top, right, bottom};
  if (depth <= 0) return;
  for (int q = 0; q < 4; ++q) {
    auto child = std::make_unique<ActorPickNode>();
    const auto [l, t, r, b] = ChildRect(rect_, q);
    child->Build(depth - 1, l, t, r, b);
    children_[static_cast<std::size_t>(q)] = std::move(child);
  }
}

void ActorPickNode::Clear() {
  records_.clear();
  for (auto& child : children_) child.reset();
}

// 004ce6c0: which child a point falls in, by half the right and half the bottom: 0 and 3
// on the left, 1 and 2 on the right, 0 and 1 above.
int ActorPickNode::Quadrant(std::int32_t x, std::int32_t y) const {
  const std::int32_t half_x = rect_[2] / 2;
  const std::int32_t half_y = rect_[3] / 2;
  if (x > half_x) return y <= half_y ? 1 : 2;
  return y <= half_y ? 0 : 3;
}

void ActorPickNode::Insert(const PickRecord& record) {
  ActorPickNode* node = this;
  while (node->children_[0] != nullptr) {
    node = node->children_[static_cast<std::size_t>(
                               node->Quadrant(Ftol(record.left), Ftol(record.top)))]
               .get();
  }
  node->records_.push_back(record);
}

const PickRecord* ActorPickNode::Find(float x, float y) const {
  if (children_[0] == nullptr) {
    const PickRecord* best = nullptr;
    float best_depth = 0.0f;
    for (const PickRecord& r : records_) {
      if (!Contains(r, x, y)) continue;
      const float d = r.depth_2 + r.depth;
      if (d > best_depth) {
        best = &r;
        best_depth = d;
      }
    }
    return best;
  }
  const int q = Quadrant(Ftol(x), Ftol(y));
  if (const PickRecord* r = children_[static_cast<std::size_t>(q)]->Find(x, y)) return r;
  for (int k = 0; k < 4; ++k) {
    if (k == q) continue;
    if (const PickRecord* r = children_[static_cast<std::size_t>(k)]->Find(x, y)) return r;
  }
  return nullptr;
}

}  // namespace ragexe::world
