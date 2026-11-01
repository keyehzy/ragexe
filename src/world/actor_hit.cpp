#include "world/actor_hit.h"

#include <array>

#include "render/actor.h"
#include "render/world_load.h"
#include "world/actor_attack.h"
#include "world/actor_effects.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kMiss = 0x59;          // to the attacker, for no damage
constexpr std::int32_t kCritical = 0x5c;      // after 0x5d's number
constexpr std::int32_t kNothing = 0x5e;       // a hit of no damage
constexpr std::int32_t kNothingShown = 0x5f;  // 0x5e's
constexpr std::int32_t kOneOfMany = 0x6f;     // with its total
constexpr std::int32_t kRestart = 0x6c;
constexpr std::int32_t kNoFlinch = 0x27;        // lands without state 4
constexpr std::int32_t kOwnStatusShown = 0x7d;  // instead of 0x59 for the player
constexpr std::int32_t kShownStatus = 0x13;     // the player's status that shows it
constexpr std::int32_t kStarted = 0x2c;         // to each effect the hit launches
constexpr std::int32_t kTowardAttacker = 0xe;   // the attacker's position, to 0xd8 and 0xd9
constexpr std::int32_t kTurn = 0x7a;            // +0x1c's turn of 90 degrees
constexpr float kTurnBy = 90.0f;                // 005e26b8
constexpr float kFullTurn = 360.0f;             // 005e2a5c
constexpr std::int32_t kSelfHit = -2;           // +0x1c of an attack on the attacker itself
constexpr std::int32_t kMonsterJobs = 0x2d;     // GetJob from which hits show white
constexpr std::int32_t kPlayerJobs = 0x16;      // ClientState 00599a00's jobs
constexpr std::int32_t kWeaponIds = 1000;       // past it vf31 is an item's id
constexpr std::int32_t kFlinch = 4;
constexpr std::int32_t kAttack = 2;
constexpr std::int32_t kDying = 3;
constexpr std::int32_t kRestartFrame = 4;
constexpr std::int32_t kMostEffectId = 60000;  // 0xea60
constexpr std::int32_t kMarksField0xa8 = 0x7f;

// ShowGameActorNumber's colours, ARGB.
constexpr std::uint32_t kRed = 0xffff0000;
constexpr std::uint32_t kWhite = 0xffffffff;
constexpr std::uint32_t kYellow = 0xffffff00;
constexpr std::uint32_t kNegative = 0x72;

// The tables ClientState 00599620 and 00599a40 fill.
constexpr std::string_view kClothes = "player_clothes.wav";
constexpr std::string_view kMetal = "player_metal.wav";
constexpr std::string_view kWooden = "player_wooden_male.wav";
constexpr std::array<std::string_view, kPlayerJobs> kPlayerHitSounds = {
    kClothes, kMetal,   kClothes, kWooden,  kClothes, kClothes, kWooden,  kMetal,
    kClothes, kClothes, kClothes, kWooden,  kWooden,  kClothes, kClothes, kClothes,
    kClothes, kClothes, kClothes, kClothes, kClothes, kClothes};
constexpr std::string_view kMace = "_hit_mace.wav";
constexpr std::string_view kSword = "_hit_sword.wav";
constexpr std::string_view kSpear = "_hit_spear.wav";
constexpr std::string_view kAxe = "_hit_axe.wav";
constexpr std::array<std::string_view, 0x17> kWeaponHitSounds = {
    kMace, kSword, kSword, kSword, kSpear,         kSpear,
    kAxe,  kAxe,   kMace,  kMace,  "_hit_rod.wav", "_hit_arrow.wav",
    kMace, kMace,  kMace,  kMace,  kMace,          kMace,
    kMace, kMace,  kMace,  kMace,  kMace};
constexpr std::array<std::string_view, 4> kEnemyHits = {
    "_enemy_hit_normal1.wav", "_enemy_hit_normal2.wav", "_enemy_hit_normal3.wav",
    "_enemy_hit_normal4.wav"};
constexpr std::array<std::string_view, 4> kFistHits = {"_hit_fist1.wav", "_hit_fist2.wav",
                                                       "_hit_fist3.wav", "_hit_fist4.wav"};

bool IsLanding(std::int32_t message) {
  switch (message) {
    case 0x1a:
    case kNoFlinch:
    case 0x5d:
    case kNothing:
    case 0x60:
    case 0x61:
    case kOneOfMany: return true;
    default: return false;
  }
}

// ClientState 005aa750: whether the player has the status (packet 0x0196's list).
bool HasOwnStatus(const StateContext& context, std::int32_t status) {
  if (context.own_statuses == nullptr) return false;
  for (const std::int32_t s : *context.own_statuses) {
    if (s == status) return true;
  }
  return false;
}

