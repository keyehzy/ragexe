#include "world/actor_arrival.h"

#include <cstdio>

#include "core/x87.h"
#include "render/world_load.h"
#include "world/actor_effects.h"
#include "world/player_look.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kStand = 0;
constexpr std::int32_t kWalk = 1;
constexpr std::int32_t kEmperiumJob = 0x2d2;  // its emblem comes in the record's +0x44..+0x4c
constexpr std::int32_t kMissingEmblemMessage = 0x8b;
constexpr std::uint32_t kFadeInColor = 0x44ffffff;
constexpr float kSixteenth = 0.0625f;  // 005e8a7c
constexpr std::int32_t kKeep = -1;     // SetColorChannels: the channel kept

constexpr std::uint32_t kSighted = 1u << 0;
constexpr std::uint32_t kHiding = 1u << 1;
constexpr std::uint32_t kCloaked = 1u << 2;
constexpr std::uint32_t kGroupedBits = 1u << 3 | 1u << 7 | 1u << 8 | 1u << 9 | 1u << 10;
constexpr std::uint32_t kBit4 = 1u << 4;
constexpr std::uint32_t kBit5 = 1u << 5;
constexpr std::uint32_t kHidden = 1u << 6;

// Who sees hidden actors, by ClientState 00597950.
constexpr std::int32_t kSeers[] = {0x186b4, 0x2933c9, 0x326b70};

void Send(const StateContext& context, const ModeMessage& message) {
  if (context.messages != nullptr) context.messages->push_back(message);
}

// vf02 to itself, from no sender: kept, and made and ended the effects of; a player's through
// CPc's own first.
void SendSelf(GameActor& actor, const StateContext& context, std::uint32_t now, std::int32_t id,
              std::int32_t a = 0) {
  if (context.actor_messages != nullptr) context.actor_messages->push_back({id, a, 0, 0});
  SendGameActorEffectMessage(actor, id, 0, a, context, now);
}

void SendSelf(PlayerActor& player, const StateContext& context, std::uint32_t now, std::int32_t id,
              std::int32_t a = 0) {
  if (context.actor_messages != nullptr) context.actor_messages->push_back({id, a, 0, 0});
  if (SendPlayerMessage(player, id, a, 0, context, now)) return;
  SendGameActorEffectMessage(player, id, 0, a, context, now);
}

// PlayWave where the actor stands, as the player hears it.
void Play(const GameActor& actor, const StateContext& context, std::string_view name) {
  if (context.player == nullptr) return;
  if (context.sounds == nullptr) return;
  const render::Vec3& at = actor.object.position;
  context.sounds->push_back({name, at[0] - (*context.player)[0], at[2] - (*context.player)[2]});
}

// CRenderObject::SetColorChannels (00500720): each channel but kKeep to its byte of +0x7c.
void SetColorChannels(render::RenderObject& o, std::int32_t alpha, std::int32_t red,
                      std::int32_t green, std::int32_t blue) {
  const auto set = [&o](std::int32_t value, int shift) {
    if (value == kKeep) return;
    o.color = (o.color & ~(0xffu << shift)) | (static_cast<std::uint32_t>(value) & 0xffu) << shift;
  };
  set(alpha, 24);
  set(red, 16);
  set(green, 8);
  set(blue, 0);
}

// 00506bf0: the effects of the shown actor ended (0x6d with 0xc8, 0xc9, then 0xca five times).
template <class Actor>
void EndShownEffects(Actor& actor, const StateContext& context, std::uint32_t now) {
  SendSelf(actor, context, now, 0x6d, 0xc8);
  SendSelf(actor, context, now, 0x6d, 0xc9);
  for (int k = 0; k < 5; ++k) SendSelf(actor, context, now, 0x6d, 0xca);
}

// What hiding and showing again end with: the grouped bits' and bit 4's effects, put away
// (0x34, 0x66) or back (0x33, 0x65).
template <class Actor>
void EndHiding(Actor& actor, const StateContext& context, std::uint32_t now, bool hidden) {
  if ((actor.effect_state & kGroupedBits) != 0) SendSelf(actor, context, now, hidden ? 0x34 : 0x33);
  if ((actor.effect_state & kBit4) != 0) SendSelf(actor, context, now, hidden ? 0x66 : 0x65);
}

