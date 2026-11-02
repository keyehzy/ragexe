# Items on the ground and corpses

Two of the world's objects are plain `CRenderObject`s, not actors: **`CItem`** (vtable
`005e98ac`), an item lying on the ground, and **`CCorpse`** (vtable `005e99f0`), what a monster
leaves when it dies ([states.md](states.md#dying)). Each has its own update (`vf01`). Both are
in the world's list of objects (`CWorld`+8), which `CWorld::UpdateGameObjects` (`004c4a10`)
updates each world step, letting go an object whose update returns 0. Items are also in a list
of their own (+0x20), by which `CWorld::GetItemById` and `DeleteItemById` (`004c4b60`, for
packet 0x00a1) find them; an item's update always returns 1, so only that deletes it
([lists.md](lists.md)). The port is `src/world/ground_item`.

| Slot | `CItem` | `CCorpse` |
|------|---------|-----------|
| vf01, the update | `0053ad60` | `00540c70` |
| vf02, `SendMsg` | `0053afb0`: message 4 | `00540d00`: `CRenderObject`'s |
| vf03, the drawing | `0053ad20` | `CRenderObject`'s (`00501870`) |
| vf05, the bounds | `0053ae40`, the pick record ([pick.md](pick.md)) | `CRenderObject`'s (`00501830`), none |

The other slots are `CGameObject`'s and `CRenderObject`'s ([render/actor.md](../render/actor.md)).

## An item

**Its constructor** (`0053abb0`) is `CRenderObject`'s (`00500400`): the scale 1, shown, the key
colour 0xffff0000, +0x6c (the time it was keyed) `timeGetTime`, and the sprite and act
`NPC\%s.spr` and `.act` with the job name of `ClientState`+0x754 entry 46. Then
`CItem::SetItemDefaults` (`0053acf0`): the scale 1 again, pickable (+0x89), with a shadow (+0x88), and the
key colour 0xffffff00. The string +0xa0 starts empty. +0x70 and +0xb4 to +0xbc are left
unset; message 4, which the game mode sends at once, sets all of them but +0xb8, which only a
falling item reads.

**`CItem::SendMsg`** (`0053afb0`) handles message 4; the others go to
`CRenderObject::SendMsg` (`00500e40`), which is `CGameObject`'s (`005002a0`) and does nothing.
The first argument points at a record:

| Offset | What |
|--------|------|
| +0 | the item's id as text, a NUL-ended string of up to 0x20 bytes, by which the tables know it |
| +0x20 | the id the pick record carries, to +0xb0 |
| +0x24, +0x28 | the cell |
| +0x2c, +0x30 | the sixteenths of a cell from its corner |
| +0x34 | not 0: the item falls |
| +0x38 | the count |
| +0x3c | a byte: identified |

Message 4:
1. +0xb0 the id; +0xa0 the name text.
2. **The place:** `CGameMode::CellToWorld` (`00511e50`): x = ((cell x − width × 0.5
   (`005e25b8`)) + sixteenths × 1/16 (`005e8a7c`)) × the cell size, z the same with the height,
   each step in single precision; the height `C3dAttr::GetHeight` there. Unlike
   `GetCellCentre`'s, the point is the cell's corner at 0 sixteenths and its centre at 8.
3. **The drop:** with +0x34 set, the height less 15 (`005e98dc`), +0xb4 set and the speed
   +0xb8 −0.6 (0xbf19999a); otherwise +0xb4 clear. +0xbc takes the height either way.
4. **The sprite and act:** `ClientState::GetItemResourceName` (`00597580`) puts "아이템\" (`0061d628`) before
   `GetItemInfoString` (`005978c0`) of the name and the identified byte, in a static
   `std::string` (`00686658`). That is the item's resource name: `GetItemInfo` (`004d5b00`) on
   `atoi` of the name, the fallback (`00689140`) when the tables do not have it, and its
   `IdNum2ItemResNameTable` name identified, `Num2ItemResNameTable` otherwise, "" for none
   ([formats/item_tables.md](../formats/item_tables.md)). With ".spr" and ".act" they go to
   `SetSprite` and `SetAct`.
5. **The label** (+0xa0): `ItemRecord::BuildDisplayName` (`004d4200`) of a record made on the
   stack: all zero but the name at +0x2c and the identified byte at +0x3c. Its type 0 is not one
   `IsNameDecoratedType` takes, so neither the refine nor the cards come in: the
   `IdNum2ItemDisplayNameTable` name identified (`CopyIdTableName`), the
   `Num2ItemDisplayNameTable` name otherwise (`CopyNumTableName`). Then `sprintf` with message
   0xb7 of `msgStringTable` (`GetMsgString`, `004d8df0`) as the format, the name and the count.
6. `GetAct`, whose result goes unused, and the start +0x70 `timeGetTime`.

**The buffers:** `BuildDisplayName` writes into 0x40 bytes and `sprintf` into 0x80. A display
name of 64 bytes or more, or a label of 128, runs over them in the client, into the stack
record and what is past it. The format takes a string and then an `int`; any other is undefined
in the client as in the port. The port's label stops at 127 bytes.

**The update** (`CItem::Update`, `0053ad60`, vf01) always returns 1:
1. t is the ticks since the start (`timeGetTime` − +0x70, unsigned, × 1/24, as the frame step's).
2. **The blink:** +0x83 (keyed, which draws it in the key colour, yellow) is set when `__ftol`(t)
   mod 92 is 90 or 91, and cleared otherwise: two ticks of every 92.
3. **The fall,** while +0xb4 is set: the height is (t × 0.083 (`005e98d4`) + the speed) × t + the
   drop height, each step in single precision. Once the ground's height there
   (`C3dAttr::GetHeight`) is below it (it has passed the ground, heights growing downward), the
   height is the ground's and +0xb4 is cleared. From 15 above at −0.6 that takes 17 ticks or so.

**The drawing** (`CItem::Render`, `0053ad20`, vf03): nothing unless shown (+0x84). Then the shadow
(`CRenderObject::DrawShadow`, `00501550`) at 0.4 (`005e98d0`) of the scale, the product in single
precision, and the body (`DrawBody`, `00500f70`), as [render/actor.md](../render/actor.md) draws
them.

## A corpse

**`CGameMode::AddCorpse`** (`00510030`) makes one for a monster that died with its body's act
and sprite names, its base action, frame and facing ([states.md](states.md#dying)):
1. **The constructor** (`00540b70`): `CRenderObject`'s, then `SetCorpseDefaults` (`00540c40`): shown, its depth from
   the other slope (+0x80, as an object lying on the ground), and the start +0x70
   `timeGetTime`.
2. It goes at the end of the world's list of objects (`CGameMode`+0xb4, then +8), the count
   +0x10 one more.
3. Its position, `SetAct` and `SetSprite` with the names, the frame (+0x28), the action as both
   the base action (+0x20) and the action (+0x24), and the facing (+0x38).

**The update** (`CCorpse::Update`, `00540c70`, vf01):
1. t is the ticks since the start, as an item's, stored as a float.
2. The action is the base action plus the direction (`vf04`, `CGameObject`'s: the facing
   against the camera's yaw, [render/actor.md](../render/actor.md#the-direction-and-the-frame)).
3. The frame is the action's last: `GetFrameCount` − 1, the count 1 for an action of fewer than
   two frames, so 0, or for one the `.act` lacks (`CActRes::GetAction`, `004219d0`, answers an
   empty action).
4. **The fade:** `SetColorChannels` (`00500720`) with the alpha `__ftol`(255 (`005e26a8`) − t)
   and the colours kept. Its −1 keeps the alpha too: from 256 ticks to just short of 257 the
   corpse keeps the alpha of its last update.
5. It returns 1 while t is below 255, 0 after: the corpse is gone about 6.1 seconds after it was
   left.

## Checks

`tools/walk_oracle.py`'s `i` and `k` queries (`tools/path_oracle.py`), on every map:
- **`i`:** a `CItem` made by its constructor at a time, then shown or not and given a scale;
  sent `SendMsg` with a message and a record, before which `LoadItemTables` (`004d48e0`) loads
  four small synthetic tables (IdNum2 and Num2 display and resource names, each missing at
  times) carried in the query, and message 0xb7 is the query's format; then updated at each of
  its times and drawn once. `SetSprite` and `SetAct` record their names; `DrawShadow` its scale
  and `DrawBody` that it was called, neither drawing. The static `std::string` of `GetItemResourceName` and
  the tables' maps and fallbacks are made again for each query.
- **`k`:** `AddCorpse` itself at a time with a position, names, action, frame and facing, on a
  world whose list of objects is empty; then its corpse updated at each of its times until it
  returns 0, every action of its `.act` with a number of frames (`GetFrameCount` answers so for
  any action, so one the `.act` lacks is not checked).
- **What varies:** the message (4 most often); the cell, walkable or not and off the map, and
  sixteenths in and past 0 to 15; dropped or not; the name text (ids the tables have, with
  spaces, signs and junk after, ones they do not, empty, wrapping past 32 bits, cut at 31
  bytes); the count; the identified byte (256 as 0); the tables' names (CP949, underscores, 63
  bytes) and resources (CP949, 100 bytes, empty); the formats (widths, precision, signs, `%%`);
  the times through the fall's end, the blinks' ticks and before the message; shown or not, the
  scale. For corpses the names, actions, frames, facings, yaws and frame counts, and times
  through the 255th tick, the 256th and before.
- **What is hashed:** for `i`, the constructor's +0x3c, +0x68, +0x6c and flags; after the
  message +0xb0, the position, +0xb4 to +0xbc, +0x70, the label and both names; each update's
  result, height, +0xb4 and +0x83; the drawing's calls. For `k`, the constructor's +0x6c, +0x70,
  +0x80 and +0x84, the world's count, `AddCorpse`'s fields and names, and each update's result,
  action, frame and colour.

The hashes equal `ragres find-paths`: **11,480 items** (`i`) through 134,582 updates, 1,749 of
them still falling at the end, and **11,480 corpses** (`k`) through 75,412 updates, 6,193 gone.
The full run, 328,450 queries on all 287 maps, keeps the older counts, and the older vector files
regenerate byte-identical.

`tools/item_vectors.py` writes `tests/unit/item_vectors.inc`, which `tests/unit/walk_test.cpp`
replays: 356 queries on the three maps of `tools/walk_vectors.py` (their scripts and queries that
each hang on one thing: the blink's ticks, the fall's last ticks on flat ground and on a slope,
the sixteenths, the names identified or not and known or not, each table missing, each format,
the messages other than 4, shown or not and the scale; the corpse's ticks 254 to 257, the frame
counts and the directions) and on a fourth, flat at −1000000, where the fall's 421st millisecond
lands exactly on the ground and the item falls one update more.

The world's lists and the packets that bring and take items are [lists.md](lists.md);
`CItem`'s pick record is [pick.md](pick.md)'s.
