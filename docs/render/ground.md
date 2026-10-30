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
| +0x4c | the frame the cell was last drawn in (`DrawCells`); `Build` sets `0xffffffff` where it places an east wall, and leaves it uninitialised elsewhere |
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

## Drawing

**Who draws it.** Each frame, `CView`'s scene (`004cf4d0`) walks the scene-graph nodes
the frustum left visible. For each, it calls the ground's `DrawCells` (slot 4,
`004be500`) with the view matrix and the node's cell rectangle.
- **Partly visible nodes** pass `cull` 1.
- **Wholly visible nodes** pass `cull` 0.

Slot 5 (`004befa0`) walks cells the same way through `004bf070`, but the view does not
call it.

**The view matrix** is 4 × 3, for row vectors: `x' = x·m0 + y·m3 + z·m6 + m9`, and
likewise for `y'` and `z'`.

**The renderer's fields:**

| Field | Offset |
|-------|--------|
| scale x | +0 |
| scale y | +4 |
| centre | +0x1c, +0x20 |
| size | +0x24, +0x28 |
| a second centre | +0x2c, +0x30 |
| frame count | +0x34 |

`SetSize` sets both centres to half the size.

### DrawCells

`DrawCells(view, rect, cull)` takes the cells from `rect`'s `(x0, z0)` to `(x1, z1)`
inclusive, row by row.
- **Skip** a cell whose `+0x4c` already holds the frame count. Otherwise, store the
  count there.
- **With a top:**
  - its water, below;
  - then `DrawSurface` of the top.
- **Then the walls:** `DrawSurface` of the wall toward `z + 1`, then toward `x + 1`.

### The water

**Its parameters.** `CWorld::LoadWorld` passes the `.rsw`'s water to
`C3dGround::SetWaterParams` (`004bc410`):

| Field | Value |
|------:|-------|
| +0x40 | the level |
| +0x50 | the type |
| +0x44 | the interval: steps each texture of the animation lasts |
| +0x48 | the step, degrees per cell: `ftol` of the `.rsw`'s float |
| +0x4c | the phase step: `ftol` of its float, then C's remainder by 360 |
| +0x54 | the wave height |

The constructor sets the defaults 3, 50, 2 and 1.0. `Reset` zeroes the frame (+0x5c)
and the phase (+0x60).

**Each frame step.** `CGameMode::OnFrame` calls `C3dGround15` `004bc450`, also called
from `GetWaterTexture` (`004bc4e0`) while no texture is set. It runs in three steps:
1. The frame moves on by one, back to 0 at interval × 32.
2. **The texture:** `TextureCache::GetTexture` of `워터\water%d%02d.jpg` (`0060b0c0`,
   CP949), with the type modulo 6 and (frame / interval) modulo 32. C's remainder leaves
   a negative type negative.
3. **The phase** moves by the phase step, less 360 once past 180.

An interval of 0 would divide by zero; the port picks texture 0.

**The wave heights.** With the wave height (`+0x54`) at 0.0, all four are the level
(`+0x40`). Otherwise each height is

```
SinDegrees(n · step + phase) · height + level
```

with `step` at +0x48 and `phase` at +0x60. `n` is `x + z` for corners 1 and 2,
`x − 1 + z` for corner 0, and `x + 1 + z` for corner 3. `n · step + phase` is an int,
converted to float and back with `ftol`.

**When it is drawn.** Only when some vertex of the top lies below the level at `x + z`:
in the y-down world, its `y` is greater.

**The corners.** The heights go into the cell's corner records (`+0x14`, `+0x20`,
`+0x2c`, `+0x38`) and stay there.

**The projection.** The four corners go through the view:
- `x' = ((y·m3 + m6·z) + m0·x) + m9`, and likewise for `y'` and `z'`;
- `rhw = 1 / max(z', 1)`, where NaN also becomes 1;
- `sx = (rhw·scale_x)·x' + centre_x`;
- `sy = (y'·rhw)·scale_y + centre_y`;
- `sz = ((1/rhw − 10)·(1500/1490))·rhw`, with near 10 (`005e88f8`) and far 1500
  (`005e88f4`).

