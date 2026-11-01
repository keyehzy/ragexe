# The walk

An actor walks the path `PathFinder` found for it ([path.md](path.md)) one update at a time.
`CGameActor::ProcessState` (`005038c0`, `vf09`) is the update. `CPc::ProcessState`
(`00542eb0`) takes over a few states of its own and passes the walk (state 1) to it;
`CPlayer::ProcessState` jumps to `CPc`'s. The port is `src/world/actor_walk`.

## The actor

The `CGameActor` fields the walk reads and writes, beyond the sprite's
([actor.md](../render/actor.md)):

| Offset | Field |
|-------:|-------|
| +0x58 | the state, 1 walking |
| +0x7c | the colour; the fade writes its alpha |
| +0x82 | held, which the hold clears |
| +0x170 | the `MovePath` |
| +0x190 | pushed: set by message 0x13 when it gives no speed; neither turns nor steps the frames |
| +0x194 | the step left, −1 on the last (`MovePath::FindSegment`) |
| +0x198 | the distance walked, in world units |
| +0x19c | `timeGetTime` at the last update |
| +0x1a0 | the server's time at the last update (`ClientState::GetServerTime`) |
| +0x1b8 | the job |
| +0x1d0 | held until `timeGetTime` passes it, or 0 |
| +0x21c | bit 5 mounts jobs 7 and 14 |
| +0x234 | fading out until `timeGetTime` passes it, or 0 |

## An update

Every state:
1. **The hold.** When +0x1d0 is set and `timeGetTime` is past it, +0x82 and +0x1d0 are
   cleared.
2. **The fade** (`UpdateFade`, `00503830`). When +0x234 is set and `timeGetTime` is past it,
   the update returns 0 and nothing else happens. Otherwise the alpha (`SetColorChannels`,
   `00500720`) becomes half the milliseconds left, at most 255.
3. **The state's work.** Below for the walk.
4. **The times.** +0x19c becomes `timeGetTime` and +0x1a0 the server's time.
5. **The height** (`C3dAttr::GetHeight`), when the work moved the actor.

It returns 1.

## Walking

From where the actor stands, (x₀, z₀), at the server's time t:
1. **The step it heads for:** `FindSegment` at t gives the step's time T and cell. +0x194
   becomes the step left. The cell's centre (`CGameMode::GetCellCentre`) is the target.
2. **The move.** r = T − +0x1a0 and e = t − +0x1a0, as signed, each at least 0. When both
   are above 0, the actor moves the fraction e / r of the way to the target, each axis
   computed as (target − x₀) × e / r + x₀ in single precision. Between updates, the time
   left to the step shrinks, so the actor reaches the centre at T.
3. **The facing**, unless pushed. When x or z changed, the facing (+0x38) becomes the angle
   from the old point to the new (`CRenderObject::GetAngleBetween`). An unordered compare
   counts as unchanged. Then the action (+0x24) becomes the base action (+0x20) plus the
   direction (`UpdateDirectionalIndex`, `00503050`, below).
4. **The distance** (`AddTravelledDistance`, `00503190`): +0x198 grows by
   `sqrt(dx² + dz²)`, whether pushed or not.
5. **The frame** (`UpdateWalkFrame`, `005031d0`, `vf32`), unless pushed: ⌊+0x198 × 1.48
   (`005e9664`) / the delay (+0x44)⌋ modulo the action's frame count. The feet keep pace
   with the ground whatever the speed.
6. **The end.** When the step left is −1 and t has reached T (unsigned), the actor is put
   on the target and `SetState(0)` makes it stand. Then pushed and +0x82 are cleared.

The update moves the actor only in steps 2 and 6.

## The direction

`CGameActor::GetDirection` (`00502b00`, `vf04`) gives an actor's direction from its facing,
where `CGameObject::vf04` gives a plain object's
([actor.md](../render/actor.md#the-direction-and-the-frame)).
It is the same turn d = −(the camera's yaw) − facing, wrapped into [0, 360). Then:
- **Rounded** for a player's standing, walking and sitting (jobs below 45, base actions 0, 8
  and 16) and an NPC's standing (jobs 46 to 999, base action 0). Below 22.5 (`005e9530`) or
  above 337.5 (`005e9528`) it is 0. Otherwise it is ⌊(d + 22.5) × 1/45 (`005e9520`, a
  double)⌋.
- **Otherwise truncated:** ⌊d × 1/45⌋ (`005e9518`, a float), as `CGameObject::vf04`.

The job is `GetJob` (`00503490`): +0x1b8, but 7 is 13 and 14 is 21 when bit 5 of +0x21c is
set (`IsBit5Set`, `0059d3d0`). Both stay below 45, so the remap never changes a direction.
Job 45, the warp portal, is never rounded.

`CRenderObject::vf08`, which steps an animation's frames, calls slot 4 through the vtable, so
an actor's frames take this direction too.

## In the port

`src/world/actor_walk.{h,cpp}`, in `ragexe_world`:
- `GameActor`: the fields above, with the sprite's `RenderObject` and the `MovePath`;
- `GameActorJob` and `GameActorDirection`;
- `ProcessWalk`, the update of a walking actor, at a given `timeGetTime` and server's time.
  It returns `kFaded`, `kWalking` or `kArrived`. At `kArrived`, standing (`SetState(0)`,
  `vf10`) is the caller's. The original stands first, but standing touches none of the
  fields the update writes after it.

`CRenderObject::GetAngleBetween` is `render::AngleBetween` (`src/render/actor`), which the
effects use too.

`ragres find-paths` takes walks: `W`, the actor's fields, then each update's server time and
`timeGetTime`.

## Checks

`tools/walk_oracle.py` loads each map's `.gat` into the original's `g_PathFinder`, as
`tools/path_oracle.py` does. Its script finds paths and walks each with the original's
`ProcessState`, on an actor in state 1 with `CGameActor`'s vtable. `SetState` is recorded,
not run. Each walk starts from a point in the start cell (at times away from it) and runs
update by update until the actor arrives or its fade ends. Varied:
- **The updates:** the server's time steps by nothing, a few milliseconds, a frame, seconds,
  and at times back. `timeGetTime` runs alongside with an offset.
- **The actor:** the facing, the camera's yaw, the delay, the distance already walked, the
  base action, the job (players, the portal, NPCs, monsters, the mounted 7 and 14), pushed,
  the actions' frames, the hold, the fade and the colour.

After each update, the result and every field the update writes are hashed. The hash and the
walk's end equal `ragres find-paths`:
- **All 287 maps**, 11,480 walks and 510,630 updates, in 1 min 48 s;
- **the walks:** 6,677 arrived, 1,674 faded, 2,943 still walking at their last update and
  186 of a path too short to walk.

`tools/walk_vectors.py` writes `tests/unit/walk_vectors.inc`: an open field, walls, and hills
whose corners' heights follow a formula, each with the oracle's mix of short walks. Then
walks that each hang on one value: a turn of exactly 22.5°, `timeGetTime` just at the fade's
end, and the server's time just at the path's end. `tests/unit/walk_test.cpp` replays them.

**Not ported:**
- the other states of `ProcessState` and `CPc::ProcessState` (standing, sitting, attacking,
  dying and the rest), and `SetState`, which changes state;
- the messages that start a walk (`vf02` 0, 5, 6 and 0x13), which come with the actors'
  messages;
- `CGameActor::GetDirection` in `CRenderObject::vf08` for actors. The port's
  `UpdateActorFrame` takes `CGameObject::vf04`'s direction.
