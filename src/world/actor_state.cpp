#include "world/actor_state.h"

#include <cmath>

#include "core/x87.h"
#include "render/actor.h"
#include "world/actor_attack.h"
#include "world/actor_effects.h"
#include "world/actor_hit.h"
#include "world/player_pursuit.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kStand = 0;
constexpr std::int32_t kWalk = 1;
constexpr std::int32_t kDying = 3;
constexpr std::int32_t kCast = 13;
constexpr std::int32_t kDyingAction = 0x40;
constexpr std::int32_t kFrozen = 2;               // the body state whose ice dying ends
constexpr std::int32_t kCorpseJobsFrom = 0x17;    // jobs past 0x16 leave a corpse
constexpr std::int32_t kQueueDelaysFrom = 0x16;   // and from 0x16 die after their queue
constexpr std::uint32_t kMonsterIdBase = 100000;  // 0x186a0, less a dead monster's id
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
constexpr std::int32_t kAttackAction = 0x50;
constexpr std::int32_t kSecondAttackAction = 0x58;
constexpr std::int32_t kSlideAction = 0x30;
constexpr std::int32_t kWaitAction = 0x10;  // a cast's for jobs from 0x16
constexpr std::int32_t kPlayerJobs = 0x16;  // ClientState 005aa610: jobs 0 to 0x15
constexpr std::int32_t kArrowJob = 0x505;   // shoots an arrow as it attacks
constexpr float kArrowSpeed = 28.0f;        // 0x41e00000, CGameActor 00504000's
constexpr float kPlayerArrowTime = 8.0f;    // 005e972c, over +0x44 added to +0x1ac
constexpr std::int32_t kBowKind = 0xb;
constexpr float kPlayerHitFrame = 6.0f;  // 0x40c00000, CPc's state 7
// The jobs CNpc's vf10, vf11 and vf12 take their own ways for (the byte tables 00540174,
// 00540280 and 00540328, by the job less 0x2d).
constexpr std::int32_t kWarpPortalJob = 0x2d;
constexpr std::int32_t kRaisedNpcJob = 0x68;   // set 5 × +0x1f0 higher
constexpr std::int32_t kDoubledNpcJob = 0x6f;  // +0x1ec and +0x1f0 doubled
constexpr std::int32_t kStillNpcJob = 0x8b;
constexpr std::int32_t kMonsterJobsFrom = 1000;
constexpr std::int32_t kPortalLeastSize = 10;
constexpr std::int32_t kPortalEffect = 0x3d;
constexpr std::int32_t kNpcAttackAction = 0x10;
constexpr std::int32_t kNpcHurtAction = 0x18;
constexpr std::int32_t kNpcDyingAction = 0x20;
constexpr std::int32_t kNpcHurtActionsFrom = 0x28;  // state 4 in these only steps the frame
constexpr std::int32_t kPetLineMessage = 0xa1;

// The action CGameActor::SetState plays once for a state, or -1 for none.
std::int32_t GameActorStateAction(std::int32_t state) {
  switch (state) {
    case 5: return 0x18;
    case 11: return 0x20;
    case 12: return 0x28;
    default: return -1;
  }
}

// vf31: CGameActor's (004eb1b0) -1, CPc's (004eb090) the weapon.
std::int32_t Weapon(const GameActor&) { return -1; }
std::int32_t Weapon(const PlayerActor& player) { return player.weapon; }

void Send(const StateContext& context, const ModeMessage& message) {
  if (context.messages != nullptr) context.messages->push_back(message);
}

// State 13's action, which SetState and its first update play once: 0x58 or 0x50 by
// AttacksWithSecondAction for jobs 0 to 0x15, otherwise 0x10.
template <typename Actor>
std::int32_t CastAction(const Actor& actor) {
  const std::int32_t job = GameActorJob(actor);
  if (job < 0 || job >= kPlayerJobs) return kWaitAction;
  return AttacksWithSecondAction(job, actor.field_0x1bc, Weapon(actor)) ? kSecondAttackAction
                                                                        : kAttackAction;
}

// CNpc's vf11 and vf12 report nothing for these jobs.
bool IsSilentNpc(const GameActor& actor) {
  if (!actor.npc) return false;
  const std::int32_t job = GameActorJob(actor);
  return job == kWarpPortalJob || job == kRaisedNpcJob || job == kDoubledNpcJob ||
         job == kStillNpcJob;
}

