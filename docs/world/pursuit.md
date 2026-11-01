# The own player's update and pursuit

The world's own player, the one the user moves (`CWorld`+0x3c), is a **`CPlayer`** (vtable
`005e9cf4`), a `CPc` with fields of its own from +0x308. Its update, **`CPlayer::Update`**
(`0054b770`, vf01), is `CPc`'s ([update.md](update.md)) with the hold and two more pieces: the
move it sends ahead of the server (**PreMove**) and the **pursuit** of what the user clicked
(`CPlayer::PursueTarget`, `0054b8e0`). The port is `src/world/player_pursuit` and
`UpdateOwnPlayer` in `src/world/actor_update`.

| Piece | `CPlayer` |
|-------|:---------:|
| the hold of stone and ice (`00507860`) | 1 |
| Sight's effect (`00507740`) | 2 |
| the hits due (`DeliverDueHits`, [hits.md](hits.md)) | 3 |
| the windows (`UpdateWindows`, [windows.md](windows.md)) | 4 |
| the key colour's end | 5 |
| the colour brightened | 6 |
| PreMove | 7 |
| the pursuit (`PursueTarget`, `0054b8e0`) | 8 |
| the state (vf09, `CPc::ProcessState`) | 9 |
| the frames' sounds (`00500940`) | 10 |

Unlike `CPc`'s, it moves no gauge. It returns what the state's update returns.

## The fields

`CPlayer::SendMsg` (`0054c5d0`) sets them; `0054b4a0` starts them at 0, but +0x348 at 1 and
+0x358, +0x35c at -1.

| Offset | What |
|--------|------|
| +0x308, +0x30c | the cell the player last asked to walk to (the goal) |
| +0x310 | `timeGetTime` when it last acted on its target |
| +0x314, +0x31c | message 9's time and flag: a move to send ahead of the server |
| +0x318 | the pursuit: 1 and 5 an actor to attack, 2 an item to pick up, 3 an actor and 4 a cell to cast on, 6 a skill's object; 0 none |
| +0x320 | message 0xa's second word: 2 attacks once |
| +0x324 | set once it acts, until the next order |
| +0x32c, +0x358, +0x35c | a move queued and its cell |
| +0x330 | the id of what is pursued |
| +0x334 | the times it acted on it |
| +0x338 | when it last asked to walk toward an attack's target |
| +0x344, +0x348, +0x34c | the skill, its range in cells and its level |
| +0x350, +0x354 | the cell to cast on |

Message 0xa sets an attack's pursuit: 5 when the target's speed (+0xc8) is more than four
thirds of the player's (`00687404`), 1 otherwise, with +0x338 1200 ms back so the first ask
goes at once. Message 0xd ends the pursuit (+0x318 and +0x324 0) and tells the game mode 0x1c.

## PreMove

With message 9's move set (+0x31c) and `timeGetTime` past +0x314 plus half the ping (unsigned,
the sum wrapping), the player sends itself message 0 with an order: the server's time, the cell
and sixteenths under it (`CGameMode::GetCellPoint`, `00511c30`) and the goal; the original logs
"PreMove". The ping is `ClientState`+0x524, the mean of the last three round trips of packet
0x007f (`ClientState::AddPingSample`, `0058e280`, from `CGameMode::HandlePacket_007F`,
`0052733f`). Message 0 clears +0x31c.

## The pursuit

`PursueTarget` picks its way by +0x318 from a jump table (`0054c524`); beyond 1 to 6 it does
nothing. "In range" is the distance within range × 5 units and 5.5 (`fild`, `fadd` to single
precision; `fcompp`, `test ah, 0x41`: a distance unordered against it is in range). The
distance is `CRenderObject::GetDistanceTo`'s (`00500560`): the root of the squares of x and z
apart, then the root of its square and y apart's, each sum and root rounded to a float
(`SqrtFloat`, `00409570`). For a big model (+0x238 1, which `C3dGrannyGameActor::vf13` sets
for jobs 0x49d and 0x504) it is 20 less, and no less than 0, an unordered one 0. Each way sends
the player messages it handles itself:

