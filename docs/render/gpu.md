# The SDL_GPU renderer

The port draws what the original's Direct3D 7 device drew. The ported scene code produces
the same calls the original makes (`DrawScene`, [scene.md](scene.md)). The renderer
replays those calls on SDL_GPU, emulating the fixed-function state they set.

## From calls to draws

`src/render/fixed_function` (no SDL) walks `DrawScene`'s calls. It starts from the
states `C3dDevice::Init` sets ([device.md](device.md#the-states-init-sets)), with Direct3D
7's defaults where the original sets nothing:
- **Render state defaults:** `ALPHABLENDENABLE` and `ALPHATESTENABLE` off, `SRCBLEND`
  `ONE`, `DESTBLEND` `ZERO`, `ALPHAFUNC` `ALWAYS`, `CULLMODE` `CCW`.
- **Stage 0's defaults:** colour `MODULATE(TEXTURE, CURRENT)`, alpha
  `SELECTARG1(TEXTURE)`. Later stages are `DISABLE`.
- **Sampler defaults:** point filtering and `WRAP` addressing.

Each draw becomes a `DrawRecord`:

| Part | Holds | Where it goes |
|------|-------|---------------|
| `PipelineKey` | blending and its factors, the depth test, write and function, the cull mode, the primitive type, and whether the vertices carry two coordinate sets | a GPU pipeline |
| `StageUniforms` | both texture stages' operations and arguments, the alpha test (on, function, reference), and fog (on, colour) | the fragment shader, per draw |
| textures and samplers | the texture ids bound to stages 0 and 1; each stage's filters and address modes | the draw's bindings |

## The device

`RenderDevice` (`src/render/gpu_device`, library `ragexe_gpu`) holds:
- textures by id, uploaded as `B8G8R8A8`, which is how an ARGB dword lies in memory;
- **id 0, no texture:** a 1 × 1 opaque black texture. That is how Direct3D's stages read an
  unbound texture: the ground's lightmap stage, `MODULATEALPHA_ADDCOLOR(TEXTURE,
  CURRENT)`, stays set after the ground with stage 1 unbound, and then passes the
  models' colour through;
- **an id never uploaded,** such as a texture that failed to load: a 1 × 1 white texture
  (`kWhiteId`);
- pipelines and samplers made on first use;
- one vertex buffer refilled each frame.

**A frame** uploads every draw's vertices in a single copy pass, fans made into lists. It
then makes one render pass:
1. Clear the colour, and the depth to 1.
2. For each draw, bind its pipeline, textures and samplers, and push its uniforms.

**Two targets:**
- **`DrawFrame`** draws into the window's swapchain.
- **`RenderToImage`** draws into a texture and reads it back, for screenshots and checks
  without a window.

**The vertices** are Direct3D's transformed ones: screen `x` and `y` in pixels, `z` from 0
to 1, and `rhw`, with diffuse and specular ARGB and two coordinate sets.
- **The vertex shader** maps `x` and `y` to normalized coordinates, `y` flipped. It
  multiplies the position through by `1/rhw`, so interpolation stays perspective-correct
  as Direct3D's is.
- **Viewport size:** the caller gives it, the size the scene was placed for; the window
  may be another size.

**The fragment shader** emulates the two stages:

| Part | Supported |
|------|-----------|
| arguments | `DIFFUSE`, `CURRENT`, `TEXTURE`, with `COMPLEMENT` and `ALPHAREPLICATE` |
| operations | `DISABLE`, `SELECTARG1`, `SELECTARG2`, `MODULATE`, `MODULATE2X`, `MODULATE4X`, `ADD`, `ADDSIGNED`, `SUBTRACT`, `BLENDDIFFUSEALPHA`, `BLENDTEXTUREALPHA`, `BLENDCURRENTALPHA`, `MODULATEALPHA_ADDCOLOR` |
| alpha test | every `D3DCMP` function, on the alpha in 0 to 255 |
| fog | the vertex fog of the specular alpha, toward the fog colour |

Stage 1 runs only when stage 0 is not `DISABLE`.

**Culling.** Direct3D culls by the winding a triangle shows on screen, and SDL_GPU's
front face is the winding as shown too. So `CW` culls clockwise triangles with
counter-clockwise ones as front faces, and `CCW` the reverse. The ground's pass sets `CW`
([scene.md](scene.md)), and the client's camera leaves the ground's faces
counter-clockwise.

## Shaders

`src/render/shaders/scene.vert` and `scene.frag` are written once in GLSL. The build
compiles them with tools it builds from pinned releases ([building.md](../building.md)):
1. glslang's `glslang` makes SPIR-V for SDL_GPU's Vulkan backend.
2. SPIRV-Cross's `spirv-cross` makes MSL from that for the Metal backend. It runs with
   `--msl-decoration-binding`, so the set-and-binding layout SDL_GPU documents maps to
   Metal's indices.
3. `cmake/embed_files.cmake` puts both in a generated header, `scene_shaders.h`.

The device asks SDL_GPU for SPIR-V or MSL and passes whichever it has. Windows therefore
runs on Vulkan; D3D12 would need DXIL.

## ragview

`ragview` draws a map's ground and models as the client draws them:

```bash
./build/dev/src/ragview prontera
```

```bash
./build/dev/src/ragview --screenshot prt_in.png --at -300 -200 prt_in
```

**Loading:**
1. Read the map's `.rsw`, its `.gnd` and its `.gat` from `data.grf`,
   `indoorRswTable.txt`, and the map's record in `fogParameterTable.txt` unless
   `--no-fog` ([fog.md](fog.md)).
2. Build the ground with the `.rsw`'s light, ISLIGHTMAP on ([ground.md](ground.md)).
3. Upload its textures the way `TextureCache` makes them:
   - `CreateTexture` at the image's size in ARGB 1555, or 4444 for an image with alpha;
   - `UpdatePixels` with black opaque;
   - widened back to 8 bits a channel ([texture.md](texture.md)).
4. Upload the lightmap pages, in 4444.
5. Place the camera as on entering the map ([camera.md](camera.md)). The player stands on
   the `.gat`'s ground at `--at X Z`, the map's centre by default. `--yaw` turns the
   camera.
6. Size the projection with `SetSize`.
7. **Models:** place each of the `.rsw`'s models as `CWorld::LoadWorld` does, and light it
   with the `.rsw`'s light and the ground's lightmap colours ([model.md](model.md)).
   - Each `.rsm` is loaded once.
   - Each texture is uploaded once by name, as the ground's are.
8. **The scene graph** over the ground and the models, with the `.rsw`'s bounds when it
   has them ([scene_graph.md](scene_graph.md)).
9. **The actor:** the sprite and actions `--actor` names (`npc\4_m_01` by default,
   `none` for none), or with `--player JOB SEX HAIR` a player's body, head and `.imf`,
   dressed by `--hair-color`, `--clothes-color`, `--headgear`, `--weapon` and `--shield`
   ([player.md](player.md)), and `shadow.spr`; each image uploaded as the sprite texture
   cache draws it ([actor.md](actor.md)).
10. **The effect:** with `--effect NAME`, `texture\effect\NAME.str`, played where the
    actor stands ([effect.md](effect.md)). With `--effect-id ID`, the `.str` that effect
    id plays and the primitives it adds ([effect-prim.md](effect-prim.md));
    `--minimal-effects` picks the lighter `_min` effects.

**Each frame:**
1. `CView::Update`'s port follows the player.
2. The frustum culls the scene graph, and the visible leaves' models and cells are drawn
   in `CView::vf03`'s order.
3. The ground's and the models' primitives go into `SceneLists`, then the actor's
   shadow and body, standing where the player does, idle, its frame following the time.
4. `DrawScene` with two-stage lightmaps, and with the fog when the map has a record in
   `fogParameterTable.txt` ([fog.md](fog.md)). Every vertex carries its fog factor, and
   the first frame starts with `SetupProjection`'s fog states.
5. The calls become `DrawRecord`s for the device.

**Controls,** in a window:
- **The mouse,** through `ProcessCameraInput`'s port: holding the right button is its
  drag state and a right double-click its reset state (ragview's choice of buttons).
  Dragging turns, with Shift pitches and with Ctrl zooms.
- **The arrow keys** walk the player one `.gat` cell.
- **Escape** quits; `--frames N` quits after N frames.

**The water** steps once a frame, as `CGameMode::OnFrame` does, and each texture of its
animation is uploaded the first time it is asked for. **The models** draw with the ground
tint, and **the clear colour** is the map's, as `CGameMode::OnInit` sets them.

## What is not reproduced yet

- **The texel offset** `CalibrateTexelOffset` measures ([device.md](device.md#calibratetexeloffset)),
  and the half-pixel difference between Direct3D 7's pixel centres and SDL_GPU's.
- **`BORDER` addressing**, which SDL_GPU lacks; it clamps to the edge instead.
- **Mipmaps** and **`TEXTUREMODE`'s divisor**.
- **The in-place texture coordinate scaling** of the scene lists ([scene.md](scene.md)).

## Checks

**Unit tests:**
- `tests/unit/fixed_function_test.cpp`: the records a call stream gives, and Direct3D's
  defaults;
- `tests/unit/texture_test.cpp`: how texels widen.

The device itself needs a GPU, which CI does not have. CI builds `ragview` and the
shaders on every platform. The frames are checked by eye for now.
