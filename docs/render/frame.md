# Whole frames

A frame of the world, from the scene graph to the device: what `CGameMode::OnFrame` steps,
what `CView::DrawWorld` hands the scene lists, the textures and primitive pools they sort
by, and the vertices `DrawScene`'s draws send. The parts are in [scene_graph.md](scene_graph.md),
[ground.md](ground.md), [model.md](model.md), [camera.md](camera.md) and [scene.md](scene.md).

## The frame

`CGameMode::OnFrame` (`0050d400`, vf04) runs in this order, as far as the world goes:
1. **Each world step due** (`GetFrameStepsDue`): `CWorld::UpdateGameObjects`, the water's
   step (`C3dGround15::StepWater`, [ground.md](ground.md#the-water)) and the view's
   (`CView::Update`, vf04, [camera.md](camera.md)).
2. **`C3dDevice::Clear`** with the map's colour, then `SceneRenderer::BeginScene`.
3. **`CView::DrawWorld`** (vf03, `004cf4d0`), below.
4. The UI, the cursor and the fade, then **`SceneRenderer::Present`** (`00410c50`), which
   counts the frame at +0x34 and +0x3c.
5. `WorldColorFade::Update` (`00586460`), then **the pools rewound** (`RewindPools`,
   `00412e70`).

## DrawWorld

`CView::DrawWorld` (`004cf4d0`):
1. **The view's shake step** pending (`g_viewShakeStepPending`, `0067cab4`).
2. **The visible scene graph**, culled and drawn as [scene_graph.md](scene_graph.md#drawing)
   describes: the crossing leaves' models and cells, then the inside leaves'.
3. **The cell cursor:** unless `g_cellCursorHidden` (`00684e28`), the ground's slot 15
   (`DrawCellCursor`, `004c0240`) at `g_cellCursorX`, `g_cellCursorZ` and
   `g_cellCursorColor` ([ground.md](ground.md#the-cell-cursor)). It asks `TextureCache` for
   `grid.tga` (`0060b0d8`) only when the cell is on the map and has a top.
4. **The world's objects:** each of the list at `CWorld`+0xc, through its slot 3.
5. **The view's direction** into the renderer's +0x6c (`CView::GetViewDirection`,
   `004cfb20`).
6. **`SceneRenderer::DrawScene`** ([scene.md](scene.md#drawscene)), then the frustum's
   lists emptied (`ClearVisibleNodes`, `004cec80`).

## Textures

`DrawScene` sorts the opaque and ground lists by the `CTexture`s' addresses
([scene.md](scene.md#sorting)), so the draws' order depends on where the textures lie.

**`TextureCache::GetTexture`** (`0040cc00`) looks the name up in its map. On a miss,
`CreateTextureFromResource` (`0040cff0`):
- **The image:** `ResourceManager::GetResource` of the name, a `CBitmapRes`
  ([bitmap.md](../formats/bitmap.md)).
- **Loaded:** `C3dDevice::CreateTexture(width, height, pixels, alpha, flag)` with the
  image's size, its pixels and its alpha flag (+0x110).
- **Not loaded:** `CreateTexture(1, 1, ...)` of one zero pixel, without alpha.

The name is then copied to the texture's +0x20 and the texture kept under it.

**`C3dDevice::CreateTexture`** (`00405350`) makes `CTexture(width / d, height / d, alpha)`,
with `d` the divisor `g_textureDivisor` (`0061ec50`, [device.md](device.md)):
- **The format** is the alpha flag itself: 0, ARGB 1555, or 1, ARGB 4444.
- **The size** is fitted to the device (`FitTextureSize`).
- **`UpdatePixels`** of the whole image then sets the uv scale at +0x18 and +0x1c
  ([texture.md](texture.md)).

**The order `LoadWorld` makes them in:**
1. **The lightmap pages,** as `LightmapAtlas::Build` fills each, in ARGB 4444
   ([ground.md](ground.md#the-lightmap-atlas)).
2. **The ground's textures,** as `C3dGround15::Build` places surfaces: cell by cell, row by
   row, the top, then the wall toward z + 1, then the wall toward x + 1. A texture is made
   when the first surface of it is placed.
3. **The models' textures,** as each `.rsm` loads: models in the order `LoadWorld` and the
   lazy loader place them ([model.md](model.md#loading)), a file at its first model. Its
   nodes in file order ask for their textures in their own order
   ([rsm.md](../formats/rsm.md)); a texture no node uses is never made.

The frames then add **the water's** texture at each step that names a new one, and
`grid.tga` when the cell cursor is first drawn.

**Their addresses.** Each texture is `operator new(0x128)`. On a heap that only grows, as
the emulation oracle's does, the addresses follow the order the textures are made in. The
port numbers its textures in that order.

## The pools

`SceneRenderer`'s constructor (`0040d1a0`) fills three pools, each a `std::list`:

| Pool | Each node holds | First size | Taken by |
|------|-----------------|-----------:|----------|
| +0x260 | a `RenderPrimitive` (0x2c-byte nodes) | 1024 | `NewTriangleList` (`00412aa0`): the models' runs of faces |
| +0x270 | a `RenderQuad` (0xcc-byte nodes) | 5120 | `NewQuad` (`00412b40`): the ground, the water, the cell cursor |
| +0x280 | a `RenderQuad` | 5120 | `NewSecondPoolQuad` (`00412be0`), which nothing calls |

**Taking a node.** Each pool has a cursor (+0x26c, +0x27c, +0x28c) that steps through its
list. At the list's end a new node is made, appended and taken, and the cursor stays at the
end. `RewindPools` (`00412e70`) moves the three cursors back to their lists' first nodes,
and empties the models' vertex vector at +0x294 that `AllocModelVertices` (`00412c80`)
fills.

**Their addresses.** The constructor makes the nodes in the order of the table, and each
node a pool grows by is made when first taken. The translucent lists break ties of depth by
the primitive's address ([scene.md](scene.md#sorting)). On a heap that only grows:
- the first triangle lists lie below the first quads;
- the nodes the pools grow by lie above both, in the order they were made.

**A model's run** takes a node only when it has faces to draw (`004b74b4`).

## In the port

`src/render/world_frame.{h,cpp}`:

| Port | Original |
|------|----------|
| `FrameTextures` | `TextureCache::GetTexture` and `C3dDevice::CreateTexture`: ids in the order made |
| `MakeWorldTextures` | the textures `LoadWorld` makes, in order |
| `PrimitivePools` | the pools: ids ordered as the nodes' addresses |
| `FramePrimitive`, `FrameDraws` | the vertices the draws send, after the in-place scaling |
| `EnterWorld` | `PlaceCamera` and the draw settings `CGameMode::OnInit` leaves |
| `DrawWorldFrame` | one frame of `OnFrame`: the steps, `DrawWorld`, `Present`'s count, the pools rewound |

**The vertices** (`FrameDraws`). Each draw first multiplies the primitive's texture
coordinates by its texture's uv scale, in place, at the x87's single precision
(`ScaleTexCoordsByTexture`, `0040c280`). Then:
- **`Draw`** (`0040c3a0`) sends the vertices.
- **`DrawWithTexCoordArray`** (`0040c590`) first writes the second coordinates over the
  first, in place. The scene marks those draws (`SceneCall::lightmap_coords`).
- **`DrawTwoTexCoords`** (`0040c3e0`) sends both sets, FVF `0x2c4`.

A primitive drawn twice is scaled twice.

**`ragres draw-frame INSTALL MAP CELL_X CELL_Y`** loads the map as `load-world` does
([model.md](model.md#loading)), with the player and the cell cursor on that cell, and draws
three frames. It prints the textures in the order made, then each frame's device calls.

## Checks

**Real maps.** `tools/frame_oracle.py` compares whole frames with the original's.
- **The original's frames:** it loads the map in the emulation oracle as
  `tools/world_oracle.py` does ([model.md](model.md#checks)), but with the textures made
  for real:
  - `TextureCache` reads each image through `CBitmapRes`;
  - `CreateTexture` makes each `CTexture` on the fake Direct3D device;
  - `CBitmapRes::LoadJpeg` alone is stood in for (its decoder is `ijl15.dll`): it gives the
    image's size and pixels and clears the alpha flag, as the original does.
- **The set-up:**
  - what the client sets up before a map: the texture limits `CreateRenderer` takes from
    the device, `InitAngleTables`, and the static initializers of `TextureCache`,
    `g_WorldColorFade`, `g_waterTextureScale` and `g_cellCursorUv`;
  - the player on the walkable cell (type 0) nearest the attribute map's centre, as it
    enters the map;
  - `CView` created and placed by `CGameMode::PlaceCamera`, with `IsIndoorMap` answered
    from `indoorRswTable.txt`;
  - the cell cursor shown over the player's cell;
  - the frame count at 1, as if the loading had presented one frame.
- **Three frames,** as `OnFrame` draws them: the camera as placed, then turned by 120
  and by 240 degrees.
- **The comparison:** every device call `DrawWorld` makes, every vertex bit for bit, and
  the order the textures were made in. It is checked against `ragres draw-frame`.
- **`ctest`** runs 4 maps (`frame_oracle_sample`); `--sample 0` runs every map.
- **The result:** all 208 maps are equal, three frames each: 14,285 textures made, 199,753
  device calls and 161,841 draws.

**Vectors** in `tests/unit/scene_vectors.inc` (`tools/scene_vectors.py`):
- **Four scaled scenes.** Textures with uv scales, and primitives with their own texture
  coordinates and a second set, some added twice, drawn:
  - with lightmaps on one stage, the ground scaled twice and its second pass written over;
  - with lightmaps on two stages;
  - without lightmaps.

  Every vertex of every draw is compared.
- **The pools** of a constructed `SceneRenderer`, taken past their first sizes over three
  frames and rewound after each: the order of every node's address.

**What the frames leave out:**
- **No player or other objects:** `CWorld::CreatePlayer` is stood in, so the list at
  `CWorld`+0xc is empty.
- **No fog:** the renderer's +0x44 to +0x4c are clear, so `DrawScene` sets no projection.
- **No sky, Granny models, effects, UI or cursor.**
- **`WorldColorFade::Update`** is not run, so the fade keeps its constructor's values.
