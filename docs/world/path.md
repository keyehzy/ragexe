# Path finding

An actor walks from cell to cell of the map's attribute map ([gat.md](../formats/gat.md)).
`PathFinder`, the one global `g_PathFinder` (`00684e30`), searches for the cells to walk
through and times each step. The port is `src/world/path_finder`.

## PathFinder

The static initializer `0050c200` constructs it (`004cc1b0`) and `atexit` destroys it
(`004cc290`). `CGameMode` `0050dc90` gives it the attribute map of the world it has just
loaded (`CWorld`+0x40) through `SetAttrMap` (`004ccc50`).

| Offset | Field |
|-------:|-------|
| +0x0 | the attribute map, a `C3dAttr` |
| +0x4, +0x8 | the goal cell of the last search |
| +0xc | the nodes taken so far in this search |
| +0x10 | 150 nodes of 0x1c bytes |
| +0x1078 | a `std::map` from a cell's key, `width × y + x`, to its node |
| +0x1088 | a `std::vector` of nodes, the open list, kept as a heap |

A node is:

| Offset | Field |
|-------:|-------|
| +0x0, +0x4 | its cell |
| +0x8 | the node it was reached from |
| +0xc | g: the cost from the start, 10 for a side and 14 for a diagonal |
| +0x10 | f: g plus the estimate, ten times the cells left along x and along y |
| +0x14 | 0 unseen, 1 open, 2 closed |
| +0x18 | the direction it was entered by |

**The nodes** come from the array in order (`004ccb80`). The 150th is refused (+0xc above
0x94), so a search may see 149 cells. The cell's node is found through the map, and a cell
without one takes the next node, unseen (`004ccba0`). A search clears the map and the open
list (`004cc4e0`) but not the nodes, which keep their fields until written again.

**The open list** is a binary heap with the least f on top, kept by the library's heap code
inline and in `std::HeapSiftUp` (`004ce400`):
- **A push** sifts the new node up past every parent with a greater f.
- **A pop** moves the top to the end, then takes the hole at the top down to the smaller of
  its two children, the right one when they are equal, to the last level. The node that was
  last then sifts up from there.
- **A node made cheaper** while open is found by a walk along the vector, comparing cells,
  and sifted up from where it is.

## A step

`004ccc80` says whether a walker on a cell may step to the next one: the next cell must be
on the map and its attribute neither 1 nor 5. For a diagonal step, so must both cells beside
it, the one along x and the one along y. A walker can't cut a corner.

## FindPath

`FindPath` (`004cc5c0`, `__thiscall`, nine arguments): the time, the start cell, where in it
the walker stands in sixteenths of a cell, the goal cell, the milliseconds a side takes,
and the `MovePath` to fill. It returns 1 when it found a path.

1. **The goal** is kept at +0x4 and +0x8. A start equal to the goal returns 0 at once,
   with the path untouched.
2. **Start:** the path's +0x10 is zeroed (`MovePath::Reset`, `004ccf60`), the map and the
   open list emptied, and the count of nodes zeroed. The start's node is opened with g 0
   and no parent, and pushed.
3. **While the open list holds a node,** pop the least.
   - **It is the goal:** the path is built (below) and the search returns 1.
   - **Otherwise,** each of the eight cells around it the walker may step to is visited, in
     this order, with its cost and the direction it is entered by:

     | Cell | Cost | Direction |
     |------|-----:|----------:|
     | x+1, y−1 | 14 | 5 |
     | x+1, y | 10 | 6 |
     | x+1, y+1 | 14 | 7 |
     | x, y+1 | 10 | 0 |
     | x−1, y+1 | 14 | 1 |
     | x−1, y | 10 | 2 |
     | x−1, y−1 | 14 | 3 |
     | x, y−1 | 10 | 4 |

     Then the node is closed.
4. **The list empty:** log "find path failed : no path found" (`0060c1d0`) and return 0.

**A visit** (`004cc9d0`) of a cell from a node, at g = the node's g plus the cost:
- **An unseen cell** takes the node as its parent, g, f and the direction, opens, and is
  pushed.
- **A cell seen before at a g no greater** is left alone.
- **A cell seen at a greater g** takes the new parent, g, f and direction:
  - if it is open, it moves up the heap;
  - if it is closed, it is marked open but **not pushed**, so it is never expanded again.
    The cells already reached through it keep their costs.
- **No node left** for a new cell: the search logs "find path aborted : buffer overflow"
  (`0060c1f4`) and returns 0.

A failed search leaves the path's steps as they were.

The estimate counts a diagonal as two sides, 20, where it costs 14. It overestimates, so a
search runs straight at the goal and may find a longer path than the shortest. It may also
find a cheaper way to a cell it has already closed, which is when that cell reopens without
going back on the heap.

**A start off the map** is still searched from. Its key, `width × y + x`, may be that of a
cell on the map, such as (−1, 4) on a map 12 wide and (11, 3). That cell then shares the
start's node, closed at g 0, and is never entered.

