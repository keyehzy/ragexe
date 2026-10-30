# The scene lists

During a frame, the world and its effects hand `SceneRenderer` their primitives. When
the view's scene ends, `DrawScene` (`0040efd0`, from `CView` slot 3) draws them all in a
fixed order of passes and empties the lists. The device's initial state is in
[device.md](device.md), the per-primitive texture stage modes in
[texture-stages.md](texture-stages.md).

## RenderPrimitive

0x24 bytes (`RenderPrimitive::RenderPrimitive` `0040c1b0`):

| Offset | Field |
|-------:|-------|
| +0 | the vertices: 32 bytes each, `D3DFVF_XYZRHW \| DIFFUSE \| SPECULAR \| TEX1` (`0x1c4`) |
| +4 | a second array of texture coordinates, 8 bytes a vertex |
| +8 | the vertex count |
| +0xc | the texture (`CTexture`) |
| +0x10 | the second texture, the ground's lightmap |
| +0x14, +0x18 | `SRCBLEND` and `DESTBLEND`, for the stage-mode list |
| +0x1c | the texture stage mode, for the stage-mode list |
| +0x20 | the `D3DPRIMITIVETYPE`, `TRIANGLELIST` unless set |

**The three draws** do nothing for a primitive of no vertices. Each first multiplies the
texture coordinates by the texture's uv scale, in place, so a primitive drawn twice is
scaled twice (`ScaleTexCoordsByTexture` `0040c280`).

| Draw | Address | What it sends |
|------|---------|---------------|
| `Draw` | `0040c3a0` | `DrawPrimitive` of the vertices |
| `DrawWithTexCoordArray` | `0040c590` | the same, after copying the second coordinates over the first |
| `DrawTwoTexCoords` | `0040c3e0` | a copy with both sets of coordinates, `0x2c4`, in one static buffer (`00625e50`) |

## AddPrimitive

`SceneRenderer::AddPrimitive(primitive, flags)` (`00412550`) puts a primitive in one list.

**Fog.** When the renderer's +0x4c is set, it first writes each vertex's fog factor into
the specular colour's alpha (`ComputeVertexFog` `0040c2e0`; [fog.md](fog.md#each-vertex)).

**Opaque**, without flag 1:

| Flags | List |
|-------|------|
| 8 | +0x164 |
| 0x400 | +0xa4, the ground |
| 0x800 | +0xb4 |
| none of them | +0x94 |

**Translucent**, flag 1:

| Flags | List | Key |
|-------|------|-----|
| 4 | +0x184, in the order added | |
| 0x100 | +0xc4 | midpoint |
| 0x200 | +0xe4 | midpoint |
| neither | +0xd4 | `rhw0` |
| 2 \| 0x100 | +0xf4 | midpoint |
| 2 \| 0x200 | +0x114 | midpoint |
| 2 | +0x104 | midpoint |

**Earlier flags win:** 8 over 0x400, 4 over the rest, 0x100 over 0x200.

**The depth key.** The translucent lists hold (key, primitive) pairs:
- **`rhw0`:** the first vertex's `rhw`.
- **Midpoint:** `(rhw0 − rhw1) · 0.5 + rhw1`, from the first two vertices, rounded to
  single precision by the x87 that `FPUSETUP` set up.

A larger `rhw` is nearer, so ascending keys draw back to front.

**Where these lists are drawn:**
- **+0xb4 and +0x164:** `DrawScene` never draws them, and empties them with the rest.
- **Two more adders:**
  - `004128e0` puts vertex-buffer ranges (an `IDirect3DVertexBuffer7` range, with
    textures at +0x44 and +0x48) in +0x124, +0x134, +0x144 or +0x154;
  - `00412720` puts a third kind in +0x174.

  Each of these lists has a pass below.

## DrawScene

