# Moves and states

An actor is told to move by a message, walks the path it finds ([walk.md](walk.md)), and
stands when it gets there. The message is `CGameActor::SendMsg` (`00504460`, `vf02`), and the
state changes are `CGameActor::SetState` (`005034e0`, `vf10`). The port is
`src/world/actor_state`, for the move and for every state. What starts an attack, and the hits
it queues on its target, are in [attack.md](attack.md); what they do when they land, state 4
among it, in [hits.md](hits.md).

## The move

**Message 0** carries an order: when (the move time), the start cell, where in it in sixteenths
of a cell, and the goal cell. `SendMsg` (`00505dd3`):
1. keeps the move time at +0x18c;
2. calls `FindMovePath` from the start to the goal at the actor's speed (+0xc8), into its path
   ([path.md](path.md#the-callers));
3. **found:** keeps the goal at +0xc0 and +0xc4, walks (`SetState(1)`) and sets the step left
   (+0x194) to 0;
4. **not found,** or the start is the goal: stands (`SetState(0)`).

**Message 0x13** (`005062b3`) moves the actor from where it stands. It carries the goal cell and
a speed:
1. the move time (+0x18c) is the server's time;
2. the start is the cell and the sixteenths under the actor's position (`CGameMode::GetCellPoint`,
   below);
3. a speed of 0 becomes 2, or 0x37 with +0xd8 set;
4. `FindMovePath` from there, then as message 0: the goal kept, walking and the step left 0, or
   standing;
5. **found,** with no speed given, the actor is pushed (+0x190: it neither turns nor steps its
   frames, [walk.md](walk.md#the-actor)); with one, pushed is cleared and +0x82 set.

`CPc` keeps `CGameActor`'s message 0x13, which calls the player's `SetState`.

## The states

`SetState` (`005034e0`, `vf10`), with the new state:
1. **Nothing** in state 3, or while +0xd0 is set; nor for state 7 in state 6.
2. **Leaving state 4,** the actor is put on its target (+0x228, +0x22c) and set on the ground.
3. **The state's own work** (below).
4. **The direction:** the action (+0x24) becomes the base action (+0x20) plus `GetDirection`
   of the facing ([walk.md](walk.md#the-direction)).
5. **The state** at +0x58 and its start, `timeGetTime`, at +0x70.

**Standing (0):**
- the action 0, played in a loop (`vf07`, below);
- held on its frame (+0x81, finished), but for the ids `IsSpecialActorIdOnKoreaService`
  (`004d8e10`) names: on the Korean service (`g_serviceType` 0), 0x2c8c7d to 0x2c8c80 and
  0x2c8c82;
- `vf11` (`00503110`): the game mode hears message 0xc with the actor's id (+0x1b4) and the
  cell it stands on (`CGameMode::GetAttrCell`).

**Walking (1):**
- the action 8 in a loop, unless +0xd8 is set;
- `vf12` (`00503160`): the game mode hears message 0xd with the actor's id.

**The states played once** (a byte of `SetState`'s jump table at `005037f0` for each state to
13; past 13 straight to the end). Each action is played once (play mode 1) through `vf07`, its
second argument again never read:

| State | Its work |
|---|---|
| 5 (`00503787`) | +0xcc and +0x82 cleared, then the action 0x18 |
| 11 (`005037a2`) | the action 0x20 |
| 12 (`005037b1`) | the action 0x28, then `vf11`, the cell to the game mode |
| 4 (`005035e3`) | message 0x57 to the actor +0x1a8 names, 0x54 and 0x53 to itself, then the action 0x30 and `vf11` |
| 13 (`0050355d`) | the action 0x58 or 0x50 for jobs 0 to 0x15 (below), otherwise 0x10, then +0x82 set |
| 6, 8, 10 and past 13 (`005037c5`) | none: only the direction, the state and its time |

State 4's 0x57 and 0x54 end the effects the actor and its target launched
(`CAbleToMakeEffect::SendMsg`, [effects.md](effects.md#the-rest)), of which the port has none;
0x53 queues the cast's gauge (+0x1cc, [windows.md](windows.md)) for removal
(`UIWindowMgr::QueueRemoval`). State 13's
jobs are `ClientState` `005aa610`'s (0 to 0x15), and its action
[AttacksWithSecondAction](attack.md#a-players-attack)'s with the job, +0x1bc and `vf31`
(`004eb1b0`, -1; `CPc`'s `004eb090`, the weapon +0x2a0).

**Attacking (2, 7 and 9)** (`0050367d`; 7 at `00503642`, which first does nothing in state 6):
1. the action 0x50 (0x58 for 9), played once;
2. **the turn** toward the actor +0x1a8 names: the player (`CWorld`+0x3c) when that is the
   player's id (`ClientState`+0xcdc), otherwise the actor `CWorld::GetActorById` finds, if it
   does (`CRenderObject::TurnTowards`);
3. **job 0x505** shoots an arrow at it (`00504000`: `CGameMode::AddArrowEffect` from the
   actor's position at speed 28);
4. **+0x1ac,** the frame the attack lands on, is `vf33` (`00503070`): the first frame of the
   action whose event is `atk` (`CActRes::GetEventName`), or with none the frame count less 2,
   but 0 for one frame (the count is at least 1, `CActRes::GetFrameCount`);
5. `vf11`, the cell to the game mode.

Dying (3) is [below](#dying).

## Each update

`CGameActor::ProcessState` (`005038c0`, `vf09`) begins with the hold and the fade in every
state ([walk.md](walk.md#an-update)), then does the state's work (its jump table is at
`00503ee8`, to state 17), and ends with the update's times (+0x19c, +0x1a0) and, if the actor
moved, its height. Standing and walking are below and in [walk.md](walk.md#walking).

| State | Its work |
|---|---|
| 2, 9 (`00503cd1`) | `vf08`, the frame by the time; the action finished (+0x81), `SetState(0)` |
| 5, 7 (`00503d70`) | +0xcc cleared, then as 12 |
| 12 (`00503d76`) | `vf08`; the action finished, `SetState(0)` |
| 4 (`00503d98`) | +0xcc cleared, `vf08`, then the slide (below) |
| 8 (`00503cec`) | +0xcc cleared, `vf08`, then the turn toward the target (below) |
| 10 (`00503ed5`) | +0xcc cleared |
| 16, 17 (`00503ec9`) | `vf08` |
| 13 (`00503939`) | the action again with +0xcc clear; at the cast's end (+0x1d8) standing and 0x53 ([windows.md](windows.md#state-13-casting)); `vf08` |
| 6, 11, 14, 15 and past 17 (`00503c96`) | none |

**The slide (state 4):**
1. The time left is 200 ms past the state's start (+0x70), less `timeGetTime`, unsigned. The
   time gone is `timeGetTime` less the last update's (+0x19c), signed and at least 0.
2. While some time is left, each of x and z becomes its own plus the way to the target (+0x228,
   +0x22c) times the time gone over the time left. Both times are loaded whole as integers, so
   the product and the quotient each round once to single precision. The actor has moved.
3. Once the action is finished, the actor is put on the target and stands (`SetState(0)`,
   which puts it there again).

**The turn toward the target (state 8):** with +0x1a8 set, `__ftol` of the milliseconds since
the state's start times 1/24 (`005e9000`, as the frame step counts them) is a tick. When a
tick's remainder by 34 is 0, `CWorld::GetActorById` looks the actor up. If it is found,
`CRenderObject::TurnTowards` (`005005b0`) sets the facing (+0x38) toward its x and z (+4, +0xc),
as `GetAngleBetween` would. The action (+0x24) isn't recomputed there; the next `vf08` takes the
new facing.

**Casting (13)** (`00503939`) plays `SetState`'s action again while +0xcc is clear, and ends at
the cast's end (+0x1d8, which message 0x52 sets): [windows.md](windows.md#state-13-casting).
State 3's is [below](#dying).

## Dying

An actor dies through **message 0x1c** (`00505266`). `CGameMode::HandlePacket_0080` sends it
(`005292ae`) to the actor the packet names, and to the world's player (`005292ee`) when that is
the player's own id.
1. **A monster's id** (`GetJob` 1001 to 0x57f) becomes 100000 (`0x186a0`) less it, wrapping.
2. **Something queued:** the list at +0x204 (its size at +0x20c) holds what message 0x1b
   queued. When it isn't empty and `GetJob` is 0x16 or past, +0x210 is set and nothing more
   happens. When that list has played, the actor sends itself **message 0xc** (`00505300`),
   which sets state 3 unless the actor is in it already.
3. **Otherwise** it is `SetState(3)`, through `vf10`.

**`SetState(3)`** (`005036f3`), after `SetState`'s guard (nothing in state 3 or with +0xd0):
1. +0x82 cleared;
2. **with the body state (+0x218) 2, frozen:** the actor sends itself 0x3b and 0x3e, which end
   its ice and break it ([effects.md](effects.md));
3. +0x80 set (the body's depth from the other slope, [actor.md](../render/actor.md)), +0x81
   and +0x82 cleared;
4. the action 0x40 played once (`vf07`);
5. the direction, the state and its time, as every state ends.

`CPc::SetState` leaves 3 to `CGameActor`'s.

**Each update in state 3** (`00503e66`), after the hold and the fade:
1. +0x82 and +0xcc cleared, then `vf08`, the frame by the time;
2. once the action is finished (+0x81) with nothing queued (+0x20c 0), the game mode hears
   message 0xd with the id, on every update;
3. then, for a `GetJob` past 0x16, `LeaveCorpse` (`vf34`, `00503f30`) leaves the corpse, and
   the update returns 0 without its times, as a fade's end does.

**The corpse** is `CGameMode::AddCorpse` (`00510030`): a `CCorpse` in the world's list at the
actor's position, with the names of the body's act (+0x9c) and sprite (+0x98, `GetActName` and
`GetSpriteName`, 0 without one), the actor's base action as its action, and its frame and
facing.

A player's update in state 3 (`CPc::ProcessState`) clears +0x82 and runs `vf08`, and stays.

## Players

`CPc` (and `CPlayer`, which calls it) overrides `SetState` (`00543020`) around `CGameActor`'s,
with the same guard and the move onto state 4's target (without the height, which `CGameActor`'s
then sets):
- **Standing:** `CGameActor`'s, then the frame comes back from +0x294. After states 2, 4 and 8,
  or with +0x224 set, the action becomes 32, which reads as the ready stance by the action
  numbering, in a loop and not held. `CGameMode`+0x2a0 is cleared.
- **Walking:** +0xcc is cleared, and +0x82 too unless +0x218 is 1 or 2. Message 0x53 goes to
  the player itself: `CGameActor::SendMsg` queues the window at +0x1cc for removal
  (`UIWindowMgr::QueueRemoval`, `004a90c0`) and clears it. +0x294 is cleared, then
  `CGameActor`'s.

- **6** (`0054308f`): `vf11`, the cell to the game mode; the action 0x10 played once, finished
  (+0x81) at once; the frame from +0x294.
- **8** (`0054306f`): `vf11`; the action 0x60 played once, finished at once.
- **2 and 9** (`005431f3`, `005432bb`), an attack: as walking, +0xcc and +0x82 cleared and
  message 0x53. Then `CGameActor`'s 9 or 2 by `ClientState` `0059a440` with the job, +0x1bc
  and the weapon (+0x2a0), and +0x1ac `0059a3e0`'s frame ([attack.md](attack.md#a-players-attack)).
  An arrow (`AddArrowEffect` from the player at +0x1a8, at +0x44 × +0x1ac) for state 2 with a
  bow (the weapon's kind 0xb, an id past 0x17 by `MapRangedIdToKind`), for 9 with the weapon 0xd
  or 0xe as it is; +0x1ac then gains 8 (`005e972c`) / +0x44. The state stays the one asked for,
  2 or 9, whichever `CGameActor`'s set.
- **7** (`0054313e`), hit, nothing in state 6: the action 0x20 for jobs 0x13, 0x14 and 0xe, and
  0xf with +0x1bc 0, otherwise 0x60, played once; +0x1ac 6.
- **3, 4, 5 and past 9** (`005433bf`) are `CGameActor`'s, called with the state, and end there.

All but those end as `CGameActor`'s does: the direction of the action, which may now be 32's,
the state and its time. Message 0 goes to `CGameActor::SendMsg`, which calls the player's
`SetState`.

**Each update** (`CPc::ProcessState`, `00542eb0`) has the same hold and fade, then for states 2
to 8 its own table (`00542ff8`). Its own states return 1 without the times:
- **2** (`00542f96`) and **7** (`00542f8d`): `vf08`; the action finished, `SetState(0)`, the
  player's.
- **3** (`00542fc8`): +0x82 cleared, then `vf08`. A dying player stays.
- **6** (`00542fb8`): `vf08`.
- **8** (`00542f13`): `vf08`, then the turn toward the target as `CGameActor`'s, keeping +0xcc.
- **4, 5 and the rest** (`00542fe9`) are `CGameActor::ProcessState`, whose `SetState(0)` is the
  player's.

**An action** (`CGameActor::vf07`, `00502ff0`) is `CRenderObject::SetAction` (`00500de0`),
then pushed (+0x190) cleared and +0x1ac −1. `SetAction`:
- the delay (+0x44) is the action's in the `.act` (`CActRes::GetActionDelay`, `00421990`: 4
  past its delays, the index unsigned), at least 1 (a NaN too, by `fcom`), times +0x50;
- the base action and the action (+0x20, +0x24) are the action, the frame (+0x28) 0;
- the play mode (+0x54) is set and finished (+0x81) cleared;
- +0x4c and +0x50 go back to 1.

Its second argument (4 for standing, 6 for walking) is never read.

**The cell and sixteenths under a point** (`CGameMode::GetCellPoint`, `00511c30`): the cell as
`GetAttrCell` finds it, and in it ⌊(the width / 2 + x / the cell size − the cell) × 16
(`005e2728`)⌋, then C's remainder by 16; the same for z. Each sum and product rounds to single
precision, and the floors and `__ftol` go through doubles. Left of 0 the cell is negative and
the sixteenths count from its own left.

**The cell under a point** (`CGameMode::GetAttrCell`, `00511d50`): floor(x / the cell size +
the width / 2) and the same for z with the height. The halves are C's (toward zero), the sum
single precision, and the floor and `__ftol` through doubles.

## Standing, each update

`CGameActor::ProcessState` (`005038c0`) for state 0 (`005039f3`), after the hold and the fade
([walk.md](walk.md#an-update)):
- **An action played once (+0x54 1) and finished** (+0x81) stands the actor again
  (`SetState(0)`: `CGameActor`'s, or `CPc`'s for a player).
- **Otherwise the player's pet may talk:** a monster (jobs 1000 to 0x57f) whose +0x1f4 is 20
  and whose id is the pet's (`ClientState`+0x7d0). Once +0x1d4 is set and `timeGetTime` is
  more than 3,000 ms past it:
  - `rand() % 100` below 25 sends the game mode message 0xa1 twice. The first carries
    `ClientState` `005aa3e0`'s line (with +0x7fc, 4 and the first level). The second carries
    the first value of the 7-dword record of the table at `ClientState`+0x824, by levels
    `005a9f40` and `005a9fa0` (+0x804, +0x808), at 5 × the first plus the second;
  - +0x1d4 is cleared, whether it talked or not.
- **Then** `vf08`, the frame by the time ([walk.md](walk.md#the-direction)), and the update's
  times. A standing actor doesn't move, so its height isn't read.

`vf08` runs only here: walking steps the frame by the distance (`UpdateWalkFrame`), and drawing
steps nothing.

## Arriving

The walk's last update ([walk.md](walk.md#walking)) calls `SetState(0)`. The actor stands where
the walk put it, at the goal's centre.

## In the port

`src/world/actor_state.{h,cpp}`, in `ragexe_world`:
- `MoveGameActor`, message 0, with a `MoveOrder`, and `MoveGameActorTo`, message 0x13;
- `SetGameActorState` for every state, and `SetGameActorAction`. The arrows shot are kept in
  the `StateContext`'s list (`Arrow`) when there is one;
- `AttrCellAt`, `CellPointAt` (`GetCellPoint`) and `IsSpecialActorIdOnKoreaService`;
- `ModeMessage`: what the game mode hears, kept in the `StateContext`'s list when there is one,
  with the windows queued for removal and `CGameMode`+0x2a0;
- `PlayerActor`, `CPc`'s fields, with `SetPlayerState` and a `MoveGameActor` that calls it;
- `KillGameActor` (0x1c) and `KillGameActorAfterQueue` (0xc), for actors and players. Dying
  updates return `StateResult::kGone` where they leave a corpse, kept in the `StateContext`'s
  list (`Corpse`), with the body's names (`act_name`, `sprite_name`) given;
- `ProcessGameActorState`, an update in any state but 13
  (`StateResult::kNotPorted`, having done nothing): walking as `ProcessWalk`, standing at the
  end, standing with the pet's talk, and the rest above. `ClientState`'s pet tables aren't
  ported: the caller gives the pet and its two lines (`PetTalk`), and the C runtime's `rand`
  (`core::CrtRand`). Nor is the world: the caller gives where the actor +0x1a8 names stands
  (`StateContext::target`) when the world has it, and where the player stands
  (`StateContext::player`).

`render::SetAction` and `render::ActionDelay` (`src/render/actor`) are `CRenderObject`'s;
`RenderObject` gained +0x50, the delay's scale.

`GameActor` gained the goal, the speed, +0xd0, +0xd8, +0x1ac, +0x1a8, the target of state 4,
+0x1bc, the hits queued (+0x204, [attack.md](attack.md)) and +0x210 for dying; `PlayerActor`
the weapon (+0x2a0). `render::TurnTowards` and `render::SpanTicks` (the 1/24 ms ticks) are shared with the
effects and the frame step.

**`ragview`** walks its actor, a player with `CPc`'s states: a left click on the cell under the
mouse sends it message 0 from the cell and sixteenths it stands in (`CellPointAt`), as a server
would, with `timeGetTime` for the server's time. Each frame walks it (`ProcessGameActorState`)
until it stands, and steps a standing actor's frame.

`ragres find-paths` takes moves: `G`, the actor's fields, the order, then the message's and each
update's times; `P`, the same for a player; `T` and `U`, message 0x13 for both; `N` and `Y`, a
standing actor and player, with `srand`'s seed, the pet and its lines; `E` and `Q`, an actor
and a player given a state or left in theirs, then updated; `D` and `H`, an actor and a player
dying, then updated; `V` and `J`, an actor and a player acting on a target or given states 2,
4, 7, 9 and 13 ([attack.md](attack.md#checks)).

## Checks

`tools/walk_oracle.py` also moves actors on every map's `.gat` (287): message 0 through the
original's `SendMsg`, then `ProcessState` update by update. `SetState` runs, and the game mode's
messages are recorded. Each move starts from a point in a walkable cell, toward a cell near it or
anywhere. The state before, the speed, +0xd0, +0xd8, the service type, the `.act`'s delays and
how many it has, the delay's scale, +0x4c, +0x81, the step left, the target of state 4, the id
and a walk's other fields vary. After the message
and each update, every field the move, the walk and the states write, the path, and the
messages are hashed, and equal `ragres find-paths`:
- **11,480 moves** and 165,490 updates;
- **the moves:** 4,090 arrived, 982 still walking at their last update, 224 faded, and 6,184
  never walked (the start the goal, no path, state 3 or +0xd0).
- **11,480 player moves** (`P`, a `CPc` with its +0x218, +0x294, the window, +0x224 and +0xcc,
  a player's job, states 2 and 8 before too) and 146,134 updates, `CGameMode`+0x2a0 and the
  windows queued hashed too: 4,037 arrived, 952 still walking, 192 faded and 6,299 never
  walked.
- **11,480 moves from where the actor stands** (`T` and, for players, `U`: message 0x13 at
  speeds of 0 too) and 127,516 updates: 4,359 arrived, 805 still walking, 186 faded and 6,130
  never walked.
- **11,480 standing actors and players** (`N` and `Y`: `ProcessState` update by update, often
  a pet whose talk is due, with `srand`'s seed and the pet's lines given; actions played once,
  finished or not) and 2,232,775 updates, +0x1d4 hashed too: 866 faded, the rest standing.
- **11,480 actors and players given states** (`E` and `Q`: `vf10`, `CGameActor::SetState` or
  `CPc::SetState`, from assorted states, or left in theirs; then `ProcessState` update by
  update) and 1,891,384 updates, +0xcc hashed too:
  - the states before and asked for are 0, 2 to 18, 0x7fffffff and −1, now and then blocked by
    state 3 or +0xd0, or 7 asked in state 6;
  - +0x1a8 named or not, the world finding it (`CWorld::GetActorById`, answered by the oracle)
    or not; the state's start near the first update;
  - 1,219 changed state (stood), 766 faded, 9,495 stayed.

- **11,480 deaths of actors and 11,480 of players** (`D` and `H`: `SetState(3)`, 0xc or 0x1c,
  then `ProcessState` update by update) and 3,774,271 updates, +0x1b4, +0x210, +0x80, +0xcc, the
  effects and the corpses (`CGameMode::AddCorpse`, recorded) hashed too:
  - from states that block it or not, frozen or not, something queued or not, monsters,
    players and NPCs, a sprite or none;
  - actors: 2,975 left a corpse, 753 faded, 231 changed state (dying later) and 7,521 still
    dying; players: 832 faded, 74 changed and 10,574 stayed.

`tools/state_vectors.py` writes `tests/unit/state_vectors.inc`:
- **moves** on `tools/walk_vectors.py`'s three maps, with short walks;
- **`GetAttrCell`** at points on and either side of the cells' edges, both sides of 0 and
  beyond the map.

`tools/player_state_vectors.py` writes `tests/unit/player_state_vectors.inc`, player moves on
the same maps, and `tools/move_to_vectors.py` writes `tests/unit/move_to_vectors.inc`: moves from
where the actor stands on them, and `GetCellPoint` at the cells' and the sixteenths' edges. `tools/stand_vectors.py` writes `tests/unit/stand_vectors.inc`: standing actors on them, then
a pet that talks, one that doesn't, and an action played once and finished.
`tools/enter_vectors.py` writes `tests/unit/enter_vectors.inc`: actors and players given states
on them, then state 8 on its 34th tick, a tick either side and on tick 0, its target not found
and none named, state 4's slide with 200 ms, 1 ms and none left, played once and finished (with
+0xd0 too, so that only the slide puts it on the target), and a way times the time gone just
past a tie of single precision; 7 asked in state 6, 5, 11 and 12 played to the end, and 2 and 7
left in and played to the end. `tools/death_vectors.py` writes `tests/unit/death_vectors.inc`:
deaths on them, then each way of dying from the states that block it or not, monsters' ids by
the job's edges and the ids', something queued by the job's edge and +0x210, the frozen body's
ice in its slot or not, the update to the action's end and past it by the job's edge for the
corpse, with a sprite or none, held through the updates, +0xcc before, and players, who stay.
`tests/unit/walk_test.cpp` replays them. States 2, 4, 7, 9 and 13 are checked with the attacks
([attack.md](attack.md#checks)).

**Not ported:** the other messages but 3 to 6 ([arrival.md](arrival.md)), 0xb and 0x1b
([attack.md](attack.md)), those that make and end effects ([effects.md](effects.md)) and those of
the windows ([windows.md](windows.md)); and `CPlayer::SetState`'s own work before `CPc`'s.