**Facing.** The quad is drawn only when it faces the view:
`(v1.x − v0.x)·(v2.y − v0.y) < (v2.x − v0.x)·(v1.y − v0.y)`.

**The quad.** It comes from the renderer's pool (`00412b40`). It keeps the pool quad's
specular `0xff000000` and type `TRIANGLESTRIP`.
- **Colour:**
  - **With `g_groundWaterFade`** (`00679f9c`): `0x90` alpha and `ftol(255·f)` of
    `g_WorldColorFade`'s multipliers at +0x2c.
  - **Water type 4:** the same with alpha `0xff`.
  - **Otherwise:** `0x90ffffff`.
- **Texture coordinates:** from `(x % 4, z % 4)` and one more, times
  `g_waterTextureScale` (0.25).
- **Texture:** `GetWaterTexture` (`004bc4e0`).
- **List:** `AddPrimitive` with flag 1, the alpha list.

### DrawSurface

`DrawSurface(view, surface, cull)` (`004bea30`).

**The projection** of the surface's four vertices:
- `x' = ((z·m6 + m0·x) + y·m3) + m9`, and likewise;
- `sx = (rhw·scale_x)·x' + centre2_x`;
- `sy = (rhw·scale_y)·y' + centre2_y`;
- `sz` as for the water.

**Culling.** With `cull`, the surface is left out when all its `x` are at or past the
width, or all below 0, and likewise for `y`. The comparisons take each pair's smaller
value as `b < a ? b : a`.

**Colour.** Each vertex colour's red, green and blue are multiplied by
`g_WorldColorFade`'s multipliers:
- the set at +0x38 (`ftol(byte·f)`);
- or, with `g_groundWaterFade`, the set at +0x2c.

The record's +0x18 would choose an alpha of `0x80` instead, but `Build` leaves it 0.

**The quad:**
- the specular of `-2¹²⁷` written as the vertex template's initial value, bits
  `0xff000000`;
- the texture coordinates;
- the lightmap coordinates as the second set;
- the surface's texture and lightmap page;
- `AddPrimitive` with flag `0x400`, the ground list.

### SinDegrees

`SinDegrees` and `CosDegrees` (`0040b3c0`, `0040b400`) read the tables `InitAngleTables`
(`0040b2f0`) fills in `WinMain`.
- **The tables** (`g_sinTable` `006247b4`, `g_cosTable` `00623c6c`) hold 0 to 360
  degrees. Each entry is `k · π · (1/180)` in single precision, the sine or cosine
  rounded to float.
- **Reduction:** angles past ±360 are reduced with C's remainder.
- **Negative angles:** the lookup starts just after entry 360, so `−k` reads entry
  `361 − k`, one degree past `360 − k`.

## The cell cursor

The green square under the mouse marks the attribute cell (`.gat`, half a ground cell) the
player would walk to.

