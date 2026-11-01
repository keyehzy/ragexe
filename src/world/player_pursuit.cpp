#include "world/player_pursuit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core/x87.h"
#include "render/world_load.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kStand = 0;
constexpr std::int32_t kWalk = 1;
constexpr std::int32_t kAttack = 2;
constexpr std::int32_t kDying = 3;
constexpr std::int32_t kSlide = 4;

constexpr std::int32_t kAttackPursuit = 1;
constexpr std::int32_t kItemPursuit = 2;
constexpr std::int32_t kActorSkillPursuit = 3;
constexpr std::int32_t kCellSkillPursuit = 4;
constexpr std::int32_t kFastAttackPursuit = 5;  // a target faster than the player
constexpr std::int32_t kObjectSkillPursuit = 6;

// What the player sends itself (CPlayer::SendMsg, 0054c5d0).
constexpr std::int32_t kStopMessage = 0xd;         // the pursuit ends
constexpr std::int32_t kPickUpMessage = 0x10;      // at the item's cell
constexpr std::int32_t kAttackOnceMessage = 0x1d;  // game mode 0x96 with 0
constexpr std::int32_t kAttackMessage = 0x28;      // game mode 0x96 with 7
constexpr std::int32_t kActorSkillMessage = 0x2a;  // game mode 0x4c
constexpr std::int32_t kCellSkillMessage = 0x42;   // game mode 0x4d
constexpr std::int32_t kWalkToMessage = 0x5b;      // game mode 0x97, at most once a second
constexpr std::int32_t kPickedMessage = 0x95;      // to the game mode, the item's id

constexpr std::int32_t kAttackOnce = 2;        // +0x320
constexpr std::uint32_t kActEvery = 1200;      // ms after +0x310
constexpr std::uint32_t kAskEvery = 450;       // ms after +0x338
constexpr std::uint32_t kAskFastEvery = 1200;  // for a target faster than the player
constexpr std::int32_t kBigModel = 1;          // +0x238
constexpr float kBigModelReach = 20.0f;        // 005e9560, off a big model's distance
constexpr float kRangeSlack = 5.5f;            // 005e9db4, past range × 5 units
constexpr std::int32_t kCellUnits = 5;
constexpr std::int32_t kBeside = 5;  // cells the cast's aim moves by
// The skills' objects attacked (0x1d) in range without a skill (+0x344 0), by job.
constexpr std::array<std::int32_t, 3> kAttackedObjectJobs = {0x8d, 0x8f, 0x98};

// The eight neighbours FreeCellToward and StepCellToward try, in order.
constexpr std::array<std::array<std::int32_t, 2>, 8> kNeighbours = {
    {{1, 0}, {1, 1}, {1, -1}, {-1, 0}, {-1, 1}, {-1, -1}, {0, 1}, {0, -1}}};

std::int32_t Wrap(std::int64_t v) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}

std::array<std::int32_t, 2> FirstFreeNeighbour(const res::AttrMap& attr, std::int32_t x,
                                               std::int32_t y) {
  std::array<std::int32_t, 2> cell{};
  for (const auto& [dx, dy] : kNeighbours) {
    cell = {Wrap(std::int64_t{x} + dx), Wrap(std::int64_t{y} + dy)};
    if (!IsCellBlocked(attr, cell[0], cell[1])) break;
  }
  return cell;
}

void Send(const StateContext& context, std::int32_t id, std::int32_t a = 0, std::int32_t b = 0) {
  if (context.actor_messages != nullptr) context.actor_messages->push_back({id, a, b, 0});
}

// Message 0x5b to a cell, kept as the goal.
void WalkTo(OwnPlayer& own, const StateContext& context, std::int32_t x, std::int32_t y) {
  own.goal_x = x;
  own.goal_y = y;
  Send(context, kWalkToMessage, x, y);
}

bool AtGoal(const OwnPlayer& own, std::int32_t x, std::int32_t y) {
  return own.goal_x == x && own.goal_y == y;
}

// Range × 5 units and 5.5 (fild, fadd), the distance within it or unordered (fcompp, test
// ah, 0x41).
bool InRange(float distance, std::int32_t range) {
  const float limit =
      core::RoundedSum(kRangeSlack, static_cast<double>(Wrap(std::int64_t{range} * kCellUnits)));
  return !(distance > limit);
}

