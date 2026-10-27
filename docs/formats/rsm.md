# Models: C3dModelRes

`C3dModelRes` (RTTI, 0x1a8 bytes) is a model: a tree of nodes, each with a mesh, its
textures, its placement and its animation keys. It is registered for `rsm` under
`model\` ([resources.md](resources.md#types)). Worlds place models by name
([rsw.md](rsw.md#objects)), and `ModelInstance` draws them through a tree of `C3dNode`.

## Members

| Offset | Field |
|-------:|-------|
| +0x110, +0x114 | a count and an array of 0x88-byte records, not touched by `Load` |
| +0x118 | `list<ModelResNode*>`: the nodes, in file order |
| +0x124 | `list<string>`: the root node's name |
| +0x130 | `map<string, ModelResMesh*>`: each node's name to its mesh |
| +0x140 | the path `Load` was given, copied with `strcpy` |
| +0x190 | int, copied to `ModelInstance+0x24` |
| +0x194 | animation length, copied to `ModelInstance+0xb8` |
| +0x198 | `list<record*>`: the volumes, 0xe8-byte records |
| +0x1a4 | alpha (byte) |

**The path at `+0x140`:** the object ends 0x50 bytes after it, at `+0x190`, so a path of
80 bytes or more would run into the fields after it.

### ModelResNode

`ModelResNode` is 0x16c bytes (constructor `0042b350`).

| Offset | Field |
|-------:|-------|
| +0 | name |
| +0x80 | parent's name, empty for none |
| +0x100 | the `C3dModelRes` |
| +0x104 | the parent node |
| +0x108 | `list<ModelResNode*>`: the children |
| +0x114 | the `ModelResMesh` |
| +0x118 | texture count |
| +0x11c | `CTexture*` array |
| +0x120 | position, 3 floats |
| +0x12c | rotation axis, 3 floats |
| +0x138 | rotation angle (float) |
| +0x13c | scale, 3 floats |
| +0x148 | alpha (byte) |
| +0x14c | `vector`: translation keys, 16 bytes each |
| +0x15c | `vector`: rotation keys, 20 bytes each |

`C3dNode::vf01` (`004b7580`) builds a drawable node from one:
- It takes over the keys. When there are none, it makes one translation key at frame 0
  from the position, and one rotation key from the axis and angle
  (`RotationKeys::AddFromAxisAngle`).
- It makes one scale key from the scale.
- It turns the alpha into `alpha / 255` (`005e88c8`).

**Keys:** a translation key is a frame (int) and x, y, z. A rotation key is a frame and
4 floats, which `Matrix::MakeRotationFromKey` (`00409ae0`) turns into a matrix.
`TranslationKeys::GetMatrixAt` (`00401520`) interpolates between keys by frame.

### ModelResMesh

`ModelResMesh` is 0x234 bytes (constructor `00408d30`).

| Offset | Field |
|-------:|-------|
| +0 | vertex count |
| +4 | face count |
| +8 | texture coordinate count |
| +0xc | vertices, 12 bytes each |
| +0x10 | face normals, 12 bytes each |
| +0x14 | corner normals, 36 bytes per face |
| +0x18 | texture coordinates, 12 bytes each: a dword, then u and v |
| +0x1c | faces, 0x18 bytes each |
| +0x24 | 32 `vector<int>`: the faces of each smoothing group |
| +0x224 | a `VertexBuffer` |

A face:

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 6 | three vertex indices (shorts) |
| 6 | 6 | three texture coordinate indices (shorts) |
| 0xc | 2 | texture index into the node's textures |
| 0xe | 2 | not interpreted |
| 0x10 | 4 | not interpreted |
| 0x14 | 4 | smoothing group, 0 to 31 |

## File

All numbers little-endian. **Names** are C strings in 40-byte records.

**Header:**

| Field | Size | When |
|-------|-----:|------|
| `GRSM` (`006061b0`) | 4 | |
| major, minor | 1 + 1 | |
| animation length (`+0x194`) | 4 | |
| `+0x190` | 4 | |
| alpha | 1 | 1.4 and later; 255 before |
| reserved, dropped | 16 | |
| texture count, then the names | 4 + 40 each | |
| the root node's name | 40 | |
| node count | 4 | |

**Each node:**

| Field | Size | When |
|-------|-----:|------|
| name, parent's name | 40 + 40 | |
| texture count, then indices into the model's textures | 4 + 4 each | |
| a matrix: rows of x, y, z, then the translation | 48 | |
| position | 12 | |
| rotation angle | 4 | |
| rotation axis | 12 | |
| scale | 12 | |
| vertex count, then the vertices | 4 + 12 each | |
| texture coordinate count, then each | 4 + 8 each | 1.1: u and v |
| | 4 + 12 each | later: a dword, u and v |
| face count, then each | 4 + 20 each | 1.1: the first 14 bytes kept |
| | 4 + 24 each | later: as in memory |
| translation key count, then the keys | 4 + 16 each | 1.5 and later |
| rotation key count, then the keys | 4 + 20 each | |

**After the nodes:**

| Field | Size | When |
|-------|-----:|------|
| translation key count and keys, for the root node | 4 + 16 each | before 1.5 |
| volume count | 4 | |
| volumes | 40 each | 1.3 and later |
| | 36 each | 1.2 |

Version 1.1 and 1.0 read the volume count and nothing more.

### In data.grf

All 1,127 `.rsm` files in `data.grf` are version 1.4 and end exactly after their last
field.
- **Indices:** every texture, vertex and texture coordinate index is in range, and every
  smoothing group is between 0 and 31.
- **Sizes:** the largest model has 17 textures and 20 nodes, and a mesh has up to 689
  vertices and 926 faces.
- **Alpha:** 1,125 models are opaque.
- **Volumes:** two models have some.
- **Keys:** 372 nodes have rotation keys, and no model has translation keys.
- **The face dword at 0x10** is 0 or 1 in 115,826 of the 115,899 faces. The other 73 hold values that
  look like leftover memory.

## Load

`C3dModelRes::Load` (slot 3, `0042be00`). It does not run `Reset`, and checks neither a
read nor an index.

**Refusals:**
- **Not `GRSM`:** show the CP949 message at `00606194` ("a model file of the wrong
  format") and fail.
- **Major not 1, or minor above 5:** show the one at `0060615c` ("an unsupported
  version; the contents may be broken") and fail.

**The model:**
1. Read the header into `+0x194`, `+0x190` and `+0x1a4`, then the texture names into a
   temporary array of strings.
2. Copy the root name into a 64-byte buffer.

**Each node:**
1. **Create it.** Give it the model's alpha, and copy its two names with `strcpy`.
2. **Its textures:** for each index, `TextureCache::GetTexture` of that texture name.
   Each texture's use count goes up (`CTexture::IncrementUseCount`).
3. **The matrix** is read into a local and multiplied into each vertex as it is read,
   then dropped. With the matrix `m[0..11]`, each sum is kept on the FPU and stored as a
   float:
   ```
   x' = ((m6·z + m3·y) + m0·x) + m9
   y' = ((m1·x + m7·z) + m4·y) + m10
   z' = ((m2·x + m8·z) + m5·y) + m11
   ```
4. **Texture coordinates:** in 1.1, u and v go through the FPU, and the dword is set to
   0xffffffff.
5. **Faces:** a 1.1 face keeps its first 14 bytes, gets a smoothing group of 0, and
   leaves bytes 0xe to 0x13 as `operator new` left them.
6. **Sort the faces** by texture index, with the [client's sort](#the-face-sort).
7. **`ModelResMesh::ComputeNormals`** (`00408ea0`, [below](#normals)).
8. **The keys.**
9. **Add the node** to the list, and its mesh to the name map. `std::map::insert` keeps
   the first mesh of a name.

**After the nodes:**
1. **Free the texture names.**
2. **Before 1.5:** read translation keys into the root node, found by
   `C3dModelRes::FindNode` (`0042cbd0`). That is the first node of that name in the
   list, compared with `strcmp`, and none would be a null pointer.
3. **Link each node** with a parent name to the first node of that name, and append it
   to that node's children. A missing parent is again a null pointer.
4. **Add the root name** to the list at `+0x124`.
5. **Read the volume count and the volumes.** From 1.3 a volume is 9 floats and an int
   flag. When the flag is 0, 3.0 (`005e29d8`) is added to the first and third floats. A
   1.2 volume is the 9 floats with a flag of 0 and nothing added.
6. **Copy the path to `+0x140`** and succeed.

`Reset` (slot 4, `0042bc70`) frees the records at `+0x114`, the nodes, the root names,
the name map and the volumes.

### The face sort

The client sorts with its `std::sort` on the texture index (`ushort` at 0xc):
- **Up to 16 faces:** `std::InsertionSort` (`00431de0`).
- **More:**
  1. `std::QuickSortRange` (`00431b30`): median-of-three quicksort down to ranges of 16;
  2. an insertion sort of the first 16;
  3. `std::ShiftInsertSorted` (`00431d40`) for each later face.

It is not stable, so faces of one texture come out in an order only this algorithm
gives.

**Every copy moves only part of a face.** `AssignModelFace` (`00431cf0`) and the
insertion sorts move the seven shorts and the dword at 0x14. Bytes 0xe to 0x13 stay
where they were, so after the sort they belong to the position, not to the face.

### Normals

`ModelResMesh::ComputeNormals`:

**A face normal** is `(p2 − p1) × (p2 − p0)`, with the corners in face order.
- **Precision:** the x and y of `p2 − p1` are stored as floats, and the other
  differences stay on the FPU:
  ```
  a, b, c = p2 − p0        d, e = (p2 − p1).x, .y as floats      f = (p2 − p1).z
  n = (e·c − f·b,  f·a − c·d,  b·d − e·a)
  ```
- **Normalising:** it is then scaled by `1 / SqrtFloat((x·x + y·y) + z·z)`. `SqrtFloat`
  (`00409570`) takes and returns a float.
- **A face with no area** gets 1/0 × 0: the x87's negative quiet NaN, `ffc00000`.

**Smoothing groups:** the faces are put into the 32 groups by their dword at 0x14. Nothing
checks that it is below 32.

**Corner normals, for each group that has faces:**
1. **Sum:** for each vertex, add the face normals of the group's faces that use it, in
   face order and once per face, as floats.
2. **Normalise** the sum the same way, unless its length is 0 or NaN (`fcomp` with
   `test ah, 0x40`).
3. **Store:** each corner of the group's faces takes its vertex's normal.

A vertex shared by faces of different groups so gets a different normal in each.

## In the port

`src/res/rsm.{h,cpp}`: `Model`, `DecodeModel`, `SortModelFaces`, `ComputeModelNormals`
and `RsmRes`, registered by `RegisterModelTypes`.
- **What it keeps:** the meshes as the client holds them after `Load`: vertices
  transformed, faces sorted, normals computed. It also keeps each node's matrix as read.
- **Textures:** each node holds indices into `Model::textures` instead of textures.

**The FPU arithmetic** is done in double, with the x87's NaN rules: an invalid
operation gives `ffc00000`, and a NaN operand comes out quiet. Every value in `data.grf`
agrees (below).

**Where the port differs:**
- **Truncated files fail,** and so do names with no NUL inside their record.
- **So do bad indices and links**, where the original reads through bad pointers:
  - out-of-range texture indices;
  - faces naming a missing vertex, or a smoothing group outside 0 to 31;
  - a missing root or parent.
- **A 1.1 face** has 0 in the bytes the original leaves unset.

**The oracle:** `tools/rsm_oracle.py` runs the original `Load` on every `.rsm` in
`data.grf`, and on 13 synthetic models of every version. `TextureCache::GetTexture` is
replaced by a stand-in that records the name.
- **What it compares:** every node's names, links, textures, placement, alpha and keys,
  and every mesh's vertices, texture coordinates, sorted faces, and face and corner
  normals, bit for bit. Also the volumes.
- **The synthetic models** cover faces with no area, faces using a vertex twice, unsorted
  textures past the quicksort threshold, and each version's layout.
- **It catches the sort:** with a stable sort, 879 of the 1,127 models differ.
