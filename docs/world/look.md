# Players coming into view and their look

A player character (`CPc`) has its own messages: **`CPc::SendMsg`** (`00543540`, vf02). Its switch
takes messages 3 to 0x7c by the bytes at `005442f0` into the table at `005442b0`; it handles 3,
4, 5, 6, 0x22, 0x23, 0x4d, 0x64, 0x69, 0x6a, 0x72 and 0x79 to 0x7c itself and leaves the rest to
`CGameActor::SendMsg` (`00504460`). The own player's `CPlayer::SendMsg` leaves these to it too
([orders.md](orders.md)). The port is `src/world/player_look`.

The arrivals are `CGameActor`'s ([arrival.md](arrival.md)) with the player's look, which the
messages dress it in through vf14 or vf15 and the layer setters
([render/player.md](../render/player.md)).

## The arrivals

Messages 3, 4, 0x79 and 0x7c bring a player into view standing, and 5, 6, 0x7a and 0x7b walking;
the first word points at a record of the player. 0x79 to 0x7c are 4, 6, 5 and 3 dressed by item
ids (vf15) instead of kinds (vf14).

| Standing | Walking | What |
|----------|---------|------|
| +0 | +0 | the id, to +0x1b4 (4, 0x79 and every walking one) |
| +4 | +4 | the job |
| +8 | +8 | the sex |
| +0xc, +0x10 | | the cell |
| +0x14 | | the direction: the facing is (direction + 4) × 45, an `int` (`lea`), then `fild` |
| | +0xc to +0x24 | the move: its time, the start, its sixteenths and the goal, as message 0's |
| +0x18 | +0x28 | the speed, to +0xc8 (4, 0x79 and every walking one) |
| +0x1c, +0x20 | | to +0x1ec and +0x1f0 |
| +0x24 | +0x2c | the hair; standing, also to +0x1f4 |
| +0x28 | +0x30 | the weapon |
| +0x2c, +0x48, +0x4c | +0x34, +0x4c, +0x50 | the headgear: top, middle, bottom |
| +0x30 | | the state: 0 standing, 1 dead, 2 sitting |
| +0x34, +0x38, +0x3c | +0x38, +0x3c, +0x40 | the status words: body, health, effect |
| +0x40 | +0x44 | to +0x224 |
| +0x44 | +0x48 | the shield |
| +0x50, +0x54 | +0x54, +0x58 | the hair's and the clothes' colours |
| +0x58 | | to +0x294, the head; walking clears it |
| +0x5c, +0x60 | +0x60, +0x64 | the guild and its emblem (vf38, `SetEmblem`) |
| +0x64, +0x68 | +0x68, +0x6c | vf37's two words (below) |

**Standing** (3 at `005436b6`, 4 at `00543a0e`, 0x79 at `00543bbb`, 0x7c at `005437ec`):
1. 4 and 0x79 take the id.
2. The player stands on the cell's centre (`CGameMode::GetCellCentre`), on the ground there
   (`C3dAttr::GetHeight`), facing the record's direction; 4 and 0x79 take the speed.
