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

The other states, their actions and side effects are not ported. `CPc` overrides
`SetState` (`00543020`) around `CGameActor`'s.

**An action** (`CGameActor::vf07`, `00502ff0`) is `CRenderObject::SetAction` (`00500de0`),
then pushed (+0x190) cleared and +0x1ac −1. `SetAction`:
- the delay (+0x44) is the action's in the `.act` (`CActRes::GetActionDelay`, `00421990`: 4
  past its delays, the index unsigned), at least 1 (a NaN too, by `fcom`), times +0x50;
- the base action and the action (+0x20, +0x24) are the action, the frame (+0x28) 0;
- the play mode (+0x54) is set and finished (+0x81) cleared;
- +0x4c and +0x50 go back to 1.

Its second argument (4 for standing, 6 for walking) is never read.

**The cell under a point** (`CGameMode::GetAttrCell`, `00511d50`): floor(x / the cell size +
the width / 2) and the same for z with the height. The halves are C's (toward zero), the sum
single precision, and the floor and `__ftol` through doubles.

## Arriving

The walk's last update ([walk.md](walk.md#walking)) calls `SetState(0)`. The actor stands where
the walk put it, at the goal's centre.

## In the port

`src/world/actor_state.{h,cpp}`, in `ragexe_world`:
- `MoveGameActor`, message 0, with a `MoveOrder`;
- `SetGameActorState` for 0 and 1 (others return false having done nothing), and
  `SetGameActorAction`;
- `AttrCellAt` and `IsSpecialActorIdOnKoreaService`;
- `ModeMessage`: what the game mode hears, kept in the `StateContext`'s list when there is one.

`render::SetAction` and `render::ActionDelay` (`src/render/actor`) are `CRenderObject`'s;
`RenderObject` gained +0x50, the delay's scale.

`GameActor` gained the goal, the speed, +0xd0, +0xd8, +0x1ac and the target of state 4.

**`ragview`** walks its actor: a left click on the cell under the mouse sends it message 0 from
where it stands, with `timeGetTime` for the server's time. Each frame walks it until it stands.

`ragres find-paths` takes moves: `G`, the actor's fields, the order, then the message's and each
update's times.

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

`tools/state_vectors.py` writes `tests/unit/state_vectors.inc`:
- **moves** on `tools/walk_vectors.py`'s three maps, with short walks;
- **`GetAttrCell`** at points on and either side of the cells' edges, both sides of 0 and
  beyond the map.

`tests/unit/walk_test.cpp` replays them.

**Not ported:** the other messages and states (sitting, attacking, dying and the rest), and
`CPc`'s and `CPlayer`'s `SetState`.
