#include "world/player_orders.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "core/x87.h"
#include "render/actor.h"
#include "render/world_load.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kStand = 0;
constexpr std::int32_t kWalk = 1;
constexpr std::int32_t kSlide = 4;
constexpr std::int32_t kFall = 6;

constexpr std::int32_t kAttackPursuit = 1;
constexpr std::int32_t kItemPursuit = 2;
constexpr std::int32_t kActorSkillPursuit = 3;
constexpr std::int32_t kCellSkillPursuit = 4;
constexpr std::int32_t kFastAttackPursuit = 5;
constexpr std::int32_t kObjectSkillPursuit = 6;

// The messages CPlayer::SendMsg handles itself, by its switch (the bytes at 0054cf18 into the
// table at 0054cec4); the rest are CPc's.
constexpr std::int32_t kMoveMessage = 0;
constexpr std::int32_t kStandMessage = 8;
constexpr std::int32_t kPreMoveMessage = 9;
constexpr std::int32_t kAttackMessage = 0xa;
constexpr std::int32_t kStopMessage = 0xd;
constexpr std::int32_t kAttackStopMessage = 0xf;
constexpr std::int32_t kLookMessage = 0x10;
constexpr std::int32_t kWalkMessage = 0x11;
constexpr std::int32_t kPickUpMessage = 0x12;
constexpr std::int32_t kActOnceMessage = 0x1d;
constexpr std::int32_t kSitMessage = 0x1e;
constexpr std::int32_t kSitToo = 0x1f;
constexpr std::int32_t kActMessage = 0x28;
constexpr std::int32_t kActorSkillMessage = 0x29;
constexpr std::int32_t kCastOnActorMessage = 0x2a;
constexpr std::int32_t kAttackAgainMessage = 0x3c;
constexpr std::int32_t kCellSkillMessage = 0x41;
constexpr std::int32_t kCastOnCellMessage = 0x42;
constexpr std::int32_t kLevelMessage = 0x5a;
constexpr std::int32_t kWalkToMessage = 0x5b;
constexpr std::int32_t kObjectSkillMessage = 0x67;
constexpr std::array<std::int32_t, 21> kOwnMessages = {
    kMoveMessage,       kStandMessage,      kPreMoveMessage,     kAttackMessage,
    kStopMessage,       kAttackStopMessage, kLookMessage,        kWalkMessage,
    kPickUpMessage,     kActOnceMessage,    kSitMessage,         kSitToo,
    kActMessage,        kActorSkillMessage, kCastOnActorMessage, kAttackAgainMessage,
    kCellSkillMessage,  kCastOnCellMessage, kLevelMessage,       kWalkToMessage,
    kObjectSkillMessage};

// What it asks of the game mode (CGameMode::SendMsg, vf05).
constexpr std::int32_t kModeStop = 0x1c;
constexpr std::int32_t kModeCastOnActor = 0x4c;
constexpr std::int32_t kModeAttackStop = 0x50;
constexpr std::int32_t kModeAct = 0x96;  // with 0 once, 2 to sit, 3 to stand, 7 on and on
constexpr std::int32_t kModeWalkTo = 0x97;
constexpr std::int32_t kModeLook = 0x98;
constexpr std::int32_t kActOnce = 0;
constexpr std::int32_t kSit = 2;
constexpr std::int32_t kStandUp = 3;
constexpr std::int32_t kActOnAndOn = 7;

constexpr std::int32_t kAttackOnce = 2;             // +0x320
constexpr std::uint32_t kAskedAgo = 1200;           // message 0x3c's +0x338, before now
constexpr std::uint32_t kWalkAskEvery = 1000;       // ms after +0x33c
constexpr std::int32_t kSkillAskedInWindow = 0x7d;  // message 0x42's skill for window 0x54
constexpr std::int32_t kSkillWhileHiding = 0x89;    // message 0x29's skill allowed while hiding
constexpr std::int32_t kPetKind = 0x14;             // a target's +0x1f4 message 0x28 leaves alone
// The jobs whose ids messages 0x1d and 0x28 act on as given, not the target's (+0x330).
constexpr std::array<std::int32_t, 3> kObjectJobs = {0x8d, 0x8f, 0x98};