## The path

`MovePath` is `CGameActor`+0x170 (0x1c bytes):

| Offset | Field |
|-------:|-------|
| +0x4 | a `std::vector` of steps, 16 bytes each: the cell, the direction, the time |
| +0x10 | zeroed by `Reset` |
| +0x14, +0x18 | where the walk starts, in cells, as floats |

**The start point** is the sixteenths times 0.0625 (`005e8a7c`). When the start cell is
above 0 the cell less 0.5 (`005e88e8`) is added: a cell 0 adds nothing. The x87 runs at the
main thread's single precision, so each product and sum rounds to a float.

**The steps** (`004ccda0`) are the goal's chain of parents, start first, each with its
cell and direction. The vector is resized to their number. The start's direction is 0: its
node, always the first taken, is never written there. The times:
- the start: the time given;
- the second: the time given and the distance from the start point to the second cell's
  corner, `sqrt(dx² + dy²)` (`004cced0`, `SqrtFloat`), times the speed, truncated;
- each after that: the step before, and the speed for a side (an even direction) or the
  speed times 1.414 (`005e8a80`), truncated, for a diagonal.

The times are unsigned and wrap.

**`GetEndKey`** (`004cd1b0`) is the last step's time.

**`FindSegment`** (`004cd1f0`) finds the step a walker heads for at a time: its time, cell
and the index of the step left. Times compare unsigned.
- **Before the first step's time:** the second step, index 0.
- **From one step's time to the next's:** the next step, the first one's index.
- **Otherwise:** the last step, index −1.

## The callers

**`CGameActor::FindMovePath`** (`00506b20`), from the actors' `vf02` (`CGameActor`, `CPc`,
`CGrannyPc`, `CPlayer`): `FindPath` into the actor's path from the actor's move time,
+0x18c. When the path is found but would already have ended by the server's time
(`ClientState` `00593200`: `timeGetTime`, less 72 and the offset at `ClientState`+0x504),
the actor is late. It searches again from the server's time, at ten thirteenths of the speed
(the speed × 10 / 13, as signed integers), and that time becomes its move time.

**`CGameMode::IsPathFound`** (`00511ec0`), from `CGameMode` `00511f40` and `00512310`: whether
`FindPath` finds a path, from `timeGetTime` at 150 ms a cell, into a path of its own that it
throws away.

## In the port

`src/world/path_finder.{h,cpp}`, the library `ragexe_world`:
- `PathFinder`, with `FindPath` returning which of its four ends it came to;
- `MovePath`, with `EndTime` (`GetEndKey`) and `FindSegment`;
- `FindMovePath`, with the move time and the server's time as arguments;
- `IsPathFound`, with the time as an argument.

`ragres find-paths GAT QUERIES` runs a script of queries on one finder and one path.

## Checks

`tools/path_oracle.py` loads each map's `.gat` (data.grf and the patches) with the original
`C3dAttr::Load` in the emulation oracle and hands it to `g_PathFinder`. A script made from the
map's cells then runs on the finder and one actor's path:
- **`FindPath`** between cells near each other and far apart, onto cells that can't be
  walked, from the map's edges and from outside it, and from a cell to itself;
- **`FindMovePath`**, both on time and late;
- **`IsPathFound`**;
- **`FindSegment`** at times before, along and after the path;
- **`Z value`**, setting the path's +0x10 (the vectors only);
- **`W …`**, a walk of the path ([walk.md](walk.md#checks), `tools/walk_oracle.py` only).

The sub-cell points, speeds and times vary, wrapping included. Every result, the message
logged and the whole path after each query match `ragres find-paths`:
- **All 287 maps** and 109,453 queries, in 2 min 40 s;
- **the searches:** 17,612 found, 11,976 with no path, 18,690 out of nodes and 3,361 from a
  cell to itself;
- **the rest:** 5,761 `IsPathFound` and 52,053 `FindSegment` queries.

`tools/path_vectors.py` writes `tests/unit/path_vectors.inc`:
- **The maps:** six small maps: an open field, walls of attributes 1 and 5 among walkable 3
  and 6, gaps only a diagonal could pass, a closed room, a random maze, and a field too
  wide to cross with 149 nodes.
- **The queries:** each map's chosen queries and the oracle's mix, with their results and
  whole paths. Then queries whose results each hang on one detail, found by running the port
  against variants of it:
  - a tie between the two children in the heap's pop;
  - a closed cell reached more cheaply, and the costs and order of the neighbours;
  - a search that needs exactly the 149th node, and a diagonal's 1.414 against √2;
  - a path ending just at the server's time;
  - the path's +0x10 set before a search (`Z value` in the script), zeroed by it and kept by
    one from a cell to itself;
  - a start off the map whose key is that of the goal, which then can't be reached.

`tests/unit/path_test.cpp` replays them.

The actors walk the path in [walk.md](walk.md).
