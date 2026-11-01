# Moves and states

An actor is told to move by a message, walks the path it finds ([walk.md](walk.md)), and
stands when it gets there. The message is `CGameActor::SendMsg` (`00504460`, `vf02`), and the
state changes are `CGameActor::SetState` (`005034e0`, `vf10`). The port is
`src/world/actor_state`, for the move and for standing and walking.

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
1. **Nothing** in state 3, or while +0xd0 is set.
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

The other states, their actions and side effects are not ported.

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

Both end as `CGameActor`'s does: the direction of the action, which may now be 32's, the state
and its time. Message 0 goes to `CGameActor::SendMsg`, which calls the player's `SetState`.

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
- `SetGameActorState` for 0 and 1 (others return false having done nothing), and
  `SetGameActorAction`;
- `AttrCellAt`, `CellPointAt` (`GetCellPoint`) and `IsSpecialActorIdOnKoreaService`;
- `ModeMessage`: what the game mode hears, kept in the `StateContext`'s list when there is one,
  with the windows queued for removal and `CGameMode`+0x2a0;
- `PlayerActor`, `CPc`'s fields, with `SetPlayerState` and a `MoveGameActor` that calls it;
- `ProcessStand`, a standing actor's update, for actors and players. `ClientState`'s pet tables
  aren't ported: the caller gives the pet and its two lines (`PetTalk`), and the C runtime's
  `rand` (`core::CrtRand`).

`render::SetAction` and `render::ActionDelay` (`src/render/actor`) are `CRenderObject`'s;
`RenderObject` gained +0x50, the delay's scale.

`GameActor` gained the goal, the speed, +0xd0, +0xd8, +0x1ac and the target of state 4.

**`ragview`** walks its actor, a player with `CPc`'s states: a left click on the cell under the
mouse sends it message 0 from the cell and sixteenths it stands in (`CellPointAt`), as a server
would, with `timeGetTime` for the server's time. Each frame walks it until it stands, and steps a
standing actor's frame.

`ragres find-paths` takes moves: `G`, the actor's fields, the order, then the message's and each
update's times; `P`, the same for a player; `T` and `U`, message 0x13 for both; `N` and `Y`, a
standing actor and player, with `srand`'s seed, the pet and its lines.

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

`tools/state_vectors.py` writes `tests/unit/state_vectors.inc`:
- **moves** on `tools/walk_vectors.py`'s three maps, with short walks;
- **`GetAttrCell`** at points on and either side of the cells' edges, both sides of 0 and
  beyond the map.

`tools/player_state_vectors.py` writes `tests/unit/player_state_vectors.inc`, player moves on
the same maps, and `tools/move_to_vectors.py` writes `tests/unit/move_to_vectors.inc`: moves from
where the actor stands on them, and `GetCellPoint` at the cells' and the sixteenths' edges. `tools/stand_vectors.py` writes `tests/unit/stand_vectors.inc`: standing actors on them, then
a pet that talks, one that doesn't, and an action played once and finished. `tests/unit/walk_test.cpp` replays them.

**Not ported:** the other messages and states (sitting, attacking, dying and the rest), and
`CPlayer::SetState`'s own work before `CPc`'s.
