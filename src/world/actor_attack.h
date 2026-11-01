#pragma once

#include <cstdint>

#include "res/act.h"
#include "world/actor_state.h"
#include "world/actor_walk.h"

namespace ragexe::world {

// The record message 0xb carries (CGameActor::ProcessActionOnTarget, 0050ad80): an action of
// the actor's on a target (docs/world/attack.md).
struct TargetAction {
  std::int32_t damage = 0;         // +0, shared out among the hits
  std::int32_t second_damage = 0;  // +4, a hit after them, for jobs below 1000
  std::int32_t type = 0;           // +8: 0, 4 and 8 to 11 attack; 1, 2 and 3 set states 5,
                                   // 6 and 0
  std::int32_t target_id = 0;      // +0xc
  std::int32_t count = 0;          // +0x14, the hits of types 8 and 9, 1 to 36
  std::int32_t cell_x = 0;         // +0x18 and +0x1c, to the hits
  std::int32_t cell_y = 0;
  std::int32_t motion = 0;         // +0x20, which vf35 makes the actor's +0x50
  std::int32_t target_motion = 0;  // +0x24, over 288 to the hits
};

// CGameActor::SendMsg's message 0xb (0050591c, ProcessActionOnTarget): +0x1a8 takes the target's
// id; types 1, 2 and 3 set states 5, 6 and 0. An attack (0, 4 and 8 to 11):
//  1. with +0xcc set, launches effect 0x83 and clears it;
//  2. vf35 sets +0x50 from the motion, at most 432 (but for a player's bow) over 432;
//  3. on another's id, attacks (SetState(2), CPc's for a player);
//  4. turns toward the target, or away from the player without one;
//  5. sends the target message 0x1b with each hit, the first after +0x1ac frames of +0x44 ticks
//     (and 0xc0, 0x390 or 0x198 ms more for jobs 0x3f8, 0x505, 0x506 and 0x507), each next of
//     types 8 and 9 200 ms later; the second damage, for jobs below 1000, 350 ms after the
//     first, the others then 100 ms after it.
// The target is the player (context.player_actor) for own_id, otherwise context.target_actor;
// the world's skills (CWorld::GetSkillById) are not ported. False for an unknown type (5 to 7
// and past 11), which the original logs and does nothing more for.
bool ActOnTarget(GameActor& actor, const TargetAction& action, const StateContext& context,
                 std::uint32_t now);
bool ActOnTarget(PlayerActor& player, const TargetAction& action, const StateContext& context,
                 std::uint32_t now);

// CGameActor::SendMsg's message 0x1b (00505829): the hit joins the end of the actor's queue;
// with a total, as message 0x6f with the total at +0x20.
void QueueGameActorHit(GameActor& actor, const QueuedHit& hit, std::int32_t total);

// CGameActor::vf33 (00503070): the first frame of the action whose event is "atk", or, with
// none, the frame count (at least 1) less 2, but 0 for one frame.
std::int32_t AttackFrame(const res::Act& act, std::int32_t action);

// ClientState::MapRangedIdToKind (005aad00): the kind of weapon an item's id names, by ranges
// of 50 from 1100 to 1999 (0 for 0, -1 for the rest).
std::int32_t WeaponKind(std::int32_t id);

// ClientState 0059a440: whether a player of the job (below 0x16) attacks with the weapon (a
// kind, or past 0x16 an id, of which its low 16 bits) by its second action (0x58, state 9),
// with +0x1bc.
bool AttacksWithSecondAction(std::int32_t job, std::int32_t field_0x1bc, std::int32_t weapon);

// ClientState 0059a3e0: the frame a player's attack lands on: 6, or 5.85 or 5.75 for jobs 5 and
// 6 by the first action, 5.85 for job 0 with +0x1bc 1 by the second, 3 for job 0xc with weapon
// kinds 0x10 to 0x16 by the second.
float PlayerAttackFrame(std::int32_t job, std::int32_t field_0x1bc, std::int32_t weapon,
                        bool second);

}  // namespace ragexe::world