// Bits 1 and 2: hidden, the body (and for bit 2 the whole object) not drawn, and to others
// than the player without the shadow either; or shown again.
template <class Actor>
void Hide(Actor& actor, const StateContext& context, std::uint32_t now, bool whole, bool hidden) {
  render::RenderObject& o = actor.object;
  const bool own = actor.id == context.own_id;
  EndShownEffects(actor, context, now);
  if (hidden) {
    o.body_shown = false;
    if (whole) o.visible = false;
    if (!own && !SeesHiddenActors(context.service_type, context.own_id)) {
      o.shadow = false;
      if (whole) {
        SendSelf(actor, context, now, 0x23);
        SendSelf(actor, context, now, 0x6d, 0x9f);
      } else {
        SendSelf(actor, context, now, 0x6d, 0x9f);
        SendSelf(actor, context, now, 0x23);
      }
    }
  } else {
    o.body_shown = true;
    if (whole) o.visible = true;
    if (!own) o.shadow = true;
  }
  EndHiding(actor, context, now, hidden);
}

// Bit 6: hidden from all but those who see hidden actors, the player too; or shown again.
template <class Actor>
void HideFromAll(Actor& actor, const StateContext& context, std::uint32_t now, bool hidden) {
  render::RenderObject& o = actor.object;
  EndShownEffects(actor, context, now);
  if (!hidden) {
    o.body_shown = o.visible = o.shadow = true;
  } else if (SeesHiddenActors(context.service_type, context.own_id)) {
    o.body_shown = o.visible = true;
    o.shadow = false;
  } else {
    o.body_shown = o.visible = o.shadow = false;
    SendSelf(actor, context, now, 0x6d, 0x9f);
  }
  EndHiding(actor, context, now, hidden);
}

// The body state the actor leaves: its effects ended and CGameMode+0x2a0 cleared.
template <class Actor>
void LeaveBodyState(Actor& actor, std::int32_t state, const StateContext& context,
                    std::uint32_t now) {
  render::RenderObject& o = actor.object;
  switch (state) {
    case 1: Play(actor, context, "_stone_explosion.wav"); break;
    case 2:
      SendSelf(actor, context, now, 0x3b);
      SendSelf(actor, context, now, 0x3e);
      Play(actor, context, "_frozen_explosion.wav");
      o.held = false;
      o.tinted = false;
      o.finished = true;
      SetColorChannels(o, kKeep, 0xff, 0xff, 0xff);
      break;
    case 3: SendSelf(actor, context, now, 0x48); break;
    case 4: SendSelf(actor, context, now, 0x46); break;
    default: return;
  }
  if (context.mode_field_0x2a0 != nullptr) *context.mode_field_0x2a0 = 0;
}

// The body state the actor takes.
template <class Actor>
void TakeBodyState(Actor& actor, std::int32_t state, const StateContext& context,
                   std::uint32_t now) {
  render::RenderObject& o = actor.object;
  switch (state) {
    case 1:
      o.held = o.tinted = o.finished = true;
      SetColorChannels(o, kKeep, 0x40, 0x40, 0x40);
      break;
    case 2:
      SendSelf(actor, context, now, 0x3a);
      o.held = o.tinted = o.finished = true;
      SetColorChannels(o, kKeep, 0, 0x80, 0xff);
      break;
    case 3: SendSelf(actor, context, now, 0x47); break;
    case 4: SendSelf(actor, context, now, 0x45); break;
    case 6:
      o.tinted = true;
      SetColorChannels(o, kKeep, 0x80, 0x80, 0x80);
      break;
    default: break;
  }
}

