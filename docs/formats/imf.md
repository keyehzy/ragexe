# Layer order: CImfRes

`CImfRes` (RTTI, 0x19bc bytes) tells a player character, for each action and frame,
which of its sprite layers to draw first, and offers a point per frame. It is registered
for `imf` under `imf\` ([resources.md](resources.md#types)). `data.grf` has 31, one per
job and sex (`검사_남.imf`, the male swordsman, …).

## Members

| Offset | Field |
|-------:|-------|
| +0x110 | `vector<vector<record>>`: the layers, each a list of 0x30-byte records, one per action |
| +0x120 | 15 ints: each layer's action count, as read |
| +0x15c | 15 × 104 ints: each action's frame count, as read |

A record holds three `vector<int>`s with one value per frame:

| Offset | Values |
|-------:|--------|
| +0 | order |
| +0x10 | x |
| +0x20 | y |

### What reads it

**`CImfRes::FindEntryWithValue(value, action, frame)`** (`0042a220`):
- **Bounds:** it checks the action and the frame against the count tables of layer
  `value`.
- **Search:** it returns the first layer whose order at that action and frame is
  `value`, or −1.

**`CPc::RenderActFrameItems`** (`00544be0`) draws a player's items in slots counting down
from 7. For two of the slots it asks `FindEntryWithValue` which `.imf` layer to draw
there, with value 0 or 1, the character's action and its frame. So the order says which
layer goes first.

**`CImfRes::GetRecordPoint`** (`0042a2d0`) gives a frame's x and y as a point.
`UIMakeCharWnd::vf20` calls it.

## File

All numbers little-endian.

| Field | Size |
|-------|-----:|
| version (float) | 4 |
| a dword, dropped | 4 |
| the last layer's index | 4 |
| each layer, from 0 to the last | |

**Each layer:**
- an action count (4 bytes);
- **each action:** a frame count (4 bytes), then three ints per frame: order, x, y.

### In data.grf

All 31 files are version 1.01 and end exactly after their last frame.
- **Layers:** every file has 2 layers of 104 actions, the width of the client's frame
  count table.
- **Frames:** an action has up to 9 frames.
- **Order values:** every one is 0 or 1.

## Load

`CImfRes::Load` (slot 3, `00429c40`). Its two failures go only to the debug log, as
CP949 messages:

| Failure | Message |
|---------|---------|
| The file does not open | `00606120`: "hey, the file %s seems to be missing." |
| The version is below 1.01 (`005e29a8`), NaN included | `006060d8`: "reading %s failed! an old version's format. Save it again with the new version and load it." |

**Reading:**
1. Read the version, check it, then read the dropped dword and the last layer index.
2. **For each layer from 0 to the last:**
   1. Store its action count in `+0x120 + layer·4`.
   2. **For each action:** store its frame count in `+0x15c + (layer·104 + action)·4`,
      then read its frames into a new record.
   3. Append the record list to `+0x110`.
3. Succeed.

**Nothing is bounds-checked.** A layer from 15 on, or an action from 104 on, writes past
its table: the first into the frame count table, the second into the next layer's
counts or past the object. A negative count reads nothing, but is stored in the table as
it is.

**`Load` does not empty `+0x110` first.** `CImfRes` keeps the base class's empty
`Reset` (`00420890`), so a second load of one object would append its layers.

`CImfRes::ComputeChecksum` (`00429fa0`) folds the orders, x and y into an int; no caller
was found.

## In the port

`src/res/imf.{h,cpp}`: `Imf` (layers of actions of `ImfFrame`s), `DecodeImf` and
`ImfRes`, registered by `RegisterImfTypes`.

**Where the port differs:**
- **Table overflows fail:** more than 15 layers or more than 104 actions, which the
  original writes past its tables.
- **Truncated files fail.**

**The oracle:** `tools/imf_oracle.py` runs the original `Load` on every `.imf` in
`data.grf` and on 8 synthetic files, and compares every layer, action and frame.
- **What the synthetic files cover:**
  - a later version;
  - a full 15 layers, and 104 actions;
  - empty actions and negative counts;
  - no layers;
  - the refused versions 1.0 and NaN.
