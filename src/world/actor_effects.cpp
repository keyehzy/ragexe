#include "world/actor_effects.h"

#include <algorithm>

#include "world/actor_arrival.h"

namespace ragexe::world {

namespace {

constexpr std::int32_t kFollow = 0x32;  // the effect follows the actor that sends it
constexpr std::int32_t kEnd = 0x35;
constexpr std::int32_t kGone = 0x30;  // from an effect that has gone
constexpr std::int32_t kEmotion = 0x15;
constexpr std::int32_t kEmotionInSlot = 0x22;
constexpr std::int32_t kMsgSprite = 0x59;   // AddMsgSpriteEffect with the message's value
constexpr std::int32_t kNumberKind = 0x16;  // a number that replaces the last of its kind
constexpr std::int32_t kDropped = 0x18;     // RemoveMsgEffectsOfType16's, from the actor
constexpr std::int32_t kKind = 0x16;        // a number's id and type (+0x10c)
constexpr std::int32_t kDigit = 0x40;       // its position, digit and offset
constexpr float kNumberKindScale = 0.5f;    // 0x3f000000, +0x3c of a number of type 0x16
constexpr std::int32_t kMostShown = 999999;
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

// CGameMode::AddSpriteEffectByType, AddEmotionEffect or AddMsgSpriteEffect at the actor's
// position, then the effect in the actor's list and told to follow it.
std::int32_t Make(GameActor& actor, const StateContext& context, EffectMaker maker,
                  std::int32_t type) {
  ActorEffects& effects = *context.effects;
  effects.made.push_back({maker, type, actor.id, &actor.object.position});
  const auto number = static_cast<std::int32_t>(effects.made.size());
  actor.effects.push_back(number);
  effects.messages.push_back({.effect = number, .id = kFollow, .sender = &actor});
  return number;
}

// ClearSlotHolding (00504090): the first slot holding the effect, in kClearOrder's order,
// emptied.
void ClearSlotHolding(GameActor& actor, std::int32_t number) {
  for (const EffectSlot slot : kClearOrder) {
    if (actor.effect_slots[slot] == number) {
      actor.effect_slots[slot] = 0;
      return;
    }
  }
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
         message == kGone || message == kMsgSprite;
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
    const std::int32_t number = Make(actor, context, EffectMaker::kSprite, s.type);
    if (s.slot == kFalconSlot)
      context.effects->made[static_cast<std::size_t>(number - 1)].field_0x44 = kFalconField44;
    actor.effect_slots[s.slot] = number;
    if (s.slot == kSleepSlot) set_state(kSleepState);
    return true;
  }
  for (const auto& [start, type] : kUnslotted) {
    if (message == start) {
      Make(actor, context, EffectMaker::kSprite, type);
      return true;
    }
  }
  switch (message) {
    case 0x33:
      if ((actor.effect_state & (kHiding | kCloaked)) == 0 && actor.effect_slots[kCartSlot] == 0)
        actor.effect_slots[kCartSlot] =
            Make(actor, context, EffectMaker::kSprite, CartType(actor.effect_state));
      break;
    case 0x34: End(actor, kCartSlot, context); break;
    case kEmotion:
      if (arg != kEmotionInSlot) {
        Make(actor, context, EffectMaker::kEmotion, arg);
      } else if (actor.effect_slots[kEmotionSlot] == 0) {
        actor.effect_slots[kEmotionSlot] = Make(actor, context, EffectMaker::kEmotion, arg);
      }
      break;
    case kMsgSprite:
      if (context.msg_sprites) {
        const std::int32_t number = Make(actor, context, EffectMaker::kMsgSprite, 0);
        context.effects->made[static_cast<std::size_t>(number - 1)].color =
            static_cast<std::uint32_t>(arg);
      }
      break;
    default: {
      const auto at = std::find(actor.effects.begin(), actor.effects.end(), sender);
      if (at == actor.effects.end()) break;
      ClearSlotHolding(actor, sender);
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

void ShowGameActorNumber(GameActor& actor, std::int32_t value, std::uint32_t color,
                         std::int32_t type, const StateContext& context) {
  if (context.guild_war_map && !SeesHiddenActors(context.service_type, context.own_id)) return;
  if (context.effects == nullptr) return;
  ActorEffects& effects = *context.effects;
  if (value > kMostShown) value = kMostShown;
  if (value < 0) value = 0;
  std::int32_t count = 1;
  for (std::int32_t edge = 10; count < 6 && value >= edge; edge *= 10) ++count;
  if (type == kNumberKind) {
    // RemoveMsgEffectsOfType16 (0050c100): each number of the kind dropped, sent 0x18 and deleted.
    for (auto at = actor.effects.begin(); at != actor.effects.end();) {
      const std::int32_t number = *at;
      const ActorEffect& e = effects.made[static_cast<std::size_t>(number - 1)];
      if (e.maker != EffectMaker::kNumber || e.type != kNumberKind) {
        ++at;
        continue;
      }
      ClearSlotHolding(actor, number);
      effects.messages.push_back({.effect = number, .id = kDropped, .sender = &actor});
      at = actor.effects.erase(at);
      effects.deleted.push_back(number);
    }
  }
  std::int32_t digits = value;
  for (std::int32_t k = 0; k < count; ++k) {
    ActorEffect digit{EffectMaker::kNumber, type, actor.id, &actor.object.position};
    digit.color = color;
    if (type == kNumberKind) digit.field_0x3c = kNumberKindScale;
    effects.made.push_back(digit);
    const auto number = static_cast<std::int32_t>(effects.made.size());
    effects.messages.push_back({.effect = number, .id = kKind, .a = actor.id, .b = type});
    effects.messages.push_back({.effect = number,
                                .id = kDigit,
                                .b = digits % 10,
                                .c = 4 * (count - 1) - 8 * k,
                                .at = &actor.object.position});
    effects.messages.push_back({.effect = number, .id = kFollow, .sender = &actor});
    actor.effects.push_back(number);
    digits /= 10;
  }
}

}  // namespace ragexe::world
