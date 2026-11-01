#include "world/actor_attack.h"

#include <bit>

#include "core/x87.h"
#include "render/actor.h"
#include "world/actor_hit.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kMostHits = 36;                                 // 0x24
constexpr std::int32_t kMotionLimit = 432;                             // 00614858
constexpr float kPerMotion = std::bit_cast<float>(0x3b17b426u);        // 005e9754, 1/432
constexpr float kPerTargetMotion = std::bit_cast<float>(0x3b638e39u);  // 005e9758, 1/288
constexpr float kMsPerTick = 24.0f;                                    // 005e91a4
constexpr std::int32_t kHitStep = 200;                                 // 00614710, ms between hits
constexpr std::int32_t kOneOfMany = 0x6f;        // the message of a hit with a total
constexpr std::int32_t kFirstMonsterJob = 1000;  // the second damage is a player's
constexpr std::int32_t kBowKind = 0xb;           // a player's motion past 432 stays
constexpr std::int32_t kStand = 0;
constexpr std::int32_t kAttack = 2;
constexpr std::int32_t kEffectWithCc = 0x83;  // launched for an attack with +0xcc set
constexpr std::int32_t kFollow = 0x32;        // to that effect, from the actor

// GetJob's later hits for a few monsters: ms added to the first's time.
std::int32_t LateHit(std::int32_t job) {
  switch (job) {
    case 0x3f8: return 0xc0;
    case 0x505: return 0x390;
    case 0x506:
    case 0x507: return 0x198;
    default: return 0;
  }
}

// The message each hit is sent as, by the attack's type, and +0x18 (-1 but for type 10).
std::int32_t HitMessage(std::int32_t type, const GameActor& actor) {
  switch (type) {
    case 4:
    case 9: return 0x27;
    case 10: return 0x5d;
    case 11:
      if (actor.field_0xcc == 0) return 0x5e;
      return 0x1a;
    default: return 0x1a;
  }
}

// The hit with a total as message 0x1b leaves it: 0x6f, and the total at +0x20.
void Retag(QueuedHit& hit, std::int32_t total) {
  if (total == 0) return;
  hit.message = kOneOfMany;
  hit.field_0x20 = total;
}

// vf35: CGameActor's (0050ad50) keeps the motion to at most 432; CPc's (00543470) not with a
// bow.
void SetMotion(GameActor& actor, std::int32_t motion) {
  if (motion > kMotionLimit) motion = kMotionLimit;
  actor.object.delay_scale = core::RoundedProduct(kPerMotion, static_cast<double>(motion));
}

void SetMotion(PlayerActor& player, std::int32_t motion) {
  std::int32_t kind = player.weapon;
  if (kind > 0x17) kind = WeaponKind(kind);
  if (kind != kBowKind && motion > kMotionLimit) motion = kMotionLimit;
  player.object.delay_scale = core::RoundedProduct(kPerMotion, static_cast<double>(motion));
}

// a + b wrapping, as the x86 adds.
std::int32_t Add(std::int32_t a, std::int32_t b) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

template <typename Actor, typename SetState>
bool Act(Actor& actor, const TargetAction& action, const StateContext& context, std::uint32_t now,
         SetState set_state) {
  render::RenderObject& o = actor.object;
  actor.target_id = action.target_id;
  GameActor* target =
      action.target_id == context.own_id ? context.player_actor : context.target_actor;
  float x = 0.0f;
  float z = 0.0f;
  if (target != nullptr) {
    x = target->object.position[0];
    z = target->object.position[2];
  } else if (context.player != nullptr) {
    // Away from the player, as far again.
    const float dx = (*context.player)[0] - o.position[0];
    const float dz = (*context.player)[2] - o.position[2];
    x = o.position[0] - dx;
    z = o.position[2] - dz;
  }
  std::int32_t count = action.count;
  if (count < 1 || count > kMostHits) count = 1;
  if (action.type != 8 && action.type != 9) count = 1;
  const std::int32_t damage = action.damage / count;
  switch (action.type) {
    case 0:
    case 4:
    case 8:
    case 9:
    case 10:
    case 11: break;
    case 1: set_state(5); return true;
    case 2: set_state(6); return true;
    case 3: set_state(kStand); return true;
    default: return false;
  }
  if (actor.field_0xcc != 0) {
    const std::int32_t effect = LaunchGameActorEffect(actor, kEffectWithCc, context);
    if (effect != 0 && context.launched_messages != nullptr)
      context.launched_messages->push_back({.effect = effect, .id = kFollow, .sender = &actor});
    actor.field_0xcc = 0;
  }
  SetMotion(actor, action.motion);
  if (action.target_id != actor.id) set_state(kAttack);
  o.facing = render::TurnTowards(o.position, x, z);
  const float frames = actor.field_0x1ac * o.delay;
  std::int32_t delay = core::Ftol(static_cast<double>(frames * kMsPerTick));
  delay = Add(delay, LateHit(GameActorJob(actor)));
  const std::int32_t message = HitMessage(action.type, actor);
  if (target == nullptr) return true;
  QueuedHit hit{message,
                damage,
                now + static_cast<std::uint32_t>(delay),
                actor.id,
                action.cell_x,
                action.cell_y,
                action.type == 10 ? 1 : -1,
                action.target_id == actor.id ? -2 : 0,
                1,
                core::RoundedProduct(kPerTargetMotion, static_cast<double>(action.target_motion))};
  // Message 0x1b, which retags the record it is sent as well as the queued copy.
  const auto send = [&](std::int32_t total) {
    QueueGameActorHit(*target, hit, total);
    Retag(hit, total);
  };
  std::int32_t total = damage;
  send(count == 1 ? 0 : total);
  if (action.second_damage != 0 && GameActorJob(actor) < kFirstMonsterJob) {
    for (std::int32_t k = 1; k < count; ++k) {
      hit.time += kHitStep / 2;
      hit.damage = damage;
      total = Add(total, damage);
      send(total);
      hit.time -= kHitStep / 2;
    }
    hit.message = message;
    hit.damage = action.second_damage;
    total = Add(total, action.second_damage);
    hit.time += (kHitStep / 2) + kHitStep + (kHitStep / 4);
    send(count == 1 ? 0 : total);
    return true;
  }
  for (std::int32_t k = 1; k < count; ++k) {
    hit.time += kHitStep;
    hit.damage = damage;
    total = Add(total, damage);
    send(total);
  }
  return true;
}

}  // namespace

