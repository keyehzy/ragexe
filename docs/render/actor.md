# Actors' sprites

Characters, monsters and NPCs are sprites: each frame of an action ([act.md](../formats/act.md))
places images of a sprite ([sprite.md](../formats/sprite.md)) around the actor's position.
`CRenderObject`, the base of every actor, projects that position and turns each image into
a screen-aligned quad in the scene lists ([scene.md](scene.md)). The port is
`src/render/actor`.

This page covers the base class's drawing, which NPCs and monsters use. Player
characters layer several sprites (`CPc::RenderActFrameItems`) and are not ported yet.

## The scales

Five globals carry the projection's and the camera's part in the sprites:

| Global | Name | Set by | Value |
|--------|------|--------|-------|
| `0061470c` | size | `SetSize` | the screen's height × 0.0017447917 (`005e26ac`) |
| `0060564c` | slope down | `SetSize`, then `CView::Step` | 0.16 (`00605648`) / the height ratio, then 0.16 tan p / the height ratio |
| `00605650` | slope up | `SetSize`, then `CView::Step` | the same with cot p |
| `00614704` | layer step | `CView::Step` | sin p / sin 45° × 300 / distance × 9·10⁻⁶ (`005e8ac0`) |
| `00614708` | depth bias | `CView::Step` | 300 / distance × 3.6·10⁻⁵ (`005e8abc`) |

- **p** is the camera's pitch, negated and truncated to whole degrees for `SinDegrees` and
  `CosDegrees`.
- **The height ratio** is `SceneRenderer`+0x18, the height over 480.
- **`CView::Step`** (`004cf7d0`) sets the four after the camera has moved, every frame.

## The direction and the frame

**The direction.** `CGameObject::vf04` (`005000d0`) turns a facing into one of eight
directions:
1. d = −(the camera's yaw) − facing, both in degrees;
2. d is wrapped into [0, 360);
3. d × 1/45 (`005e9518`) is truncated.

**The frame.** `CRenderObject::vf08` (`00500830`) runs once a frame:
- **The action** (+0x24) becomes the base action (+0x20) plus the direction of the facing
  (+0x38).
- **The time:** t = (timeGetTime − the start +0x70) / 24 (`005e9000`). The milliseconds are
  exact on the x87 and the product is rounded once.
- **The frame** (+0x28) depends on the play mode (+0x54). f is t over the delay (+0x44) and
  n is the action's frame count, at least 1:
  - **0, looping:** ⌊f⌋ mod n.
  - **1, once:** ⌊f⌋ mod n. When f / n reaches 1, the last frame; when it also reaches
    +0x4c, finished (+0x81).
  - **Any other:** the frame is left alone.
- **Finished or held** (+0x81, +0x82): the frame is only kept within the action.

**The delay** is set by `vf07` (`00500de0`) when an action starts: the action's delay from
the `.act`, at least 1, times +0x50.

## The body

`CRenderObject::DrawBody` (`00500f70`) takes the view matrix.

**Placing the object:**
1. **Project** the position (+4) through the view; the depth z is at least 10.
2. **Screen point:** (sx, sy) = scale · (x, y) / z + centre.
3. **Pixel scale:** k = (1/z) · `SceneRenderer`+0 × 1/7 (`005e9564`). The pixel scale
   +0x48 is k / size.
4. **The x offset:** +0x5c × width ratio × +0x48 × the object's scale (+0x3c), in screen
   pixels.
5. **The depth bias:** +0x48 × depth bias.

**Each item** of the current frame with an image:
- **Its edges,** in the image's pixels, its own scale, the object's scale and k:
  - top: y;
  - bottom: (halved y + 1) × height + y, plus 1;
  - left and right from x and (halved x + 1) × width + x, the right one plus 1.

  Flag 1 mirrors the item by swapping which of x's edges is left.
- **Kept** when the screen's height is above the top, the bottom is above 0, the width is
  above the left (the right when mirrored), and the other side is above 0.
- **The rhw of the top and bottom** depends on the flags:
  - **+0x86 (flat):** every layer takes one rhw: (index × step + 1/10 − step × 20) +
    +0x64 × step;
  - **otherwise:** 1 / ((edge − sy) × slope down + z), or with +0x80 1 / (z − (edge − sy) ×
    slope up). To that is added (+0x64 + index + 3) × layer step, then the depth bias.

  Later layers come nearer, so they draw over earlier ones.
- **The colour:** each channel is the item's × the object's (+0x7c) / 255, truncated. Red,
  green and blue are also multiplied by the ground's colour, `GetGroundColor`
  (`00500e60`). When the object is keyed (+0x83) with a key colour (+0x68) other than
  0xffff0000, the colour is 0xffff0000 instead.
- **The angle:** the item's plus +0x60.
- **The flags:** 0x101 (translucent, writing depth) when the alpha is 255, 1 otherwise.
- **The palette** is the sprite's (+0x110), or with +0x34 that of the resource named by
  +0x14.

**The ground's colour** is always white in this client. `GetGroundColor` asks the ground
for the colour of the attribute cell under the actor (`C3dGround15::GetCellTint`,
`004bfc90`). That reads a vector at `C3dGround15`+0xac which nothing fills: only
`CWorld::LoadWorld` constructs it (`004c3d0b`), and `Reset` and the destructor clear it.
So it answers 1, 1, 1.

## The shadow

`CRenderObject::DrawShadow` (`00501550`) takes the view matrix and a scale.

**When it draws:** only with +0x88 set, a non-zero shadow scale (+0x40) and a non-zero
scale argument. `Render` (`vf03`, `00501870`) passes the object's scale, then draws the
body, both only while +0x84 is set.

**What it draws:**
- **The image:** the first image of `shadow.spr`, placed as the first item of
  `shadow.act`'s first frame.
- **Where:** projected at the attribute map's height under the actor
  (`C3dAttr::GetHeight`), not at the actor's own height.
- **The size:** the scale times +0x40, with no mirroring.
- **The x offset:** +0x5c, in screen pixels as it is.
- **The rhw:** 1 / (z − (edge − sy) × slope up) + 10⁻⁵ (`005e9568`), for the top and the
  bottom.
- **The colour:** the item's. **The flags:** 0x101.

## The quad

`SceneRenderer::AddSpriteQuad` (`0040e980`) takes a quad of top, left, bottom, right, the
bottom's rhw, the top's rhw, a colour, an angle and an image.

**The texture:**
- **A 32-bit image** has its own `CTexture`, spanning 0 to its +0x18 and +0x1c.
- **An 8-bit image** is drawn through the palette into the sprite texture cache:
  - **Pages:** 256 × 256 pages split into slots of a size class. The class comes from the
    image's width and height, each up to 32, 64, 128 or more.
  - **The lookup:** `FindCachedSprite` (`0040e540`) finds the image and palette, or
    `CacheSpriteImage` (`0040e130`) takes the oldest slot or a new page.
  - **Drawing:** `CTexture::DrawRleImage` writes each index's palette entry (16 bits,
    ARGB 1555). Index 0 and the rest of the slot are 0, which is clear.
  - **The extent** is the slot's over 256.