void TellLaunched(const StateContext& context, const EffectMessage& message) {
  if (message.effect != 0 && context.launched_messages != nullptr)
    context.launched_messages->push_back(message);
}

// With +0x210 set and at most this hit queued, message 0xc to itself.
template <typename Actor>
void DieAfterLastHit(Actor& actor, const StateContext& context, std::uint32_t now) {
  if (actor.field_0x210 != 0 && actor.hits.size() <= 1)
    KillGameActorAfterQueue(actor, context, now);
}

template <typename Actor, typename SetState>
void Land(Actor& actor, std::int32_t message, const QueuedHit& hit, const StateContext& context,
          std::uint32_t now, SetState set_state) {
  render::RenderObject& o = actor.object;
  if (hit.cell_x == -1 || hit.cell_y == -1) {
    actor.target_x = o.position[0];
    actor.target_z = o.position[2];
  } else {
    const std::array<float, 2> centre = render::CellCentre(*context.attr, hit.cell_x, hit.cell_y);
    actor.target_x = centre[0];
    actor.target_z = centre[1];
  }
  std::int32_t job = GameActorJob(actor);
  std::int32_t weapon = -1;
  const bool by_player = hit.attacker_id == context.own_id;
  GameActor* attacker = by_player ? context.player_actor : context.target_actor;
  if (attacker != nullptr) {
    job = GameActorJob(*attacker);
    weapon = by_player ? context.player_weapon : context.target_weapon;
  }
  std::int32_t damage = message == kNothing ? 0 : hit.damage;
  const bool negative = damage < 0;
  if (negative) damage = static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(damage));
  if (damage == 0) {
    if (message == kNothing) {
      SendGameActorEffectMessage(actor, kNothingShown, 0, 0, context, now);
    } else if (attacker != nullptr) {
      if (job < kMonsterJobs) {
        SendGameActorEffectMessage(*attacker, kMiss, 0, static_cast<std::int32_t>(kRed), context,
                                   now);
      } else if (actor.id == context.own_id && HasOwnStatus(context, kShownStatus)) {
        SendGameActorEffectMessage(actor, kOwnStatusShown, 0, 0, context, now);
      } else {
        SendGameActorEffectMessage(*attacker, kMiss, 0, -1, context, now);
      }
    }
    DieAfterLastHit(actor, context, now);
    return;
  }
  o.keyed_at = now;
  if (hit.field_0x1c == kSelfHit) {
    o.keyed = false;
  } else {
    o.keyed = true;
    core::CrtRand none;
    core::CrtRand& rand = context.rand != nullptr ? *context.rand : none;
    const std::int32_t own_job = GameActorJob(actor);
    std::string_view name;
    if (message == 0x60 || message == 0x61) {
      name = WeaponHitSound(-1, rand);
    } else if (own_job >= 0 && own_job < kPlayerJobs) {
      name = PlayerHitSound(own_job, rand);
    } else {
      name = WeaponHitSound(weapon > kWeaponIds ? WeaponKind(weapon) : weapon, rand);
    }
    if (context.sounds != nullptr && context.player != nullptr) {
      context.sounds->push_back(
          {name, o.position[0] - (*context.player)[0], o.position[2] - (*context.player)[2]});
    }
  }
  TellLaunched(
      context,
      {.effect = LaunchGameActorEffect(actor, hit.field_0x18, context), .id = kStarted, .a = 1});
  std::int32_t second = hit.field_0x1c;
  if (second == kTurn) {
    o.facing += kTurnBy;
    if (o.facing > kFullTurn) o.facing -= kFullTurn;
    second = -1;
  }
  const std::int32_t launched = LaunchGameActorEffect(actor, second, context);
  TellLaunched(context, {.effect = launched, .id = kStarted, .a = 1});
  if ((second == 0xd8 || second == 0xd9) && attacker != nullptr)
    TellLaunched(context,
                 {.effect = launched, .id = kTowardAttacker, .at = &attacker->object.position});
  if (negative) {
    ShowGameActorNumber(actor, damage, kNegative, 0, context);
    DieAfterLastHit(actor, context, now);
    return;
  }
  if (message == 0x5d) {
    ShowGameActorNumber(actor, damage, kYellow, 0x10, context);
    SendGameActorEffectMessage(actor, kCritical, 0, 0, context, now);
  } else if (GameActorJob(actor) < kMonsterJobs) {
    ShowGameActorNumber(actor, damage, kRed, 0xe, context);
  } else if (message == kOneOfMany) {
    ShowGameActorNumber(actor, damage, kWhite, 0x15, context);
    ShowGameActorNumber(actor, hit.field_0x20, kYellow, 0x16, context);
  } else {
    ShowGameActorNumber(actor, damage, kWhite, 0xe, context);
  }
  const float motion = hit.field_0x24;
  if (GameActorJob(actor) < kMonsterJobs) {
    if (actor.state != kDying && actor.state != kAttack && message != kNoFlinch) {
      set_state(kFlinch);
      if (motion > 1.0f) {
        o.repeats = motion;
      } else {
        o.delay = motion * o.delay;
      }
    }
  } else if (actor.state != kDying && message != kNoFlinch) {
    set_state(kFlinch);
    if (motion > 1.0f) {
      o.repeats = motion;
    } else {
      o.delay = motion;
    }
  }
  DieAfterLastHit(actor, context, now);
}