// The distance from a target to the player, 20 less, no less than 0, for a big model.
float ReachOf(const GameActor& target, const render::Vec3& at) {
  float d = DistanceTo(target.object, at[0], at[1], at[2]);
  if (target.model_kind == kBigModel) {
    d -= kBigModelReach;
    if (!(d >= 0.0f)) d = 0.0f;
  }
  return d;
}

bool IsAttacked(const GameActor& object) {
  return std::ranges::find(kAttackedObjectJobs, GameActorJob(object)) != kAttackedObjectJobs.end();
}

// 0054ba64 and 0054bef9: five cells beside the cell by the angle from the player to the
// target (CRenderObject::GetAngleBetween), in eight sectors from 25° by 45°. Those from 205°
// and from 295° move as their opposites across x would (x − 5 with y + 5, and with y − 5);
// unordered, y + 5.
void AimBeside(float angle, std::int32_t& x, std::int32_t& y) {
  std::int32_t dx = 0;
  std::int32_t dy = 0;
  if (!(angle >= 25.0f) || !(angle < 340.0f)) {
    dy = kBeside;
  } else if (angle < 70.0f) {
    dx = kBeside;
    dy = kBeside;
  } else if (angle < 115.0f) {
    dx = kBeside;
  } else if (angle < 160.0f) {
    dx = kBeside;
    dy = -kBeside;
  } else if (angle < 205.0f) {
    dy = -kBeside;
  } else if (angle < 250.0f) {
    dx = -kBeside;
    dy = kBeside;
  } else if (angle < 295.0f) {
    dx = -kBeside;
  } else {
    dx = -kBeside;
    dy = -kBeside;
  }
  x = Wrap(std::int64_t{x} + dx);
  y = Wrap(std::int64_t{y} + dy);
}

// In range or at its goal, the attack: with +0x320 2 the pursuit ends (0xd); then, once since
// the order and 1200 ms after the last, it acts (0x1d with +0x320 2, 0x28 otherwise).
void Attack(OwnPlayer& own, const StateContext& context, std::uint32_t now) {
  if (own.attack_mode == kAttackOnce) Send(context, kStopMessage);
  if (own.acted != 0 || !(now > own.acted_at + kActEvery)) return;
  own.acted_at = now;
  own.acted = 1;
  own.acts = Wrap(std::int64_t{own.acts} + 1);
  Send(context, own.attack_mode == kAttackOnce ? kAttackOnceMessage : kAttackMessage);
}

void PursueToAttack(PlayerActor& player, OwnPlayer& own, const StateContext& context,
                    std::uint32_t now) {
  const GameActor* target = context.target_actor;
  if (target == nullptr) {
    Send(context, kStopMessage);
    return;
  }
  if (target->state == kDying) return;
  const res::AttrMap& attr = *context.attr;
  const render::Vec3& at = player.object.position;
  const auto [cx, cy] = AttrCellAt(attr, at[0], at[2]);
  const std::int32_t state = player.state;
  if (GameActorJob(player) != 0x10 && state != kWalk && state != kStand && state != kSlide) return;
  if (AtGoal(own, cx, cy) || InRange(ReachOf(*target, at), context.attack_range)) {
    Attack(own, context, now);
    return;
  }
  const bool fast = own.pursuit == kFastAttackPursuit;
  const bool due = now > own.asked_at + (fast ? kAskFastEvery : kAskEvery);
  const render::Vec3& t = target->object.position;
  // A big model's aim beside it (AimBeside) is made here and lost: FreeCellToward overwrites
  // the cell.
  const std::array<float, 2> toward =
      fast ? std::array<float, 2>{t[0], t[2]}
           : render::CellCentre(attr, target->goal_x, target->goal_y);
  const auto [x, y] =
      FreeCellToward(attr, at[0], at[2], toward[0], toward[1], context.attack_range);
  if (AtGoal(own, x, y) || state == kAttack || own.acted != 0 || !due) return;
  own.asked_at = now;
  WalkTo(own, context, x, y);
}