// vf11 (00503110; a CNpc's 00540210): the cell under the actor, to the game mode.
void ReportCell(const GameActor& actor, const StateContext& context) {
  if (!actor.reports || IsSilentNpc(actor)) return;
  const render::Vec3& at = actor.object.position;
  const auto [cx, cz] = AttrCellAt(*context.attr, at[0], at[2]);
  Send(context, {kStandsMessage, actor.id, cx, cz});
}

// vf02 to itself, from no sender: kept, and made and ended the effects of.
void SendSelf(GameActor& actor, const StateContext& context, std::uint32_t now, std::int32_t id) {
  if (context.actor_messages != nullptr) context.actor_messages->push_back({id, 0, 0, 0});
  SendGameActorEffectMessage(actor, id, 0, 0, context, now);
}

// Message 0x53 to itself (00506175): the cast's gauge queued for removal.
void CloseWindow(GameActor& actor, const StateContext& context) {
  if (actor.cast_gauge == 0) return;
  if (context.closed_windows != nullptr) context.closed_windows->push_back(actor.cast_gauge);
  actor.cast_gauge = 0;
}

// vf12 (00503160; a CNpc's 005402e0): that the actor walks, to the game mode.
void ReportWalking(const GameActor& actor, const StateContext& context) {
  if (!actor.reports || IsSilentNpc(actor)) return;
  Send(context, {kWalksMessage, actor.id, 0, 0});
}

// CGameMode::AddArrowEffect (005101f0), which makes one only for a target.
void Shoot(const StateContext& context, const render::Vec3& from, std::int32_t target_id,
           float speed) {
  if (target_id != 0 && context.arrows != nullptr)
    context.arrows->push_back({&from, target_id, speed});
}

// CGameActor::LaunchRangedAttack (00504000) at the target: jobs 0x3f8 and 0x505 shoot an arrow
// at the speed given, jobs 0x3fc and 0x42c attack as a Mandragora does
// (CGameMode::AddMandraAttackEffect), the rest nothing.
void LaunchRangedAttack(const GameActor& actor, const StateContext& context, std::int32_t target_id,
                        float speed) {
  const std::int32_t job = GameActorJob(actor);
  if (job == 0x3f8 || job == kArrowJob) {
    Shoot(context, actor.object.position, target_id, speed);
  } else if ((job == 0x3fc || job == 0x42c) && context.mandra_attacks != nullptr) {
    context.mandra_attacks->push_back({target_id, job});
  }
}

// The turn toward the actor +0x1a8 names as states 2, 7 and 9 begin: the player (CWorld+0x3c)
// for own_id, otherwise the actor the world finds.
void FaceActionTarget(GameActor& actor, const StateContext& context) {
  const render::Vec3* at = actor.target_id == context.own_id ? context.player : context.target;
  if (at == nullptr) return;
  render::RenderObject& o = actor.object;
  o.facing = render::TurnTowards(o.position, (*at)[0], (*at)[2]);
}

