# Actions: CActRes

`CActRes` (RTTI, 0x144 bytes) says how a sprite is animated. It holds actions, each a
list of frames; each frame places sprite images (items) and anchor points. It is
registered for `act` under `sprite\` ([resources.md](resources.md#types)), and each
`.act` goes with the `.spr` of the same name ([sprite.md](sprite.md)).

## Members

| Offset | Field |
|-------:|-------|
| +0x114 | `vector<vector<ActFrame>>`: the actions, each 16 bytes |
| +0x120 | the largest item count of any frame |
| +0x128 | `vector<string>`: event names, 16 bytes each |
| +0x138 | `vector<float>`: one delay per action, 4.0 by default |

Accessors:
- `GetAction` (`004219d0`), `GetFrame` (`00421a60`) and `GetFrameCount` (`00421a80`);
  `GetFrameCount` answers 1 when an action has fewer than two frames;
- `GetEventName` (`00420f20`), "" out of range;
- `GetActionDelay` (`00421990`), 4.0 out of range.

### ActFrame

`ActFrame` is 0x4c bytes:

| Offset | Field |
|-------:|-------|
| +0 | eight dwords copied from the file; no consumer found yet |
| +0x20 | `vector<ActItem>` |
| +0x30 | item count |
| +0x34 | event index, −1 for none |
| +0x38 | `vector<ActAnchor>` |
| +0x48 | anchor count |

`SetItemCount` (`00420400`) stores the count, sets the event to −1, and sizes the item
vector to **min(count, 1000)**. The loader then reads `count` items into it all the
same.

### ActItem

`ActItem` is 0x24 bytes. The drawing code is `SceneRenderer::method_00410620` and
`CRenderObject::method_00500f70`.

| Offset | Field | Evidence |
|-------:|-------|----------|
| +0 | x | the image's left edge, times the x scale |
| +4 | y | the image's top edge, times the y scale |
| +8 | image index | −1 for none |
| +0xc | flags | bit 0 mirrors the image horizontally |
| +0x10 | R, G, B, A | the vertex colour is `A << 24 \| R << 16 \| G << 8 \| B` |
| +0x14 | x scale | float |
| +0x18 | y scale | float |
| +0x1c | angle | integer; added to the object's `+0x60` and passed to drawing as a float |
| +0x20 | image type | 0 for the sprite's 8-bit images (vector `+0x510`), 1 for its 32-bit images (`+0x520`) |

### ActAnchor

`ActAnchor` is 12 bytes: x, y and a kind. `CPc::RenderActFrameItems` matches anchors of
the same kind between two frames and offsets one by their difference.

## File

All numbers little-endian. **Header**, 16 bytes:

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 2 | `AC` |
| 2 | 2 | version: minor byte, then major |
| 4 | 2 | action count |
| 6 | 10 | not used |

**Then, for each action:** a frame count (4 bytes), and for each frame:
- 32 bytes, kept as the frame's first eight dwords;
- an item count (4 bytes), then that many items, whose layout depends on the version:
  - **below 0x200:** 16 bytes: x, y, image index, flags. The other fields default to
    colour `ff ff ff ff`, scales 1.0, angle 0 and image type 0.
  - **0x200 to 0x203:** 32 bytes: x, y, image index, flags, the 4 colour bytes, one scale
    (used for both x and y), angle, image type.
  - **0x204:** 36 bytes, the item as it is in memory.
  - **0x205:** 36 bytes, then the image's width and height (4 bytes each).
- **0x200 and later:** the event index (4 bytes); version 0x200 itself replaces it
  with −1;
- **0x203 and later:** an anchor count (4 bytes), then that many 16-byte anchors. The
  first 4 bytes of each are dropped and the other 12 kept: the bytes are read into a
  `std::string` local, whose buffer, length and capacity fields are then copied.

**After the actions:**
- **0x201 and later:** an event count (4 bytes), then that many 40-byte names, each up
  to its first NUL;
- **0x202 and later:** one float delay per action.

In `data.grf`:

| Version | Files |
|---------|------:|
| 0x200 | 3 |
| 0x201 | 217 |
| 0x203 | 336 |
| 0x204 | 1409 |
| 0x205 | 144 |

Every file ends exactly after its last field.

## Load

`CActRes::Load` (slot 3, `00420f60`). No `DataFile::Read` result is checked.

1. **Open.** If `DataFile::Open(path)` fails, show "Cannot find File : " (`00605c3c`)
   followed by the path, and fail.
2. **Header.** Read 16 bytes.
   - **Not `AC`:** show "Illegal file format" (`00605c28`), then a box with the path,
     and fail.
   - **Version 0x206 or above:** show "Unsupported Version. Content may be corrupted"
     (`00605bf0`) and fail.
3. **The matching sprite's name:** the path without its last 4 characters, then `.spr`
   (`00605c20`).
4. **Size the actions** (`00420af0`): `Reset` (slot 4), resize the action vector to the
   count, and give the delay vector one 4.0 per action.
5. **Read every action, frame and item** as above. Keep the largest item count in
   `+0x120`.
6. **Adjust items** (versions 0x200 to 0x205) whose image index is not −1, with
   `00420e70`. It needs the image's width, height and halved flags:
   - **0x200 to 0x204:** from the matching sprite, fetched with
     `ResourceManager::GetResource` for every item. The record is the one at the image
     index in the vector the image type selects.
   - **0x205:** the width and height stored with the item, and no halving.

   With `w` and `h` doubled where the sprite was halved, `c(v) = v / 2 + v % 2` (C's
   signed division and remainder), and `__ftol` (`005c2084`):

   ```
   x = (int)((x − c(w) · x_scale) / x_scale)
   y = (int)((y − c(h) · y_scale) / y_scale)
   ```

   - **`__ftol`** truncates toward zero with a 64-bit `fistp` and returns the low
     dword. Values past 32 bits wrap, and NaN or infinity (an item scale of 0, as in
     `몬스터\ambernite.act` and `이팩트\miss.act`) gives 0.
   - **So** the file stores each image's centre, and the client keeps its top-left
     corner.
   Because a halved sprite's width is rounded down, the result depends on the detail
   level the sprite was loaded at.
7. **Events and delays,** then succeed.

**When the sprite is missing,** `GetResource` shows its errors and returns null. The
loader then reads through the null pointer. Seven 0x204 actions in `data.grf`, item
icons such as `아이템\landmine.act`, have no sprite and one item that needs it.

`Reset` (slot 4, `00420d60`) destroys the actions and the event names. It does not touch
the largest item count or the delays.

## In the port

`src/res/act.{h,cpp}`:
- **`DecodeAct`** takes a lookup for the matching sprite's image records.
- **`ActRes`** gets its sprite through the resource manager it was registered with,
  once, when the first item needs it.
- **What fails in the port:**
  - a missing sprite or an image index outside it (the original reads past them);
  - truncated files;
  - frames of more than 1000 items (the original writes past its item vector).
- **The arithmetic** is done in double precision. The original's x87 runs at whatever
  precision Direct3D left. The values are small, and every one in `data.grf` agrees
  (below).
- **The oracle:** `tools/act_oracle.py` runs the original `Load` in the emulation oracle,
  with the original resource manager and the sprite type registered, so the matching
  sprite really loads, at each detail level. It compares every action, frame, item,
  anchor, event and delay.