void PursueToPickUp(PlayerActor& player, OwnPlayer& own, const StateContext& context) {
  if (context.item == nullptr) {
    Send(context, kStopMessage);
    return;
  }
  const res::AttrMap& attr = *context.attr;
  const render::Vec3& at = player.object.position;
  const auto [cx, cy] = AttrCellAt(attr, at[0], at[2]);
  const auto [ix, iy] = AttrCellAt(attr, (*context.item)[0], (*context.item)[2]);
  const auto [x, y] = StepCellToward(attr, cx, cy, ix, iy);
  if (player.state != kStand) return;
  if (IsWithinCellRange(cx, cy, ix, iy, 1)) {
    Send(context, kPickUpMessage, ix, iy);
    if (context.picked != nullptr &&
        std::ranges::find(*context.picked, own.target_id) == context.picked->end()) {
      context.picked->push_back(own.target_id);
      if (context.messages != nullptr) context.messages->push_back({kPickedMessage, own.target_id});
    }
    Send(context, kStopMessage);
    return;
  }
  if (AtGoal(own, x, y)) return;
  WalkTo(own, context, x, y);
}

void PursueToCastOnActor(PlayerActor& player, OwnPlayer& own, const StateContext& context) {
  const GameActor* target = own.target_id == context.own_id ? &player : context.target_actor;
  if (target == nullptr) {
    Send(context, kStopMessage);
    return;
  }
  const res::AttrMap& attr = *context.attr;
  const render::Vec3& at = player.object.position;
  const auto [cx, cy] = AttrCellAt(attr, at[0], at[2]);
  const render::Vec3& t = target->object.position;
  auto [x, y] = FreeCellToward(attr, at[0], at[2], t[0], t[2], own.range);
  const std::int32_t state = player.state;
  if (state != kWalk && state != kStand && state != kSlide && state != 0x11 && state != 0x10)
    return;
  if (InRange(ReachOf(*target, at), own.range) || AtGoal(own, cx, cy)) {
    Send(context, kActorSkillMessage);
    Send(context, kStopMessage);
    return;
  }
  if (AtGoal(own, x, y) || state == kAttack) return;
  own.goal_x = x;
  own.goal_y = y;
  if (target->model_kind == kBigModel)
    AimBeside(render::AngleBetween(at[0], at[2], t[0], t[2]), x, y);
  Send(context, kWalkToMessage, x, y);
}

void PursueToCastOnCell(PlayerActor& player, OwnPlayer& own, const StateContext& context) {
  const res::AttrMap& attr = *context.attr;
  const render::Vec3& at = player.object.position;
  const auto [cx, cy] = AttrCellAt(attr, at[0], at[2]);
  const auto [fx, fz] = render::CellCentre(attr, own.cell_x, own.cell_y);
  const auto [x, y] = FreeCellToward(attr, at[0], at[2], fx, fz, own.range);
  const std::int32_t state = player.state;
  if (state != kWalk && state != kStand && state != kSlide) return;
  if (InRange(DistanceTo(player.object, fx, at[1], fz), own.range) || AtGoal(own, cx, cy)) {
    Send(context, kCellSkillMessage);
    Send(context, kStopMessage);
    return;
  }
  if (AtGoal(own, x, y) || state == kAttack) return;
  WalkTo(own, context, x, y);
}

void PursueToCastOnObject(PlayerActor& player, OwnPlayer& own, const StateContext& context) {
  const GameActor* object = context.skill_object;
  if (object == nullptr) {
    Send(context, kStopMessage);
    return;
  }
  const res::AttrMap& attr = *context.attr;
  const render::Vec3& at = player.object.position;
  const auto [cx, cy] = AttrCellAt(attr, at[0], at[2]);
  const render::Vec3& o = object->object.position;
  const auto [ox, oy] = AttrCellAt(attr, o[0], o[2]);
  const auto [fx, fz] = render::CellCentre(attr, ox, oy);
  const auto [x, y] = FreeCellToward(attr, at[0], at[2], fx, fz, own.range);
  const std::int32_t state = player.state;
  if (state != kWalk && state != kStand && state != kSlide) return;
  if (InRange(DistanceTo(player.object, fx, at[1], fz), own.range) || AtGoal(own, cx, cy)) {
    if (IsAttacked(*object) && own.skill == 0) {
      Send(context, kAttackOnceMessage);
      return;
    }
    Send(context, kActorSkillMessage);
    Send(context, kStopMessage);
    return;
  }
  if (AtGoal(own, x, y)) return;
  if (state == kAttack && !IsAttacked(*object)) return;
  WalkTo(own, context, x, y);
}

}  // namespace