// The health state's bits, each time.
template <class Actor>
void ShowHealthState(Actor& actor, const StateContext& context, std::uint32_t now) {
  render::RenderObject& o = actor.object;
  const std::uint32_t health = actor.health_state;
  if ((health & 1) != 0) {
    o.tinted = true;
    SetColorChannels(o, kKeep, 0xff, 0x80, 0xff);
  }
  if ((health & 2) != 0) {
    SendSelf(actor, context, now, 0x43);
    o.tinted = true;
    SetColorChannels(o, kKeep, 0xc8, 0x32, 0x32);
  } else {
    SendSelf(actor, context, now, 0x44);
  }
  SendSelf(actor, context, now, (health & 4) != 0 ? 0x49 : 0x4a);
  SendSelf(actor, context, now, (health & 0x20) != 0 ? 0x4b : 0x4c);
  SendSelf(actor, context, now, (health & 0x40) != 0 ? 0x4e : 0x4f);
  if (actor.id == context.own_id && context.ground_water_fade != nullptr)
    *context.ground_water_fade = (health & 0x10) != 0 ? 1 : 0;
}

template <class Actor>
void SetStatus(Actor& actor, std::int32_t body_state, std::uint32_t effect_state,
               std::uint32_t health_state, const StateContext& context, std::uint32_t now) {
  const std::int32_t old_body = actor.body_state;
  const std::uint32_t old_effect = actor.effect_state;
  actor.body_state = body_state;
  actor.effect_state = effect_state;
  actor.health_state = health_state;
  const std::uint32_t changed = effect_state ^ old_effect;
  if ((changed & kSighted) != 0 && (effect_state & kSighted) != 0)
    Play(actor, context, "effect\\EF_Sight.wav");
  if ((changed & kHiding) != 0) Hide(actor, context, now, false, (effect_state & kHiding) != 0);
  if ((changed & kCloaked) != 0) Hide(actor, context, now, true, (effect_state & kCloaked) != 0);
  if ((changed & kGroupedBits) != 0) {
    SendSelf(actor, context, now, 0x34);
    if ((effect_state & kGroupedBits) != 0)
      SendSelf(actor, context, now, 0x33, static_cast<std::int32_t>(effect_state));
  }
  if ((changed & kBit4) != 0)
    SendSelf(actor, context, now, (effect_state & kBit4) != 0 ? 0x65 : 0x66);
  if ((changed & kBit5) != 0)
    SendSelf(actor, context, now, (effect_state & kBit5) != 0 ? 0x69 : 0x6a);
  if ((changed & kHidden) != 0) HideFromAll(actor, context, now, (effect_state & kHidden) != 0);
  if (old_body != body_state) {
    LeaveBodyState(actor, old_body, context, now);
    TakeBodyState(actor, body_state, context, now);
  }
  ShowHealthState(actor, context, now);
  if (health_state == 0 && body_state == 0) {
    render::RenderObject& o = actor.object;
    o.tinted = false;
    o.held = false;
    SetColorChannels(o, kKeep, 0xff, 0xff, 0xff);
  }
}

}  // namespace

bool SeesHiddenActors(std::int32_t service_type, std::int32_t own_id) {
  if (service_type != 0) return false;
  for (const std::int32_t id : kSeers) {
    if (own_id == id) return true;
  }
  return false;
}

std::string EmblemFileName(std::string_view server_name, std::int32_t guild, std::int32_t version) {
  char name[256];
  if (version > 0) {
    std::snprintf(name, sizeof name, "%.*s_%d_%d.ebm", static_cast<int>(server_name.size()),
                  server_name.data(), guild, version);
  } else {
    std::snprintf(name, sizeof name, "..\\data\\texture\\effect\\flagGraffiti%d.ebm", guild);
  }
  return name;
}

void SetGameActorEmblem(GameActor& actor, std::int32_t guild, std::int32_t emblem,
                        const StateContext& context) {
  if (guild < 1) return;
  const std::int32_t job = GameActorJob(actor);
  if (emblem < 1 && job != kEmperiumJob) return;
  const bool shows =
      (job >= 0 && job < 0x16) || (job >= 0x505 && job <= 0x507) || job == kEmperiumJob;
  if (!shows) return;
  actor.guild = guild;
  actor.emblem = emblem;
  const std::string name = EmblemFileName(context.server_name, guild, emblem);
  if (!context.resource_exists || context.resource_exists(name)) return;
  Send(context, {kMissingEmblemMessage, actor.guild, 0, 0});
}

