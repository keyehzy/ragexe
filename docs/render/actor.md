# Actors' sprites

Characters, monsters and NPCs are sprites: each frame of an action ([act.md](../formats/act.md))
places images of a sprite ([sprite.md](../formats/sprite.md)) around the actor's position.
`CRenderObject`, the base of every actor, projects that position and turns each image into
a screen-aligned quad in the scene lists ([scene.md](scene.md)). The port is
`src/render/actor`.

This page covers the base class's drawing, which NPCs and monsters use. Player
characters layer several sprites ([player.md](player.md)).

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

## The point on screen

**Every drawing ends with the object's point on screen,** the (sx, sy) of its placing (step 2
above). It passes it on with one of two virtual calls:
- **`vf05`** when an item was kept, with the bounds below;
- **`vf06`** (`CRenderObject::vf06`, `00501840`) when none was. `vf06` stores the point,
  truncated (`__ftol`), at +0x90 and +0x94.

**`vf05` depends on the class:**
- **A bare `CRenderObject`'s** (`00501830`) returns at once, so it keeps its old +0x90 and
  +0x94.
- **`CGameActor`'s** (`00502cc0`) turns the bounds into the actor's pick rectangle when
  +0x89 is set, then calls `vf06` too. The rectangle carries a value of 50 to 400 by job
  and settings, and is grown to a minimum size (`0067cf20`). It is registered with
  `CActorPickNode`.

**The bounds** start at the screen's width and height and 0, 0: left, top, right and
bottom. While +0x89 is set, each kept item's quad widens them with `ExpandBounds`
(`004fffb0`), which takes:
- the lesser and greater of the quad's left and right (offset included);
- its top and bottom.

A player's drawing (`CPc::RenderActFrameItems`, [player.md](player.md#drawing)) widens them
only for the body and the head.

**An object not drawn** (+0x84 clear, or `CPc` without +0x85) gets its point from
`CRenderObject::ProjectToScreen` (`00500ed0`), which places it as the drawing does and
calls `vf06`. `CGameMode::MarkModelsOnPlayerRay` reads +0x90 and +0x94
([scene_graph.md](scene_graph.md#picking)).

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

## On the screen

`SceneRenderer::DrawActFrame` (`00410620`) draws an action frame straight onto the screen,
outside the lists. It takes (x, y, act, sprite, action, frame, scale, angle, colour, z
test, point filtering). Its callers are the mouse cursor, a mini game and six `CGameMode`
functions.

**The states,** once:
- `ALPHATESTENABLE` when the colour, as unsigned, is above `0xff000000`, so only an opaque
  colour that is not black; `ALPHABLENDENABLE`; `ZENABLE` as asked.
- With point filtering asked for and the device not already filtering by points
  (`C3dDevice`+0x1e4, `00623bb4`): stage 0's `MAGFILTER` and `MINFILTER` `POINT`, and
  `LINEAR` again at the end. `InitAudioAndDevice` sets +0x1e4 after `Init`, so in the client
  these are never set.

**Each item** of the frame (none when the action or frame is out of range,
`CActRes::GetFrame` answering an empty frame):
1. **The texture:** an 8-bit image's from the texture cache, with where it put the image;
   a 32-bit image's own `CTexture` (its record's +8). It is set when it is not the bound
   one (+0x50).
2. Stage 0's `ALPHAOP` `MODULATE` and stage 1's `COLOROP` `DISABLE`.
3. **The corners:** from the item's point to `(halved + 1) × size − 1` past it, each times
   the item's scale (once rounded, from the integer) and then the scale.
4. **The quad,** a `TRIANGLESTRIP` of `0x1c4` vertices:
   - **Unturned** (an angle of 0, or NaN, which compares equal): the corners plus `x + 0.5`
     and `y + 0.5` (`005e25b8`), left and right swapped for a mirrored item.
   - **Turned:** the corners turned by `CosDegrees` and `SinDegrees` of the angle truncated,
     then plus x and y and 0.5. The mirror is ignored, and the third vertex is the bottom
     right and the fourth the bottom left, so the strip folds over itself.
   - z 1e-6 (`0x358637bd`), rhw 0.999999 (`0x3f7fffef`), the colour as diffuse, specular
     `0xff000000`.
   - **The texture coordinates** are the cache's corners. A 32-bit image's are whatever
     the last 8-bit image of the call left, or 0 before any.

The draw skips no item: one whose image index is out of its sprite's table reads past the
table. The port skips it.

**The cursor.** `CMode::DrawCursor` (`004cb830`) draws `cursors.act` and `cursors.spr`
each frame of the login and game modes. `CMode`'s cursor fields:

| Offset | Holds |
|-------:|-------|
| +0x24 | when the action started (`timeGetTime`; `CMode::CMode` sets it) |
| +0x30, +0x34 | an offset added to the mouse |
| +0x38 | the action |
| +0x3c | the frame |

1. **The steps:** the time since the start in 24 ms units, over the action's step (20 for
   action 5, 10 for 7, 1 for 9, 6 for 10, 3 for 11, otherwise 4). The units are unsigned,
   so a start in the future wraps.