`0040efd0`. After setting up the projection and fog (`00411090`, when the renderer's
+0x48 is set; [fog.md](fog.md#the-devices-states)), it makes these calls:

1. **Stage states:** stage 0 `MIPFILTER` `LINEAR`, `COLOROP` and `ALPHAOP` `MODULATE`;
   stage 1 `ALPHAOP` `DISABLE`.
2. **Render states:**
   - `FOGENABLE` the renderer's +0x44;
   - `ALPHAREF` `0xcf`;
   - `ALPHATESTENABLE` 1;
   - `ALPHABLENDENABLE` 0;
   - `ZENABLE` and `ZWRITEENABLE` 1.
3. **+0x94,** by texture (below).
4. **+0x124,** by texture.
5. **`AMBIENT` `0xffffffff`, `CULLMODE` `CW`. The ground, +0xa4:**
   - **Without `ISLIGHTMAP`:** by texture, with `Draw`.
   - **With it, without two-stage blending:**
     1. As above.
     2. Stage 0 `COLOROP` and `ALPHAOP` `SELECTARG1`; `ALPHABLENDENABLE` 1,
        `ALPHAREF` 1, blend `ONE` / `SRCALPHA`.
     3. The list again, sorted by lightmap alone, with the lightmap as the texture,
        drawn with `DrawWithTexCoordArray`.
     4. Stage 0 back to `MODULATE`.
   - **With both:**
     1. Stage 1 `COLOROP` `MODULATEALPHA_ADDCOLOR`.
     2. The list by texture, with the lightmap on stage 1, drawn with
        `DrawTwoTexCoords`.
     3. +0x134, by its first texture, with the second on stage 1.
     4. Stage 1 `DISABLE`.
6. **`AMBIENT` 0, `CULLMODE` `NONE`. +0x174,** by its elements' addresses, as
   `TRIANGLELIST`s.
7. **`ALPHAREF` 1, `ALPHABLENDENABLE` 1. Then the translucent lists, each by key:**

   | Blend | `ZWRITEENABLE` | `ZENABLE` | `FOGENABLE` | Lists, in order |
   |-------|---------------:|----------:|------------:|-----------------|
   | `SRCALPHA` / `INVSRCALPHA` | 1 | 1 | +0x44 | +0xc4 |
   | `ONE` / `ONE` | 1 | 1 | +0x44 | +0xf4 |
   | `SRCALPHA` / `INVSRCALPHA` | 0 | 1 | +0x44 | +0xd4, then +0x144 by texture |
   | `ONE` / `ONE` | 0 | 1 | 0 | +0x154 by texture, +0x104, then +0x184 |
   | `SRCALPHA` / `INVSRCALPHA` | 0 | 0 | 0 | +0xe4 |
   | `ONE` / `ONE` | 0 | 0 | 0 | +0x114 |

   **+0x184,** in the order added, sets for each primitive its texture, its stage mode
   (`SetStageMode` `004112d0`), then `SRCBLEND` and `DESTBLEND` from +0x14 and +0x18.
8. **Empty every list** (`0040ecf0`).
9. **Reset:** `SRCALPHA` / `INVSRCALPHA`, stage 0 `MIPFILTER` `NONE`.

**What stays changed.** `DrawScene` leaves `ZENABLE`, `ZWRITEENABLE` and `FOGENABLE` at
0, and `ALPHAREF` at 1, for whatever draws after it.

### Sorting

Each pass sorts its list in place with the library's `std::sort`, which is not stable:

| Pass | Order |
|------|-------|
| by texture (`IsPrimitiveLessByTexture` `0040f440`) | the texture's address, then the lightmap's |
| by lightmap | the lightmap's address |
| translucent (`IsFloatUintPairLess` `00415f50`) | the key, then the primitive's address |
| vertex-buffer ranges | the texture at +0x44 |

**Equal keys** end in the order that exact algorithm leaves them, and the ground is
sorted again in place by its second pass. Since addresses order the textures, the
draw order follows where the heap put things.

### Textures

**The bound textures** (+0x50 for stage 0, +0x54 for stage 1) carry over from one draw
to the next, and across frames. Each pass sets a texture only when it differs from the
bound one, through `SetTexture` with the `CTexture`'s surface, or 0 for none.

**Stage 1** is unbound after the passes that use it: the opaque primitives, and the
two-texture ground. The scene's 2D draws (`00410620`) keep the same bound texture.

## In the port

`src/render/scene.{h,cpp}`:

| Port | Original |
|------|----------|
| `ScenePrimitive` | what the lists use of a `RenderPrimitive`, with ids that order as the original's pointers |
| `SelectSceneList`, `SceneDepthKey` | `AddPrimitive`'s choice and key |
| `SceneLists` | the primitive lists |
| `AppendStageModeCalls` | `SetStageMode` |
| `DrawScene` | the calls `DrawScene` makes after the projection, and its emptying of the lists |

**The sorts** use `Vc6Sort` (`src/res/vc6_sort.h`), so equal keys end where the
original leaves them.

**Not ported yet:**
- **The vertex-buffer lists and +0x174** are drawn empty:
  - **The vertex-buffer lists** are dead. Their adder `004128e0` has one caller,
    `FUN_004b8a50`, which only calls itself.
  - **+0x174** holds the Granny models' primitives (`00412720`, called by
    `C3dGrannyGameActor` and `CGrannyPc`), which come with the Granny sub-milestone.
- **The projection's transforms.** The fog's set-up and `ComputeVertexFog` are in
  `src/render/fog` ([fog.md](fog.md)).
- **The in-place texture coordinate scaling.**

**Where the port differs:** a translucent key that is NaN orders where a plain
comparison puts it. The original's comparator and its inlined insertion disagree there.

**The vectors.** `tools/scene_vectors.py` runs the original in the emulation oracle,
with the x87 at single precision, and writes `tests/unit/scene_vectors.inc`.
`tests/unit/scene_test.cpp` compares the port with every call, in order.
- **`SetStageMode`'s calls** for modes 0 to 16.
- **Six scenes:**
  - 40 to 200 primitives each, with random textures, lightmaps, keys (many equal), blends,
    modes, primitive types and empty primitives;
  - added through `AddPrimitive` with mixed flags;
  - drawn with `DrawScene` under each combination of fog, `ISLIGHTMAP` and two-stage
    blending.

  The two largest pass 16 entries in some lists, so the sorts take their quicksort
  path.