| Message | `CPlayer::SendMsg` |
|---------|--------------------|
| 0xd | ends the pursuit |
| 0x10 (x, y) | in states 0, 4 and 6, faces the cell (+0x38, +0x294), and more |
| 0x1d | the game mode's 0x96 with 0 and +0x330 |
| 0x28 | the game mode's 0x96 with 7 and +0x330, unless that actor's +0x1f4 is 20 |
| 0x2a | the game mode's 0x4c (+0x344, +0x330, +0x34c) |
| 0x42 | the game mode's 0x4d with +0x344, the cell and +0x34c; for skill 0x7d a window |
| 0x5b (x, y) | the game mode's 0x97 to the cell, at most once a second |

Asking to walk is message 0x5b, the cell kept as the goal; it is never asked twice in a row for
the same cell.

- **1 and 5, an attack** (`0054b90e`): the target is `CWorld::GetActorById`'s; none, 0xd. A
  dying one (state 3) is left. The player pursues standing, walking or sliding (0, 1, 4), or
  in any state as job 0x10. In range by the attack range (`ClientState`+0x640, which packets
  0x0139 and 0x013a set), or at its goal, it acts: with +0x320 2, 0xd first; then, if it has
  not acted since the order and `timeGetTime` is past +0x310 + 1200, +0x310 takes it, +0x324
  is set, +0x334 counted, and 0x1d (+0x320 2) or 0x28 sent. Otherwise it asks to walk to the
  free cell toward the target's cell centre (`CGameMode::GetCellCentre`), or for 5 where the
  target stands, the attack range away, unless it attacks (state 2), has acted, or 450 ms
  (1200 for 5) have not passed since +0x338, which takes the time.
- **2, an item** (`0054c215`): `CWorld::GetItemById`'s; none, 0xd. Only standing. Within a
  cell of it on both axes (`CGameMode::IsWithinCellRange`, `005118d0`, the differences'
  absolute values by `cdq`, `xor`, `sub`, so wrapping), 0x10 with the item's cell, its id added
  to `CGameMode`+0x198's list (`AddIdToList`, `00511990`) unless there (`ListContainsId`,
  `00511910`) and the game mode told 0x95 with it, then 0xd. Otherwise it asks to walk to the
  item's cell stepped once toward the player (`CGameMode::StepCellToward`, `00511b60`).
- **3, a cast on an actor** (`0054bd9c`): the player itself when +0x330 is its id
  (`ClientState`+0xcdc), otherwise `GetActorById`'s; none, 0xd. It pursues in states 0, 1, 4,
  0x10 and 0x11. In range by +0x348, or at its goal, 0x2a and 0xd. Otherwise, unless attacking,
  the free cell toward where the target stands, +0x348 away, becomes the goal, and the cell
  asked for is it, but for a big model five cells beside it by the angle from the player
  (`CRenderObject::GetAngleBetween`, `0054bef9`); an unordered angle is taken as below 25°:

  | From | 340° (and below 25°) | 25° | 70° | 115° | 160° | 205° | 250° | 295° |
  |-------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
  | x | | +5 | +5 | +5 | | −5 | −5 | −5 |
  | y | +5 | +5 | | −5 | −5 | +5 | | −5 |

  The y of 205° and of 295° are swapped: the others' pattern would give −5 and +5.
- **4, a cast on a cell** (`0054c0e8`): its centre and the player's distance to it at the
  player's height. States 0, 1 and 4. In range or at its goal, 0x42 and 0xd; otherwise, unless
  attacking, the free cell toward it.
- **6, a skill's object** (`0054c355`): `CWorld::GetSkillById`'s; none, 0xd. Its cell's centre,
  as 4. In range or at the goal, an object of jobs 0x8d, 0x8f or 0x98 without a skill
  (+0x344 0) is attacked (0x1d); any other, 0x2a and 0xd. Otherwise the free cell toward it.

