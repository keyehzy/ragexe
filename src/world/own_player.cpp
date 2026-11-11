#include "world/own_player.h"

#include "render/world_load.h"
#include "res/gat.h"
#include "world/actor_arrival.h"
#include "world/actor_effects.h"
#include "world/actor_hit.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kSwordsman = 7;  // 0059d2e0: mounted, 0xd
constexpr std::int32_t kMountedSwordsman = 0xd;
constexpr std::int32_t kCrusader = 0xe;  // mounted, 0x15
constexpr std::int32_t kMountedCrusader = 0x15;
constexpr std::uint32_t kMountedBit = 0x20;       // IsBit5Set (0059d3d0)
constexpr std::uint32_t kLastActedBefore = 1200;  // 0054b5b6: 0x4b0 ms
constexpr std::int32_t kEndEffect = 0x6d;
constexpr std::int32_t kStatusIcons = 0xa4;  // ProcessStatusIcons (00562b30)
constexpr std::int32_t kAfterImages = 0xa6;  // ProcessAfterImages (00562d90)
constexpr std::int32_t kSetColor = 0x2c;
constexpr std::int32_t kAllChannels = 0xffff;

struct StatusEffect {
  std::int32_t status;
  std::int32_t effect;
  std::int32_t color;  // message 0x2c's second word, 0 for none sent
};

// 0054cf80's cases, in order.
constexpr std::array<StatusEffect, 6> kStatusEffects{{
    {2, kAfterImages, 0x82},
    {0x19, kAfterImages, 0x80},
    {0x1f, kAfterImages, 0xa9},
    {0x44, kAfterImages, 0xfa},
    {0x56, 0xcb, 0},
    {0x57, 0xfe, 0},
}};

}  // namespace

std::int32_t OwnDressJob(const OwnSession& session) {
  const bool mounted = (session.effect_state & kMountedBit) != 0;
  if (session.job == kSwordsman && mounted) return kMountedSwordsman;
  if (session.job == kCrusader && mounted) return kMountedCrusader;
  return session.job;
}

PlayerDress OwnDress(const OwnSession& session) {
  return {OwnDressJob(session), session.sex,    session.hair,       session.weapon,
          session.headgear,     session.shield, session.hair_color, session.body_color};
}

void LaunchOwnStatusEffects(PlayerActor& player, const StateContext& context) {
  if (context.own_statuses == nullptr) return;
  for (const std::int32_t status : *context.own_statuses) {
    for (const StatusEffect& s : kStatusEffects) {
      if (s.status != status) continue;
      const std::int32_t effect = LaunchGameActorEffect(player, s.effect, context);
      if (effect != 0 && s.color != 0 && context.launched_messages != nullptr) {
        context.launched_messages->push_back(
            {.effect = effect, .id = kSetColor, .a = kAllChannels, .b = s.color});
      }
    }
  }
}

PlayerActor MakeOwnPlayer(const OwnSession& session, std::array<std::int32_t, 2> screen_origin,
                          const StateContext& context, std::uint32_t now,
                          std::uint32_t server_time) {
  PlayerActor player = MakePlayerActor(now, server_time);
  const res::AttrMap& attr = *context.attr;
  const std::array<float, 2> at = render::CellCentre(attr, session.cell_x, session.cell_y);
  player.object.position = {at[0], res::AttrHeight(attr, at[0], at[1]), at[1]};
  OwnPlayer own;
  // The goal, which CPlayer::SetState(0) below forgets again.
  const std::array<std::int32_t, 2> cell = AttrCellAt(attr, at[0], at[1]);
  own.goal_x = cell[0];
  own.goal_y = cell[1];
  // 0054b52b: (direction × 5 + 20) × 9 in 32 bits, as an integer to a float.
  const std::uint32_t degrees = (static_cast<std::uint32_t>(session.direction) * 5u + 20u) * 9u;
  player.object.facing = static_cast<float>(static_cast<std::int32_t>(degrees));
  player.id = session.id;
  player.guild = session.guild;
  player.emblem = session.emblem;
  player.segment = -1;
  player.object.pickable = true;
  own.acted_at = now - kLastActedBefore;
  own.asked_at = now - kLastActedBefore;
  own.walk_asked_at = now - kLastActedBefore;
  player.object.screen_x = screen_origin[0];
  player.object.screen_y = screen_origin[1];
  player.own = own;
  player.reports = false;
  SetPlayerState(player, 0, context, now);
  player.speed = session.speed;
  // CGameActor::SetSex (00503020) is left to vf14, which sets the sex the same way.
  const std::array<std::int32_t, 3> kept{player.body_state,
                                         static_cast<std::int32_t>(player.effect_state),
                                         static_cast<std::int32_t>(player.health_state)};
  player.body_state = session.body_state;
  player.effect_state = session.effect_state;
  player.health_state = session.health_state;
  DressPlayer(player, OwnDress(session), false, context);
  player.body_state = kept[0];
  player.effect_state = static_cast<std::uint32_t>(kept[1]);
  player.health_state = static_cast<std::uint32_t>(kept[2]);
  SetPlayerStatus(player, session.body_state, session.effect_state, session.health_state, context,
                  now);
  if (context.actor_messages != nullptr)
    context.actor_messages->push_back({kEndEffect, kStatusIcons, 0, 0});
  if (!SendPlayerMessage(player, kEndEffect, kStatusIcons, 0, context, now))
    SendGameActorEffectMessage(player, kEndEffect, 0, kStatusIcons, context, now);
  player.own->status_icons = LaunchGameActorEffect(player, kStatusIcons, context);
  LaunchOwnStatusEffects(player, context);
  return player;
}

}  // namespace ragexe::world