constexpr float kTurn = 45.0f;             // 005e93c0
constexpr float kFullTurn = 360.0f;        // 005e2a5c
constexpr double kHalfSector = 22.5;       // 005e9530
constexpr double kPerSector = 1.0 / 45.0;  // 005e9520
constexpr std::int32_t kSectors = 8;

// IsBit1Set (0059d350) of the effect state (+0x21c): hiding.
bool Hiding(const PlayerActor& player) { return (player.effect_state >> 1 & 1) != 0; }

// Those ordered while falling (state 6) or hiding do nothing.
bool Busy(const PlayerActor& player) { return player.state == kFall || Hiding(player); }

std::int32_t Wrap(std::int64_t v) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}

void ToMode(const StateContext& context, std::int32_t id, std::int32_t a = 0, std::int32_t b = 0,
            std::int32_t c = 0) {
  if (context.messages != nullptr) context.messages->push_back({id, a, b, c});
}

bool IsObjectJob(std::int32_t job) {
  return std::ranges::find(kObjectJobs, job) != kObjectJobs.end();
}

// Message 0 (0054ca73): the move as CGameActor's, at the player's speed (ClientState+0xd94),
// clearing +0x328 and PreMove; without a path, again from the cell under the player
// (CGameMode::GetCellPoint), which walks on from the segment it was on.
void Move(PlayerActor& player, PathFinder& finder, const MoveOrder& order,
          const StateContext& context, std::uint32_t now, std::uint32_t server_time) {
  OwnPlayer& own = *player.own;
  own.walk_asked = 0;
  own.premove = 0;
  player.move_time = order.time;
  const auto walk = [&] {
    player.goal_x = order.goal_x;
    player.goal_y = order.goal_y;
    SetPlayerState(player, kWalk, context, now);
  };
  if (FindMovePath(finder, player.path, player.move_time, server_time, order.start_x, order.start_y,
                   order.sub_x, order.sub_y, order.goal_x, order.goal_y,
                   context.own_speed) == PathResult::kFound) {
    walk();
    player.segment = 0;
    return;
  }
  const render::Vec3& at = player.object.position;
  const CellPoint from = CellPointAt(*context.attr, at[0], at[2]);
  if (FindMovePath(finder, player.path, player.move_time, server_time, from.cell_x, from.cell_y,
                   from.sub_x, from.sub_y, order.goal_x, order.goal_y,
                   context.own_speed) == PathResult::kFound)
    walk();
}

// Message 0xa (0054cd77): the attack on the actor CWorld finds, 5 for one faster than four
// thirds of the player's speed (unsigned), otherwise 1; +0x320 the second word.
void Attack(PlayerActor& player, std::int32_t id, std::int32_t mode, const StateContext& context) {
  if (Busy(player)) return;
  const GameActor* target = context.target_actor;
  if (target == nullptr) return;
  OwnPlayer& own = *player.own;
  const std::int32_t limit = Wrap(std::int64_t{context.own_speed} * 4) / 3;
  own.pursuit = static_cast<std::uint32_t>(target->speed) > static_cast<std::uint32_t>(limit)
                    ? kFastAttackPursuit
                    : kAttackPursuit;
  own.acted = 0;
  own.acts = 0;
  own.target_id = id;
  own.attack_mode = mode;
}