**Choosing the cell.** Each input step `CGameMode::method_005159d0` picks the cell under the
mouse ([scene_graph.md](scene_graph.md#picking)). With the option `g_ClientState`+0x184c on
(the client's state constructor sets it, `00587258`, and a key toggles it), it keeps:
- the cell (`00684e20`, `00684e24`);
- whether the cursor is hidden (`00684e28`);
- its colour (`00684e2c`).

Before the first pick it is hidden, in `0xaa55ffff` (`static_init_166`, `0050c240`). The
cell's attribute comes from `CGameMode::GetCellAttribute` (`005121d0`), which gives 1 off
the map:
- **1:** hidden, in `0xfeff0000` (red);
- **5:** hidden, in `0xfeffff00` (yellow);
- **anything else:** shown, in `0x6600ffaa`.

So the cursor appears only over cells the player can walk on. A failed pick leaves it as it
was.

**Drawing it.** `CView::DrawWorld` (`004cf73d`), after the visible nodes, calls the ground's
slot 15 (`C3dGround15::DrawCellCursor`, `004c0240`) with the view matrix, the cell and the
colour.
1. **The ground cell** under it is (x / 2, z / 2). Nothing is drawn when that cell is off the
   map or has no top.
2. **It loads `grid.tga`** and calls `DrawCellCursorQuad` (`004bc570`).
3. **The corners** come from slot 16 (`GetCellCursorCorners`, `004c02d0`): the top
   surface's four vertex positions. A surface that several cells share holds the corners of
   the last of them `Build` placed, so the cursor follows that cell.
   `C3dGround::SetAttrHeights` (`004bc500`) then puts the attribute cell's four heights in
   their y.
4. **The quarter:** each corner's x moves halfway to corner 1's x when x is odd, and halfway
   to corner 0's when even; z the same with corners 2 and 0.
5. **Projection** is as the surfaces', with its own orders:
   - x = ((y · m₃ + m₀ · x) + z · m₆) + m₉, y and z alike;
   - z kept at least 1;
   - the screen x = (rhw · scale x) · x + origin x, and the screen y = (rhw · y) · scale y
     + origin y.
6. **A little nearer:** the rhw is 0.00002 (`005e8944`) more, and z is taken from it.
7. **The quad** has the colour, the specular `0xff000000`, and the coordinates (0, 0),
   (1, 0), (0, 1), (1, 1) (`00679f78`, which `static_init_093` fills). It is drawn with
   blends 5 and 6 (source alpha) and goes on the translucent list (flags 1).

## In the port

`src/render/ground.{h,cpp}`:

| Port | Original |
|------|----------|
| `BuildLightmapAtlas` | the atlas; the pages hold the ARGB texels |
| `BuildGround` | `Build`: cells, surfaces with their vertices, and the atlas |
| `GroundMesh` | the cells and surfaces, with their corner records and frame stamps |
| `DrawGroundCells` | `DrawCells` with `DrawSurface` and the water; `GroundPrimitive`s for `AddPrimitive` |
| `PlaceCellCursor`, `DrawCellCursor` | the cell cursor's choice and its quad (`CellCursor`) |
| `core::SinDegrees`, `CosDegrees` | the angle tables, `src/core/angles` |

- **Textures** are indices into the file's names, not loaded.
- **The page textures** are the atlas pages' indices.
- **Precision:** the arithmetic follows the original's order with single-precision
  floats. The build sets `-ffp-contract=off` so no compiler fuses a multiply and an
  add.

**Where the port differs:**
- **Frame stamps** start at `0xffffffff`; the original's are uninitialised except where an
  east wall is placed.
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
- **Drawing** (`tests/unit/ground_draw_test.cpp`). `tools/ground_draw_vectors.py`
  draws synthetic grounds with the original and writes
  `tests/unit/ground_draw_vectors.inc`.
  - **The set-up:**
    - the original builder;
    - `C3dDevice::Init` and a `SceneRenderer` sized 800 × 600;
    - `InitAngleTables` and the water texture scale, which `WinMain` and a static
      initializer set.
  - **Five cases.** They vary the view, culling, the fade flag, ISLIGHTMAP, and water:
    none, flat, waves, and type 4.
  - **What the port matches,** bit for bit:
    - every primitive added, in order: positions, rhw, colours, coordinates, lightmap
      coordinates, texture and page;
    - every cell's frame stamp and water heights;
    - the angle tables.
  - **The cell cursor:**
    - 180 calls to slot 15 over a 12 × 10 attribute map, in and around it, under the five
      views and two rolled ones. Each call's quad matches bit for bit.
    - `GetCellAttribute` for every cell and a ring off the map.
- **Real maps** (`ground_oracle_sample` in `ctest`, when the data is present).
  `tools/ground_oracle.py` builds maps of `data.grf` both ways in the original and
  through `ragres build-grounds`, and compares hashes of the same values.
  - **In `ctest`:** a sample of 12.
  - **With `--sample 0`:** every map; all 208 match, both ways.