template <typename Actor, typename SetState>
void Restart(Actor& actor, const StateContext& context, std::uint32_t now, SetState set_state) {
  render::RenderObject& o = actor.object;
  set_state(kAttack);
  actor.field_0x1ac = static_cast<float>(AttackFrame(*context.act, o.action));
  ReportGameActorCell(actor, context);
  o.start_time = now;
  o.frame = kRestartFrame;
  o.delay = 1.0f;
  o.action += GameActorDirection(actor, o.facing, context.camera_yaw);
  DieAfterLastHit(actor, context, now);
}

template <typename Actor>
void Deliver(Actor& actor, const StateContext& context, std::uint32_t now) {
  std::size_t k = 0;
  while (k < actor.hits.size()) {
    if (now < actor.hits[k].time) {
      ++k;
      continue;
    }
    const QueuedHit hit = actor.hits[k];
    if (hit.message == kRestart) {
      RestartGameActorAttack(actor, context, now);
    } else {
      LandGameActorHit(actor, hit.message, hit, context, now);
    }
    actor.hits.erase(actor.hits.begin() + static_cast<std::ptrdiff_t>(k));
  }
}

}  // namespace

std::string_view PlayerHitSound(std::int32_t job, core::CrtRand& rand) {
  if (job < 0 || job >= kPlayerJobs) return WeaponHitSound(-1, rand);
  return kPlayerHitSounds[static_cast<std::size_t>(job)];
}

std::string_view WeaponHitSound(std::int32_t kind, core::CrtRand& rand) {
  if (kind == -1) return kEnemyHits[static_cast<std::size_t>(rand.Next() % 4)];
  if (kind == 0) return kFistHits[static_cast<std::size_t>(rand.Next() % 4)];
  if (kind < 0 || static_cast<std::size_t>(kind) >= kWeaponHitSounds.size()) return "";
  return kWeaponHitSounds[static_cast<std::size_t>(kind)];
}

bool LaunchesEffect(std::int32_t id, bool effects_on) {
  switch (id) {
    case 0x23:
    case 0x24:
    case 0x3c:
    case 0x72:
    case 0x89:
    case 0x8a:
    case 0x9a:
    case 0x9b:
    case 0x9f:
    case 0xa0:
    case 0xa4: return true;
    default: return effects_on && id >= 0 && id <= kMostEffectId;
  }
}

std::int32_t LaunchGameActorEffect(GameActor& actor, std::int32_t id, const StateContext& context) {
  if (id == kMarksField0xa8) actor.field_0xa8 = 1;
  if (context.launched == nullptr) return 0;
  context.launched->push_back(id);
  if (!LaunchesEffect(id, context.effects_on)) return 0;
  return static_cast<std::int32_t>(context.launched->size());
}

bool LandGameActorHit(GameActor& actor, std::int32_t message, const QueuedHit& hit,
                      const StateContext& context, std::uint32_t now) {
  if (!IsLanding(message)) return false;
  Land(actor, message, hit, context, now,
       [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
  return true;
}

bool LandGameActorHit(PlayerActor& player, std::int32_t message, const QueuedHit& hit,
                      const StateContext& context, std::uint32_t now) {
  if (!IsLanding(message)) return false;
  Land(player, message, hit, context, now,
       [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
  return true;
}

void RestartGameActorAttack(GameActor& actor, const StateContext& context, std::uint32_t now) {
  Restart(actor, context, now,
          [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

void RestartGameActorAttack(PlayerActor& player, const StateContext& context, std::uint32_t now) {
  Restart(player, context, now,
          [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
}

void DeliverGameActorHits(GameActor& actor, const StateContext& context, std::uint32_t now) {
  Deliver(actor, context, now);
}

void DeliverGameActorHits(PlayerActor& player, const StateContext& context, std::uint32_t now) {
  Deliver(player, context, now);
}

}  // namespace ragexe::world