void SetGameActorStatus(GameActor& actor, std::int32_t body_state, std::uint32_t effect_state,
                        std::uint32_t health_state, const StateContext& context,
                        std::uint32_t now) {
  SetStatus(actor, body_state, effect_state, health_state, context, now);
}

void SetPlayerStatus(PlayerActor& player, std::int32_t body_state, std::uint32_t effect_state,
                     std::uint32_t health_state, const StateContext& context, std::uint32_t now) {
  SetStatus(player, body_state, effect_state, health_state, context, now);
}

bool ArriveGameActor(GameActor& actor, std::int32_t message, StandingArrival record,
                     std::int32_t how, const StateContext& context, std::uint32_t now) {
  render::RenderObject& o = actor.object;
  if (message == 4) actor.id = record.id;
  const std::array<float, 2> centre =
      render::CellCentre(*context.attr, record.cell_x, record.cell_y);
  o.position = {centre[0], res::AttrHeight(*context.attr, centre[0], centre[1]), centre[1]};
  o.facing = static_cast<float>(
      static_cast<std::int32_t>((static_cast<std::uint32_t>(record.direction) + 4) * 45));
  actor.field_0x1ec = record.field_0x1ec;
  actor.field_0x1f0 = record.field_0x1f0;
  actor.goal_x = record.cell_x;
  actor.goal_y = record.cell_y;
  if (message == 4) actor.speed = record.speed;
  actor.field_0x1f4 = record.field_0x1f4;
  SetGameActorStatus(actor, record.body_state, record.effect_state, record.health_state, context,
                     now);
  SetGameActorState(actor, kStand, context, now);
  if (message == 3) {
    SetGameActorEmblem(actor, record.guild, record.emblem, context);
    return true;
  }
  actor.field_0x224 = record.field_0x224;
  if (record.job == kEmperiumJob) {
    record.guild = static_cast<std::int32_t>(static_cast<std::uint32_t>(record.field_0x48) << 16 |
                                             static_cast<std::uint32_t>(record.field_0x4c));
    record.emblem = record.field_0x44;
    SetGameActorEmblem(actor, record.guild, record.emblem, context);
  }
  if (how == 1) {
    o.color = kFadeInColor;
  } else if (how == 2 && (record.effect_state & kHidden) == 0) {
    return false;  // LaunchEffect(6)
  }
  return true;
}

PathResult ArriveWalkingGameActor(GameActor& actor, PathFinder& finder, std::int32_t message,
                                  WalkingArrival record, std::uint32_t server_time,
                                  const StateContext& context, std::uint32_t now) {
  if (message == 6) {
    actor.id = record.id;
  } else {
    actor.speed = record.speed;
    actor.move_time = record.order.time;
  }
  actor.field_0x1f4 = record.field_0x1f4;
  SetGameActorStatus(actor, record.body_state, record.effect_state, record.health_state, context,
                     now);
  actor.field_0x224 = record.field_0x224;
  SetGameActorEmblem(actor, record.guild, record.emblem, context);
  const MoveOrder& order = record.order;
  if (message == 6) {
    actor.speed = record.speed;
    actor.move_time = order.time;
    render::RenderObject& o = actor.object;
    const std::array<float, 2> centre =
        render::CellCentre(*context.attr, order.start_x, order.start_y);
    const float step =
        core::RoundedProduct(kSixteenth, static_cast<double>(context.attr->cell_size));
    const float x = core::RoundedProduct(step, static_cast<double>(order.sub_x - 8)) + centre[0];
    const float z = core::RoundedProduct(step, static_cast<double>(order.sub_y - 8)) + centre[1];
    o.position = {x, res::AttrHeight(*context.attr, x, z), z};
  }
  const PathResult found =
      FindMovePath(finder, actor.path, actor.move_time, server_time, order.start_x, order.start_y,
                   order.sub_x, order.sub_y, order.goal_x, order.goal_y, record.speed);
  if (found != PathResult::kFound) {
    if (message == 6) SetGameActorState(actor, kStand, context, now);
    return found;
  }
  actor.goal_x = order.goal_x;
  actor.goal_y = order.goal_y;
  SetGameActorState(actor, kWalk, context, now);
  actor.segment = 0;
  return found;
}

}  // namespace ragexe::world
