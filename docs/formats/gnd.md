# Grounds: CGndRes

`CGndRes` (RTTI, 0x1e8 bytes) is a map's ground mesh: a grid of cells, each with four
corner heights and up to three textured quads (surfaces): its top, and the walls toward
the next row and the next column. It is registered for `gnd` with no folder
([resources.md](resources.md#types)). `C3dGround15::vf03` (`004bcc30`) builds the
drawable ground from it.

## Members

The loader keeps the file's bytes in its own `DataFile`, open for the resource's whole
life, and points into them.

| Offset | Field |
|-------:|-------|
| +0x110 | 1 when the file starts with `GRGN`, else 0; `C3dGround15::vf03` copies it to the ground's `+0x64` |
| +0x114 | major version (byte) |
| +0x115 | minor version (byte) |
| +0x118 | width, in cells |
| +0x11c | height, in cells |
| +0x120 | cell size in world units (float), 10.0 from the constructor (`00429170`) |
| +0x124 | texture count |
| +0x128 | surface count |
| +0x12c | lightmap count |
| +0x130 | `vector<char*>`: the texture names, pointers into the file |
| +0x140 | the lightmaps |
| +0x144 | the surfaces |
| +0x148 | the cells, for minor versions below 6 |
| +0x14c | the cells, for minor version 6 |
| +0x150 | the cells, for minor version 7 |
| +0x154 | the cells of an unversioned file; nothing reads it |
| +0x158 | the `DataFile` |

**The DataFile's path:**
- The object ends 0x90 bytes into the `DataFile`, so the path at its `+0x10` has 128
  bytes.
- A longer path would be written past the object. The longest `.gnd` path in `data.grf`
  is far shorter.

## File

All numbers little-endian.

### Header

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | `GRGN` (`006060a8`) |
| 4 | 1 | major version |
| 5 | 1 | minor version |
| 6 | 4 | width |
| 10 | 4 | height |
| 14 | 4 | cell size (float) |
| 18 | 4 | texture count |
| 22 | 4 | name size |

### Sections

**Texture names:** `count` records of `name size` bytes each. A name is the C string at
the start of its record.

**Lightmaps:**
- A count, then three dwords the loader reads and drops.
- Then `count` records of 256 bytes: 64 alpha values, then 64 R, G, B triples, each an
  8 × 8 block row by row.
- `LightmapAtlas::Build` (`004bbcc0`) packs them into textures as `A << 24 | R << 16 |
  G << 8 | B`, when the ISLIGHTMAP option (`0061ec60`) is set.

**Surfaces:** a count, then `count` records of 40 bytes:

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 16 | u of the four corners (floats) |
| 16 | 16 | v of the four corners (floats) |
| 32 | 2 | texture index, signed |
| 34 | 2 | lightmap index, unsigned |
| 36 | 4 | colour |

**Cells:** `width × height` records, row by row:
- **Minor version 7:** 0x1c bytes: four corner heights (floats), then three surface
  indices as ints.
- **Below 7:** 0x18 bytes: the four heights, three surface indices as shorts, and 2
  unused bytes.

The surface indices are: the top, the wall toward the next row (z + 1), and the wall
toward the next column (x + 1). −1 means none.

### In data.grf

All 208 `.gnd` files in `data.grf` are version 1.7 and end exactly after their last
cell. Every file has:
- a name size of 80, with each name NUL-terminated inside its record;
- the lightmap dwords 8, 8 and 1;
- a cell size of 10.0.

The largest are 208 cells wide, 202 high, with 59 textures and 57,404 surfaces.
`gl_cas01.gnd` has 37,365 lightmaps, so its surfaces' lightmap indices reach past 32,767.

| Surface index −1 | Cells |
|------------------|------:|
| top | 358,901 |
| wall toward the next row | 4,953,426 |
| wall toward the next column | 4,956,075 |

## Load

`CGndRes::Load` (slot 3, `00429200`). No `DataFile::Read` result is checked, and the
pointers into the file are never compared with its size.

1. **Open** the path with `DataFile::Open`; if it fails, fail, with no message.
2. **Magic.** Read 4 bytes.
   - **`GRGN`:** set `+0x110` and continue with `ParseVersioned` (`004293f0`).
   - **Anything else:** seek back to 0 and continue with `ParseUnversioned` (`004292e0`).

**ParseVersioned:**
1. Read the version bytes, width, height, cell size, texture count and name size.
2. Size the name vector to the count. Point each name at the read position, then advance
   the position by the name size.
3. Read the lightmap count and the three dwords. Point `+0x140` at the position, then
   advance by `count × 256`.
4. Read the surface count. Point `+0x144` at the position, then advance by `count × 40`.
5. Point the cell array for the minor version at the position: `+0x148` below 6, `+0x14c`
   at 6, `+0x150` at 7, then succeed.
6. **Any other minor version** fails, after all the reads. The major version is never
   checked.

**ParseUnversioned:**
1. Read the texture count, width and height.
2. Point the names at consecutive 0x50-byte records.
3. Point `+0x154` at what follows, then succeed.

Nothing sets the version bytes, which neither constructor initialises, so `GetCell` would
switch on whatever they held.

`Reset` (slot 4, `00429270`):
- empties the name vector and zeroes the pointers `+0x140` to `+0x154`;
- keeps the size, the counts and the `DataFile`;
- `Load` does not call it.

The destructor (`00428fe0`) closes the `DataFile` and frees the name vector.

## Reading cells

`GetCell(out, index)` (`004295f0`) gives a cell as 0x1c bytes, whatever the version:
- **Minor 7:** a copy of the record.
- **Below 6:** `GndCell::SetFromRecord` (`00429070`) copies the heights and widens the
  three shorts to ints.
- **Minor 6:** `SetFromRecordV6` (`004290b0`), the same body.
- **Any other minor version:** `out` is left alone.

`C3dGround15::vf03` is its only caller. It calls `GetCell` with `row × width + column`,
and once more for the neighbours a wall joins.

## Building the ground

The builder is `C3dGround15::vf03` (`004bcc30`). Its full port belongs to the map
renderer (M4); this is what it shows about the fields:
- **Cell corners:** cell (x, z) spans `((x − width/2)·s, (z − height/2)·s)` to the next
  cell, with `s` the cell size. Corners 0 to 3 are (x, z), (x+1, z), (x, z+1),
  (x+1, z+1), at the heights in that order.
- **Surfaces:**
  - **Bounds:** a surface index is used only when it is above −1 and below the surface
    count.
  - **Corners:** corner k of the quad takes `(u[k], v[k])`.
  - **Texture:** `TextureCache::GetTexture(name[texture])`, with the texture index
    sign-extended and not bounds-checked.
  - **Colour:** copied to the builder's surface record (`+0x14`).
  - **Lightmap:** with ISLIGHTMAP, the index selects the atlas block, as an unsigned
    short and not bounds-checked.

## In the port

`src/res/gnd.{h,cpp}`: `Ground`, `DecodeGround` and `GndRes`, registered by
`RegisterGroundTypes`. Cells are held as `GetCell` gives them; the lightmap index is
unsigned and the texture index signed, as the builder reads them.

**Where the port differs:**
- **An unversioned file fails.** The client would load one, then read its cells through
  an uninitialised version byte. `CWorld::LoadWorld` builds no ground from one either
  ([rsw.md](rsw.md#where-the-fields-go)).
- **Truncated files fail,** where the original points past the buffer.
- **So do these:** negative counts or sizes, and a name with no NUL inside its record.
  The original reads such a name on into what follows.

**The oracle:** `tools/gnd_oracle.py` runs the original `Load` on every `.gnd` in
`data.grf`, and on synthetic grounds of every minor version.
- **What it compares:** the header, the names (read through the original's pointers),
  the lightmaps, the surfaces and every cell.
- **Where the cells come from:** the original `GetCell`, for every cell of a small
  ground, and for the first and last cells of a large one. A large ground's other cells
  are read from the record array that `GetCell` indexes.
