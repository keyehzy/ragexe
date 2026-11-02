# Attacks

An actor acts on another, usually attacking it, through **message 0xb** to its
`CGameActor::SendMsg` (`00504460`, `vf02`, case `0050591c`), which calls
`ProcessActionOnTarget` (`0050ad80`). The actor turns to its target, starts its attack (state
2, [states.md](states.md#the-states)) and queues the hits on the target with **message 0x1b**,
each to land at its time. The port is `src/world/actor_attack`.

What a hit does when it lands (messages 0x1a, 0x27, 0x5d, 0x5e and 0x6f, which the target
sends itself from its queue) is in [hits.md](hits.md).

## The record

Message 0xb's first argument is a record of ten words:

| Offset | What |
|-------:|------|
| +0 | the damage, shared out among the hits |
| +4 | a second damage, a hit after them |
| +8 | the type |
| +0xc | the target's id |
| +0x10 | unread |
| +0x14 | how many hits, for types 8 and 9 |
| +0x18, +0x1c | a cell, to the hits |
| +0x20 | the actor's motion, which `vf35` makes its +0x50 |
| +0x24 | the target's motion, over 288 (`005e9758`) to the hits |

## The action

`ProcessActionOnTarget`:
1. **+0x1a8** takes the target's id.
2. **The target:** the player (`CWorld`+0x3c) when the id is the player's
   (`ClientState`+0xcdc), otherwise the actor `CWorld::GetActorById` finds, or the skill
   `GetSkillById` finds. With none, the point the actor turns to is away from the player, as
   far again: x − (the player's x − x), and the same for z.
3. **The hits:** the count, unless it is 1 to 36 (0x24) and the type is 8 or 9, is 1. The
   damage of each is the damage over the count, truncated.
4. **The type** (a jump table at `0050b268`):
   - 1, 2 and 3 set states 5, 6 and 0 (`vf10`) and end there;
   - 0, 4 and 8 to 11 attack (below);
   - the rest log "Unknown Action Type == %d" (`0061485c`) and end there.

An attack:
1. **With +0xcc set,** the actor launches effect 0x83 (`CAbleToMakeEffect::LaunchEffect`,
   [hits.md](hits.md#launcheffect)), tells it to follow (0x32) when it is made, and clears +0xcc.
2. **`vf35`** sets +0x50 to the motion over 432 (`005e9754`), the motion at most 432
   (`00614858`): `CGameActor`'s `0050ad50`; `CPc`'s `00543470` leaves it whole for a bow (the
   weapon's kind 0xb).
3. **On another's id** (not +0x1b4), the actor attacks: `SetState(2)`, through `vf10`.
4. **The turn** toward the target's point (`CRenderObject::TurnTowards`).
5. **The first hit's time:** `timeGetTime` plus `__ftol` of +0x1ac (the frame the attack lands
   on, which `SetState(2)` set) × +0x44 (the frame's ticks) × 24 (`005e91a4`), each product
   rounded to single precision; plus 0xc0 ms for job 0x3f8, 0x390 for 0x505, and 0x198 for
   0x506 and 0x507.
6. **The message** each hit is sent as, by the type (`0050b298`): 0x27 for 4 and 9, 0x5d for
   10, 0x5e for 11 (0x1a with +0xcc set, which it no longer is), 0x1a for the rest.
7. **Without a target** it ends there.

Then each hit goes to the target's `vf02` as **message 0x1b** from the actor, with the hit and
a total. The hit, ten words:

| Offset | What |
|-------:|------|
| +0 | the message |
| +4 | the damage |
| +8 | the time it lands |
| +0xc | the actor's id |
| +0x10, +0x14 | the record's cell |
| +0x18 | −1, or 1 for type 10 |
| +0x1c | 0, or −2 when the target's id is the actor's own |
| +0x20 | 1 |
| +0x24 | the target's motion over 288 (rounded once) |

The first hit's total is 0 for one hit, otherwise its damage. **Message 0x1b** (`00505829`)
adds a copy of the hit to the end of the target's queue (the `std::list` at +0x204, its size
at +0x20c). With a total, it first makes the hit's message 0x6f and its +0x20 the total, and
the hit that the actor goes on sending stays so changed.

The next hits, each with the damage and the total so far (wrapping):
- **with a second damage, for jobs below 1000:** each other hit 100 ms after the first (the
  time is moved 100 on and back again for each); then the second damage, as the type's
  message again, 350 ms after the first, with the total for more than one hit or 0;
- **otherwise** each other hit 200 ms (`00614710`) after the one before.

`CGameMode::GetAttrCell` of the player's x and y, and `GetJob` of the target, are read and not
used; with a target, the world must have a player.

## A player's attack

`CPc::SetState`'s 2 and 9 ([states.md](states.md#players)) take three of `ClientState`'s:
- **The second action** (`0059a440`): whether a job attacks with action 0x58 (state 9) rather
  than 0x50, by the job (a jump table at `0059a56c`, jobs 0 to 0x15), +0x1bc and the weapon's
  kind (the weapon, or past 0x16 the kind of its low 16 bits):

  | Jobs | The second action with |
  |------|---|
  | 0 | kind 1 with +0x1bc 0; kinds 2, 3 and 6 to 10 with +0x1bc 1 |
  | 2, 5 | kind 1 |
  | 1, 7, 13, 14, 21 | kinds 4 and 5 |
  | 3 | any kind but 0xb |
  | 6, 11, 17, 19, 20 | kind 0xb |
  | 8 | kind 0xf |
  | 9 | kind 1 with +0x1bc 1, kind 10 with +0x1bc 0 |
  | 10, 18 | kinds 2 and 6 to 8 |
  | 12 | kinds 0x10 to 0x16 |
  | 15 | kinds 0 and 0xc |
  | 16 | kinds 0xa and 0xf |
  | 4 and the rest | none |

- **The frame the attack lands on** (`0059a3e0`), for +0x1ac: 6 (`005e9720`), but by the first
  action 5.85 (`005e9fc0`) for job 5 and 5.75 (`005e9fbc`) for job 6, and by the second 5.85 for
  job 0 with +0x1bc 1 and 3 (`005e29d8`) for job 0xc with the weapon 0x10 to 0x16.
- **The weapon's kind** (`MapRangedIdToKind`, `005aad00`) of an item's id: 0 for 0; by ranges
  of 50 from 1100: 2, 3, 1, 0x10, 6, 7, 4, 5, 8 and 0xf to 1600; 0xa to 1700; 0xb to 1750; none
  to 1800; 0xc to 1900; 0xd and 0xe to 2000; −1 for the rest.

## In the port

`src/world/actor_attack.{h,cpp}`, in `ragexe_world`:
- `ActOnTarget`, message 0xb, with a `TargetAction`, for actors and players;
- `QueueGameActorHit`, message 0x1b, onto `GameActor::hits` (`QueuedHit`);
- `AttackFrame` (`vf33`), `WeaponKind`, `AttacksWithSecondAction` and `PlayerAttackFrame`.

`StateContext` gained the target and the player as actors (`target_actor`, `player_actor`),
whose queues take the hits, the arrows shot (`Arrow`) and the effects launched, by id, which
the port doesn't make.

`ragres find-paths` takes `V` and `J`: an actor and a player, then by `how` vf10's state,
message 0xb with the record, or 0x1b with it as a hit and a total; then the updates.

## Checks

`tools/walk_oracle.py` also has actors and players act on targets on every map's `.gat` (287):
`SendMsg`'s 0xb or 0x1b, or `vf10`, through the original's, then `ProcessState` update by
update.
- **The oracle:** the target and the player are `CGameActor`s with only their position, their
  launched effects and their queue; `CWorld::GetActorById` finds one of them, the actor itself
  or none; the world has no skills. `LaunchEffect` and `AddArrowEffect` are recorded, and the
  act's frames have events by the query (`CActRes::GetFrame` and `GetEventName` answer).
- **What varies:** every type, the target found, the actor itself, the player by its id or
  none; the counts and damages, the second damage; +0xcc; the motion, the target's; monsters
  with late hits and arrows, players' jobs, +0x1bc and weapons; the events; hits queued before;
  the state before, now and then blocking.
- **What is hashed:** after the message and each update, every field the states write, the
  game mode's messages, +0x1a8, +0xcc, what was logged, launched and shot, and the three
  queues, every word of every hit.

The hashes equal `ragres find-paths`:
- **11,480 actors and 11,480 players** (`V`, `J`): 15,322 messages 0xb, 5,039 states through
  `vf10` and 2,599 hits queued by 0x1b, over 2,364,672 updates;
- **actors:** 4,500 changed state, 781 faded and 6,199 stayed; **players:** 4,803 changed, 586
  faded and 6,091 stayed.

A player's attack with a motion of 0 found that the frame step took a NaN (0 / 0, no time yet
and no delay) as finished, where the original's `fcom` does not
([actor.md](../render/actor.md)); the port now agrees.

`tools/attack_vectors.py` writes `tests/unit/attack_vectors.inc`, which
`tests/unit/walk_test.cpp` replays. It holds the three maps' scripts, then attacks that each
hang on one thing:
- every type, for actors and players;
- the target found, the actor itself, none (away from the player), the player by its id,
  another or the actor itself;
- the hits' counts (−1 to 37) and damages (0, ±7, the 32-bit edges) for types 0, 8 and 9;
- the second damage by the job's edge (1000), with one hit and three, damage 0 or not;
- +0xcc; the motion by 432, players with a bow by kind and by id, and none with an update at
  once, whose 0 / 0 frames play on ([actor.md](../render/actor.md));
- the late hits' jobs and their neighbours; the target's motion by its edges; the cell;
- the act's frames' events, "atk" first, later, among others or not at all, on 0 to 12 frames;
- states 2, 4, 7, 9 and 13 through `vf10` from states that block them or not, the turn to the
  player, the actor found or not; job 0x505's arrows; players' 2 and 9 by job and weapon, 7 by
  job and +0x1bc, and 13 by job;
- a hit queued with a total or none, onto hits queued before; played to the end, standing
  again.

Then `kWeaponKinds`, `MapRangedIdToKind` at each range's edges and either side, and
`kAttackChoices`, `0059a440` and `0059a3e0` for jobs −1 to 0x16 and 0x7fffffff, +0x1bc 0 to 2
and weapons from −1 to 0x18 and ids in each range.

**Not ported:** skills as targets. `CPc::SendMsg` ([look.md](look.md)) leaves 0xb and 0x1b to
`CGameActor`'s.
