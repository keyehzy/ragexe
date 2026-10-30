# Worlds: C3dWorldRes

`C3dWorldRes` (RTTI, 0x1c0 bytes) describes a map:
- which ground (`.gnd`, [gnd.md](gnd.md)) and attribute map (`.gat`, [gat.md](gat.md))
  make it;
- its water and its light;
- the models placed in it, its sound and effect sources;
- for version 2.1, the bounds of its scene graph.

It is registered for `rsw` with no folder ([resources.md](resources.md#types)).
`CWorld::LoadWorld` (`004c3ab0`) and `CWorld::CreateEffectAndSoundMakers` (`004c3890`)
use it.

## Members

| Offset | Field |
|-------:|-------|
| +0x110 | `list<record*>`: the models, 0x104-byte records |
| +0x11c | `list<record*>`: the effects, 0x74-byte records |
| +0x128 | `list<record*>`: the sounds, 0xc0-byte records |
| +0x134 | the root `SceneGraphNode` (0x80 bytes), version 2.1 only |
| +0x138 | `string`: the ground's name |
| +0x148 | `string`: the attribute map's name |
| +0x158 | `string`: the header's last name; nothing reads it |
| +0x168 | water level (float) |
| +0x16c | water type (int) |
| +0x170 | float, to `C3dGround` `+0x54` |
| +0x174 | float, truncated: degrees a water phase advances each frame |
| +0x178 | float, truncated, to `C3dGround` `+0x48` |
| +0x17c | frames per water texture (int) |
| +0x180 | light angle about y, degrees (int) |
| +0x184 | light angle about x, degrees (int) |
| +0x188 | light vector, 3 floats, computed by the loader |
| +0x194 | diffuse colour, 3 floats |
| +0x1a0 | ambient colour, 3 floats |
| +0x1ac | major version (byte) |
| +0x1ad | minor version (byte) |
| +0x1b0 | 4 ints |

### Where the fields go

**`CWorld::LoadWorld`:**
- **The attribute map:** fetched by the name at `+0x148`, and its cell size set to 5.
  - **An empty name** shows the CP949 message at `0060ba88` ("the attr file is not named
    in the rsw") and ends the load.
  - **A map that does not load** shows the one at `0060ba70` ("reading the attr file
    failed") and ends the load.
- **The ground:** fetched by the name at `+0x138`, then built into a `C3dGround15`.
  - **An empty name** shows the message at `0060ba44` ("the gnd file is not named in
    the rsw") and ends the load.
  - **An unversioned ground** (`CGndRes+0x110` clear) gets no `C3dGround15`: the load
    goes on with whatever ground `CWorld+0x38` already held.
- **The water:** `C3dGround::SetWaterParams(level, type, +0x17c, (int)+0x178,
  (int)+0x174, +0x170)` (`004bc410`), truncating with `__ftol`.
  - The water texture is `water<type % 6><frame>.jpg`, with `+0x17c` frames per
    texture (`C3dGround15::method_004bc450`).
  - That function also adds `+0x174` (mod 360) to a phase each frame.
- **The light:** `SceneRenderer::method_0040ef30` (`0040ef30`) fills a `D3DLIGHT7` and
  calls `IDirect3DDevice7::SetLight` and `LightEnable`:
  - a directional light, shining along the negated light vector;
  - `+0x194` as its diffuse colour and `+0x1a0` as its ambient, both with alpha 255.0.
- **The four ints at `+0x1b0`:** copied to the game mode's `+0x48`, `+0x4c`, `+0x40` and
  `+0x44`. In `data.grf` they look like whatever memory the writing tool had there.
- **Models:** see [Objects](#objects).

**`CWorld::CreateEffectAndSoundMakers`:**
- a `CEffectMaker` for each effect record;
- a `CSoundMaker` for each sound record, when the sound mode (`00605b10`) is on.

## File

All numbers little-endian.

**Version gates:** "v ≥ 1.x" below means a major version above 1, or major 1 with a
minor of at least x. Major 0 is below all of them.

| Field | Size | When |
|-------|-----:|------|
| `GRSW` (`006065d4`) | 4 | |
| major, minor | 1 + 1 | |
| a name, dropped | 40 | |
| the ground's name | 40 | |
| the attribute map's name | 40 | v ≥ 1.4 |
| the last name | 40 | |
| water level | 4 | v ≥ 1.3 |
| water type, `+0x170`, `+0x174`, `+0x178` | 16 | v ≥ 1.8 |
| frames per water texture | 4 | v ≥ 1.9 |
| light angles about y and x, diffuse, ambient | 32 | v ≥ 1.5 |
| a dword, dropped | 4 | v ≥ 1.7 |
| the four ints | 16 | v ≥ 1.6 |
| object count | 4 | |
| objects | | |
| scene graph bounds | 1365 × 48 | exactly 2.1 |

**Names** are C strings in 40-byte records.

**Defaults** for what an older version lacks:

| Field | Default |
|-------|---------|
| water type, `+0x170`, `+0x174`, `+0x178` | 0, 1.0, 2.0, 50.0 |
| frames per water texture | 3 |
| light angles | 45 and 45 |
| diffuse | (1, 1, 1) |
| ambient | (0.3, 0.3, 0.3) |
| the four ints | −500, 500, −500, 500 |

Below 1.3, the water level is left as the object held it. Below 1.4, the attribute map's
name is left as it was.

### Objects

Each object starts with a 4-byte type.

**Type 1, a model.** From 1.3 on:
1. a 40-byte name;
2. an int (`+0xec`), with no consumer found yet;
3. a float animation speed (`+0xf4`);
4. 4 bytes (`+0xf0`).

Then, for every version, 0xc4 bytes:

| Offset | Size | Field | Record |
|-------:|-----:|-------|-------:|
| 0 | 80 | the model's `.rsm` | +0x28 |
| 0x50 | 80 | a node of that model | +0x78 |
| 0xa0 | 12 | position | +0xc8 |
| 0xac | 12 | rotation in degrees about x, y and z | +0xd4 |
| 0xb8 | 12 | scale | +0xe0 |

The name goes to `+0` of the record.

How the loader treats these:
- **The animation speed** becomes 1.0 when it is above 100.0, or not above 0.0, NaN
  included (`fcomp` with `test ah, 0x41` at `0043647d`).
- **Below 1.3** the int is 0, the speed 1.0 and the 4 bytes 0.
- **Below 1.3 the name** is whatever the loader's 40-byte name buffer last held: the
  header's last name.

`CWorld::LoadWorld` builds a `ModelInstance` at once for each model within 200.0
(`005e8974`) of the player; the others go to a list that `LazyModelLoadThread` loads
later. The node name selects part of the model (`C3dModelRes::FindNode`, `0042cbd0`). It
copies:
- the position, rotation and scale, which `ModelInstance::method_004b98c0` turns into a
  matrix;
- the name, to `+0x2c`;
- the 4 bytes, to `+0x1f0`;
- the speed times 1/3 (`005e8968`), to `+0xb4`.

`ModelInstance::AdvanceAnimation` adds `+0xb4` × 100 to the animation time each step.

**Type 2:** 0x6c bytes, read and dropped.

**Type 3, a sound:**
- **Major 2:** 0xc0 bytes, kept as read.
- **Below major 2:** 0xbc bytes, converted:
  - two 80-byte names, each copied with `strncpy(…, 79)`, leaving the record's bytes
    0x4f and 0x9f as `operator new` left them;
  - then 28 bytes as read;
  - then the float 4.0 at 0xbc.

**Type 4, an effect:** 0x74 bytes, kept as read.

**Any other type:** "not implemented type == %d (%d/%d)" (`0060657c`) goes to the debug
log, and nothing more is read for that object.

### Scene graph

At 2.1 only, `SceneGraphNode::Subdivide` builds a tree of nodes, each with four
children, down to depth 5: 1,365 nodes. `SceneGraphNode::LoadBounds` (`004c67e0`) then
reads 12 floats for each node, depth first, a node before its children:

| Floats | Field |
|-------:|-------|
| 0..2 | the box's maximum, `+0x20` |
| 3..5 | its minimum, `+0x14` |
| 6..8 | its half extents, `+0x38` |
| 9..11 | its centre, `+0x2c` |

`CWorld::LoadWorld` keeps the tree in `CWorld+0xe8`.

### In data.grf

All 208 `.rsw` files in `data.grf` end exactly after their last field.

| Version | Files | Models | Type 2 | Sounds | Effects |
|---------|------:|-------:|-------:|-------:|--------:|
| 1.9 | 2 | 223 | 117 | 23 | 0 |
| 2.0 | 4 | 1,095 | 331 | 1 | 0 |
| 2.1 | 202 | 144,509 | 13,659 | 12,059 | 5,440 |

**Names:** every file names a `.gnd` and a `.gat`, and its first and last names are
empty.

## Load

`C3dWorldRes::Load` (slot 3, `00435dd0`) runs `Reset` first, then reads the file
through a stack `DataFile` without checking a single read.

**Refusals:**
- **Open fails:** fail, with no message.
- **Not `GRSW`:** show "Invalid Format" (`006065c4`) and fail. The check compares the 4
  bytes, NUL-terminated, with `strcmp`.
- **Version 2.2 and above, or major 3 and above:** show "C3dWorldRes :: Unsupported
  Version" (`006065a0`) and fail.

**Reading:**
1. **The names and values** in the order of the table above.
2. **The light vector**, after the light values:
   - `Matrix::MakeRotationX(x · π · 1/180)` (`004095d0`), then `Matrix::RotateY(y · π ·
     1/180)` (`00409770`);
   - π is the float at `005e2aa8` (3.1415920), 1/180 the float at `005e2690`;
   - each angle is an int, multiplied on the FPU and stored as a float. `LoadWorld` loads
     the world on the main thread, at single precision, so the product with π rounds to a
     float before the product with 1/180;
   - the vector is the sum of the matrix's second row and its translation row, so it is
     `(sin x · sin y, cos x, sin x · cos y)`.
3. **The objects.**
4. **At 2.1, the scene graph.** "SceneGraphNode Load." goes to the debug log.
5. **Without ISLIGHTMAP** (`0061ec60` not 1; it is 1 by default), each ambient
   component is multiplied by 1.5 (`005e2ad0`), and set to 1.0 when the product is
   above 1.0.
6. **Copy the diffuse and ambient colours** into `g_WorldColorFade` (`00686598`), close
   the file and succeed.

`Reset` (slot 4, `00435c90`):
- frees the model, effect and sound records and empties their lists;
- deletes the scene graph;
- keeps the names and the header values.

## In the port

`src/res/rsw.{h,cpp}`: `World`, `DecodeWorld`, `WorldLightVector` and `RswRes`.
- **Registration:** `RegisterWorldTypes` registers it, with the ISLIGHTMAP option as a
  parameter.
- **The colour fade** in `g_WorldColorFade` belongs to the renderer and is not ported
  here.
- **Records:** sound and effect records are kept as bytes until their makers are
  ported. The bytes a 1.x sound conversion leaves unset are 0.

**The light vector** is computed as the main thread computes it:
- each angle rounded after × π and again after × 1/180 (`00436231`);
- one sine and one cosine of each angle, rounded to float as the original stores them;
- the products of floats are exact in double, and each sum has only one non-zero term, so
  each is one product rounded once, as at single precision.

The angle's first rounding matters for some angles: `mjolnir_04_1`'s 25 and 35 degrees give
a light vector one float step apart at double precision.

**Where the port differs:**
- truncated files fail;
- so do names with no NUL inside their record, which the original copies on past it;
- a world below 1.3 gets a water level of 0.

**The oracle:** `tools/rsw_oracle.py` runs the original `Load` at single precision on every
`.rsw` in `data.grf`, and on 19 synthetic worlds, with ISLIGHTMAP on and off. It compares every
name, water and light value (the light vector bit for bit), model, sound and effect
record, and every scene-graph bound.

The synthetic worlds cover:
- every version from 0.9 to 2.1, and the refused ones;
- models below 1.3;
- 1.x sound records with a name too long for `strncpy`;
- unknown object types;
- animation speeds out of range, NaN included;
- light angles up to ±100,000 degrees.
