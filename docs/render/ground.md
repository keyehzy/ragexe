# The ground

`C3dGround15` (RTTI, base `C3dGround`) is a map's drawable ground. `CWorld::LoadWorld`
(`004c3ab0`) builds one from the map's `.gnd` ([gnd.md](../formats/gnd.md)), and only
from a file that starts with `GRGN`.

**Construction** is inlined in `LoadWorld`:
- `operator new(0xec)`;
- `C3dGround::C3dGround` (`004bc220`);
- the seven vectors from +0x7c to +0xdc emptied;
- the vtable `005e897c`.

**Building.** `LoadWorld` then calls `Build` (slot 3) with:
- the ground resource;
- three vectors from the map's `.rsw` ([rsw.md](../formats/rsw.md)), as `LoadWorld`
  copies them:
  - the light vector (`g_worldLightDirection`, `0067bf80`);
  - the diffuse colour (`g_worldDiffuse`, `0067bf70`);
  - the ambient colour (`g_worldAmbient`, `0067bf60`).

## Fields

| Offset | Field |
|-------:|-------|
| +8, +0xc | width and height in cells |
| +0x10 | the cell size, from the file |
| +0x14 | `LightmapAtlas` (0x28 bytes): block count, a vector of 0x128-byte blocks, page count, a vector of page textures |
| +0x64 | 1 for a `GRGN` file |
| +0x7c | the vertices: 4 for each surface |
| +0x9c | the surfaces: one 0x20-byte record for each surface of the file |
| +0xbc | the cells: 0x54 bytes each, row by row |

**A surface record:**

| Offset | Field |
|-------:|-------|
| +0 | its four vertices |
| +8 | the texture (`TextureCache::GetTexture` of its name) |
| +0xc | the lightmap page's texture, or 0 without ISLIGHTMAP |
| +0x10 | the lightmap block |
| +0x14 | the file's colour |

**A vertex** is 0x20 bytes:

| Offset | Field |
|-------:|-------|
| +0 | position |
| +0xc | ARGB colour, lit |
| +0x10 | texture coordinates |
| +0x18 | lightmap coordinates |

**A cell record:**

| Offset | Field |
|-------:|-------|
| +0 | its four heights |
| +0x10 | its corners on the plane: `y` 0, `x` and `z` as below |
| +0x40 | the top's surface record, or 0 |
| +0x44 | the wall toward `x + 1` |
| +0x48 | the wall toward `z + 1` |
| +0x4c | `0xffffffff` once an east wall is placed, otherwise uninitialised |
| +0x50 | the top's lightmap colour, doubled and clamped to 255, white without one |

`Reset` (slot 2, `004bc9c0`) sizes the cells and the per-block lists, and leaves the
cells' surface records at 0.

## Build

`C3dGround15::Build` (slot 3, `004bcc30`) takes (ground, light vector, diffuse, ambient).

### The cells

1. **Reset** to the file's width and height, and take the cell size.
2. **With ISLIGHTMAP,** build the atlas (below).
3. **Size the surfaces and vertices** to the file's surface count.
4. **For each cell, row by row** (`GetCell` gives its heights `h0`–`h3` and its three
   surface indices):
   - **Place on the plane.** With `w` and `h` the size in cells and `s` the cell size,
     cell `(x, z)` spans:
     - `x0 = (x − w/2)·s` to `x1 = (x − w/2 + 1)·s`;
     - `z0` to `z1` likewise.

     `w/2` and `h/2` are C's integer division. Each product is an int converted to
     float, times `s`.
   - **The top,** when its index is above −1 and below the surface count:
     - **Vertices:** `(x0, h0, z0)`, `(x1, h1, z0)`, `(x0, h2, z1)`, `(x1, h3, z1)`.
     - **Two triangle normals,** from `(0, h0, 0)`, `(s, h1, 0)`, `(0, h2, s)` and
       `(s, h1, 0)`, `(0, h2, s)`, `(s, h3, s)`, each normalised.
     - **With ISLIGHTMAP,** the cell's lightmap colour: the block's colour doubled,
       at most 255.
   - **The wall toward `z + 1`,** when there is a next row and its index is valid.
     - **Vertices:** `(x0, h2, z1)`, `(x1, h3, z1)`, then the next row's cell's `h0` and
       `h1` at the same points.
     - **Normal:** `(0, 0, 1)` when that `h0` is at most `h2`, else `(0, 0, −1)`.
   - **The wall toward `x + 1`,** when there is a next column and its index is valid.
     - **Vertices:** `(x1, h3, z1)`, `(x1, h1, z0)`, then the next column's cell's `h2`
       at `z1` and `h0` at `z0`.
     - **Normal:** `(1, 0, 0)` when that `h0` is at most this cell's `h2`, else
       `(−1, 0, 0)`.
     - `+0x4c` becomes `0xffffffff`.

**Placing a surface:**
- copy the file's colour;
- the texture `TextureCache::GetTexture(names[texture])`, with the index
  sign-extended and not bounds-checked;
- corner `k`'s texture coordinates `(u[k], v[k])`.

**With ISLIGHTMAP**, also:
- the block of the lightmap index, an unsigned short and not bounds-checked;
- its page's texture;
- corner `k`'s lightmap coordinates from the block.

**A surface used by several cells** keeps the vertices of the last cell part that
placed it.

### The lighting

A second pass gives each vertex a colour.

**The top's corners.** Each takes a normal that sums the faces meeting there. A
neighbour contributes only where its height at that point is equal, as a float, to
this cell's:

