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
constexpr std::int32_t kOnce = 1;                // the play mode of an action played once
constexpr std::int32_t kSlideTime = 200;         // ms from state 4's start
constexpr std::int32_t kFaceEvery = 34;          // ticks, state 8's turn toward its target

// Whether CGameActor::SetState's work for a state is ported.
bool SetsGameActorState(std::int32_t state) {
  switch (state) {
    case 2:
    case 3:
    case 4:
    case 7:
    case 9:
    case 13: return false;
    default: return true;
  }
}

// The action CGameActor::SetState plays once for a state, or -1 for none.
std::int32_t GameActorStateAction(std::int32_t state) {
  switch (state) {
    case 5: return 0x18;
    case 11: return 0x20;
    case 12: return 0x28;
    default: return -1;
  }
}

void Send(const StateContext& context, const ModeMessage& message) {
  if (context.messages != nullptr) context.messages->push_back(message);
}

// vf11 (00503110): the cell under the actor, to the game mode.
void ReportCell(const GameActor& actor, const StateContext& context) {
  const render::Vec3& at = actor.object.position;
  const auto [cx, cz] = AttrCellAt(*context.attr, at[0], at[2]);
  Send(context, {kStandsMessage, actor.id, cx, cz});
}

// State 8's turn toward the actor +0x1a8 names: while it names one, on every 34th tick since
// the state began, when the world has it.
void FaceTarget(GameActor& actor, const StateContext& context, std::uint32_t now) {
  render::RenderObject& o = actor.object;
  if (actor.target_id == 0) return;
  if (core::Ftol(render::SpanTicks(now - o.start_time)) % kFaceEvery != 0) return;
  if (context.target == nullptr) return;
  o.facing = render::TurnTowards(o.position, (*context.target)[0], (*context.target)[2]);
}

// State 4's slide toward the target: true when the actor moved.
template <typename SetState>
bool Slide(GameActor& actor, std::uint32_t now, SetState set_state) {
  render::RenderObject& o = actor.object;
  const render::Vec3 from = o.position;
  const float dx = actor.target_x - from[0];
  const float dz = actor.target_z - from[2];
  const std::uint32_t left = o.start_time - now + kSlideTime;
  std::int32_t gone = static_cast<std::int32_t>(now - actor.processed_at);
  if (gone < 0) gone = 0;
  bool moved = false;
  if (left != 0) {
    moved = true;
    const auto g = static_cast<double>(gone);
    const auto l = static_cast<double>(left);
    o.position[0] = core::RoundedQuotient(core::RoundedProduct(dx, g), l) + from[0];
    o.position[2] = core::RoundedQuotient(core::RoundedProduct(dz, g), l) + from[2];
  }
  if (o.finished) {
    o.position[0] = actor.target_x;
    o.position[2] = actor.target_z;
    moved = true;
    set_state(kStand);
  }
  return moved;
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
StateResult Stand(Actor& actor, const StateContext& context, std::uint32_t now,
                  std::uint32_t server_time, SetState set_state) {
  if (!BeginGameActorUpdate(actor, now)) return StateResult::kFaded;
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
  return StateResult::kDone;
}

template <typename Actor, typename SetState>
StateResult Update(Actor& actor, const StateContext& context, std::uint32_t now,
                   std::uint32_t server_time, SetState set_state) {
  render::RenderObject& o = actor.object;
  switch (actor.state) {
    case kStand: return Stand(actor, context, now, server_time, set_state);
    case kWalk: {
      const WalkResult r =
          ProcessWalk(actor, *context.attr, *context.act, context.camera_yaw, now, server_time);
      if (r == WalkResult::kFaded) return StateResult::kFaded;
      if (r == WalkResult::kArrived) set_state(kStand);
      return StateResult::kDone;
    }
    case 3:
    case 13: return StateResult::kNotPorted;
    default: break;
  }
  if (!BeginGameActorUpdate(actor, now)) return StateResult::kFaded;
  const auto frame = [&] { UpdateGameActorFrame(actor, *context.act, context.camera_yaw, now); };
  bool moved = false;
  switch (actor.state) {
    case 5:
    case 7: actor.field_0xcc = 0; [[fallthrough]];
    case 2:
    case 9:
    case 12:
      frame();
      if (o.finished) set_state(kStand);
      break;
    case 4:
      actor.field_0xcc = 0;
      frame();
      moved = Slide(actor, now, set_state);
      break;
    case 8:
      actor.field_0xcc = 0;
      frame();
      FaceTarget(actor, context, now);
      break;
    case 10: actor.field_0xcc = 0; break;
    case 16:
    case 17: frame(); break;
    default: break;
  }
  actor.processed_at = now;
  actor.server_at = server_time;
  if (moved) o.position[1] = res::AttrHeight(*context.attr, o.position[0], o.position[2]);
  return StateResult::kDone;
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
  if (state == 7 && actor.state == 6) return true;
  if (!SetsGameActorState(state)) return false;
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
    ReportCell(actor, context);
  } else if (state == kWalk) {
    if (actor.field_0xd8 == 0) SetGameActorAction(actor, *context.act, kWalkAction, 0);
    // vf12 (00503160).
    Send(context, {kWalksMessage, actor.id, 0, 0});
  } else if (const std::int32_t action = GameActorStateAction(state); action >= 0) {
    if (state == 5) {
      actor.field_0xcc = 0;
      o.held = false;
    }
    SetGameActorAction(actor, *context.act, action, kOnce);
    if (state == 12) ReportCell(actor, context);
  }
  FinishState(actor, state, context, now);
  return true;
}

bool SetPlayerState(PlayerActor& player, std::int32_t state, const StateContext& context,
                    std::uint32_t now) {
  const std::int32_t before = player.state;
  if (before == 3 || player.field_0xd0 != 0) return true;
  if (state == 7 && before == 6) return true;
  const bool own = state == kStand || state == kWalk || state == 6 || state == 8;
  if (state == 2 || state == 7 || state == 9 || (!own && !SetsGameActorState(state))) return false;
  render::RenderObject& o = player.object;
  if (before == 4) {
    o.position[0] = player.target_x;
    o.position[2] = player.target_z;
  }
  if (!own) return SetGameActorState(player, state, context, now);
  if (state == 6 || state == 8) {
    ReportCell(player, context);
    SetGameActorAction(player, *context.act, state == 6 ? 0x10 : 0x60, kOnce);
    o.finished = true;
    if (state == 6) o.frame = player.saved_frame;
  } else if (state == kStand) {
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

StateResult ProcessGameActorState(GameActor& actor, const StateContext& context, std::uint32_t now,
                                  std::uint32_t server_time) {
  return Update(actor, context, now, server_time,
                [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

StateResult ProcessGameActorState(PlayerActor& player, const StateContext& context,
                                  std::uint32_t now, std::uint32_t server_time) {
  const auto set_state = [&](std::int32_t state) { SetPlayerState(player, state, context, now); };
  switch (player.state) {
    case 2:
    case 3:
    case 6:
    case 7:
    case 8: break;
    default: return Update(player, context, now, server_time, set_state);
  }
  if (!BeginGameActorUpdate(player, now)) return StateResult::kFaded;
  render::RenderObject& o = player.object;
  if (player.state == 3) o.held = false;
  UpdateGameActorFrame(player, *context.act, context.camera_yaw, now);
  if (player.state == 8) {
    FaceTarget(player, context, now);
  } else if ((player.state == 2 || player.state == 7) && o.finished) {
    set_state(kStand);
  }
  return StateResult::kDone;
}

}  // namespace ragexe::world