bool IsCellBlocked(const res::AttrMap& attr, std::int32_t x, std::int32_t y) {
  if (x < 0 || x >= attr.width || y < 0 || y >= attr.height) return true;
  const std::int32_t attribute =
      attr.cells[static_cast<std::size_t>(attr.width) * static_cast<std::size_t>(y) +
                 static_cast<std::size_t>(x)]
          .attribute;
  return attribute == 1 || attribute == 5;
}

std::array<std::int32_t, 2> FreeCellToward(const res::AttrMap& attr, float from_x, float from_z,
                                           float to_x, float to_z, std::int32_t steps) {
  const float dx = from_x - to_x;
  const float dz = from_z - to_z;
  const float inverse = 1.0f / std::sqrt(dz * dz + dx * dx);
  const float ux = inverse * dx;
  const float uz = inverse * dz;
  const auto units = static_cast<double>(Wrap(std::int64_t{steps} * kCellUnits));
  const float x = core::RoundedProduct(ux, units) + to_x;
  const float z = core::RoundedProduct(uz, units) + to_z;
  const auto cell = AttrCellAt(attr, x, z);
  if (!IsCellBlocked(attr, cell[0], cell[1])) return cell;
  return FirstFreeNeighbour(attr, cell[0], cell[1]);
}

std::array<std::int32_t, 2> StepCellToward(const res::AttrMap& attr, std::int32_t toward_x,
                                           std::int32_t toward_y, std::int32_t x, std::int32_t y) {
  std::array<std::int32_t, 2> cell = {x, y};
  if (toward_x > x)
    cell[0] = Wrap(std::int64_t{x} + 1);
  else if (toward_x < x)
    cell[0] = Wrap(std::int64_t{x} - 1);
  if (toward_y > y)
    cell[1] = Wrap(std::int64_t{y} + 1);
  else if (toward_y < y)
    cell[1] = Wrap(std::int64_t{y} - 1);
  if (!IsCellBlocked(attr, cell[0], cell[1])) return cell;
  return FirstFreeNeighbour(attr, x, y);
}

bool IsWithinCellRange(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by,
                       std::int32_t range) {
  const auto apart = [](std::int32_t a, std::int32_t b) {
    const std::int32_t d = Wrap(std::int64_t{a} - b);
    const std::int32_t sign = d < 0 ? -1 : 0;
    return Wrap(std::int64_t{d ^ sign} - sign);
  };
  return apart(ax, bx) <= range && apart(ay, by) <= range;
}

float DistanceTo(const render::RenderObject& object, float x, float y, float z) {
  const render::Vec3& p = object.position;
  const float dx = x - p[0];
  const float dz = z - p[2];
  const float dy = y - p[1];
  const float across = std::sqrt(dz * dz + dx * dx);
  return std::sqrt(across * across + dy * dy);
}

void SendQueuedMove(PlayerActor& player, const StateContext& context) {
  if (!player.own) return;
  OwnPlayer& own = *player.own;
  const std::int32_t state = player.state;
  if (own.move_queued == 0 || (state != kAttack && state != 5 && state != 6)) return;
  const std::int32_t x = own.queued_x;
  const std::int32_t y = own.queued_y;
  own.queued_x = own.queued_y = -1;
  own.move_queued = 0;
  Send(context, kWalkToMessage, x, y);
}

void PursueTarget(PlayerActor& player, const StateContext& context, std::uint32_t now) {
  if (!player.own) return;
  OwnPlayer& own = *player.own;
  switch (own.pursuit) {
    case kAttackPursuit:
    case kFastAttackPursuit: PursueToAttack(player, own, context, now); break;
    case kItemPursuit: PursueToPickUp(player, own, context); break;
    case kActorSkillPursuit: PursueToCastOnActor(player, own, context); break;
    case kCellSkillPursuit: PursueToCastOnCell(player, own, context); break;
    case kObjectSkillPursuit: PursueToCastOnObject(player, own, context); break;
    default: break;
  }
}

}  // namespace ragexe::world
