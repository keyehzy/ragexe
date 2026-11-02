# The update

An actor's `vf01` is its update. `CGameActor`'s is pure; each class has its own, and three are
ported: **`CNpc::Update`** (`005404d0`) and **`CPc::Update`** (`00542e40`) here, and the own
player's, `CPlayer::Update` (`0054b770`), in [pursuit.md](pursuit.md). Each runs pieces in an order of its own and returns what the state's
update (`vf09`, `ProcessState`, [states.md](states.md)) returns. The port is `src/world/actor_update`.

| Piece | `CNpc` | `CPc` |
|-------|:------:|:-----:|
| the fade (`UpdateFade`, `00503830`): over, the update returns 0 at once | 1 | |
| the key colour's end | 2 | 5 |
| the hold of stone and ice (`00507860`) | 3 | |
| Sight's effect (`00507740`) | 4 | 1 |
| the hits due (`DeliverDueHits`, [hits.md](hits.md)) | 5 | 2 |
| the colour brightened | 6 | 6 |
| the windows (`UpdateWindows`, [windows.md](windows.md)) | 7 | 3 |
| the gauge (`00543400`) | | 4 |
| the state (`vf09`) | 8 | 7 |
| the frames' sounds (`00500940`) | 9 | 8 |

## `CNpc`'s way by job