2. **The frame** (`GetFrameCount` counting an action of fewer than two frames as 1):
   - **Action 1** plays once: the steps over action 1's count, held at its last frame once
     they pass the count.
   - **Actions 2 and 9** play the same way while the mouse button is held
     (`g_DirectInputMouse`+0x28, `00628b70`). Released, the start becomes now (a second
     `timeGetTime`) and the frame 0.
   - **Any other** loops.
3. The frame goes to `UIWindowMgr` (`004b3970`, message 0x10).
4. **The draw:** at the mouse (`g_DirectInputMouse`+0x14 and +0x18, `00628b5c` and
   `00628b60`) plus the offset, each truncated. The scale is 1 with no angle, the colour
   `0xfaffffff` (so no alpha test), with the z test and point filtering.

`CActRes::GetActionDelay` is called and its answer dropped.

## In the port

`src/render/actor.{h,cpp}`:
- **The scales:** `SpriteScales`, `SetSpriteScalesForSize` and `StepSpriteScales`.
- **The object:** `RenderObject`, the fields above.
- **Animation:** `ActorDirection` and `UpdateActorFrame`.
- **Drawing:** `DrawActorBody` and `DrawActorShadow`, which return the quads. The body also
  returns the point on screen and the bounds (`ActorBody`). `ProjectToScreen`,
  `ScreenPosition` (`vf06`'s truncation) and `ExpandBounds` are separate.
- **The quad:** `SpriteQuadVertices`, with the texture's extent as `SpriteTextureRect`.

`src/render/cursor.{h,cpp}`: `DrawActFrame`, which returns the device calls and the quads
(`ScreenSprites`), asking for each image's texture; `Cursor` and `StepCursor`, the cursor's
frame and what it asks `DrawActFrame` to draw. The second `timeGetTime` is the same now.

**ragview** puts an actor where the player stands: `--actor SPRITE`, `npc\4_m_01` by
default. Each frame it:
1. steps the scales with the camera;
2. picks the action-0 frame for the time;
3. adds the shadow's and the body's quads to the scene lists, with their fog.

It uploads each 8-bit image as the cache would draw it, at the corner of a page of its
own. In a window it draws the client's cursor, the arrow of `cursors.act`, at the mouse
after the scene, through the same device state, in place of the system's.

## Checks

`tools/actor_vectors.py` runs the original in the emulation oracle, with the x87 at single
precision, and writes `tests/unit/actor_vectors.inc`. It records:
- **`SetSize`'s scales** at 8 sizes;
- **`CView::Step`'s scales and `BuildView`'s view** under 10 cameras;
- **240 objects,** under those views and with assorted flags, colours, scales and ground
  colours, drawn with a synthetic sprite (12 8-bit and 3 32-bit images) and action (4
  actions of 3 frames): 458 body quads and 72 shadows;
- **200 quads** through `AddSpriteQuad`, with and without angles, on both kinds of image;
- **120 directions and 200 frame steps;**
- **160 objects for their points on screen,** pickable or not, a fifth far off. For each:
  - the call `DrawBody` ends with (seen through code hooks), with its bounds and point;
  - +0x90 and +0x94 after the drawing, and after `ProjectToScreen`.

`tests/unit/actor_test.cpp` matches all 15,388 values.

`tools/cursor_vectors.py` writes `tests/unit/cursor_vectors.inc`. Its actions are real
`CActRes` structures, so `GetFrame`, `GetItem`, `GetFrameCount` and `GetActionDelay` run as
the original's. It records:
- **`DrawActFrame`, 320 calls** on 40 synthetic acts and sprites (8-bit and 32-bit images,
  some with no texture, some found by `CacheSpriteImage` after `FindCachedSprite` misses).
  The calls have assorted positions, scales, angles (0, −0, NaN and turned), colours either
  side of `0xff000000`, the z test, point filtering with the device's flag either way,
  bound textures and actions out of range. Some items' points and some positions lie past
  2^24, where rounding the integer to a float first would differ. Every call to the fake
  Direct3D device is recorded: 376 draws with their vertices.
- **`DrawCursor`, 500 times** over actions 0 to 12 (of 0 to 12 frames, two of fewer than
  two): assorted times since the start, some wrapping and many within three plays of the
  action, offsets, mouse points and button states. It records the
  frame and start it leaves, the message to `UIWindowMgr`, and what it asks
  `DrawActFrame` to draw.

`tests/unit/cursor_test.cpp` matches all 31,487 values, and checks that an item with no
image is skipped.

**Not ported:**
- `CGameActor::vf05`'s pick rectangle, which comes with the actors (M5);
- `vf08`'s play modes beyond 0 and 1, which it leaves alone too;
- the texture cache's slots and pages, which change only where in a texture an image lies;
- the palettes of other resources (+0x34).