**The free cell** (`CGameMode::GetFreeCellToward`, `00511a40`): the cell (`GetAttrCell`) of the
point steps × 5 units from the target toward the player along the unit vector (1 over
`SqrtFloat` of the squares' sum, times each difference, in single precision). When it is off
the map or attribute 1 or 5 (`CGameMode::IsCellBlocked`, `00512230`), the first of its eight
neighbours that is not, in the order (1, 0), (1, 1), (1, −1), (−1, 0), (−1, 1), (−1, −1),
(0, 1), (0, −1), or the last.
`StepCellToward` tries the neighbours of the cell it started from.

**Found on the way:** an attack on a big model works out the aim beside it as the cast does,
then `GetFreeCellToward` overwrites the cell; the aim is lost. The 6's check of the attack
state (2) before asking to walk can't be met: the states that reach it are 0, 1 and 4.

## Its states and reports

`CPlayer::SetState` (`0054c550`, vf10) runs before `CPc`'s ([states.md](states.md#players)):
nothing while dying (state 3); the goal forgotten (-1) for every state but walking; then a
queued move (`CPlayer::SendQueuedMove`, `0054b880`): with +0x32c set and the player attacking,
flinching or falling (states 2, 5 and 6), message 0x5b to +0x358, +0x35c, which become -1, and
+0x32c is cleared.
For state 3 it tells the game mode 5 and launches effect 0x150 where the player stands
(`CGameMode::LaunchEffectAt`, `0050ff10`).

`CPlayer`'s vf11 and vf12 (`ReportCell`, `0054b3d0`, and `ReportWalking`, `0054b3e0`) are
empty: the own player tells the game mode neither the cell it stands on (0xc) nor that it walks
(0xd), which `SetState` asks for.

## In the port

`src/world/player_pursuit.{h,cpp}`, in `ragexe_world`: `IsCellBlocked`, `FreeCellToward`,
`StepCellToward`, `IsWithinCellRange`, `DistanceTo`, `SendQueuedMove` and `PursueTarget`;
`PreMove` and `UpdateOwnPlayer` in `src/world/actor_update`. `PlayerActor::own` (an
`OwnPlayer`) holds the fields and makes `SetPlayerState` `CPlayer`'s; `GameActor::reports`
false keeps the reports back; `GameActor::model_kind` is +0x238. What the player sends itself
goes to `StateContext::actor_messages`, a move's order to `own_moves`; the world's finds are
`target_actor`, `item` and `skill_object`, and the list `picked`.

`ragres find-paths` takes `o`: a player updated whole as the own player.

## Checks

`tools/walk_oracle.py` updates own players on every map's `.gat` (287).
- **The oracle:** `CPlayer::Update` runs on a `CPlayer`. The messages `CPlayer::SendMsg`
  handles itself are recorded at its switch (`0054c5fb`) and it returns; the rest go on to
  `CPc::SendMsg`. One stand-in is what `GetActorById`, `GetItemById` and `GetSkillById` find,
  each or not. Otherwise it is as `p`'s ([update.md](update.md#checks)).
- **What varies:** p's fields; each pursuit; the target near or far, its cell, state, model
  and job, found or not; the goal at the cell under the player or not; +0x310, +0x314 and
  +0x338 about their periods; acted, single or not; the ranges; the ping; a queued move; the
  player's id; the list before.
- **What is hashed:** after each update, p's, the `CPlayer`'s fields, what it sent itself and
  the orders of its moves, and the list's size and ids.

The hashes equal `ragres find-paths`: **11,480 own players** (`o`) over 2,252,355 updates, 892
faded and 10,588 updated to the end. The full run, 282,530 queries on all 287 maps, keeps the older counts, and the older vector
files regenerate byte-identical.

`tools/own_update_vectors.py` writes `tests/unit/own_update_vectors.inc` (1,281), which
`tests/unit/walk_test.cpp` replays: the three maps' scripts, then updates that each hang on one
thing:
- PreMove about half the ping; each pursuit, found or not;
- the range's edge a float either side, wrapping ranges, a big model's 20 and its floor under a
  negative range, unordered and infinite distances, y apart;
- where the squares' sum rounded to a float decides the range, and where the products rounded
  to floats decide the free cell;
- the times about 1200, 450 and 1200 ms; the states and job 0x10;
- the cast's aim at each sector's edge a float either side (no float angle is 25° itself);
- items at each distance and the list; the cast on the player itself, on cells, on objects
  attacked or not and off their cell's centre; a queued move as states end;
- among the walls, the free cell and the step into walls and off the map.

Walking (state 1) is not among them: the update needs a path for it. So a pursuit while walking,
the goal kept for walking, and `SetState` from states 3 and 6 (which no update sets a state
from) are left unchecked.

**Not ported:** `CPlayer::SendMsg` itself (what the messages above do), and `SetState`'s
state 3 work, the game mode's 5 and effect 0x150.
