# Picking objects

The mouse finds the actor, item or skill under it through records that their drawings leave.
Each pickable object (+0x89) that drew an item passes its bounds on screen to `vf05`
([actor.md](../render/actor.md#the-point-on-screen)). `vf05` turns them into a record in
`CActorPickNode`, the global `g_CActorPickNode` (`0067ca60`). Each frame `CGameMode`
(`00515d10`) finds the record under the mouse there. The cell under the mouse is found
apart, by a ray ([scene_graph.md](../render/scene_graph.md#picking)). The port is
`src/world/actor_pick`.

## The record

0x28 bytes, allocated by `vf05` and freed when the tree is cleared:

| Offset | Field |
|-------:|-------|
| +0x0, +0x4 | the left and top |
| +0x8 | the depth: the point's 1/z plus the bias |
| +0xc, +0x10 | the right and bottom |
| +0x14 | the depth again |
| +0x18 | the id: `CGameActor`+0x1b4, `CItem`+0xb0, `CSkill`+0x280 |
| +0x1c | the job (`GetJob`), 0x580 for an item |
| +0x20 | the kind: 0 an actor, 1 an item, 2 a skill, 3 a monster whose +0x1f4 is 20 |
| +0x24 | `CGameActor`+0x224, 0 for the others |

**The rectangle.** The bounds are left, top, right and bottom, then the 1/z of the object's
point (the drawing's `1 / z`, `005e25bc` over the depth). The left is the lesser of the
bounds' left and right, by `fcom`; an unordered pair keeps them as they are. The top and
bottom are copied.

**The least size.** When the width, `__ftol(right − left)`, is short of a minimum, both ends
move out by half the shortfall, the halving C's (toward zero); the height the same. The
minimum is `__ftol` of the renderer's width over 640 (`SceneRenderer`+0x14) times 40 for an
actor (`0067cf20`) or 34 for an item (`0068602c`) or a skill (`00687fa8`). Each is worked out
the first time it is needed and kept.

`__ftol` (`005c2084`) stores a 64-bit integer (`fistp qword`) and returns its low dword. So a
width of 2³¹ or more wraps, and NaN or one beyond 2⁶³ gives 0. The shortfall wraps as x86's
subtraction does.

## The bias

The bias ranks the records: the depth sum decides between rectangles under the mouse, and the
bias outweighs any 1/z.
- **An item**: 300 (`005e2738`).
- **A skill**: 350 (`005ea084`).
- **An actor** (`CGameActor::OnScreenBounds`, `00502cc0`), by its job and two switches,
  Shift and the aimed skill. Players come first when exactly one of them is on, monsters
  otherwise:

| Actor | Monsters first | Players first |
|-------|---------------:|--------------:|
| a player (job below 45), not on a guild-war map | 100 | 400 |
| on a guild-war map, a player of the player's guild | 100 | 400 |
| on a guild-war map, any other player | 400 | 100 |
| an NPC (45 to 999) | 200 | 100 |
| job 1000 | 0 | 0 |
| a monster whose +0x1f4 is 20 | 50 | 50 |
| any other monster (above 1000) | 400 | 200 |

The two switches:
- **Shift:** `00685fec`, the high byte of `GetAsyncKeyState(VK_SHIFT)`, taken each frame just
  before the find (`00515d10`).
- **The aimed skill:** `00686020`, set while `CGameMode` aims a skill (+0x360) that
  `IsSkillIdInFlagList` (`004d9920`) names, and cleared when the aim ends (`SendMsg`).

**The guild-war map** is `CWorld`+0x60 (`ClientState` `005aa630`). Packet 0x199 sets it with
its type 3, and 0x1d6 sets it on the maps that `ClientState` `005aacf0` names. There a player
is of the player's guild (`ClientState` `005aa680`) when its guild (+0x23c, `vf39`) is not 0
and is the player's (`CWorld`+0x3c). With no player, none is.

## The tree

`CActorPickNode` (0x30 bytes):

| Offset | Field |
|-------:|-------|
| +0x4 to +0x10 | the left, top, right and bottom, as integers |
| +0x14 to +0x20 | four children, or none |
| +0x24 | a `std::list` of the records' pointers |

- **Build** (`004ce8e0`; depth, rectangle): clears the node, keeps the rectangle and, while
  the depth isn't 0, builds four children at one less. A negative depth recurses without end.
- **The children's rectangles** (`004ce820`) split at half the right and half the bottom, not
  at the middle: 0 keeps the left and top up to them, 1 the right part, 2 the bottom right,
  3 the bottom left, each half plus 1 where it starts. Below the root, where the left isn't
  0, the halves miss the node. A node's left and top are kept but never read.
- **The quadrant of a point** (`004ce6c0`, integers): right of half the right is 1 or 2,
  below half the bottom 2 or 3.
- **Insert** (`004ce570`): down the children to a leaf by the quadrant of the record's
  top-left corner (each `__ftol`), then to the end of its list.
- **Find** (`004ce5f0`; x, y as floats): in a leaf, the record containing the point
  (`BoxContainsPointXY`, `004ce4a0`, strictly inside) with the greatest depth sum, above 0;
  the first of equals. In a node, the child of the point's quadrant, then the first other
  child to answer, in order. None is 0.
- **Clear** (`004ce710`): frees the records and empties the list, and deletes the children.

**In the game,** `CGameMode` `0050dc90` builds the tree at depth 1 over the screen when a
world is entered. But each drawn frame of `CGameMode::OnFrame` (`0050d400`) clears it before
the drawing, children and all, so from the first drawn frame on it is one list. The find
runs earlier in the frame (`00515d10`), so it finds the last drawn frame's records: the
record under the mouse with the greatest depth sum, the first of equals.

`00515d10` hands the record to `005149c0` and `00514900` (when no window is under the mouse),
and to `00513c20` or `00512d00` (its id).

## In the port

`src/world/actor_pick.{h,cpp}`, in `ragexe_world`:
- `PickRecord`, `PickKind` and `PickPriority` (the switches, the guild-war map and the
  player's guild);
- `PickMinimumSize`, `ActorPickBias`, and `ActorPick`, `ItemPick` and `SkillPick`, each
  object's `vf05`;
- `ActorPickNode`, the tree, with `Build`, `Clear`, `Insert` and `Find`.

`GameActor` (`src/world/actor_walk.h`) gained the id, +0x1f4, +0x224 and the guild. The
drawings' `ActorBody` keeps the point's 1/z (`inverse_z`) beside the bounds.

## Checks

`tools/pick_vectors.py` writes `tests/unit/pick_vectors.inc` from the original in the
emulation oracle:
- **400 objects' records:** `OnScreenBounds`, `CItem::vf05` and `CSkill::vf05`, each taken
  from the real `g_CActorPickNode` after the call. The bounds are ordered, swapped, too
  small, or beyond 2³¹. The jobs cover players, the portal, NPCs, 1000, monsters and the
  mounted 7 and 14. Also varied: +0x1f4, the guilds, Shift, the aimed skill, the guild-war
  map, the player or none, the renderer's width and whether the object is pickable.
- **41 tree scripts:** builds at depths 0 to 2 over four screens; inserts of 0 to 20
  records, some with unequal depths or a depth of 0 or below; 1,267 finds inside, on the
  edges and outside of them; clears and refills. The last script's answer hangs on a
  record whose left is just the split.

`tests/unit/pick_test.cpp` replays them.

**Not ported:** what `CGameMode` does with the record: the cursor, the name shown and the
click, which come with the game mode.
