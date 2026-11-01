#pragma once

#include <cstdint>
#include <string_view>

#include "core/crt_rand.h"
#include "world/actor_state.h"
#include "world/actor_walk.h"

namespace ragexe::world {

// ClientState 00599a00: the sound of a player of the job (0 to 0x15) being hit, from the
// table 00599620 fills at +0x16d4; other jobs WeaponHitSound(-1)'s.
std::string_view PlayerHitSound(std::int32_t job, core::CrtRand& rand);

// ClientState 00599df0: the sound of a hit by a weapon of the kind: for -1 one of
// _enemy_hit_normal1.wav to 4 and for 0 one of _hit_fist1.wav to 4, by rand() % 4; kinds 1 to
// 0x16 from the table 00599a40 fills at +0x16e4. The original reads past the table for the
// others; the port gives "".
std::string_view WeaponHitSound(std::int32_t kind, core::CrtRand& rand);

// Whether CAbleToMakeEffect::LaunchEffect (00501e10) makes the effect: its own few (0x23,
// 0x24, 0x3c, 0x72, 0x89, 0x8a, 0x9a, 0x9b, 0x9f, 0xa0 and 0xa4, by the byte table 00502008)
// always, the rest from 0 to 60000 with effects on (00687ea8).
bool LaunchesEffect(std::int32_t id, bool effects_on);

// CAbleToMakeEffect::LaunchEffect (00501e10) on the actor, as the port keeps it: the id in
// context.launched; effect 0x7f sets +0xa8 first. The effect made (LaunchesEffect), its place
// in context.launched from 1, otherwise 0 (also when the launches are not kept).
std::int32_t LaunchGameActorEffect(GameActor& actor, std::int32_t id, const StateContext& context);

// CGameActor::SendMsg's messages 0x1a, 0x27, 0x5d, 0x5e, 0x60, 0x61 and 0x6f (00505326): a hit
// landing on the actor, at timeGetTime `now` (docs/world/hits.md). +0x228 and +0x22c take the
// hit's cell's centre, or the actor's position without one; the attacker is the player
// (context.player_actor) for own_id, otherwise context.target_actor. The damage (0 for 0x5e),
// made positive:
//  - none: 0x5e sends the actor 0x5f; otherwise an attacker of a job from 0x2d sends itself
//    0x59 with -1, unless the actor is the player with status 0x13, which sends itself 0x7d;
//    one below 0x2d 0x59 with 0xffff0000;
//  - some: +0x6c the time and +0x83 set (clear for +0x1c -2, without a sound). The sound of the
//    hit, from the player (context.player): -1's for 0x60 and 0x61, then the actor's job's
//    (PlayerHitSound) for jobs 0 to 0x15, otherwise the attacker's weapon's (vf31, an id past
//    1000 as its kind). Effect +0x18 launched, and +0x1c (-1 for 0x7a, which turns the actor 90
//    degrees), each sent 0x2c, and 0xd8 and 0xd9 the attacker's position (0xe). Then the
//    number: negative, 0x72 and nothing more; 0x5d yellow (0x10) and 0x5c; jobs from 0x2d
//    white (0xe), and 0x6f its total (+0x20) yellow too (0x15 and 0x16); the rest red (0xe).
//    Last, but in states 3 and 2 (jobs from 0x2d only 3) and for 0x27, state 4, the hit's +0x24
//    then +0x4c past 1, otherwise times +0x44 (for jobs from 0x2d, +0x44 itself).
// Either way, with +0x210 set and at most one hit queued (this one), the actor sends itself
// 0xc. A player's states are CPc::SetState's. False for the other messages, which it leaves
// alone.
bool LandGameActorHit(GameActor& actor, std::int32_t message, const QueuedHit& hit,
                      const StateContext& context, std::uint32_t now);
bool LandGameActorHit(PlayerActor& player, std::int32_t message, const QueuedHit& hit,
                      const StateContext& context, std::uint32_t now);

// CGameActor::SendMsg's message 0x6c (0050579a), which skills queue: state 2 (vf10), +0x1ac the
// action's attack frame (vf33), the cell reported (vf11), then the action restarted at frame 4,
// +0x44 1 and the direction added to it; then 0xc as LandGameActorHit's.
void RestartGameActorAttack(GameActor& actor, const StateContext& context, std::uint32_t now);
void RestartGameActorAttack(PlayerActor& player, const StateContext& context, std::uint32_t now);

// CGameActor 00503f80, which the classes' vf01 call before ProcessState: each hit queued
// (+0x204) whose time has come (timeGetTime `now` not below it), in order, sent to the actor
// as its message (LandGameActorHit, 0x6c RestartGameActorAttack) and then taken off the queue.
// Other messages are not ported: they are only taken off.
void DeliverGameActorHits(GameActor& actor, const StateContext& context, std::uint32_t now);
void DeliverGameActorHits(PlayerActor& player, const StateContext& context, std::uint32_t now);

}  // namespace ragexe::world
