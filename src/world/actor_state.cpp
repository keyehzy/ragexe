#include "world/actor_state.h"

#include <cmath>

#include "core/x87.h"
#include "render/actor.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kStand = 0;
constexpr std::int32_t kWalk = 1;
constexpr std::int32_t kStandAction = 0;
constexpr std::int32_t kWalkAction = 8;
constexpr std::int32_t kReadyAction = 32;
constexpr float kSixteenths = 16.0f;  // 005e2728
constexpr std::int32_t kPushSpeed = 2;
constexpr std::int32_t kPushSpeedAlone = 0x37;   // with +0xd8 set
constexpr std::int32_t kStandsMessage = 0xc;     // the actor's id and the cell it stands on
constexpr std::int32_t kWalksMessage = 0xd;      // the actor's id
constexpr std::int32_t kPetTalksMessage = 0xa1;  // a line of the pet's
constexpr std::uint32_t kPetTalkDelay = 3000;    // ms after +0x1d4
constexpr std::int32_t kPetTalkChance = 25;      // in 100

void Send(const StateContext& context, const ModeMessage& message) {
  if (context.messages != nullptr) context.messages->push_back(message);
}

// The direction, the state and its start: the end of both SetStates.
void FinishState(GameActor& actor, std::int32_t state, const StateContext& context,
                 std::uint32_t now) {
  render::RenderObject& o = actor.object;
  o.action = GameActorDirection(actor, o.facing, context.camera_yaw) + o.base_action;
  actor.state = state;
  o.start_time = now;
}

template <typename Actor, typename SetState>
PathResult Move(Actor& actor, PathFinder& finder, const MoveOrder& order, std::uint32_t server_time,
                SetState set_state) {
  actor.move_time = order.time;
  const PathResult found =
      FindMovePath(finder, actor.path, actor.move_time, server_time, order.start_x, order.start_y,
                   order.sub_x, order.sub_y, order.goal_x, order.goal_y, actor.speed);
  if (found != PathResult::kFound) {
    set_state(kStand);
    return found;
  }
  actor.goal_x = order.goal_x;
  actor.goal_y = order.goal_y;
  set_state(kWalk);
  actor.segment = 0;
  return found;
}

template <typename Actor, typename SetState>
StandResult Stand(Actor& actor, const StateContext& context, std::uint32_t now,
                  std::uint32_t server_time, SetState set_state) {
  if (!BeginGameActorUpdate(actor, now)) return StandResult::kFaded;
  render::RenderObject& o = actor.object;
  if (o.play_mode == 1 && o.finished) {
    set_state(kStand);
  } else {
    const std::int32_t job = GameActorJob(actor);
    if (job >= 1000 && job < 0x580 && actor.field_0x1f4 == 20 && context.pet != nullptr &&
        actor.id == context.pet->pet_id && actor.pet_talk_at != 0 &&
        now - actor.pet_talk_at > kPetTalkDelay) {
      if (context.rand != nullptr && context.rand->Next() % 100 < kPetTalkChance) {
        Send(context, {kPetTalksMessage, context.pet->first, 0, 0});
        Send(context, {kPetTalksMessage, context.pet->second, 0, 0});
      }
      actor.pet_talk_at = 0;
    }
  }
  UpdateGameActorFrame(actor, *context.act, context.camera_yaw, now);
  actor.processed_at = now;
  actor.server_at = server_time;
  return StandResult::kStanding;
}

template <typename Actor, typename SetState>
PathResult MoveTo(Actor& actor, PathFinder& finder, std::int32_t goal_x, std::int32_t goal_y,
                  std::int32_t speed, std::uint32_t server_time, const StateContext& context,
                  SetState set_state) {
  actor.move_time = server_time;
  const render::Vec3& at = actor.object.position;
  const CellPoint from = CellPointAt(*context.attr, at[0], at[2]);
  std::int32_t pace = speed;
  if (pace == 0) pace = actor.field_0xd8 != 0 ? kPushSpeedAlone : kPushSpeed;
  const PathResult found =
      FindMovePath(finder, actor.path, actor.move_time, server_time, from.cell_x, from.cell_y,
                   from.sub_x, from.sub_y, goal_x, goal_y, pace);
  if (found != PathResult::kFound) {
    set_state(kStand);
    return found;
  }
  actor.goal_x = goal_x;
  actor.goal_y = goal_y;
  set_state(kWalk);
  actor.segment = 0;
  if (speed == 0) {
    actor.pushed = true;
  } else {
    actor.pushed = false;
    actor.object.held = true;
  }
  return found;
}

}  // namespace

std::array<std::int32_t, 2> AttrCellAt(const res::AttrMap& attr, float x, float z) {
  const auto cell = static_cast<float>(attr.cell_size);
  const std::int32_t half_width = attr.width / 2;
  const std::int32_t half_height = attr.height / 2;
  const float cx = (x / cell) + static_cast<float>(half_width);
  const float cz = (z / cell) + static_cast<float>(half_height);
  return {core::Ftol(std::floor(static_cast<double>(cx))),
          core::Ftol(std::floor(static_cast<double>(cz)))};
}