**The vertices,** top left, top right, bottom left and bottom right, into a `RenderQuad`
from the frame's pool (`NewQuad`, `00412b40`; a `TRIANGLESTRIP`):
- **Position:** x and y half a pixel in (+0.5).
- **A non-zero angle** turns the corners about the quad's centre, by `CosDegrees` and
  `SinDegrees` of the angle truncated.
- **Depth:** z = (1/rhw − 10) × 1500 / 1490 × rhw.
- **Specular:** 0xff000000. The texture coordinates are the extent's corners.

`AddPrimitive` then puts the quad in the lists and writes its fog
([fog.md](fog.md#each-vertex)).

## In the port

`src/render/actor.{h,cpp}`:
- **The scales:** `SpriteScales`, `SetSpriteScalesForSize` and `StepSpriteScales`.
- **The object:** `RenderObject`, the fields above.
- **Animation:** `ActorDirection` and `UpdateActorFrame`.
- **Drawing:** `DrawActorBody` and `DrawActorShadow`, which return the quads.
- **The quad:** `SpriteQuadVertices`, with the texture's extent as `SpriteTextureRect`.

**ragview** puts an actor where the player stands: `--actor SPRITE`, `npc\4_m_01` by
default. Each frame it:
1. steps the scales with the camera;
2. picks the action-0 frame for the time;
3. adds the shadow's and the body's quads to the scene lists, with their fog.

It uploads each 8-bit image as the cache would draw it, at the corner of a page of its
own.

## Checks

`tools/actor_vectors.py` runs the original in the emulation oracle, with the x87 at single
precision, and writes `tests/unit/actor_vectors.inc`. It records:
- **`SetSize`'s scales** at 8 sizes;
- **`CView::Step`'s scales and `BuildView`'s view** under 10 cameras;
- **240 objects,** under those views and with assorted flags, colours, scales and ground
  colours, drawn with a synthetic sprite (12 8-bit and 3 32-bit images) and action (4
  actions of 3 frames): 458 body quads and 72 shadows;
- **200 quads** through `AddSpriteQuad`, with and without angles, on both kinds of image;
- **120 directions and 200 frame steps.**

`tests/unit/actor_test.cpp` matches all 13,744 values.

**Not ported:**
- the screen bounds `DrawBody` keeps with +0x89 (`ExpandBounds`);
- `vf05` and `vf06`, the screen position it stores;
- `vf08`'s play modes beyond 0 and 1, which it leaves alone too;
- the texture cache's slots and pages, which change only where in a texture an image lies;
- the palettes of other resources (+0x34);
- `CPc`'s layered characters.