// Message 0x10 (0054c898): standing, sliding or fallen, the player looks toward the cell's
// centre. The sector of the angle to it from the facing ((int)(facing − angle), 360 more when
// below 0, (+ 22.5) / 45, wrapped once into 0 to 7) turns the head (+0x294: 0 ahead, 1 and 2
// aside) or the body: ahead (0) and behind (4) the body faces it with the head ahead; 1 and 7
// turn the head toward it the first time and the body the second; 2 and 3 face 45° short of it
// with the head 1, 5 and 6 45° past it with the head 2, wrapped into [0, 360). A sector past
// them changes nothing. Then the frame is the head and the game mode hears 0x98 with the facing's
// direction ((int)(facing + 180) / 45 % 8, C's) and the head.
void Look(PlayerActor& player, std::int32_t x, std::int32_t y, const StateContext& context) {
  const std::int32_t state = player.state;
  if (state != kStand && state != kSlide && state != kFall) return;
  render::RenderObject& o = player.object;
  const auto [cx, cz] = render::CellCentre(*context.attr, x, y);
  const float angle = render::AngleBetween(o.position[0], o.position[2], cx, cz);
  std::int32_t turn = core::Ftol(static_cast<double>(o.facing - angle));
  if (turn < 0) turn += 360;
  const float sector = static_cast<float>(static_cast<double>(turn) + kHalfSector);
  std::int32_t s = core::Ftol(core::RoundedProduct(sector, kPerSector));
  if (s < 0) s += kSectors;
  if (s >= kSectors) s -= kSectors;
  std::int32_t& head = player.saved_frame;
  using F = core::X87<float>;
  const auto face = [&] {
    o.facing = angle;
    head = 0;
  };
  switch (s) {
    case 0:
    case 4: face(); break;
    case 1:
      if (head == 1)
        face();
      else
        head = 1;
      break;
    case 7:
      if (head == 2)
        face();
      else
        head = 2;
      break;
    case 2:
    case 3: {
      const F v = F(angle) + F(kTurn);
      head = 1;
      o.facing = static_cast<float>(!(v >= F(kFullTurn)) ? v : v - F(kFullTurn));
      break;
    }
    case 5:
    case 6: {
      const F v = F(angle) - F(kTurn);
      head = 2;
      o.facing = static_cast<float>(!(v >= F(0.0f)) ? v + F(kFullTurn) : v);
      break;
    }
    default: break;
  }
  o.frame = head;
  const std::int32_t direction =
      Wrap(std::int64_t{core::Ftol(static_cast<double>(o.facing))} + 180) / 45 % kSectors;
  ToMode(context, kModeLook, direction, head);
}

// Message 0x11 (0054c7c2): the pursuit ends (0xd); then while attacking, flinching or falling
// the move waits (+0x32c, +0x358, +0x35c), otherwise it is asked for (0x5b).
void Walk(PlayerActor& player, PathFinder& finder, std::int32_t x, std::int32_t y,
          const StateContext& context, std::uint32_t now, std::uint32_t server_time) {
  SendOwnPlayerMessage(player, finder, {kStopMessage}, context, now, server_time);
  OwnPlayer& own = *player.own;
  const std::int32_t state = player.state;
  if (state == 2 || state == 5 || state == kFall) {
    own.move_queued = 1;
    own.queued_x = x;
    own.queued_y = y;
    return;
  }
  own.queued_x = own.queued_y = -1;
  own.move_queued = 0;
  SendOwnPlayerMessage(player, finder, {kWalkToMessage, x, y}, context, now, server_time);
}

void Record(const OwnMessage& message, const StateContext& context) {
  if (context.actor_messages != nullptr) {
    context.actor_messages->push_back(
        {message.id, message.id == kMoveMessage ? 0 : message.a,
         message.id == kMoveMessage || message.id == kObjectSkillMessage ? 0 : message.b,
         message.id == kMoveMessage ? 0 : message.c});
  }
  if (message.id == kMoveMessage && context.own_moves != nullptr)
    context.own_moves->push_back(message.order);
}

}  // namespace

