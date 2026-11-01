#include "world/actor_effects.h"

#include <algorithm>

namespace ragexe::world {

namespace {

constexpr std::int32_t kFollow = 0x32;  // the effect follows the actor that sends it
constexpr std::int32_t kEnd = 0x35;
constexpr std::int32_t kGone = 0x30;  // from an effect that has gone
constexpr std::int32_t kEmotion = 0x15;
constexpr std::int32_t kEmotionInSlot = 0x22;
constexpr std::int32_t kSleepState = 10;  // 0x45's SetState
constexpr float kFalconField44 = 6.0f;    // 0x40c00000, which 0x65 writes to its effect's +0x44
constexpr std::uint32_t kHiding = 1u << 1;
constexpr std::uint32_t kCloaked = 1u << 2;

// A message that makes an effect into a slot, the one that ends it, and the sprite's type.
struct Slotted {
  std::int32_t start;
  std::int32_t end;
  EffectSlot slot;
  std::int32_t type;
};
constexpr Slotted kSlotted[] = {
    {0x65, 0x66, kFalconSlot, 0xe},     {0x38, 0x39, kEmotionSpriteSlot, 2},
    {0x3a, 0x3b, kFrozenSlot, 3},       {0x43, 0x44, kCurseSlot, 6},
    {0x45, 0x46, kSleepSlot, 7},        {0x47, 0x48, kStunSlot, 8},
    {0x49, 0x4a, kHealthBit2Slot, 9},   {0x4b, 0x4c, kHealthBit5Slot, 10},
    {0x4e, 0x4f, kHealthBit6Slot, 0xb},
};
constexpr std::int32_t kFrozenEndToo = 0x6b;

// The messages that make an effect without a slot, and its type.
constexpr std::int32_t kUnslotted[][2] = {
    {0x3e, 4}, {0x3f, 5}, {0x5c, 0xc}, {0x5f, 0xd}, {0x7d, 0x14}};

// ClearSlotHolding (00504090) looks at the slots in this order.
constexpr EffectSlot kClearOrder[] = {
    kCartSlot, kFalconSlot,     kEmotionSpriteSlot, kFrozenSlot,     kCurseSlot,  kSleepSlot,
    kStunSlot, kHealthBit5Slot, kHealthBit6Slot,    kHealthBit2Slot, kEmotionSlot};

// 0x33's sprite by the whole effect state, the carts: 손수레 (0060908c) for 8 and anything
// else, 손수레1 to 손수레4 for 0x80 to 0x400.
std::int32_t CartType(std::uint32_t effect_state) {
  switch (effect_state) {
    case 0x80: return 0x10;
    case 0x100: return 0x11;
    case 0x200: return 0x12;
    case 0x400: return 0x13;
    default: return 1;
  }
}

// CGameMode::AddSpriteEffectByType or AddEmotionEffect at the actor's position, then the
// effect in the actor's list and told to follow it.
std::int32_t Make(GameActor& actor, const StateContext& context, bool emotion, std::int32_t type) {
  ActorEffects& effects = *context.effects;
  effects.made.push_back({emotion, type, actor.id, &actor.object.position});
  const auto number = static_cast<std::int32_t>(effects.made.size());
  actor.effects.push_back(number);
  effects.messages.push_back({number, kFollow});
  return number;
}

void End(GameActor& actor, EffectSlot slot, const StateContext& context) {
  std::int32_t& number = actor.effect_slots[slot];
  if (number == 0) return;
  context.effects->messages.push_back({number, kEnd});
  number = 0;
}

bool IsEffectMessage(std::int32_t message) {
  for (const Slotted& s : kSlotted) {
    if (message == s.start || message == s.end) return true;
  }
  for (const auto& [start, type] : kUnslotted) {
    if (message == start) return true;
  }
  return message == 0x33 || message == 0x34 || message == kFrozenEndToo || message == kEmotion ||
         message == kGone;
}

template <class Actor, class SetState>
bool Send(Actor& actor, std::int32_t message, std::int32_t sender, std::int32_t arg,
          const StateContext& context, SetState set_state) {
  if (!IsEffectMessage(message)) return false;
  if (context.effects == nullptr) return true;
  for (const Slotted& s : kSlotted) {
    if (message == s.end || (s.slot == kFrozenSlot && message == kFrozenEndToo)) {
      End(actor, s.slot, context);
      return true;
    }
    if (message != s.start) continue;
    if (actor.effect_slots[s.slot] != 0) return true;
    const std::int32_t number = Make(actor, context, false, s.type);
    if (s.slot == kFalconSlot)
      context.effects->made[static_cast<std::size_t>(number - 1)].field_0x44 = kFalconField44;
    actor.effect_slots[s.slot] = number;
    if (s.slot == kSleepSlot) set_state(kSleepState);
    return true;
  }
  for (const auto& [start, type] : kUnslotted) {
    if (message == start) {
      Make(actor, context, false, type);
      return true;
    }
  }
  switch (message) {
    case 0x33:
      if ((actor.effect_state & (kHiding | kCloaked)) == 0 && actor.effect_slots[kCartSlot] == 0)
        actor.effect_slots[kCartSlot] = Make(actor, context, false, CartType(actor.effect_state));
      break;
    case 0x34: End(actor, kCartSlot, context); break;
    case kEmotion:
      if (arg != kEmotionInSlot) {
        Make(actor, context, true, arg);
      } else if (actor.effect_slots[kEmotionSlot] == 0) {
        actor.effect_slots[kEmotionSlot] = Make(actor, context, true, arg);
      }
      break;
    default: {
      const auto at = std::find(actor.effects.begin(), actor.effects.end(), sender);
      if (at == actor.effects.end()) break;
      for (const EffectSlot slot : kClearOrder) {
        if (actor.effect_slots[slot] == sender) {
          actor.effect_slots[slot] = 0;
          break;
        }
      }
      actor.effects.erase(at);
      break;
    }
  }
  return true;
}

}  // namespace

bool SendGameActorEffectMessage(GameActor& actor, std::int32_t message, std::int32_t sender,
                                std::int32_t arg, const StateContext& context, std::uint32_t now) {
  return Send(actor, message, sender, arg, context,
              [&](std::int32_t state) { SetGameActorState(actor, state, context, now); });
}

bool SendGameActorEffectMessage(PlayerActor& player, std::int32_t message, std::int32_t sender,
                                std::int32_t arg, const StateContext& context, std::uint32_t now) {
  return Send(player, message, sender, arg, context,
              [&](std::int32_t state) { SetPlayerState(player, state, context, now); });
}

}  // namespace ragexe::world