CellPoint CellPointAt(const res::AttrMap& attr, float x, float z) {
  const auto cell = static_cast<float>(attr.cell_size);
  const std::int32_t half_width = attr.width / 2;
  const std::int32_t half_height = attr.height / 2;
  const float across = static_cast<float>(half_width) + (x / cell);
  const float down = static_cast<float>(half_height) + (z / cell);
  CellPoint p;
  p.cell_x = core::Ftol(std::floor(static_cast<double>(across)));
  p.cell_y = core::Ftol(std::floor(static_cast<double>(down)));
  const float in_x = (across - static_cast<float>(p.cell_x)) * kSixteenths;
  const float in_y = (down - static_cast<float>(p.cell_y)) * kSixteenths;
  p.sub_x = core::Ftol(std::floor(static_cast<double>(in_x))) % 16;
  p.sub_y = core::Ftol(std::floor(static_cast<double>(in_y))) % 16;
  return p;
}

bool IsSpecialActorIdOnKoreaService(std::int32_t service_type, std::int32_t id) {
  if (service_type != 0) return false;
  const auto u = static_cast<std::uint32_t>(id);
  return (u >= 0x2c8c7du && u <= 0x2c8c80u) || u == 0x2c8c82u;
}

void SetGameActorAction(GameActor& actor, const res::Act& act, std::int32_t action,
                        std::int32_t play_mode) {
  render::SetAction(actor.object, act, action, play_mode);
  actor.pushed = false;
  actor.field_0x1ac = -1.0f;
}

bool SetGameActorState(GameActor& actor, std::int32_t state, const StateContext& context,
                       std::uint32_t now) {
  if (actor.state == 3 || actor.field_0xd0 != 0) return true;
  if (state != kStand && state != kWalk) return false;
  render::RenderObject& o = actor.object;
  if (actor.state == 4) {
    o.position[0] = actor.target_x;
    o.position[2] = actor.target_z;
    o.position[1] = res::AttrHeight(*context.attr, actor.target_x, actor.target_z);
  }
  if (state == kStand) {
    SetGameActorAction(actor, *context.act, kStandAction, 0);
    // A standing actor holds its frame, but for a few ids on the Korean service.
    if (!IsSpecialActorIdOnKoreaService(context.service_type, actor.id)) o.finished = true;
    // vf11 (00503110).
    const auto [cx, cz] = AttrCellAt(*context.attr, o.position[0], o.position[2]);
    Send(context, {kStandsMessage, actor.id, cx, cz});
  } else {
    if (actor.field_0xd8 == 0) SetGameActorAction(actor, *context.act, kWalkAction, 0);
    // vf12 (00503160).
    Send(context, {kWalksMessage, actor.id, 0, 0});
  }
  FinishState(actor, state, context, now);
  return true;
}

bool SetPlayerState(PlayerActor& player, std::int32_t state, const StateContext& context,
                    std::uint32_t now) {
  const std::int32_t before = player.state;
  if (before == 3 || player.field_0xd0 != 0) return true;
  if (state != kStand && state != kWalk) return false;
  render::RenderObject& o = player.object;
  if (before == 4) {
    o.position[0] = player.target_x;
    o.position[2] = player.target_z;
  }
  if (state == kStand) {
    SetGameActorState(player, kStand, context, now);
    o.frame = player.saved_frame;
    if (before == 4 || before == 2 || player.field_0x224 != 0 || before == 8)
      SetGameActorAction(player, *context.act, kReadyAction, 0);
    if (context.mode_field_0x2a0 != nullptr) *context.mode_field_0x2a0 = 0;
  } else {
    player.field_0xcc = 0;
    if (player.field_0x218 != 1 && player.field_0x218 != 2) o.held = false;
    // SendMsg(0x53) to itself.
    if (player.window != 0) {
      if (context.closed_windows != nullptr) context.closed_windows->push_back(player.window);
      player.window = 0;
    }
    player.saved_frame = 0;
    SetGameActorState(player, kWalk, context, now);
  }
  FinishState(player, state, context, now);
  return true;
}

PathResult MoveGameActor(GameActor& actor, PathFinder& finder, const MoveOrder& order,
                         std::uint32_t server_time, const StateContext& context,
                         std::uint32_t now) {
  return Move(actor, finder, order, server_time,
              [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

PathResult MoveGameActor(PlayerActor& player, PathFinder& finder, const MoveOrder& order,
                         std::uint32_t server_time, const StateContext& context,
                         std::uint32_t now) {
  return Move(player, finder, order, server_time,
              [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
}

PathResult MoveGameActorTo(GameActor& actor, PathFinder& finder, std::int32_t goal_x,
                           std::int32_t goal_y, std::int32_t speed, std::uint32_t server_time,
                           const StateContext& context, std::uint32_t now) {
  return MoveTo(actor, finder, goal_x, goal_y, speed, server_time, context,
                [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

PathResult MoveGameActorTo(PlayerActor& player, PathFinder& finder, std::int32_t goal_x,
                           std::int32_t goal_y, std::int32_t speed, std::uint32_t server_time,
                           const StateContext& context, std::uint32_t now) {
  return MoveTo(player, finder, goal_x, goal_y, speed, server_time, context,
                [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
}

StandResult ProcessStand(GameActor& actor, const StateContext& context, std::uint32_t now,
                         std::uint32_t server_time) {
  return Stand(actor, context, now, server_time,
               [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

StandResult ProcessStand(PlayerActor& player, const StateContext& context, std::uint32_t now,
                         std::uint32_t server_time) {
  return Stand(player, context, now, server_time,
               [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
}

}  // namespace ragexe::world
