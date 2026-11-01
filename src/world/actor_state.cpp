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
constexpr std::int32_t kStandsMessage = 0xc;  // the actor's id and the cell it stands on
constexpr std::int32_t kWalksMessage = 0xd;   // the actor's id

void Send(const StateContext& context, const ModeMessage& message) {
  if (context.messages != nullptr) context.messages->push_back(message);
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
  o.action = GameActorDirection(actor, o.facing, context.camera_yaw) + o.base_action;
  actor.state = state;
  o.start_time = now;
  return true;
}

PathResult MoveGameActor(GameActor& actor, PathFinder& finder, const MoveOrder& order,
                         std::uint32_t server_time, const StateContext& context,
                         std::uint32_t now) {
  actor.move_time = order.time;
  const PathResult found =
      FindMovePath(finder, actor.path, actor.move_time, server_time, order.start_x, order.start_y,
                   order.sub_x, order.sub_y, order.goal_x, order.goal_y, actor.speed);
  if (found != PathResult::kFound) {
    SetGameActorState(actor, kStand, context, now);
    return found;
  }
  actor.goal_x = order.goal_x;
  actor.goal_y = order.goal_y;
  SetGameActorState(actor, kWalk, context, now);
  actor.segment = 0;
  return found;
}

}  // namespace ragexe::world
