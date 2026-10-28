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
- a 1 × 1 white texture for id 0 and unknown ids;
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

**Culling.** Direct3D culls by the winding on screen, with `y` down. The shader flips `y`,
so `CW` culls triangles counter-clockwise in normalized coordinates, and `CCW` the
others.

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

`ragview` draws a map's ground as the client draws it:

```bash
./build/dev/src/ragview prontera
```

```bash
./build/dev/src/ragview --screenshot prontera.png prontera
```

**Loading:**
1. Read the map's `.rsw` and its `.gnd` from `data.grf`.
2. Build the ground with the `.rsw`'s light, ISLIGHTMAP on ([ground.md](ground.md)).
3. Upload its textures the way `TextureCache` makes them:
   - `CreateTexture` at the image's size in ARGB 1555, or 4444 for an image with alpha;
   - `UpdatePixels` with black opaque;
   - widened back to 8 bits a channel ([texture.md](texture.md)).
4. Upload the lightmap pages, in 4444.

**Each frame:**
1. `DrawGroundCells` over every cell.
2. The primitives go into `SceneLists`.
3. `DrawScene` with two-stage lightmaps.
4. The calls become `DrawRecord`s for the device.

**Controls.** In a window, the arrow keys turn and zoom and Escape quits. `--frames N`
quits after N frames.

**Stand-ins until the code they replace is ported:**
- **The camera** is an orbit:
  - yaw, pitch (90 is straight down) and distance around a point (`--camera`, `--centre`);
  - screen right is mirrored from a right-handed basis, so that the ground's faces wind
    counter-clockwise on screen and the client's `CW` culling keeps them, as the
    original's `CView` must leave them.
- **The projection scale** is half the height, a 90° field of view, where `SetSize`
  (`0040db70`) derives it from the client's angles.
- **The water** is left out: its textures are not loaded yet.

## What is not reproduced yet

- **The texel offset** `CalibrateTexelOffset` measures ([device.md](device.md#calibratetexeloffset)),
  and the half-pixel difference between Direct3D 7's pixel centres and SDL_GPU's.
- **`BORDER` addressing**, which SDL_GPU lacks; it clamps to the edge instead.
- **Mipmaps,** **`TEXTUREMODE`'s divisor** and **the fog's set-up** (`SetupProjection`).
- **The in-place texture coordinate scaling** of the scene lists ([scene.md](scene.md)).

## Checks

**Unit tests:**
- `tests/unit/fixed_function_test.cpp`: the records a call stream gives, and Direct3D's
  defaults;
- `tests/unit/texture_test.cpp`: how texels widen.

The device itself needs a GPU, which CI does not have. CI builds `ragview` and the
shaders on every platform. The frames are checked by eye for now.