bool SendOwnPlayerMessage(PlayerActor& player, PathFinder& finder, const OwnMessage& message,
                          const StateContext& context, std::uint32_t now,
                          std::uint32_t server_time) {
  if (!player.own) return false;
  const std::int32_t id = message.id;
  if (std::ranges::find(kOwnMessages, id) == kOwnMessages.end()) return false;
  Record(message, context);
  OwnPlayer& own = *player.own;
  const std::int32_t a = message.a;
  const std::int32_t b = message.b;
  const std::int32_t c = message.c;
  switch (id) {
    case kMoveMessage: Move(player, finder, message.order, context, now, server_time); break;
    case kStandMessage: SetPlayerState(player, kStand, context, now); break;
    case kPreMoveMessage:
      own.premove = 1;
      own.premove_at = now;
      own.goal_x = a;
      own.goal_y = b;
      break;
    case kAttackMessage: Attack(player, a, b, context); break;
    case kStopMessage:
      own.acted = 0;
      own.pursuit = 0;
      ToMode(context, kModeStop);
      break;
    case kAttackStopMessage:
      if (own.pursuit == kAttackPursuit || own.pursuit == kFastAttackPursuit) {
        if (own.attack_mode == 0) break;
        own.attack_mode = kAttackOnce;
        if (own.acts == 0) break;
        SendOwnPlayerMessage(player, finder, {kStopMessage}, context, now, server_time);
        ToMode(context, kModeAttackStop);
      } else if (own.pursuit == kActorSkillPursuit) {
        ToMode(context, kModeStop);
      }
      break;
    case kLookMessage: Look(player, a, b, context); break;
    case kWalkMessage: Walk(player, finder, a, b, context, now, server_time); break;
    case kPickUpMessage:
      if (Busy(player)) break;
      own.pursuit = kItemPursuit;
      own.target_id = a;
      break;
    case kActOnceMessage:
      ToMode(context, kModeAct, kActOnce, IsObjectJob(b) ? a : own.target_id);
      break;
    case kSitMessage:
    case kSitToo:
      if (Hiding(player)) break;
      if (player.state == kStand)
        ToMode(context, kModeAct, kSit, own.target_id);
      else if (player.state == kFall)
        ToMode(context, kModeAct, kStandUp, own.target_id);
      break;
    case kActMessage:
      if (IsObjectJob(b)) {
        ToMode(context, kModeAct, kActOnAndOn, a);
      } else if (context.target_actor == nullptr || context.target_actor->field_0x1f4 != kPetKind) {
        ToMode(context, kModeAct, kActOnAndOn, own.target_id);
      }
      break;
    case kActorSkillMessage:
      if (player.state == kFall || (Hiding(player) && b != kSkillWhileHiding)) break;
      own.skill = b;
      own.pursuit = kActorSkillPursuit;
      own.target_id = a;
      own.range = c;
      break;
    case kCastOnActorMessage:
      ToMode(context, kModeCastOnActor, own.skill, own.target_id, own.level);
      break;
    case kAttackAgainMessage:
      if (Busy(player)) break;
      own.pursuit = own.pursuit == kAttackPursuit ? kFastAttackPursuit : kAttackPursuit;
      own.asked_at = now - kAskedAgo;
      own.acted = 0;
      own.acts = 0;
      own.target_id = a;
      own.goal_x = own.goal_y = -1;
      break;
    case kCellSkillMessage:
      if (Busy(player)) break;
      own.pursuit = kCellSkillPursuit;
      own.skill = a;
      own.cell_x = b;
      own.cell_y = c;
      own.range = context.skill_range;
      own.target_id = 0;
      break;
    case kCastOnCellMessage:
      if (context.cell_skills != nullptr) {
        context.cell_skills->push_back(
            {own.skill == kSkillAskedInWindow, own.skill, own.cell_x, own.cell_y, own.level});
      }
      break;
    case kLevelMessage: own.level = a; break;
    case kWalkToMessage:
      if (own.walk_asked != 0 && !(now > own.walk_asked_at + kWalkAskEvery)) break;
      own.walk_asked = 1;
      own.walk_asked_at = now;
      ToMode(context, kModeWalkTo, a, b);
      break;
    case kObjectSkillMessage:
      if (Busy(player)) break;
      own.pursuit = kObjectSkillPursuit;
      own.skill = message.object_skill[0];
      own.level = message.object_skill[2];
      own.range = message.object_skill[1];
      own.target_id = a;
      break;
    default: break;
  }
  return true;
}

}  // namespace ragexe::world
