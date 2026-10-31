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

`ragview` draws a map as the client draws it:

```bash
./build/dev/src/ragview prontera
```

```bash
./build/dev/src/ragview --screenshot prt_in.png --cell 160 100 prt_in
```

**Loading:**
1. Read `data.grf` with the install's `.gpf` patches over it, as the patcher leaves them
   (`res::OpenInstallArchives`).
2. Load the world as `CWorld::LoadWorld` does ([model.md](model.md#loading)): the `.rsw`,
   its `.gnd` and `.gat`, the ground built with the `.rsw`'s light and ISLIGHTMAP on
   ([ground.md](ground.md)), the models placed and lit, and the scene graph.
3. **Enter it** (`EnterWorld`, [frame.md](frame.md#in-the-port)) with the player on the
   `.gat`'s ground:
   - at `--at X Z`, the map's centre by default, or in the centre of the cell `--cell X Y`
     (`CGameMode::GetCellCentre`, `00511de0`);
   - the camera placed as on entering the map ([camera.md](camera.md)); `--yaw` turns it;
   - the projection sized for the window with `SetSize`.
4. **The fog,** as `CGameMode::OnInit` sets it with the FOG setting on: the map's record in
   `fogParameterTable.txt` unless `--no-fog`, then `SetFogEnabled` on a device with vertex
   fog ([fog.md](fog.md)).
5. **The textures** in `FrameTextures`' order and ids: the lightmap pages, the ground's and
   the models' as `LoadWorld` makes them. Each named one is uploaded the way `TextureCache`
   makes it:
   - `CreateTexture` at the image's size, fitted to the device, in ARGB 1555, or 4444 for an
     image with alpha;
   - `UpdatePixels` with black opaque;
   - widened back to 8 bits a channel ([texture.md](texture.md)).
6. **The actor:** the sprite and actions `--actor` names (`npc\4_m_01` by default,
   `none` for none), or with `--player JOB SEX HAIR` a player's body, head and `.imf`,
   dressed by `--hair-color`, `--clothes-color`, `--headgear`, `--weapon` and `--shield`
   ([player.md](player.md)), or with `--job JOB` the NPC or monster of a job id as
   `CNpc::vf16` names it ([client_tables.md](../formats/client_tables.md#built-in-tables)).
   Then `shadow.spr`, scaled by the job's shadow scale. Each image gets a texture made as
   the sprite texture cache makes one ([actor.md](actor.md#the-quad)): an 8-bit image at the
   corner of a 256 × 256 page, a 32-bit one in its own texture, spanning 0 to its uv scale.
   ragview makes them as it loads the sprites, before the first frame.
7. **The effect:** with `--effect NAME`, `texture\effect\NAME.str`, played where the
   actor stands ([effect.md](effect.md)). With `--effect-id ID`, the `.str` that effect
   id plays and the primitives it adds ([effect-prim.md](effect-prim.md));
   `--minimal-effects` picks the lighter `_min` effects.

**Each frame** is `DrawWorldFrame` ([frame.md](frame.md)), which matches the original's
frames call for call:
1. The water's step and `CView::Update`'s, following the player.
2. `CView::DrawWorld`: the visible scene graph's models and cells, and the cell cursor under
   the mouse, in a window.
3. **The world's objects,** through `DrawWorldFrame`'s hook, into the same lists:
   - the actor's shadow and body, standing where the player does, idle, its frame
     following the time, each quad from the renderer's quad pool as `AddSpriteQuad` takes
     it;
   - the effects' triangles, stepped once a world step (at most 8 a frame). Each effect
     keeps its own primitives, so ragview gives them ids in the order added, above the
     pools'; the original's are where each effect was allocated, which is not ported.
4. `DrawScene`, the fog's states first on the first frame. Every vertex carries its fog
   factor, as `AddPrimitive` writes it.
5. The textures the frame made (the water's, `grid.tga`, the effects') are uploaded.
6. The calls become `DrawRecord`s for the device, each draw with the vertices it sent:
   texture coordinates scaled by the texture's uv scale in place, as the original's draws
   scale them, and the lightmap pass's written over.
7. **The cursor,** in a window: `cursors.act`'s arrow at the mouse, drawn onto the screen
   after the scene as `CMode::DrawCursor` draws it ([actor.md](actor.md#on-the-screen)).
   The system's cursor is hidden.
8. **Text,** last, as GDI drew it on the back buffer before `Present`
   ([text.md](text.md#the-glyphs)): the runs drawn into a layer the screen's size, then
   one draw of the layer with an alpha test, its own state, outside the scene's.
   - `--fps`: the frame rate as `SceneRenderer` `00410cf0` would draw it, which the
     client never calls;
   - `--stamp`: on a screenshot, the date and time as the client stamps its own.

**Controls,** in a window:
- **The mouse,** through `ProcessCameraInput`'s port: holding the right button is its
  drag state and a right double-click its reset state (ragview's choice of buttons).
  Dragging turns, with Shift pitches and with Ctrl zooms.
- **The arrow keys** walk the player one `.gat` cell.
- **Escape** quits; `--frames N` quits after N frames.

**The clear colour** is the map's, as `CGameMode::OnInit` sets it.

**`--screenshot FILE.png`** renders one frame offscreen and exits. With **`--digest`** it also
prints what the frame gave the GPU: `draws N D textures T`, the FNV-1a digests of the draws
(their pipeline, stage, sampler and texture state, and their vertices) and of every texture
uploaded (its id, size and texels). These do not depend on the GPU.

## Baselines

Reviewed frames of reference scenes guard the renderer against regressions
(`tools/frame_baselines.py`):
- **The scenes,** in `tests/baselines/frames.tsv`: 15 of them, each a map and a cell, some
  with a turned camera, a player, a monster or an effect. Between them: a city with water,
  indoor maps, dungeons, fog, lava, the sea, the sky-blue clear colours, and maps from the
  patches.
- **What is kept:** for each, `--digest`'s draws and digests, the SHA-256 of its image as
  reviewed, and the GPU driver it was drawn on. The images show the game's data, so they
  stay out of the repository, under `original/baselines/frames`.
- **The check:** each scene's digests must stay equal. Its image must match the reviewed
  copy, where there is one: at most 0.1% of the pixels may differ by more than 16 in a
  channel, which allows another driver's rounding but not a draw missing or misplaced.
- **`--update`** takes the frames as the new baselines, after review.
- **`ctest`** runs it (`frame_baselines`) when the data is present, and skips it when SDL_GPU
  has no device.

**The review** looked at each frame for: the ground lit and lightmapped, models in place,
water and lava, the fog's colour and depth, the clear colour, the actor standing on the
ground with its shadow, a player dressed and attacking, and both kinds of effect.

## What is not reproduced yet

- **The texel offset** `CalibrateTexelOffset` measures ([device.md](device.md#calibratetexeloffset)),
  and the half-pixel difference between Direct3D 7's pixel centres and SDL_GPU's.
- **`BORDER` addressing**, which SDL_GPU lacks; it clamps to the edge instead.
- **Mipmaps** and **`TEXTUREMODE`'s divisor**.

## Checks

**Unit tests:**
- `tests/unit/fixed_function_test.cpp`: the records a call stream gives, and Direct3D's
  defaults;
- `tests/unit/texture_test.cpp`: how texels widen.

The device itself needs a GPU, which CI does not have. CI builds `ragview` and the
shaders on every platform. The frames are kept as baselines (above), checked where there is
a GPU and the data.
