# The world's lists

`CWorld` keeps its objects in four lists of pointers, each an MSVC `std::list` (an allocator
byte, the head node, the count; a node is the next, the previous and the pointer), and the own
player apart. The port is `src/world/world_objects`.

| Offset | Head, count | What it holds |
|--------|-------------|---------------|
| +8 | +0xc, +0x10 | every object, in the order the world updates and draws them: actors, items, corpses, effects, the effect and sound makers, the own player |
| +0x14 | +0x18, +0x1c | the actors the server sent (`CNpc`, `CPc`), which `GetActorById` looks among |
| +0x20 | +0x24, +0x28 | the items on the ground (`CItem`, [items.md](items.md)) |
| +0x2c | +0x30, +0x34 | the skills' objects (`CSkill`) |
| +0x3c | | the own player (`CPlayer`), in +8 but not in +0x14 |

## Joining

An object joins the end of +8, and an actor, an item or a skill then the end of its own list.
None is put anywhere else:
- **`CGameMode::AddToWorldObjects`** (`00511150`) appends to +8 alone. `ShowNumberEffect`
  adds its digits with it; `AddCorpse` ([items.md](items.md#a-corpse)) and
  `CWorld::CreateEffectAndSoundMakers` (`004c3890`) append the same way inline.
- **An actor** (packet 0x0078 and the like): `GetActorById` first (`0052682a`); without one a
  new `CPc` at the end of +8, then of +0x14.
- **An item:** packets 0x009d and 0x009e, below.
- **`CWorld::CreatePlayer`** (`004c4e60`): a new `CPlayer` at +0x3c and at the end of +8. A
  player there before stays in +8. `LoadWorld` calls it, and `CGameMode` `0050e390` after
  `ClearGameObjects` (`0050e399`).

## The update

**`CWorld::UpdateGameObjects`** (`004c4a10`) runs each world step due
([frame.md](../render/frame.md#the-frame)):
1. **The models** the loader thread placed since, into the scene graph
   ([scene_graph.md](../render/scene_graph.md#building-it)).
2. **Each object of +8 in order,** through its update (`vf01`). It reads the next node after
   the update (`004c4aa1`), so an object that joins the end meanwhile is updated in the same
   pass: the corpse an actor leaves as it dies, the effects it makes.
3. **An object whose update returns 0** is let go:
   - out of +0x14 (`CWorld::RemoveActor`, `004c4ab0`: the first node holding it);
   - deleted (its deleting destructor, `vf00` with 1);
   - its node unlinked from +8.

   It stays in +0x20, +0x2c and +0x3c if it was there.

**Which updates return 0:**
- actors once their fade is over or they die ([update.md](update.md));
- a corpse after 255 ticks ([items.md](items.md#a-corpse)).

`CItem::Update` and `CSkill`'s (`005abaa0`) never do: their only returns are `mov al, 1`
(`005ac32d`, `005ac4e5` for `CSkill`). So only the server lets an item or a skill go:
packets 0x00a1 and 0x0120. Were the own player's update to return 0, +0x3c would be left
pointing at it, freed.

## Finding and deleting

- **`GetActorById`** (`004c4cf0`), **`GetItemById`** (`004c4d20`) and **`GetSkillById`**
  (`004c4db0`): the first of +0x14, +0x20 or +0x2c whose id (+0x1b4, +0xb0, +0x280) is the one
  asked, or 0. The own player is not among the actors, so the callers compare with its id
  (`ClientState`+0xcdc) first.
- **`DeleteActorById`** (`004c4b00`, packet 0x0080 as an actor leaves the view),
  **`DeleteItemById`** (`004c4b60`, packet 0x00a1) and **`DeleteSkillById`** (`004c4d50`, packet
  0x0120): the first with the id is deleted (`DeleteGameObject`) and its node unlinked; 1, or 0
  without one.
- **`CWorld::DeleteGameObject`** (`004c4bc0`): the first node of +8 holding the object. When
  there is one, the object is deleted and the node unlinked; otherwise nothing is deleted. Its
  other callers are an actor's messages that end its effects
  ([effects.md](effects.md)).
- **`CWorld::ClearGameObjects`** (`004c4c20`): every object of +8 deleted in order, +0x3c 0,
  and the four lists emptied. `UnloadWorld` and `CGameMode` `0050e390` call it.

## The item packets

`CGameMode::HandlePacket_009D` (`00529780`, an item lying in view) and `HandlePacket_009E`
(`00529900`, an item dropped) make message 4's record ([items.md](items.md#an-item)) on the
stack, all 0x40 bytes zeroed first:

| Record | 0x009d | 0x009e |
|--------|--------|--------|
| +0, the name | `_itoa` of the word +6, unsigned, in decimal | the same |
| +0x20, the id | +2 | +2 |
| +0x3c, identified | the byte +8 | the byte +8 |
| +0x24, +0x28, the cell | the words +9 and +0xb, signed | the same |
| +0x2c, +0x30, the sixteenths | the bytes +0xf and +0x10 | the bytes +0xd and +0xe |
| +0x38, the count | the word +0xd, signed | the word +0xf, signed |
| +0x34, dropped | 0 | 1 |

Then `GetItemById` of the id:
- **Found:** that item is sent message 4 again. It is placed anew: cleared of a fall it had
  unless this record drops it.
- **Not found:** a new `CItem` (0xc0 bytes, made at that time), at the end of +8 and then of
  +0x20, is sent message 4.

`CGameMode::HandlePacket_00A1` (`0052a070`), an item gone (picked up or out of view):
`DeleteItemById`, then `CGameMode::RemoveIdFromList` (`00511940`). That unlinks the first node
holding the id from +0x198, the items the own player asked to pick up
([pursuit.md](pursuit.md)), whether or not the item was there.

Next to these functions, `004c4900`, `004c4960`, `004c49c0` and `004c4de0` (cell and world
conversions and a height over the models) have no callers.

## The port

`WorldObjects` keeps the objects as `WorldObject`, one of:
- `GameActor` (a `CNpc`);
- `PlayerActor` (a `CPc`, or a `CPlayer` with its own fields);
- `GroundItem`;
- `CorpseObject`.

The list owns them; `actors()`, `items()` and `player()` point into it. `Update` takes the
update to run on each, which picks the class's: `UpdateNpc`, `UpdatePlayer` or
`UpdateOwnPlayer` ([update.md](update.md)), `UpdateGroundItem`, `UpdateCorpse`. An item or the
own player let go is also taken out of `items()` and `player()`, where the original would leave
a freed pointer (for an item, a case its update never reaches).

`ItemInView` and `ItemDropped` read the packets' records; `ShowGroundItem` and
`RemoveGroundItem` are the rest of the three handlers.

## Checks

`tools/walk_oracle.py`'s `l` queries (`tools/path_oracle.py`) run scripts op by op on a `CWorld`
whose lists start empty, through the original's own functions:
- **Stand-ins:** objects alone, actors and the player, with vtables of two slots. The update
  returns 1 a given number of times, then 0. The deleting destructor records which and frees
  nothing. They join through `AddToWorldObjects` and `std::list::insert` (`004c6e30`).
- **Items:** packets 0x009d and 0x009e through their handlers, with real `CItem`s (numbered as
  made, recorded as deleted), and packet 0x00a1. The ids are few, so that an item comes again.
  `LoadItemTables` loads four small synthetic tables first, each name apart, so that the
  item's number and identified byte show in its names.
- **The rest:** `AddIdToList`, `DeleteActorById`, `GetActorById`, `GetItemById`,
  `UpdateGameObjects` as time goes on (the items fall and blink) and `ClearGameObjects`.

The hash takes, after each op:
- its result and the objects deleted, in order;
- each list's count and objects, the player, the ids asked to be picked up;
- each item's id, position, fall, the time it was made and started, and its blink;
- after a packet, the item's names and label.

`ragres find-paths` gives the same hashes for **11,480 scripts** on all 287 maps.

`tools/list_vectors.py` writes `tests/unit/list_vectors.inc`, which `tests/unit/walk_test.cpp`
replays. On the three maps of `tools/walk_vectors.py` it has their scripts, and on the first,
scripts that each hang on one thing:
- actors of one id, found and deleted first to last;
- an update that lets go objects among others;
- an item sent again by either packet, then gone and made anew;
- the ids to pick up, one twice and one never an item;
- the player made twice;
- clearing and starting again;
- the packets' words at their edges.

Two more tests cover what the stand-ins cannot:
- an object that joins during an update is updated in that update;
- the own player let go is no longer `player()`.

**Not ported:**
- the skills' list and its functions (`CSkill`, M8);
- the effects and the effect and sound makers as objects of the list (the port keeps an
  actor's effects as records, [effects.md](effects.md));
- the packets that bring actors and skills (M6).