`CNpc::Update` runs the fade and the key colour's end, then picks its way by the job (`GetJob`,
`00503490`) from a jump table (`005405a8` bytes into `00540598`) for jobs 0x2d to 0x8b:
- **0x2d and 0x8b:** nothing more; the update returns 1.
- **0x68:** the effect it repeats (`CNpc::RepeatEffectPeriodically`, `00540390`): once
  `timeGetTime` is past +0x290 plus +0x1ec seconds (+0x1ec × 1000, wrapping, unsigned), +0x290
  takes `timeGetTime` and the effect +0xc8 is launched (`LaunchEffect`, [hits.md](hits.md#launcheffect)).
  The update returns 1.
- **0x6f:** the hits due, the windows and the state.
- **any other**, the rest of 0x2e to 0x8a among them: the hold, Sight's effect, the hits due,
  the colour brightened while it is not white (+0x7c not 0xffffffff), the windows and the state.

Then the frames' sounds, unless +0x1f4 is 20.

## The pieces

**The key colour's end.** A hit keys the actor red (+0x83 set, +0x6c the time,
[hits.md](hits.md)). Once `timeGetTime` is past +0x6c + 100 (unsigned, the sum wrapping) +0x68
is back to 0xffff0000 and +0x83 cleared.

**The hold** (`CGameActor::HoldWhileStoneOrFrozen`, `00507860`): +0x82 set while the body word
(+0x218, [arrival.md](arrival.md#the-status)) is 1, stone, or 2, frozen. `CPc`'s update does
not hold; `CPlayer`'s does.

**Sight's effect** (`CGameActor::RepeatSightEffect`, `00507740`), +0x230 counting updates:
1. With no effect word (+0x21c 0, `IsZero`), +0x230 is 36000 (0x8ca0) and nothing more.
2. With bit 0 (Sight, `IsBit0Set`): the effect is 0x21 for jobs 4, 8 and 15 (`GetJob`, so a
   mounted job 7 or 14 is not), 0x16 for the rest. Once +0x230 reaches that effect's duration
   (`g_effectDurations`, `00617148`: 200 and 270 steps), it is cleared and the effect launched;
   the effect made is sent 0x2c with (`timeGetTime` >> 4) % 10 + 1. The new one starts as the
   last one ends.
3. +0x230 counts the update.

**The colour brightened** (`CGameActor::BrightenColor`, `00500660`, with 8 for each): unless a
status colours the actor (+0x87), each byte of +0x7c (blue, green, red, alpha) plus its amount,
no less than 0 and no more than 255.

**The gauge** (`CPc::MoveGauge`, `00543400`): the gauge over a `CPc` (+0x2dc, `UIPcGage`, which
`CPc::SendMsg`'s 0x22 makes, [look.md](look.md)) moved (`vf18`) to 0x1e left of the point on screen (+0x90) and the screen's
height (`g_pSceneRenderer` +0x28) × 12 / 480, whole, × +0x48 below it (+0x94): `fild`, `fmul`,
`fiadd`, each rounded to single precision, then `__ftol`.

**The frames' sounds** (`CRenderObject::PlayFrameSounds`, `00500940`): +0x2c and +0x30 keep the
base action (+0x20) and frame (+0x28) whose sounds were last played.
1. Nothing is played while both are the same.
2. A new base action starts from frame 0 (+0x30 cleared), which is not played.
3. **The frame ahead:** each frame after +0x30 to the frame.
4. **The frame behind,** the action having gone round: +0x30 no more than the frame count
   less 1 (`CActRes::GetFrameCount`, `00421a80`: 1 for fewer than two frames), each frame after
   it to the last, then each from 0 to the frame.
5. Then +0x2c and +0x30 take the base action and the frame.

A frame is played when its event (`CActRes::GetFrame`, `00421a60`, of the action +0x24) is not
-1 and its name (`CActRes::GetEventName`, `00420f20`, "" past the act's) has ".wav" in it,
case and all: `PlayWave` with the name, the actor's position less the world's player's
(`CWorld`+0x3c, read without a check), y 0, 250, 40 and 1.0. A frame past the action's, or of
an action the act lacks, is the empty `g_ActFrame` (`00628eb8`), whose event is 0: the act's
first event plays there if its name is a sound's.

Both updates play the frames' sounds after a state's update that returned 0 too.

## In the port

`src/world/actor_update.{h,cpp}`, in `ragexe_world`: `UpdateNpc` (`CNpc::Update`) and
`UpdatePlayer` (`CPc::Update`), with `HoldWhileStoneOrFrozen`, `RepeatSightEffect`,
`BrightenColor`, `PlayFrameSounds`, `RepeatNpcEffect` and `MovePlayerGauge`. `UpdateFade` is
`UpdateGameActorFade` (`src/world/actor_walk`). `GameActor` gained +0x230 and +0x290,
`PlayerActor` its gauge (+0x2dc) and `RenderObject` +0x2c and +0x30. The gauge is one of the
port's `ActorWindows`, as the cast's is.

`ragres find-paths` takes `u` and `p`: an actor and a player updated whole.

## Checks

`tools/walk_oracle.py` updates actors and players on every map's `.gat` (287).
- **The oracle:** `CNpc::Update` runs on the actor and `CPc::Update` on the player. The effects
  `LaunchEffect` makes are stand-ins whose `vf02` is recorded, the windows those of the casts
  ([windows.md](windows.md#checks)), the act's frames and events stand-ins with four names
  ("foot.wav", "a.wav.b", "a.wa", "X.WAV") and 256 actions, as ragres's act has; the world
  always has its player. No hits are queued.
- **What varies:** the job (each of `CNpc`'s ways, the acolytes, mounted or not); the state;
  the effect word, +0x230 about each period, effects on; the key colour and +0x6c about the
  100 ms; the colour and +0x87; the body word; job 0x68's period and effect; +0x1f4; the
  frames' events, +0x2c, +0x30, the frame and +0x24; the world's player; the gauge, the point on
  screen, +0x48 and the screen's height; a cast's gauge and its times.
- **What is hashed:** after each update its result, the fields the states write, the game
  mode's messages, +0x230, +0x290, +0x68, +0x6c, +0x83, +0x2c, +0x30, the cast's times, +0xcc,
  the gauges, for a player `CGameMode`+0x2a0 and +0x294, and what was played, launched and sent
  to what was launched, what the windows were told and which were queued for removal.

The hashes equal `ragres find-paths`:
- **11,480 actors and 11,480 players** (`u`, `p`) over 4,455,093 updates;
- **actors:** 986 faded and 10,494 updated to the end; **players:** 906 and 10,574.

The full run, 271,050 queries on all 287 maps, keeps the older counts, and the older vector
files regenerate byte-identical.

`tools/update_vectors.py` writes `tests/unit/update_vectors.inc`, which
`tests/unit/walk_test.cpp` replays: the three maps' scripts, then updates that each hang on one
thing (`CNpc`'s way about each edge of its jump table; Sight by job, mount, period, effects on
and the time; the key colour about its 100 ms and its wrap; the colour by channel, white and
tinted; the hold by body word, an NPC's and not a player's; job 0x68's period, its wrap and
effect; the frames' sounds by event, a new action, the frame ahead, behind and past the last,
an action the act lacks; the gauge by the screen's height, +0x48 and the point; a cast's gauge ending in state 8; the
fade).

**Not ported:** the other classes' `vf01`.
