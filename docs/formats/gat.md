# Attribute maps: C3dAttr

`C3dAttr` (RTTI, 0x12c bytes) is a map's grid of cells: for each cell, the height of
its four corners and an attribute. It is registered for `gat` with no folder
([resources.md](resources.md#types)).

## Members

| Offset | Field |
|-------:|-------|
| +0x110 | width, in cells |
| +0x114 | height, in cells |
| +0x118 | cell size in world units: 5, set by the constructor (`00426ba0`) and never changed |
| +0x11c | `vector<cell>` of 0x14-byte cells, row by row |

A cell is four floats and an int, as in the file.

## File

All numbers little-endian. The cells follow the header row by row (`width` per row),
20 bytes each: four corner heights as floats, then the attribute as an int.

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | `GRAT` (`00605ee4`) |
| 4 | 1 | major version |
| 5 | 1 | minor version |
| 6 | 4 | width |
| 10 | 4 | height |
| 14 | `width × height × 20` | cells |

In `data.grf`, all 208 `.gat` files are version 1.2 and exactly this size, up to 400 × 400 cells.

| Attribute | Cells |
|----------:|------:|
| 0 | 7,825,026 |
| 1 | 11,848,757 |
| 3 | 370 |
| 5 | 838,232 |
| 6 | 3,891 |
| 4,476,020 | 68,924 |

## Load

`C3dAttr::Load` (slot 3, `00426ec0`):

1. **Open:** `DataFile::Open(path)`, without checking the result. A missing file reads
   nothing and fails the next step.
2. **Magic:** read 4 bytes and compare them, as a string, with `GRAT`. On a mismatch,
   show the CP949 message at `00605e88` ("Block Attr 이 지원되지 않는 옛버젼의 GAT":
   an old GAT version without block attributes) and fail.
3. **Version:** read the major and minor bytes.
   - **Major 1 and minor below 3:** read width and height, then `Resize` (`00426bf0`):
     `Reset` (slot 4), size the cell vector to `width × height`, store the size.
     Then read all the cells in one `DataFile::Read`, which reads nothing when the file
     is short.
   - **Any other version:** show "C3dAttr::Unsupported Version" (`00605e68`), and still
     **succeed**, with an empty map.

`Reset` (slot 4, `00426eb0`) zeroes width and height and keeps the cells.

## Queries

These give the cells their meaning in the world. The port adds them with the walking
code (M5), checked against the oracle there.

- **The grid is centred on the origin:** cell (x, z) spans world
  `((x − width/2)·5, (z − height/2)·5)` to the next cell (`CellToWorld` `00427100`).
- **`GetCellAttributeAt(x, z)`** (`00427050`): the attribute of the cell under a world
  position; outside the grid, 1.
- **`GetHeight(x, z)`** (`00427150`) interpolates the four corner heights bilinearly;
  outside the grid, 0.0. Corners 0 and 1 are the cell's lower z edge, 0 and 2 its
  lower x edge.
- **Others:** `IntersectCellRay` (`00427230`) picks with the cell's two triangles, and
  `GetHeightRange` (`00427390`) gives the lowest and highest corner over a rectangle of
  cells.

## In the port

`src/res/gat.{h,cpp}`: `AttrMap`, `DecodeAttrMap` and `AttrRes`, registered by
`RegisterAttrTypes`.
- **Where the port differs:**
  - a missing file fails with its own message;
  - a short file, whose cells the original would leave as uninitialised memory, fails;
  - so do negative sizes.
- **The oracle:** `tools/gat_oracle.py` runs the original `Load` on every `.gat` in
  `data.grf` and compares the size, the cell size and every cell.