| Corner | Starts from | Adds |
|-------:|-------------|------|
| 0 | the first normal | the cell at `z − 1`, both normals, if its `h2` = `h0`; at `x − 1`, both, if its `h1` = `h0`; at `(x − 1, z − 1)`, its second, if its `h3` = `h0` |
| 1 | both normals | at `x + 1`, its first, if its `h0` = `h1`; at `z − 1`, its second, if its `h3` = `h1`; at `(x + 1, z − 1)`, both, if its `h2` = `h1` |
| 2 | both normals | at `x − 1`, its second, if its `h3` = `h2`; at `z + 1`, its first, if its `h0` = `h2`; at `(x − 1, z + 1)`, both, if its `h1` = `h2` |
| 3 | **the first normal** | at `x + 1`, both, if its `h2` = `h3`; at `z + 1`, both, if its `h1` = `h3`; at `(x + 1, z + 1)`, its first, if its `h0` = `h3` |

- **Corner 3** belongs to the second triangle but starts from the first's normal.
- **A neighbour without a top** has normals that are never set. They come from fresh
  `operator new` memory; the port's are zeros.

**Then, for each corner:**
1. **Normalise:** `(x² + y²) + z²`, `1 / sqrt`.
2. **Intensity:** `(y·Ly + z·Lz) + x·Lx`, with `L` the light vector. 0 when it is at most
   0.
3. **Colour:** red, green and blue are each `ftol(byte × min(intensity · diffuse +
   ambient, 1))`, and alpha is `0xff`.

**Where the colour bytes come from.** Each grid point takes its colour from the top of
the cell whose corner 0 it is:
- **The top:** corner 0 from this cell, 1 from `x + 1`, 2 from `z + 1`, 3 from
  `(x + 1, z + 1)`, or 0 where that cell or top is missing.
- **The wall toward `z + 1`:** one intensity from its normal,
  `(ny·Ly + Lx·nx) + Lz·nz`. Its vertices 0 and 2 take the colour of the top at `z + 1`,
  1 and 3 that of `(x + 1, z + 1)`.
- **The wall toward `x + 1`:** the same with its normal. Vertices 0 and 2 take
  `(x + 1, z + 1)`'s colour, 1 and 3 that of `x + 1`.

**Precision.** Every operation is on the x87 at single precision, from `FPUSETUP`, in the
order the code takes them. `ftol` truncates.

### The lightmap atlas

`LightmapAtlas::Build` (`004bbcc0`) packs the file's 8 × 8 lightmaps into pages.

**Pages.** Each page is 256 × 256 ARGB texels and holds 1024 lightmaps.
- Lightmap `i` of a page goes to column `i % 32` and row `i / 32`, 8 texels each.
- Each texel is the alpha byte, then red, green and blue.

**Each block keeps:**
- **Corner coordinates,** inset by a texel:
  - `u` from `(column·8 + 1)/256` to `(column·8 + 7)/256`;
  - `v` likewise;
  - each clamped to [0, 1].
- **The colour** of texel (4, 4).
- **Its page's texture.**

**The page buffer** is one array on the stack, 256 KB.
- **Creation:** after each page, `C3dDevice::CreateTexture(256, 256, buffer, 1, 0)`
  makes an ARGB 4444 texture from it, divided by the texture divisor as any texture is
  ([device.md](device.md#textures)).
- **Reuse:** the buffer is not cleared between pages. A page that is not full keeps the
  previous page's texels past its last lightmap, and the first page keeps whatever was
  on the stack.

## In the port

`src/render/ground.{h,cpp}`:

| Port | Original |
|------|----------|
| `BuildLightmapAtlas` | the atlas; the pages hold the ARGB texels |
| `BuildGround` | `Build`: cells, surfaces with their vertices, and the atlas |
| `GroundMesh` | the cells and surfaces |

- **Textures** are indices into the file's names, not loaded.
- **The page textures** are the atlas pages' indices.
- **Precision:** the arithmetic follows the original's order with single-precision
  floats. The build sets `-ffp-contract=off` so no compiler fuses a multiply and an
  add.

**Where the port differs:**
- **Failures:** `BuildGround` fails on a texture index outside the names, or a lightmap
  index past the lightmaps, where the original reads outside its arrays.
- **Fresh memory is zero:** a first page that is not full is zero past its last
  lightmap, and the normals of a cell without a top are zero.

**The checks:**
- **Synthetic grounds** (`tests/unit/ground_test.cpp`). `tools/ground_vectors.py` runs
  the original `CGndRes::Load` and `Build` in the emulation oracle, and writes
  `tests/unit/ground_vectors.inc`.
  - **The set-up:** `TextureCache::GetTexture` and `C3dDevice::CreateTexture` are
    replaced, and the x87 is at single precision.
  - **The grounds:** five, from 1 × 1 to 5 × 3 cells. They have shared and differing
    corner heights, walls, reused and missing surfaces, and in one 1,030 lightmaps, so
    two pages.
  - **Each is built** with ISLIGHTMAP off and on.
  - **What the port matches:** every cell, surface, vertex (bit for bit, colours
    included) and block. It also matches every page's texels, except outside the
    blocks of a first page with room left.
- **Real maps** (`ground_oracle_sample` in `ctest`, when the data is present).
  `tools/ground_oracle.py` builds maps of `data.grf` both ways in the original and
  through `ragres build-grounds`, and compares hashes of the same values.
  - **In `ctest`:** a sample of 12.
  - **With `--sample 0`:** every map; all 208 match, both ways.
