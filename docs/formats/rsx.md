# Animated models: C3dAnimModelRes

`C3dAnimModelRes` (RTTI, 0x164 bytes) is a model animated by frames: each node keeps a
full set of vertices for every frame, instead of keys. It is registered for `rsx` under
`model\` ([resources.md](resources.md#types)). `CRmaRes` names `.rsx` files for its
actions (`CRmaRes::GetActionRsxName`, `00421f50`).

`data.grf` has a single one, `model\내부소품\돌문01.rsx`: version 1.3, two textures,
three nodes and 31 frames.

## Members

| Offset | Field |
|-------:|-------|
| +0x110, +0x114 | a count and an array of 0x88-byte records, not touched by `Load` |
| +0x118 | `list<AnimModelResNode*>`: the nodes, in file order |
| +0x124 | `list<string>`: root names, one appended by each `Load` |
| +0x140 | int, 2 by default |
| +0x144 | frame count |
| +0x148 | int, 0 by default |
| +0x14c | float, 1.0 by default |
| +0x154 | 3 floats, 0 by default |
| +0x160 | int, 255 by default |

The defaults come from the constructor `Clone` uses (`0042dbb0`). No caller of the
drawing helper `AnimModelResNode::ComputeVertexColors` (`0042ce40`) was found. It takes a
shade type (0, 1 or 2) and an alpha, which these values may feed.

### AnimModelResNode

`AnimModelResNode` is 0x58 bytes (constructor `0042ccd0`).

| Offset | Field |
|-------:|-------|
| +0 | `string`: name |
| +0x10 | `string`: parent's name |
| +0x20 | the `C3dAnimModelRes` |
| +0x24 | the parent node |
| +0x28 | `list<AnimModelResNode*>`: the children |
| +0x34 | vertex count |
| +0x38 | texture coordinate count |
| +0x3c | face count |
| +0x40 | texture count |
| +0x44 | 3 colours per face, for `ComputeVertexColors` |
| +0x48 | one 8-byte entry per frame: the frame's vertices, then its texture coordinates |
| +0x4c | faces, 0x18 bytes each |
| +0x50 | `CTexture*` array |
| +0x54 | non-zero when one set of texture coordinates serves every frame |

**Vertices** are 12 bytes (x, y, z). `ComputeVertexColors` transforms them by a matrix
and indexes them with a face's first three shorts.

**Texture coordinates** are 12 bytes, the `.rsm` layout ([rsm.md](rsm.md#modelresmesh)):
- They are indexed by a face's next three shorts.
- Their first dword chooses between two ways of colouring the corner.
- With the shared flag set, every frame uses the set at frame 0.

**Faces** have the `.rsm` layout.

## File

All numbers little-endian. **Names** are C strings in 40-byte records.

**Header:**

| Field | Size | When |
|-------|-----:|------|
| 4 bytes, never checked (`GRSX` in `data.grf`) | 4 | |
| major, minor | 1 + 1 | |
| `+0x14c`, `+0x154` | 4 + 12 | major 1, minor 2 or more |
| `+0x160` | 4 | 1.3 exactly |
| `+0x140`, frame count, `+0x148` | 12 | |
| reserved, dropped | 16 | |
| texture count, then the names | 4 + 40 each | |
| the root node's name | 40 | |
| node count | 4 | |

**Each node:**

| Field | Size |
|-------|-----:|
| `12 34 56 78` (0x12345678) | 4 |
| name, parent's name | 40 + 40 |
| texture count, then indices into the model's textures | 4 + 4 each |
| vertex count | 4 |
| the vertices of each frame | frames × count × 12 |
| texture coordinate count, the shared flag | 4 + 4 |
| the texture coordinates of each frame, or one set when shared | sets × count × 12 |
| face count, then the faces | 4 + 24 each |
| 0x12345678 | 4 |

In `data.grf`'s one file every node has the shared flag set, and the file ends exactly
after its last node.

## Load

`C3dAnimModelRes::Load` (slot 3, `0042df00`). There is no version check and no magic
check, and no read or index is checked.

**The model:**
1. **Open the file;** if that fails, fail, with no message.
2. **Read the header.** Each texture name is copied into a temporary array of strings, and
   fetched once with `TextureCache::GetTexture`.
3. **Read the root name.**

**Each node:**
1. **Create it,** with an array of entries for the model's frame count.
2. **Its markers:** where the first or last marker is not 0x12345678, show "data corrupt"
   (`006061f0`) and fail. The nodes read so far stay in the list.
3. **Its textures:** each through `GetTexture` of that name, with its use count raised.
4. **The vertices:** allocate and read them frame by frame.
5. **The texture coordinates:** frame by frame, or into frame 0 only when shared. With
   no frames, that writes past the end of the entry array.
6. **The faces:** allocate 3 colours per face, then read the faces.
7. **Append the node.**

**After the nodes:**
1. **Free the texture names.**
2. **Link each node** with a parent name to the first node of that name, found by
   `C3dAnimModelRes::FindNode` (`0042de70`), and append it to that node's children. A
   missing parent is a null pointer.
3. **Append the root name** to `+0x124` and succeed.

**Name buffers:** names are copied with `strlen` from a 40-byte stack buffer. The 4
magic bytes and a NUL come right after it, so an unterminated name takes those bytes
too.

`Reset` (slot 4, `0042db50`) calls `ClearNodes` (`0042ddc0`), which frees the records at
`+0x114` and the nodes. The root names stay.

## In the port

`src/res/rsx.{h,cpp}`: `AnimModel`, `DecodeAnimModel` and `RsxRes`, registered by
`RegisterAnimModelTypes`.
- **Layout:** vertices and texture coordinates are kept flat, frame by frame.
- **Shared types:** faces and texture coordinates use the `.rsm` types.
- **Magic:** as in the original, it is not checked.

**Where the port differs:**
- **Truncated files fail,** as do names with no NUL inside their record.
- **So do bad references,** where the original reads through bad pointers:
  out-of-range texture indices and a missing parent.
- **A negative frame count fails.**

**The oracle:** `tools/rsx_oracle.py` runs the original `Load` on the `.rsx` in
`data.grf` and on 8 synthetic files.
- **The synthetic files** cover every minor version, a major 2, shared and per-frame
  texture coordinates, zero frames and a bad marker.
- **What it compares:** the header values, the textures it asked for, the root name,
  and each node's names, links, textures, every frame's vertices and texture
  coordinates, and its faces.