// vf34 (00503f30): the corpse, at the actor's position, of its body and how it stands.
void LeaveCorpse(const GameActor& actor, const StateContext& context) {
  if (context.corpses == nullptr) return;
  const render::RenderObject& o = actor.object;
  context.corpses->push_back(
      {o.position, context.act_name, context.sprite_name, o.base_action, o.frame, o.facing});
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

// CPc::SetState's start of walking and attacking: +0xcc cleared and, but for body states 1
// and 2, +0x82.
void Ready(PlayerActor& player) {
  player.field_0xcc = 0;
  if (player.body_state != 1 && player.body_state != 2) player.object.held = false;
}

// CGameActor::SetState (005034e0), on an actor or a player, whose vf31 state 13 reads.
template <typename Actor>
void SetBaseState(Actor& actor, std::int32_t state, const StateContext& context,
                  std::uint32_t now) {
  if (actor.state == 3 || actor.field_0xd0 != 0) return;
  if (state == 7 && actor.state == 6) return;
  render::RenderObject& o = actor.object;
  if (actor.state == 4) {
    o.position[0] = actor.target_x;
    o.position[2] = actor.target_z;
    o.position[1] = res::AttrHeight(*context.attr, actor.target_x, actor.target_z);
  }
  switch (state) {
    case kStand:
      SetGameActorAction(actor, *context.act, kStandAction, 0);
      // A standing actor holds its frame, but for a few ids on the Korean service.
      if (!IsSpecialActorIdOnKoreaService(context.service_type, actor.id)) o.finished = true;
      ReportCell(actor, context);
      break;
    case kWalk:
      if (actor.field_0xd8 == 0) SetGameActorAction(actor, *context.act, kWalkAction, 0);
      ReportWalking(actor, context);
      break;
    case 2:
    case 7:
    case 9:
      SetGameActorAction(actor, *context.act, state == 9 ? kSecondAttackAction : kAttackAction,
                         kOnce);
      FaceActionTarget(actor, context);
      // CGameActor 00504000, which for this job is an arrow.
      if (GameActorJob(actor) == kArrowJob)
        Shoot(context, o.position, actor.target_id, kArrowSpeed);
      actor.field_0x1ac = static_cast<float>(AttackFrame(*context.act, o.action));
      ReportCell(actor, context);
      break;
    case kDying:
      o.held = false;
      if (actor.body_state == kFrozen) {
        SendSelf(actor, context, now, 0x3b);
        SendSelf(actor, context, now, 0x3e);
      }
      o.depth_up = true;
      o.finished = false;
      SetGameActorAction(actor, *context.act, kDyingAction, kOnce);
      break;
    case 4:
      // Messages 0x57 to the target and 0x54 to itself end effects they launched, which the
      // port doesn't launch.
      CloseWindow(actor, context);
      SetGameActorAction(actor, *context.act, kSlideAction, kOnce);
      ReportCell(actor, context);
      break;
    case kCast:
      SetGameActorAction(actor, *context.act, CastAction(actor), kOnce);
      o.held = true;
      break;
    default:
      if (const std::int32_t action = GameActorStateAction(state); action >= 0) {
        if (state == 5) {
          actor.field_0xcc = 0;
          o.held = false;
        }
        SetGameActorAction(actor, *context.act, action, kOnce);
        if (state == 12) ReportCell(actor, context);
      }
      break;
  }
  FinishState(actor, state, context, now);
}

// CNpc::SetState (0053fca0, vf10). By the job first: a warp portal (0x2d) keeps its area and
// launches its effect once; 0x68 rises; 0x6f doubles its size; 0x8b does nothing; other NPCs
// play action 0 and report their cell, whatever the state. Monsters (from job 1000) leave state
// 4 on its target, then by the state, with no guard against dying again. Last the action's
// direction, the state and its time, but for 7 in state 6 and 4 in an action from 0x28.
void SetNpcState(GameActor& actor, std::int32_t state, const StateContext& context,
                 std::uint32_t now) {
  render::RenderObject& o = actor.object;
  const std::int32_t job = GameActorJob(actor);
  const auto unless_stone_or_frozen = [&] {
    if (actor.body_state != 1 && actor.body_state != kFrozen) o.held = false;
  };
  const auto end_ice = [&] {
    if (actor.body_state != kFrozen) return;
    SendSelf(actor, context, now, 0x3b);
    SendSelf(actor, context, now, 0x3e);
  };
  if (job == kWarpPortalJob) {
    if (actor.field_0x1f0 <= kPortalLeastSize) actor.field_0x1f0 = kPortalLeastSize;
    if (actor.field_0x1ec <= kPortalLeastSize) actor.field_0x1ec = kPortalLeastSize;
    const std::int32_t half_x = actor.field_0x1ec / 2;
    const std::int32_t half_height = actor.field_0x1f0 / 2;
    const auto half_z = static_cast<float>(half_height);
    actor.portal_area = {core::RoundedSum(o.position[0], -static_cast<double>(half_x)),
                         core::RoundedSum(o.position[2], -static_cast<double>(half_z)),
                         core::RoundedSum(o.position[0], static_cast<double>(half_x)),
                         core::RoundedSum(o.position[2], static_cast<double>(half_z))};
    if (actor.portal_effect == 0 && o.body_shown) {
      actor.portal_effect = 1;
      LaunchGameActorEffect(actor, kPortalEffect, context);
    }
  } else if (job == kRaisedNpcJob) {
    const auto rise = static_cast<std::int32_t>(static_cast<std::uint32_t>(actor.field_0x1f0) * 5u);
    o.position[1] = core::RoundedSum(o.position[1], -static_cast<double>(rise));
  } else if (job == kDoubledNpcJob) {
    actor.field_0x1ec =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(actor.field_0x1ec) << 1);
    actor.field_0x1f0 =
        static_cast<std::int32_t>(static_cast<std::uint32_t>(actor.field_0x1f0) << 1);
  } else if (job == kStillNpcJob) {
  } else if (job < kMonsterJobsFrom) {
    SetGameActorAction(actor, *context.act, kStandAction, 0);
    ReportCell(actor, context);
  } else {
    if (actor.state == 4) {
      o.position[0] = actor.target_x;
      o.position[2] = actor.target_z;
    }
    switch (state) {
      case kStand: {
        o.depth_up = false;
        o.pickable = true;
        SetGameActorAction(actor, *context.act, kStandAction, 0);
        if (actor.field_0x224 == 0) {
          if (actor.state != kStand) actor.pet_talk_at = now;
          ReportCell(actor, context);
          break;
        }
        // The pet's performance: an action played once, and its line when it is the pet.
        const PetTalk pet = context.pet != nullptr ? *context.pet : PetTalk{};
        const std::int32_t level = PetLevel(pet.field_0x804);
        std::int32_t line = state;
        std::int32_t action = 0x28;
        if (actor.field_0x224 >= 1 && actor.field_0x224 <= 4) {
          action = 0x28 + 8 * (actor.field_0x224 - 1);
          line = PetLine(job, 6 + actor.field_0x224, level, pet.field_0x18d0);
        }
        SetGameActorAction(actor, *context.act, action, kOnce);
        if (actor.id == pet.pet_id) Send(context, {kPetLineMessage, line, 0, 0});
        actor.field_0x224 = 0;
        o.finished = false;
        ReportCell(actor, context);
        break;
      }
      case kWalk:
        unless_stone_or_frozen();
        SetGameActorAction(actor, *context.act, kWalkAction, 0);
        ReportWalking(actor, context);
        break;
      case 7:
        if (actor.state == 6) return;
        [[fallthrough]];
      case 2:
        unless_stone_or_frozen();
        SetGameActorAction(actor, *context.act, kNpcAttackAction, kOnce);
        actor.field_0x1ac = static_cast<float>(AttackFrame(*context.act, o.action));
        ReportCell(actor, context);
        LaunchRangedAttack(actor, context, actor.target_id,
                           core::RoundedProduct(o.delay, static_cast<double>(actor.field_0x1ac)));
        break;
      case kDying:
        o.held = false;
        end_ice();
        o.depth_up = true;
        o.held = false;
        o.pickable = false;
        SetGameActorAction(actor, *context.act, kNpcDyingAction, kOnce);
        ReportWalking(actor, context);
        break;
      case 4:
        // Message 0x53 through vf02 to itself.
        if (context.actor_messages != nullptr) context.actor_messages->push_back({0x53, 0, 0, 0});
        CloseWindow(actor, context);
        if (o.action >= kNpcHurtActionsFrom) {
          UpdateGameActorFrame(actor, *context.act, context.camera_yaw, now);
          return;
        }
        SetGameActorAction(actor, *context.act, kNpcHurtAction, kOnce);
        ReportCell(actor, context);
        break;
      case 10:
        end_ice();
        o.depth_up = false;
        o.finished = true;
        o.held = true;
        SetGameActorAction(actor, *context.act, kStandAction, kOnce);
        break;
      default: break;
    }
  }
  FinishState(actor, state, context, now);
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
    case kDying:
      o.held = false;
      actor.field_0xcc = 0;
      frame();
      if (o.finished && actor.hits.empty()) {
        Send(context, {kWalksMessage, actor.id, 0, 0});
        if (GameActorJob(actor) >= kCorpseJobsFrom) {
          LeaveCorpse(actor, context);
          return StateResult::kGone;
        }
      }
      break;
    case 10: actor.field_0xcc = 0; break;
    case kCast:
      if (actor.field_0xcc == 0) {
        actor.field_0xcc = 1;
        SetGameActorAction(actor, *context.act, CastAction(actor), kOnce);
        o.held = true;
      }
      if (now >= actor.cast_end) {
        actor.field_0xcc = 0;
        o.held = false;
        set_state(kStand);
        CloseWindow(actor, context);
      }
      frame();
      break;
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
void Kill(Actor& actor, SetState set_state) {
  const std::int32_t job = GameActorJob(actor);
  if (job > 1000 && job < 0x580)
    actor.id = static_cast<std::int32_t>(kMonsterIdBase - static_cast<std::uint32_t>(actor.id));
  if (!actor.hits.empty() && GameActorJob(actor) >= kQueueDelaysFrom) {
    actor.field_0x210 = 1;
    return;
  }
  set_state(kDying);
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

PlayerActor MakePlayerActor(std::uint32_t now, std::uint32_t server_time) {
  PlayerActor player;
  static_cast<GameActor&>(player) = MakeGameActor(now, server_time);
  return player;
}

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

std::int32_t PetLevel(std::int32_t value) {
  if (value > 90 && value <= 100) return 4;
  if (value > 75 && value <= 90) return 3;
  if (value > 25 && value <= 75) return 2;
  if (value > 10 && value <= 25) return 1;
  return 0;
}

std::int32_t PetLine(std::int32_t job, std::int32_t kind, std::int32_t level,
                     std::int32_t field_0x18d0) {
  const std::uint32_t row =
      static_cast<std::uint32_t>(kind) + (static_cast<std::uint32_t>(job) * 5u - 5000u) * 2u;
  const std::uint32_t line = static_cast<std::uint32_t>(level) + row * 10u;
  return static_cast<std::int32_t>(line + (field_0x18d0 == 1 ? 50000u : 90000u));
}

void SetGameActorState(GameActor& actor, std::int32_t state, const StateContext& context,
                       std::uint32_t now) {
  if (actor.npc) {
    SetNpcState(actor, state, context, now);
  } else {
    SetBaseState(actor, state, context, now);
  }
}

void SetPlayerState(PlayerActor& player, std::int32_t state, const StateContext& context,
                    std::uint32_t now) {
  const std::int32_t before = player.state;
  if (player.own) {
    if (before == kDying) return;
    if (state != kWalk) player.own->goal_x = player.own->goal_y = -1;
    SendQueuedMove(player, context);
  }
  if (before == 3 || player.field_0xd0 != 0) return;
  if (state == 7 && before == 6) return;
  render::RenderObject& o = player.object;
  if (before == 4) {
    o.position[0] = player.target_x;
    o.position[2] = player.target_z;
  }
  switch (state) {
    case 6:
    case 8:
      ReportCell(player, context);
      SetGameActorAction(player, *context.act, state == 6 ? 0x10 : 0x60, kOnce);
      o.finished = true;
      if (state == 6) o.frame = player.saved_frame;
      break;
    case kStand:
      SetBaseState(player, kStand, context, now);
      o.frame = player.saved_frame;
      if (before == 4 || before == 2 || player.field_0x224 != 0 || before == 8)
        SetGameActorAction(player, *context.act, kReadyAction, 0);
      if (context.mode_field_0x2a0 != nullptr) *context.mode_field_0x2a0 = 0;
      break;
    case kWalk:
      Ready(player);
      CloseWindow(player, context);
      player.saved_frame = 0;
      SetBaseState(player, kWalk, context, now);
      break;
    case 2:
    case 9: {
      Ready(player);
      CloseWindow(player, context);
      const std::int32_t job = GameActorJob(player);
      const bool second = AttacksWithSecondAction(job, player.field_0x1bc, player.weapon);
      SetBaseState(player, second ? 9 : 2, context, now);
      player.field_0x1ac = PlayerAttackFrame(job, player.field_0x1bc, player.weapon, second);
      std::int32_t kind = player.weapon;
      if (state == 2 && kind > 0x17) kind = WeaponKind(kind);
      // State 2 shoots for a bow's kind, 9 for the weapons 0xd and 0xe as they are.
      if (state == 2 ? kind == kBowKind : (kind == 0xd || kind == 0xe)) {
        Shoot(context, o.position, player.target_id, o.delay * player.field_0x1ac);
        player.field_0x1ac = (kPlayerArrowTime / o.delay) + player.field_0x1ac;
      }
      break;
    }
    case 7: {
      const std::int32_t job = GameActorJob(player);
      const bool low =
          job == 0x13 || job == 0x14 || job == 0xe || (job == 0xf && player.field_0x1bc == 0);
      SetGameActorAction(player, *context.act, low ? 0x20 : 0x60, kOnce);
      player.field_0x1ac = kPlayerHitFrame;
      break;
    }
    default: SetBaseState(player, state, context, now); return;
  }
  FinishState(player, state, context, now);
}

void KillGameActor(GameActor& actor, const StateContext& context, std::uint32_t now) {
  Kill(actor, [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

void KillGameActor(PlayerActor& player, const StateContext& context, std::uint32_t now) {
  Kill(player, [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
}

void ReportGameActorCell(const GameActor& actor, const StateContext& context) {
  ReportCell(actor, context);
}

void KillGameActorAfterQueue(GameActor& actor, const StateContext& context, std::uint32_t now) {
  if (actor.state != kDying) SetGameActorState(actor, kDying, context, now);
}

void KillGameActorAfterQueue(PlayerActor& player, const StateContext& context, std::uint32_t now) {
  if (player.state != kDying) SetPlayerState(player, kDying, context, now);
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