bool ActOnTarget(GameActor& actor, const TargetAction& action, const StateContext& context,
                 std::uint32_t now) {
  return Act(actor, action, context, now,
             [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

bool ActOnTarget(PlayerActor& player, const TargetAction& action, const StateContext& context,
                 std::uint32_t now) {
  return Act(player, action, context, now,
             [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
}

void QueueGameActorHit(GameActor& actor, const QueuedHit& hit, std::int32_t total) {
  QueuedHit queued = hit;
  Retag(queued, total);
  actor.hits.push_back(queued);
}

std::int32_t AttackFrame(const res::Act& act, std::int32_t action) {
  std::int32_t count = 1;
  const std::vector<res::ActFrame>* frames = nullptr;
  if (action >= 0 && static_cast<std::size_t>(action) < act.actions.size()) {
    frames = &act.actions[static_cast<std::size_t>(action)];
    if (frames->size() > 1) count = static_cast<std::int32_t>(frames->size());
  }
  for (std::int32_t k = 0; k < count; ++k) {
    // CActRes::GetFrame gives an empty frame, without an event, past the action's.
    if (frames == nullptr || static_cast<std::size_t>(k) >= frames->size()) continue;
    const std::int32_t event = (*frames)[static_cast<std::size_t>(k)].event;
    if (event < 0 || static_cast<std::size_t>(event) >= act.events.size()) continue;
    if (act.events[static_cast<std::size_t>(event)] == "atk") return k;
  }
  return count == 1 ? 0 : count - 2;
}

std::int32_t WeaponKind(std::int32_t id) {
  if (id == 0) return 0;
  struct Range {
    std::int32_t from;
    std::int32_t to;
    std::int32_t kind;
  };
  static constexpr Range kRanges[] = {{1100, 1150, 2},    {1150, 1200, 3},   {1200, 1250, 1},
                                      {1250, 1300, 0x10}, {1300, 1350, 6},   {1350, 1400, 7},
                                      {1400, 1450, 4},    {1450, 1500, 5},   {1500, 1550, 8},
                                      {1550, 1600, 0xf},  {1600, 1700, 0xa}, {1700, 1750, 0xb},
                                      {1800, 1900, 0xc},  {1900, 1950, 0xd}, {1950, 2000, 0xe}};
  for (const Range& r : kRanges) {
    if (id >= r.from && id < r.to) return r.kind;
  }
  return -1;
}

bool AttacksWithSecondAction(std::int32_t job, std::int32_t field_0x1bc, std::int32_t weapon) {
  std::int32_t kind = weapon;
  if (kind > 0x16)
    kind = WeaponKind(static_cast<std::int32_t>(static_cast<std::uint32_t>(kind) & 0xffffu));
  const auto either = [kind](std::int32_t a, std::int32_t b) { return kind == a || kind == b; };
  switch (job) {
    case 0:
      if (field_0x1bc == 0) return kind == 1;
      if (field_0x1bc == 1) return (kind >= 2 && kind <= 3) || (kind >= 6 && kind <= 10);
      return false;
    case 2:
    case 5: return kind == 1;
    case 1:
    case 7:
    case 13:
    case 14:
    case 21: return kind >= 4 && kind <= 5;
    case 3: return kind != 0xb;
    case 6:
    case 11:
    case 17:
    case 19:
    case 20: return kind == 0xb;
    case 8: return kind == 0xf;
    case 9: return (kind == 1 && field_0x1bc == 1) || (kind == 10 && field_0x1bc == 0);
    case 10:
    case 18: return kind == 2 || (kind >= 6 && kind <= 8);
    case 12: return kind >= 0x10 && kind <= 0x16;
    case 15: return either(0, 0xc);
    case 16: return either(0xa, 0xf);
    default: return false;
  }
}

float PlayerAttackFrame(std::int32_t job, std::int32_t field_0x1bc, std::int32_t weapon,
                        bool second) {
  constexpr float kFrame = 6.0f;       // 005e9720
  constexpr float kFrameJob5 = 5.85f;  // 005e9fc0
  constexpr float kFrameJob6 = 5.75f;  // 005e9fbc
  constexpr float kFrameJob12 = 3.0f;  // 005e29d8
  if (!second) {
    if (job == 5) return kFrameJob5;
    if (job == 6) return kFrameJob6;
    return kFrame;
  }
  if (job == 0) return field_0x1bc == 1 ? kFrameJob5 : kFrame;
  if (job == 12 && weapon >= 0x10 && weapon <= 0x16) return kFrameJob12;
  return kFrame;
}

}  // namespace ragexe::world
