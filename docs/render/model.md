# Model instances

A world model is a `ModelInstance` (0x200 bytes) holding a tree of `C3dNode` (RTTI, 0x150
bytes), one for each node of its `.rsm` ([rsm.md](../formats/rsm.md)). The instance
places the model in the world. Each node keeps its keyframe matrix, its faces' colours
and its box, and draws its faces into the scene lists ([scene.md](scene.md)).

The port is `src/render/model`, with the matrices in `src/render/matrix`.

## Making an instance

`CWorld::LoadWorld` (`004c3ab0`) makes one for each `.rsw` model ([rsw.md](../formats/rsw.md#objects))
in seven steps:
1. **The constructor** (`004b9340`) sets:
   - scale (1, 1, 1);
   - time 0 and loop mode 2 (+0xb0);
   - the world matrix marked stale (+0xbc);
   - alpha 0xff (+0x1ed).
2. **The speed** (+0xb4) is the `.rsw`'s × 1/3 (`005e8968`), or 0 when it is 0.
3. **The model's volumes** are copied (`004b9550`); the port leaves them out.
4. **The position, rotation and scale** come from the `.rsw`.
5. **`Setup`** (`004b9820`):
   - the shade type (+0x24) and animation length (+0xb8) come from the model (its +0x190
     and +0x194);
   - the nodes are set up (`C3dNode::vf01`, `004b7580`) from the model's **first node in
     file order** down, children in the order the loader linked them;
   - their matrices are computed at time 0;
   - the box is computed (`004b98c0`).
6. **The name and the `.rsw`'s 4 bytes** go to +0x2c and +0x1f0.
7. **`Light`** (`004ba270`) lights it from the map's light.

**A node's set-up** (`vf01`) takes from its `ModelResNode`:
- **the alpha** (+0xa0): the model's alpha byte × 1/255 (`005e88c8`);
- **the textures** and **the mesh**;
- **the translation keys:** the node's own, or one at time 0 from its position;
- **the rotation keys:** the node's own, or one at time 0 from its axis and angle
  (`RotationKeys::AddFromAxisAngle`, `00401a70`: sin(angle/2) · axis, cos(angle/2));
- **the scale key:** one, from the node's scale.

## The node's matrix

`C3dNode::ComputeMatrixAtTime` (`004b6db0`) sets +0xb4 to scale · rotation ·
translation, each from its keys at the time:

| Keys | 0 | 1 | more |
|------|---|---|------|
| translation (`00401520`) | identity | that key | linear between the keys either side (`LerpTranslationKey`, `00401000`) |
| rotation (`00401d10`) | identity | `MakeRotationFromKey` | spherical (`SlerpRotationKey`, `00401050`), or identity when both keys have w exactly 1.0 |
| scale (`00402070`) | identity | `MakeScale` | — |

- **Finding the pair:** the keys either side are the first key whose time is after the
  given time, and the one before it. The fraction is (time − earlier) / (later − earlier).
- **Past the last key** the original reads one key beyond the vector. The port holds the
  last key.
- **The slerp:**
  - The dot product picks the shorter arc.
  - Within 1e-6 (`005e25a0`) of 1 the interpolation is linear.
  - Otherwise it takes the angle from `_CIacos` (`005c1770`), which computes
    atan2(√((1 + x)(1 − x)), x). The CRT keeps the x87's precision, so those operands round
    to single precision.

**`AdvanceAnimation`** (`004b96a0`) runs for each visible model every frame (`CView::vf03`):
- it recomputes the matrices at the current time;
- it moves the time (+0xac) on by ⌊speed × 100 + time⌋;
- **mode 2** wraps the time by the length (a length of 0 becomes 1);
- **mode 1** keeps the old time once the new one reaches the length.

## The box

`ModelInstance` `004b98c0` runs in four steps:
1. **The model's box:** every node's vertices go through their matrices from the root
   (`C3dNode::ExpandBoundingBox`, `004b7940`), starting from ±100,000.
2. **The centring** (+0x120): −(x and z midpoints), and −(the largest y). With y down,
   that stands the model on the top of its box, centred on x and z.
3. **The world matrix** (+0xc0), as `004b95f0` makes it:
   1. `MakeTranslation(centring)`;
   2. `Scale`;
   3. `RotateY`, then `RotateX`, then `RotateZ`, each of π (`005e8818`) · degrees ·
      1/180;
   4. `Translate(position)`.

   `004b95f0` also stores the inverse at +0xf0 and clears +0xbc. The draws and the
   lighting call it whenever +0xbc is set.
4. **What it derives:**

   | Offset | Field |
   |-------:|-------|
   | +0x128 of each node | its vertices' box through the world matrix (`C3dNode::UpdateBoundingBox`, `004b7a80`) |
   | +0x16c | the box's eight corners through the world matrix |
   | +0x1cc | their minimum and maximum |
   | +0x130 | the oriented box's half extents: \|size × scale\| × 0.5 |
   | +0x13c | the oriented box's centre: the midpoint of the box's smallest and largest corners through the world matrix |
   | +0x148 | its three axes: the rows of scale · rotations, normalised |

   `ViewFrustum::ClassifyOrientedBox` reads the oriented box (+0x130..+0x168).

## Lighting

`Light` (`004ba270`) calls `C3dNode::vf02` (`004b7e30`) on the root. Each node takes its
matrix · its parent's, with the translation cleared, and fills one 0x24-byte record a
face (+0x118): three ARGB colours, then the three corners' texture coordinates.

**By shade type:**
- **0:** white.
- **1:** the face normal, transformed and normalised, lights the whole face.
- **2:** each corner's normal lights that corner.

**A lit channel** is (max(n · light, 0) × diffuse + ambient), at most 1, × 255. Where the
texture coordinate's colour field (+0) is not zero, it is × 128 instead, which halves it.
**The alpha** is the node's alpha × 255. Every byte is `__ftol`'s truncation.

**The ground tint.** `C3dNode` `004b8510` adds, at each corner, the colour of the ground
under it to the face's colour, clamped to 255 (+0x144, 9 ints a face).
- **The corner** is placed by the node's own matrix plus the instance's position. That
  leaves out the parents, the rotation and the scale.
- **The colour** comes from `C3dGround15::GetCellColor` (`004c03b0`): the lightmap colour
  of the cell at ⌊x / cell size⌋ + width/2, ⌊z / cell size⌋ + height/2, white outside
  ([ground.md](ground.md)).

## Drawing

`ModelInstance::Render` (`004b9740`) draws through world · view, with a winding flip
when an odd number of the scales are negative. `C3dNode::vf00` (`004b6e50`) handles each
node:
1. **Projection:** each vertex goes through the node's matrix · its parent's into
   `00662870`, as the ground's are ([ground.md](ground.md#drawing)):
   - z is at least 10;
   - x and y are the scale × the coordinate × rhw, plus the centre;
   - z is ((1/rhw − 10) · 1500/1490) · rhw.
2. **Each face,** in runs of one texture (the faces are sorted by texture):
   - **With clipping** (`Render`'s second argument), it is kept only when its screen box
     meets the viewport.
   - **With `00688a74` set** (`LoadVideoSettings` copies it from `006884f8`), the box must
     also lie within the renderer's rectangle at +0x58 to +0x64. The port leaves that
     check out.
   - **Facing:** it is kept when it faces the view, or either way when the face's
     `field_10` (the sort's leftover bytes) is not zero.
   - **Its corners** take the record's texture coordinates and a colour.
3. **The colour,** the first that applies:

   | Condition | Colour |
   |-----------|--------|
   | `g_groundWaterFade` (`00679f9c`) | the record's colour × `g_WorldColorFade`'s first multipliers |
   | the node is translucent (+0x124, set by `SetTranslucentIfRayHits`) | the record's colour, alpha 0x80 |
   | `g_WorldColorFade`+0xd | the ground tint × the second multipliers |
   | otherwise | the record's colour × the second multipliers |

   The alpha is the instance's (+0x1ed).
4. **Each run** goes to `AddPrimitive` as a triangle list:
   - **its texture** is the node's texture at (index mod count);
   - **flags 0** (opaque) when the alpha is 0xff and the texture is not ARGB 4444 (its
     format, +0x10, is not 1), **flags 1** (translucent) otherwise.
5. **Afterwards** the node's translucency is cleared, and its children are drawn.

**The instance's alpha** (+0x1ed) then steps by 5: down to 0x5a while +0x1ec is set (it
is then cleared), otherwise up to 0xff.

## Loading

`CWorld::LoadWorld` does not place the `.rsw`'s models in the file's order. It splits
them by their distance from the player:
1. **The player's point** is the centre of the cell the player enters on
   (`ClientState`+0x620 and +0x624, `00686c90`), from `CGameMode::GetCellCentre`
   (`00511de0`): `(cell − side × 0.5) × cell size + cell size × 0.5`, each step in single
   precision. `LoadWorld` sets the attribute map's cell size to 5 first. The point's height,
   `0067bf44`, is 0: nothing writes it after its static initializer.
2. **The distance** to each model is `sqrt((dx² + dy²) + dz²)`, in single precision.
3. **A model within 200** (`005e8974`) is placed at once, in the file's order. So is one
   whose distance is NaN, since the test is `fcom` and C0 or C3.
4. **The others** go into a vector of (distance, record) pairs (`0067bf50`).
5. `LoadWorld` then builds the scene graph from the models it has, makes the effects'
   and sounds' makers, and starts `LazyModelLoadThread` (`004c33a0`). If `CreateThread`
   fails, it runs the function itself and joins it.
6. **The thread** sorts the pairs (`004c7410`: by the distance, then by the record's
   address) and places each model in turn. `CWorld::AddLoadedModel` (`004c5030`) appends
   it to +0x48, and `CWorld::UpdateGameObjects` (`004c4a10`) puts the new ones into the
   scene graph each frame, in that order.

**A model is skipped** in either loop when its `.rsm` does not load, or when the named
node is not found and the model's first node entry is empty.

**The final order** of +0x48, and of the models in the scene graph's leaves, is therefore
the near models in the file's order, then the far ones from the nearest. The records are
allocated one after another as the world resource reads them, so their addresses follow
the file's order, and the port breaks ties by it.

## The loader's precision

The model's vertices are baked at load ([rsm.md](../formats/rsm.md)), and the loader runs
on two threads:
- **The main thread** loads the models within 200 of the player ([loading](#loading))
  after DirectDraw's `FPUSETUP`, so its x87 rounds them to single precision.
- **`LazyModelLoadThread`** loads the rest at the CRT's default, double precision. A new
  thread starts at the default control word, so it also places and lights them (`Setup`,
  `Light`) at double precision.

The port's loader does either ([rsm.md](../formats/rsm.md#the-loaders-precision)). A
file is loaded once, by the first model that names it, so it loads at single precision when
any near model names it. ragview loads each file so.

**Placement and lighting at double precision.** Under the double control word the x87 keeps
53 bits for whatever stays on its stack, and rounds to single precision only where a value
is stored as a float. The port's matrix functions, `CreateModelInstance`, `LightModel` and
`GroundCellColor` take the precision, and keep doubles exactly where the original keeps
values on the stack:
- **Each matrix element** is one expression, rounded once when it is stored. In
  `MakeRotationFromKey` (`00409ae0`), 2x·x, 2y·x and 2z·z stay on the stack, and z·2z is
  also stored and reloaded for the second diagonal element (`00409b4d`). In `Invert`
  (`00409ed0`) the first cofactor stays on the stack while 1/det is stored (`00409f14`).
- **Angles:** π × degrees × 1/180 is rounded once, when pushed as the argument.
- **Keys:** the fraction is an `fidiv` stored as a float. The translation's 1 − t stays on the
  stack (`00401006`). In the slerp, b's weight sin(φt)/sin ω stays on the stack while a's is
  stored (`00401108`); on the linear path both stay. `_CIacos`'s square root and its
  operands are double.
- **The box** (`004b98c0`): the centre of the box's two x corners, which are summed
  unrounded (`004b9c4a`), and the z half extent, whose
  difference and product stay on the stack (`004b9ca2`). Each axis's 1/√ stays on the stack.
- **The lighting** (`004b7e30`): each normal's components are stored, its length's
  reciprocal is not. For the face normal (shade type 1) z·(1/length) stays on the stack
  for the dot product (`004b8212`). Red and green are stored before their scaling to bytes;
  blue is scaled from the stack, as is the alpha (`004b8091`).
- **The ground's colour** (`004b8510`): a corner's x is added to the instance's x on the
  stack. `GetCellCoords` (`004bc3b0`) stores x / size as a double for `floor`. At single precision
  the quotient of a point just below a cell edge can round up onto the edge; at double it
  stays in the cell below.

`LoadWorldScene` places and lights a model at single precision when it is near the
player, as the main thread does, and at double precision otherwise.

**NaNs.** A model scaled by 0 on an axis has a determinant of 0, so `Invert` multiplies 0 by
∞ and its axes divide 0 by 0. The x87 gives the negative "real indefinite" for an invalid
operation, where ARM gives a positive NaN, and of two NaN operands it keeps the one with the
larger significand, the positive one when they tie, where ARM and SSE keep the first. The
port computes on `core::X87` values, which follow the x87's rules
(`tests/unit/x87_test.cpp` checks them against the emulated x87).

## Checks

`tools/model_vectors.py` writes the original's results in the emulation oracle to
`tests/unit/model_vectors.inc`. The models are five synthetic ones, one per version, and
five from `data.grf`, small ones that cover:
- the three shade types;
- hierarchies of up to three nodes;
- rotation keys.

Each is loaded at double precision, then placed, set up and lit at single precision.

**What is recorded:**
- the instance's fields +0xc0 to +0x1e4;
- each node's alpha, matrix and box, its face records and its ground tint;
- every primitive `Render` hands `AddPrimitive`, under:
  - three views, clipped and not;
  - the fade's other set;
  - the ground tint;
  - translucency;
- the node matrices after three animation steps;
- 200 calls to `SlerpRotationKey`, fractions inside and outside 0 to 1.

**At double precision,** as `LazyModelLoadThread` runs them:
- the same 200 calls to `SlerpRotationKey`;
- each model placed twice more, at other rotations and scales, and lit: the instance's
  fields and its nodes, as above;
- 400 calls to `GetCellCoords` under both control words, on points at and a few float steps
  either side of cell edges; 14 fall in different cells at the two precisions;
- each model with a scale of 0 on some axis, at double and at single precision.

`GetCellColor` is replaced by a function of the point's bits: seven bits from the fifth
bit up or higher for the placements at single precision, the last six bits for those at
double, so that there a point a float step away gives another colour. `GetTexture` gives each
texture name a stand-in whose format is 4444 for a name of odd length.

The port matches every value bit for bit (`tests/unit/model_test.cpp`).

**The matrices.** `tools/matrix_vectors.py` runs these at both precisions, and
`tests/unit/matrix_test.cpp` matches every value:
- `Multiply` and `Invert` of 300 random matrices, some with a zero row;
- `MakeRotationFromKey` of 300 quaternions, unit and not;
- `TranslationKeys::GetMatrixAt` and `RotationKeys::GetMatrixAt` on 200 tracks each, of two
  to four keys, at times before, on and between the keys;
- `SqrtFloat` of zeros, infinities, NaNs and negatives, and `SinFloat` and `CosFloat` of
  finite angles. The emulator's `fsin` and `fcos` leave an infinity as it is, where the x87
  gives the real indefinite, so those are not compared.

**The x87's arithmetic.** `tools/x87_vectors.py` runs `fadd`, `fsub`, `fmul`, `fdiv` and
`fsqrt` in the emulator, floats at single precision and doubles at double, on every pair of
zeros, ones, infinities, NaNs of either sign with assorted payloads, signalling NaNs and large
numbers. `core::X87` matches every result (`tests/unit/x87_test.cpp`).

**The order.** `tools/world_vectors.py` runs `LoadWorld` itself on 150 synthetic worlds
and writes `tests/unit/world_vectors.inc`. `CreateThread` answers 0, so the lazy loader runs
in `LoadWorld`, and the sort, `AddLoadedModel` and the cell's centre are the original's.
- **The resources, the map's name and `FindNode`** are stood in for, and so is whatever
  places, lights and draws.
- **The worlds** have up to 40 models around the player's cell, on attribute maps of
  assorted sizes, with cells off the map and cells past 2²⁴. Among the models:
  - some exactly 200 away, some a float step either side of it, and some with a NaN position;
  - some at another's point;
  - points where summing the squares in another order would cross 200;
  - some with no `.rsm` or no node.
- **It records** the records the instances at +0x48 come from, in order, and the cell's
  centre.

`tests/unit/world_load_test.cpp` matches all 2,870 values with `CellCentre` and
`ModelLoadOrder` (`src/render/world_load`). ragview places the models in that order from
where its player stands.

**Real maps.** `tools/world_oracle.py` runs `LoadWorld` itself on maps from the install,
then `LazyModelLoadThread` to its end, then `CWorld::UpdateGameObjects`. It compares them
with `ragres load-world`, which loads the port's world with `LoadWorldScene`
(`src/render/world_scene`), the player in the centre of the attribute map both times. Both
read `data.grf` with the `.gpf` patches over it, in name order, as the patcher merges them.
- **Real in the oracle:** the resource manager, with its types registered by their static
  initializers; every loader; the ground's build; the models' placement and lighting; the
  scene graph. The x87 runs at single precision, and at double in `LazyModelLoadThread`, as
  on its own thread.
- **Stood in for:** the textures, the effects' and sounds' makers, the player, the UI, and
  Direct3D's lights.
- **Compared:** each model of +0x48 (its `.rsw` record, its fields +0xc0 to +0x1e4, its
  nodes' alpha, matrix, box, face colours and ground colours), and each scene graph node's
  box, cells and models.

**On all 208 maps** every model's record, in order, every model's fields and nodes, near and
far, and every scene graph node are equal. Run with `--sample 0`, the comparison takes about
two hours.

In the oracle `LazyModelLoadThread` runs to its end before `UpdateGameObjects`. In the
client the two threads overlap, so the main thread may insert the far models over several
frames, with the world drawn in between.

**Not ported yet:** the volumes and their matrices.

**`SetTranslucentIfRayHits` never fires** in this client: it needs the instance's +0x1e8,
which nothing sets ([scene_graph.md](scene_graph.md#picking)). The node's flag it would set
(+0x124) is ported for the draw.

Which models are drawn, and whether clipped, is the scene graph's
([scene_graph.md](scene_graph.md)).