3. +0x1ec, +0x1f0, the goal (+0xc0, +0xc4) the cell, +0x294 and +0x1f4.
4. **The dress:** the job mounted (`00504190`: 7 is 0xd and 0xe is 0x15 with bit 5 of the
   record's effect state), which `SetJob` takes as its own again, then vf14 or vf15 (below).
5. The status words (`SetStatus`, [arrival.md](arrival.md#the-status-words)). What changing them
   sends the player goes through `CPc::SendMsg`: 0x23 closes its gauge, and 0x69 and 0x6a, which
   bit 5 sends, dress it again.
6. +0x224 (`SetField224`, all four; `CGameActor`'s 3 leaves it), the emblem, and vf37's words.
7. **The state** by the record's +0x30: 0 stands (`SetState(0)`), 1 dies (3), 2 sits (6), any
   other stands, 4 and 0x79 logging "UNKNOWN PCSTATE" first (`DebugLog`, empty in this build).
8. 4 and 0x79: with the message's next word 2 and bit 6 of +0x21c clear, effect 6
   (`CAbleToMakeEffect::LaunchEffect`); then `CGameActor::LaunchEffectA0IfIdListed`
   (`005041e0`) with the record's id. `CGameActor`'s 4 fades in with 1; `CPc`'s does not.

**Walking** (5 at `0054404a`, 6 at `00543d84`, 0x7a at `00543e91`, 0x7b at `00544155`): the id,
the speed and the move's time (+0x18c), +0x294 0; the dress, status, +0x224, emblem and vf37 as
above. 6 and 0x7a then put the player at the start's centre and sixteenths, as
`CGameActor`'s 6. `FindMovePath` from the start at the record's speed: found, the goal is kept,
the player walks (`SetState(1)`) and +0x194 is 0; not found, 6 and 0x7a stand and 5 and 0x7b do
nothing more. Then `LaunchEffectA0IfIdListed`. Unlike `CGameActor`'s, 5 takes the id.

**vf37** (`00543510`, `SetEmotionWords`) calls vf36 (`005434c0`, `SetEmotionWord`) with its first
word and keeps the second at +0x290. vf36 below 0 sends the player 0x15 with 0x22 (the emotion in
its slot, [effects.md](effects.md)), otherwise 0x76, and keeps the word at +0x28c.
`CGameActor::SendMsg`'s 0x76 (`005044a7`) has the world delete (`CWorld::DeleteGameObject`,
`004c4bc0`) the emotion in that slot (+0x26c), if any; the slot keeps it.

## The dress

**vf14** (`00542c50`, `Dress`) takes the job, the sex, the hair, the weapon's kind, the three
headgear, the shield and the two colours:
1. **The sex** (`CGameActor` `00503020`, `SetSex`): +0x1bc its low bit, but 1 for the id
   0x2100e.
2. The fields: the hair (+0x298), the weapon (+0x2a0), the headgear (+0x2a4, +0x2a8, +0x2ac),
   the shield (+0x2b0), the hair's colour (+0x29c) and the clothes' (+0x34).
3. **The job** (`CGameActor` `00503440`, `SetJob`): +0x1b8, 0xd as 7 and 0x15 as 0xe (the
   mounted jobs as their own), then `UpdateBody` (vf16).
4. Each layer named again: the hair (vf17), the weapon by its kind (vf19), the headgear (vf21
   to vf23), the shield (vf24); then the `.imf` (vf26) and the palettes (vf27, vf28).

**vf15** (`00542d20`, `DressByItems`) takes the same, but the weapon is a word: an item's id in
its low half, the shield's in the high half. A weapon's kind in the high half
(`ClientState::MapRangedIdToKind`, `005aad00`, not -1) leaves no shield; otherwise the high half is
the shield, its kind past 2000 (`MapHighIdToKind`). vf15 ignores its eighth argument, the shield.
The weapon is then `SetWeapon` (vf20) with the low half, which keeps it at +0x2a0 in place of the
word.

The setters, by the names of [render/player.md](../render/player.md#the-names):

| Setter | What |
|--------|------|
| `UpdateBody` (vf16, `00542320`) | the bard (job 0x13) is male and the dancer (0x14) female, by `GetJob` each time; the shadow's scale +0x40 by the job (`0059b750`); layer 0; the object's own actions and sprite (`CRenderObject::SetAct`, `SetSprite`, `00501910`, `005018a0`) by layer 0's names |
| `SetHair` (vf17, `00542400`) | a style outside 0 to 19 becomes 13, written back to +0x298; layer 1 |
| `SetWeaponKind` (vf19, `005426a0`) | +0x2a0; layers 5 and 6 when the job shows the kind, otherwise none |
| headgear (vf21 to vf23) | +0x2a4 to +0x2ac; layers 2 to 4 |
| `SetShield` (vf24, `005428d0`) | an id past 2000 as its kind, to +0x2b0; layer 7 |
| `UpdateImfName` (vf26, `00542960`) | +0x27c |
| `UpdateHeadPalette`, `UpdateBodyPalette` (vf27, vf28) | +0x2cc and +0x10, left as they were for colour 0 |

**The layers' names** (`SetLayerAct`, `SetLayerSprite`, `00546290`, `00546070`): the layer's
resource released and the one the name names taken (`ResourceManager::GetResource`, none for
""), except for the ids `IsInAdminListB` (`004cad40`) finds among the clientinfo's admins
(`0067c978`): their layer 0 is the administrators' body, 인간족\몸통\<sex>\운영자_<sex>, and layer
5 their weapon, 인간족\운영자\운영자_<sex>_검 (`.Act`, `.Spr`, `00615820` to `00615a00`), the
second set (운영자2) for `IsSpecialActorIdOnKoreaService`'s ids; their layer 6 is none.

### The weapon by item

`SetWeapon` (vf20, `005464b0`) takes an item's id in the low half and the second hand's in the
high half; the game mode's handlers pass both.
1. 0: layers 5 and 6 none.
2. **Two hands**, the high half's kind past 0: layer 5 by the kind `ClassifyIdPair` (`00546a30`)
   gives the pair, and layer 6 is left as it was. By 50-wide ranges:

   | First | Second | Kind |
   |-------|--------|------|
   | 0x44c to 0x47d | 0x44c to 0x47d | 0x12 |
   | 0x44c to 0x47d | 0x514 to 0x545 | 0x16 |
   | 0x44c to 0x47d | other | 2 |
   | 0x4b0 to 0x4e1 | 0x4b0 to 0x4e1, 0x44c to 0x47d, 0x514 to 0x545 | 0x11, 0x14, 0x15 |
   | 0x4b0 to 0x4e1 | other | 1 |
   | 0x514 to 0x545 | 0x514 to 0x545 | 0x13 |
   | 0x514 to 0x545 | other | 6 |
   | other | | 0 |

   A first of 0 takes the second alone, and two 0s are 0.
3. **The item's own sprite**, 인간족\<job folder><suffix>_<id>, when its `.act` is there
   (`ResourceManager::Exists`): layer 5; the glow by the kind of the whole word (+0x2a0).
4. **Otherwise by the low id's kind:** none (-1 or 0) or 0x63f clears layers 5 and 6 (0x63f's
   kind, a book's, has no glow to take after); job 6 holds kinds 6, 7 and 8 as 2; jobs 0xc and
   0x11 with 8 and job 8 with 0xc show nothing either; the rest name layer 5 by the kind.
5. **The glow,** layer 6, for kinds 1 to 7 and 0x10 to 0x16, otherwise none.

## The other messages

| Message | Words | What it does |
|---------|-------|--------------|
| 0x22 | the first bar's two | the gauge (`UIPcGage`, +0x2dc), made if there is none (0xa8 bytes, `UIWindow::Create` 0x3c by 5, `UIWindowMgr::AddWindow`); its first bar (`SetFirstBar`, `004431b0`) the two words and the second (`SetSecondBar`) 0 and 0; moved (its vtable's +0x18) to 0x1e left of the point on screen (+0x90) and the screen's height × 12 / 480, whole, below it (+0x94), in `int`s |
| 0x23 | | the gauge queued for removal (`UIWindowMgr::QueueRemoval`) and +0x2dc 0 |
| 0x4d | | nothing |
| 0x64 | the frame | standing or sitting (0, 6), +0x28 |
| 0x69, 0x6a | | `UpdateBody`, the `.imf`, the weapon by its kind again (vf19 of vf31, +0x2a0), the shield (+0x2b0) and the palettes |
| 0x72 | a, b | +0x2e4 a, +0x2e0 b |

`CPlayer`'s own update moves the gauge each update ([update.md](update.md)); 0x22's rise is
not scaled by +0x48 as that one is.

## In the port

`src/world/player_look.{h,cpp}`, in `ragexe_world`:
- `ArrivePlayer` (3, 4, 0x79, 0x7c) with a `PlayerStandingArrival` and `ArriveWalkingPlayer` (5,
  6, 0x7a, 0x7b) with a `PlayerWalkingArrival`;
- `SendPlayerMessage` for the rest, false for those `CPc` leaves to `CGameActor`;
- `DressPlayer` (vf14, vf15) and `SetPlayerWeapon` (vf20), `TwoWeaponsKind` (`ClassifyIdPair`).

`PlayerActor` keeps the look in `PlayerLook`: the fields, each layer's names as the setters ask
for them (with their extensions), the object's own and the `.imf` and palettes' names, and
+0x28c, +0x2e0 and +0x2e4. `GameActor::repeated_at` is +0x290, which a `CPc` uses for vf37's second
word. `StateContext::admins` is `IsInAdminListB`'s list. `SetPlayerStatus` is `SetStatus` for a
player, its messages to itself through `SendPlayerMessage` first. The resources themselves
(`GetResource`'s lower-cased, folder-prefixed names) are the resource manager's; the port keeps
the names the setters give.

`ragres find-paths` takes `c`: a player sent a script of these messages.

**Found on the way:**
- `CPc`'s 3 and 5 take what `CGameActor`'s leave (+0x224, the id), and its 4 does not fade in.
- vf15 ignores its shield argument; a shield comes only from the weapon word's high half.
- Two hands leave the glow as it was.

## Checks

`tools/walk_oracle.py` sends players these messages on every map's `.gat` (287).
- **The oracle:** a `CPc` sent each message through the original's `CPc::SendMsg`. Stand-ins:
  `ResourceManager::GetResource` gives a resource named as asked (none for ""); `SetAct` and
  `SetSprite` record their names; `CPc::SendMsg`'s entry records what the player sends itself
  (but 0x53, which `SetState` sends to close the cast's gauge and the port does in place);
  `LaunchEffect` records its id and makes nothing; `UIPcGage`'s constructor, `Create`,
  `AddWindow`, the bars and the move are recorded by window; the effects are B's
  ([effects.md](effects.md#checks)). `ClientState`'s name tables (`00588ba0`) and shadow scales
  (`0059a5e0`) are the client's own. `CWorld`+0x5c and +0x64 are clear, so
  `LaunchEffectA0IfIdListed` does nothing.
- **What varies:** the jobs (mounted or not, the bard and dancer), sexes, hair styles in and
  past 0 to 19, headgear, shields by kind and by id, colours 0 or not, weapons by kind and by
  item word (both halves), the item's own sprite there or not, an administrator among the
  players' ids on both sets and 0x2100e; the status words as A's; the record's state; vf37's
  first word; a gauge before, the screen's height; one to 16 messages.
- **What is hashed:** after each message, the fields, the path, the game mode's messages, the
  look's fields, the eight layers' names, the `.imf`, palettes and the object's own names, the
  arrival's fields, what the player sent itself, played and asked for, the effects launched,
  made, sent and deleted, and what the windows were told and which were closed.

The hashes equal `ragres find-paths`: **11,480 players** (`c`) sent 72,114 messages. The full
run, 305,490 queries on all 287 maps, keeps the older counts, and the older vector files
regenerate byte-identical.

`tools/look_vectors.py` writes `tests/unit/look_vectors.inc`, which `tests/unit/walk_test.cpp`
replays: 704 players on the three maps (their scripts and messages that each hang on one
thing: each message in each state, the arrivals' states and effect 6, every job's weapons by
kind, the hair, headgear, shields and colours' edges, the weapon words, the administrators, the
mounted jobs, vf37's word and the gauge at the screen's edges), and 1,872 calls of vf20 with both
halves (`ClassifyIdPair`'s pairs, the own sprite there or not, the jobs that hold a kind
otherwise, 0x63f).

**Not ported:** `LaunchEffectA0IfIdListed` (`ClientState`'s list at +0xe4c, when `CWorld`+0x5c
or +0x64 is set); vf18 (layer 9) and vf25 (+0x2b4), which these messages do not call; what the
resource manager does with the names.
