#pragma once

#include <cstddef>
#include <cstdint>

#include "world/actor_state.h"
#include "world/actor_walk.h"

namespace ragexe::world {

// GameActor::effect_slots: which status each slot's effect shows (docs/world/effects.md), by
// the slot's offset from +0x244.
enum EffectSlot : std::size_t {
  kFalconSlot,         // +0x244, bit 4 of the effect state (0x65, 0x66)
  kCartSlot,           // +0x248, its bits 3 and 7 to 10 (0x33, 0x34)
  kEmotionSpriteSlot,  // +0x24c (0x38, 0x39)
  kFrozenSlot,         // +0x250, body state 2 (0x3a, 0x3b and 0x6b)
  kCurseSlot,          // +0x254, bit 1 of the health state (0x43, 0x44)
  kSleepSlot,          // +0x258, body state 4 (0x45, 0x46)
  kStunSlot,           // +0x25c, body state 3 (0x47, 0x48)
  kHealthBit2Slot,     // +0x260, bit 2 of the health state (0x49, 0x4a)
  kHealthBit5Slot,     // +0x264, bit 5 (0x4b, 0x4c)
  kHealthBit6Slot,     // +0x268, bit 6 (0x4e, 0x4f)
  kEmotionSlot,        // +0x26c, emotion 0x22 (0x15)
};

// CGameActor::SendMsg's (00504460, vf02) messages that make and end the actor's effects, with
// `arg` the message's first argument (0x15's emotion) and `sender` the effect, by number, that
// sends it (0x30's: it has gone). Each new effect is told to follow the actor (0x32) and joins
// its list:
//  - 0x33 and 0x65, 0x38, 0x3a, 0x43, 0x45, 0x47, 0x49, 0x4b and 0x4e make one into their slot
//    when it is empty, 0x33 by the effect state and not while hiding or cloaked; 0x45 then
//    sets state 10. Their ends (0x34, 0x66, 0x39, 0x3b and 0x6b, 0x44, 0x46, 0x48, 0x4a, 0x4c,
//    0x4f) tell the slot's effect to end (0x35) and empty the slot.
//  - 0x3e, 0x3f, 0x5c, 0x5f and 0x7d make one without a slot; 0x15 makes the emotion `arg`,
//    0x22 into its slot when it is empty; 0x59, with context.msg_sprites (00687ec0), makes
//    one with msg.spr (CGameMode::AddMsgSpriteEffect), its +0x7c `arg`.
//  - 0x30 takes the sender out of the list and its slot (ClearSlotHolding, 00504090).
// False for the other messages, which it leaves alone. Without context.effects nothing is
// made or ended.
bool SendGameActorEffectMessage(GameActor& actor, std::int32_t message, std::int32_t sender,
                                std::int32_t arg, const StateContext& context, std::uint32_t now);

// The same to a player (CPc::vf02, which leaves these to CGameActor's), whose 0x45 sets the
// state through CPc::SetState.
bool SendGameActorEffectMessage(PlayerActor& player, std::int32_t message, std::int32_t sender,
                                std::int32_t arg, const StateContext& context, std::uint32_t now);

// CGameActor::SendMsg's message 0x58 (0050456b), a number shown over the actor: nothing on a
// guild war's map (context.guild_war_map) unless the player has privilege 7
// (SeesHiddenActors); otherwise ShowNumberEffect (0050beb0). The value, 0 to 999999, is shown
// a digit an effect from the last, each a CMsgEffect with +0x7c the colour, sent its id and
// the type (0x16, which its +0x10c keeps), the actor's position with the digit and its offset
// (4 × (digits - 1) - 8 × its place), and told to follow the actor (0x32), then put in the
// actor's list. Type 0x16 first drops the numbers of that type from the list
// (RemoveMsgEffectsOfType16, 0050c100: each out of its slot, sent 0x18 from the actor and
// deleted) and gives its digits +0x3c 0.5. Without context.effects nothing is made.
void ShowGameActorNumber(GameActor& actor, std::int32_t value, std::uint32_t color,
                         std::int32_t type, const StateContext& context);

}  // namespace ragexe::world
