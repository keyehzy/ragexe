# An NPC's drawing

Monsters and NPCs (`CNpc`) draw through **`CNpc::Render`** (`vf03`, `00540610`), which
`mg::CMGNpc` and `mg::mgType1::CMonster` share (their vtables `005e8f54` and `005e91d8`). It
takes the view matrix, as each object's `vf03` does when `CView::DrawWorld` walks the world's
list ([lists.md](lists.md)). Players draw through `CPc::Render`
([render/player.md](../render/player.md#drawing)), items through `CItem::Render`
([items.md](items.md)). The port is `src/world/actor_draw`.

## By job

The job (`GetJob`, `00503490`: 7 and 14 mounted as 13 and 21) picks the way, through the byte
table `005408e4` into the jump table `005408d4`, for jobs 45 to 139:

| Job | What it draws | `CNpc::Update` ([update.md](update.md#cnpcs-way-by-job)) |
|----:|---------------|----------------------|
| 45 | with the body shown (+0x85): bounds to `vf05`, then with effects off (`00687ea8`) the shadow at three times (`005e29d8`) the scale; otherwise the point (`ProjectToScreen`) | nothing more |
| 104 | nothing | the effect it repeats |
| 111 | bounds to `vf05`, sized by +0x1ec and +0x1f0, whether shown or not | the hits, windows and state |
| 139 | nothing | nothing more |
| any other | as below | |

**Any other job,** below 45 and above 139 too:
1. nothing at all unless shown (+0x84), not even the point;
2. the shadow (`DrawShadow`, `00501550`) at the scale (+0x3c) when the colour (+0x7c) is
   opaque, its alpha 255;
3. the body (`DrawBody`, `00500f70`) with +0x85, otherwise only the point (`ProjectToScreen`,
   `00500ed0`).

So an actor whose alpha is below 255 (fading, [walk.md](walk.md)) has no shadow, and job 45
has its shadow only with effects off.

## The bounds

Jobs 45 and 111 draw no sprite, but hand `vf05` (`CGameActor::OnScreenBounds`, which makes the
pick record, [pick.md](pick.md)) bounds of their own. Both place the point as `DrawBody` does
([render/actor.md](../render/actor.md#the-direction-and-the-frame)): the view's depth z, at
least 10 (`005e9914`); the point (sx, sy); and the pixel scale p = 1/z × the projection's x
scale × 1/7 (`005e9564`) over the sprite size (`0061470c`), stored at +0x48. Then, with w and
h the projection's width and height ratios (`SceneRenderer`+0x14 and +0x18):

| Bound | 45 | 111 |
|-------|----|-----|
| left | sx − p·w·50 (`005e99c0`) | sx − (+0x1ec / 2)·p·w |
| top | sy − p·h·50 | sy − (+0x1f0 / 2)·p·h |
| right | p·w·50 + sx | (+0x1ec / 2)·p·w + sx |
| bottom | p·h·50 + sy | sy |

The halves are C's, toward 0. For job 111 the rectangle stands on the point and does not reach
below it. Its left and top take the halves exactly, as `fild` loads them. The right takes the
width again from the float `fst` stored it as, so a half that a float cannot hold (odd, past
2²⁴) rounds differently on the two sides. After the bounds, `vf05` takes the 1/z and the point.

## The port

`DrawNpc` returns what `CNpc::Render` does (`NpcDrawing`): the bounds for `vf05` with the pixel
scale (`NpcBounds`), the shadow's scale, and whether the body or only the point follows. The
calls come in that order. The placing is `render::PlaceOnScreen` and the pixel scale
`render::PixelScale`, which `DrawActorBody`, `DrawPlayer` and `ProjectToScreen` share.

## Checks

`tools/npc_draw_vectors.py` runs `CNpc::Render` in the emulation oracle, with the x87 at single
precision, on 420 `CNpc` objects. They vary:
- the job: the table's edges, the mounted 7 and 14, players, NPCs and monsters;
- +0x84, +0x85, the colour's alpha, the scale, and effects on or off;
- +0x1ec and +0x1f0: odd, negative, and past 2²⁴, among them halves that a float cannot hold;
- the projection (`SetSize` at five sizes) and the view (`BuildView` under six cameras), with
  positions near, far and behind the camera.

`DrawShadow`, `DrawBody`, `ProjectToScreen` and `vf05` are stood in for: each call is recorded
in order with its arguments (the shadow's scale; the bounds, 1/z and point), then +0x48.
`tests/unit/actor_draw_test.cpp` matches all 420 drawings.
