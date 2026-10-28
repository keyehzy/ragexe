# The scene graph and the view frustum

`CWorld` (+0x68) holds a quadtree of `SceneGraphNode` over the map. Each frame `CView`
culls it against the view's frustum, then draws the models and ground cells of the leaves
that survive. The port is `src/render/scene_graph`.

## SceneGraphNode

`SceneGraphNode` is 0x80 bytes (constructor `004c5270`, which sets only the links, the
model vector and the dirty flag):

| Offset | Field |
|-------:|-------|
| +0 | the parent |
| +4 | four children, none at depth 5 |
| +0x14, +0x20 | the box's minimum and maximum |
| +0x2c, +0x38 | its centre and half extents, which the frustum tests |
| +0x44 | dirty: the heights want recomputing |
| +0x4c | at depth 5, the models that reach into it |
| +0x58, +0x5c | the ground, and its cells under the box (x0, z0, x1, z1) |
| +0x6c, +0x70 | the attribute map, and its cells under the box |

## Building it

`CWorld::BuildSceneGraph` (`004c4f10`) runs after the world's models are placed
([model.md](model.md)), in six steps:
1. **The root** spans the ground: −width·cell/2 to width·cell/2 − 1 in x, and the same in
   z with the height.
2. **`Subdivide`** (`004c5340`) splits every node to depth 5. That is 4⁵ = 1,024 leaves
   and 1,365 nodes. Each node gets:
   - y from −999,999 to 999,999 (`005e28cc`), centre y 0;
   - its x and z midpoints as centre;
   - four children: x low and high, then z low and high.

   The half extents are left as `operator new` left them.
3. **Models:** `InsertModel` (`004c54c0`) puts each model, in the world's order, into
   every leaf its oriented box meets (`OrientedBoxIntersectsBox`, `004c56d0`). It marks the
   leaf and its ancestors dirty.
4. **Cells:** `SetGround` (`004c5d70`) and `SetAttr` (`004c5de0`) give every node the
   cells under its box's corners: ⌊x / cell size⌋ + width/2, ⌊z / cell size⌋ + height/2.
   They also mark it dirty.
5. **A version 2.1 `.rsw`** carries the tree's bounds ([rsw.md](../formats/rsw.md)), 202
   of `data.grf`'s 208 maps. `CopyBounds` (`004c68c0`) copies the box, centre and half
   extents of each node, depth first, and clears the dirty flags.
6. **Otherwise** `UpdateHeightBounds` (`004c5fa0`) visits the dirty nodes. At each leaf:
   - y spans the lowest and highest of:
     - the ground's tops over its cells (`C3dGround15::vf08`, `004bf7a0`, which gives 0
       and 0 when the cells leave the ground or are a single row or column);
     - the attribute map's corners (`C3dAttr::GetHeightRange`, `00427390`);
     - its models' world bounds (+0x1d0, +0x1dc).
   - Then, for the leaf and each ancestor, y is widened to the leaf's, and the centre y
     and the half extents are recomputed.
   - **The ancestors** start at ±999,999, so their y never narrows: only the leaves are
     tight.

**`OrientedBoxIntersectsBox`** is the separating axis test on 15 axes:
- the oriented box's three axes;
- the world's three;
- the nine cross products.

A box is separated on an axis when its reach falls short of the distance between the
centres (or the comparison is unordered).

**Later models.** `LazyModelLoadThread` places the models farther than 200 from the
player. `CWorld::UpdateGameObjects` then inserts them, so they reach the leaves after the
first ones. ragview places every model before building the tree.

## The frustum

`ViewFrustum` (`CView`+0xf4) is the frustum.

**`CullSceneGraph`** (`004ceb00`) builds six planes in view space:

| Plane | Normal, distance |
|-------|------------------|
| sides | (∓slope x, 0, 1), 0 |
| top, bottom | (0, ∓slope y, 1), 0 |
| near | (0, 0, 1), −10 (`005e8a98`) |
| far | (0, 0, −1), 1500 (`005e8a94`) |

- **The slopes** are `SetSize`'s tan 82.5° ([camera.md](camera.md#the-projection)).
- **Into the world:** each plane goes through the view matrix's rows: normal′ᵢ =
  rowᵢ · normal, and distance′ = translation · normal + distance.
- **Inside** means normal · p + distance > 0.

**Classifying a box** (`ClassifyNodeBox`, `004ced00`; `ClassifyOrientedBox`, `004ced90` on
a model's +0x130) gives one of three results:

| Result | When |
|-------:|------|
| 0, outside | some plane's distance is at most −reach |
| 1, inside | every plane's distance exceeds the reach |
| 2, crossing | otherwise |

The reach is the box's half extents projected on the normal.

**`CollectVisibleNodes`** (`004cee60`) walks the tree from the root:
- **a child classed outside** is skipped;
- **a child wholly inside** passes that on, and its subtree is not tested further;
- **the leaves reached** are listed as inside (+0xa8) or crossing (+0xb4), in the order
  found.

## Drawing

`CView::vf03` (`004cf4d0`) runs in four steps:
1. **Culling:** `CullSceneGraph` with the view matrix.
2. **The crossing leaves first.** For each:
   - **its models:** each one not drawn this frame (+0x1e4 against the renderer's frame)
     is classified and stepped (`AdvanceAnimation`). Unless it is outside, it is drawn
     (`ModelInstance::Render`), clipped when crossing. It is stamped either way.
   - **its ground cells:** `C3dGround15::DrawCells` with culling.
3. **Then the inside leaves.** Each model not drawn this frame is drawn unclipped,
   without stepping or classifying. The cells are drawn without culling.
4. **The cell range:** along the way it keeps the smallest and largest cell coordinates
   of the visible leaves (`0067caa0` to `0067caac`).

A model or cell in several leaves is drawn once, at its first.

## Checks

`tools/scene_graph_vectors.py` writes the original's results in the emulation oracle to
`tests/unit/scene_graph_vectors.inc`. The world is a synthetic 40 × 32 ground built by the
original, an 80 × 64 attribute map and 30 model stand-ins.

**What is recorded:**
- `BuildSceneGraph` from the heights and with copied bounds: every node's box, flag, cells
  and models, depth first;
- under five views, `CullSceneGraph`'s planes, the inside and crossing leaves, and each
  model's class;
- 300 calls to `OrientedBoxIntersectsBox`.

The port matches every value (`tests/unit/scene_graph_test.cpp`). The drawing order is
ported from `vf03` without a vector of its own.

**Not ported:**
- the cell range globals;
- `SceneRayQuery`, the picking that finds the leaves a ray crosses.
