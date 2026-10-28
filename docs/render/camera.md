# The camera and the projection

The client looks at the world through `CView` (RTTI), which `CGameMode::OnInit`
(`0050cfb0`) creates (0x1c0 bytes). Each frame its view matrix follows the player, and the
`SceneRenderer`'s projection, fixed when the device is sized, places what it sees on
screen. `CGameMode` turns the mouse into the view's targets and keeps the player's chosen
pitch and distance.

The port is `src/render/camera` with `src/render/matrix`, and `res::AttrHeight` for the
ground under the eye.

## The projection

`SceneRenderer::SetSize` (`0040db70`) runs when `C3dDevice::CreateRenderer` sizes the
renderer to the screen. It derives:

| Offset | Field | Value |
|-------:|-------|-------|
| +0 | scale x | (width / 2) / tan 7.5°, times the aspect |
| +4 | scale y | (height / 2) / tan 7.5° |
| +8 | slope x | tan 82.5°, times the aspect |
| +0xc | slope y | tan 82.5° |
| +0x10 | aspect | height / width |
| +0x14, +0x18 | ratios | width / 640, height / 480 |
| +0x1c, +0x20 | centre | half the width and height, whole |
| +0x24, +0x28 | size | width, height |
| +0x2c, +0x30 | origin | as the centre |

- **The angles:** `fptan` of π (`005e2694`) · 15 (`005e26a0`, `005e26a4`) · 1/360
  (`005e26bc`). So the client's field of view is **15°** high: a long lens, far back.
- **The slopes** are the tangents of (90 − 15 · 0.5) · π · 1/180. `CullSceneGraph` builds
  the frustum's side planes from them.
- **Precision:** the precision control does not round `fptan`, so each scale is the half
  size divided by the unrounded tangent, rounded once.
- **Points** project to centre + scale · (x, y) / z ([ground.md](ground.md#drawing)).

`SetSize` also sets globals the actors' sprites read (`0061470c`, `0060564c`,
`00605650`; [actor.md](actor.md#the-scales)).

## The view

`CView`'s fields:

| Offset | Field |
|-------:|-------|
| +4 to +0x18 | the shake: active, start time, three amplitudes, duration |
| +0x1c, +0x20, +0x24 | pitch, yaw and distance, in degrees and world units |
| +0x28 | the point looked at |
| +0x34, +0x38, +0x3c | the target pitch, yaw and distance |
| +0x40 | the target point |
| +0x7c | the eye |
| +0x88 | up, (0, 1, 0) |
| +0x94 | the view matrix, world to view |
| +0xc4 | its inverse |
| +0xf4 | `ViewFrustum` |
| +0x1b4 | the world |

**World space has y downward,** so a negative pitch looks down on the ground.

**`Reset`** (vf01, `004cf1d0`) sets pitch −45, yaw 0, distance 300, both points at the
origin, and the same targets.

**Each frame** `Update` (vf04, `004cf260`) runs four steps:
1. **Target:** the target point becomes the player's position (the world's +0x3c, its
   +4).
2. **`Step`** (`004cf7d0`) moves the view toward its targets:
   - The target yaw is wrapped into [0, 360). The yaw then turns a tenth (`005e8ab8`) of
     the shorter way to it and is wrapped too.
   - The pitch, the distance and the point move a tenth of the way to their targets.
   - With `g_isFixedCamera` (`00685ecc`, a saved option) the point jumps to its target.
   - It also sets four scales for the actors' sprites from the pitch and distance
     (`0060564c`, `00605650`, `00614704`, `00614708`; [actor.md](actor.md#the-scales)).
3. **`ApplyShake`** (`004cf2a0`) jitters the view while a shake lasts. It uses `rand`,
   and is not ported.
4. **`BuildView`** (`004cfa10`) builds the matrices:
   - **The eye** is distance back from the point along row 2 of `MakeRotationX(pitch)`
     then `RotateY(yaw)`, angles as π (`005e8a90`) · degrees · 1/180.
   - **The ground:** where `C3dAttr::GetHeight` (`00427150`) at the eye's x and z is above
     it (a smaller y), the eye drops to that height, so it never sinks into the ground.
   - **The view** is `Matrix::MakeLookAt(eye, point, up)` (`00409bc0`), and the inverse
     `Matrix::Invert` (`00409ed0`).

**The view matrix** goes to `ViewFrustum::CullSceneGraph`, each visible node's ground
drawing (`C3dGround15::DrawCells`), the models' `Render` and the actors.

**`GetHeight`** interpolates a `.gat` cell's four heights ([gat.md](../formats/gat.md)),
along z and then x. World (0, 0) is the map's centre. Outside the map it gives 0.

## The matrices

`Matrix` is 4 × 3 for row vectors: rows 0 to 2 are the linear part and row 3 the
translation. Each builder starts from the identity at `00605598`.

| Function | Address | Result |
|----------|---------|--------|
| `MakeRotationX` | `004095d0` | (1,1) and (2,2) the cosine, (1,2) the sine, (2,1) its negation |
| `MakeRotationY` | `00409620` | (0,0) and (2,2) the cosine, (2,0) the sine, (0,2) its negation |
| `Multiply` | `00409d90` | a then b, the translation added last |
| `RotateY` | `00409770` | `Multiply(m, MakeRotationY(angle))` |
| `MakeLookAt` | `00409bc0` | columns: side = up × forward, true up = forward × side, forward; each normalised by 1 / `SqrtFloat`; translation −(axis · eye) |
| `Invert` | `00409ed0` | the 3 × 3 part's cofactors over its determinant; translation −t · inverse |

The sums run in the original's order, each operation rounded to single precision as the
client's x87 runs. The port follows that order term for term.

## Placing the camera

`CGameMode` keeps the player's pitch (called latitude) and distance:

| Address | Name | Holds |
|---------|------|-------|
| `00614884`, `00614888` | `g_viewLatitude`, `g_viewDistance` | the current ones |
| `0061488c`, `00614890` | `g_indoorViewLatitude`, `g_indoorViewDistance` | saved for indoor maps |
| `00614894`, `00614898` | `g_outdoorViewLatitude`, `g_outdoorViewDistance` | saved for outdoor ones |

The saved ones go to the registry under those names (`ClientState::SaveOptions`).

**On entering a map,** `PlaceCamera` (`0050d9e0`) works in four steps:
1. The view's point and target point go to the player.
2. **The map's kind:** `ClientState::IsIndoorMap` (`00598ba0`) looks the map's name up in
   the sorted `indoorRswTable.txt`. `CGameMode`+0x250 is set for an outdoor map.
3. **Indoors**, the yaw, its target and `CGameMode`+0x270 become 45°, and the indoor
   setting becomes current. **Outdoors**, the outdoor setting becomes current.
4. **Limits:** the current setting is clamped, then becomes the view's pitch and distance,
   current and target.

| | Indoors | Outdoors |
|-|---------|----------|
| latitude | −55 to −35 | −65 to −25 |
| distance | 230 to 300 | 230 to 400 |

The constants: −45 (`005e978c`) and 45 (`005e93c0`) around a range of 10 (`005e3bb4`)
or 20 (`005e9560`); distances 230 (`00614878`), 300 (`00614880`) and 400 (`0061487c`).

**On leaving,** `SaveViewSetting` (`0050db70`) copies the current setting back to the
kind's.

## The mouse

`ProcessCameraInput` (`005156d0`) runs once a frame, by the mouse state at `00628b74`.
Afterwards the caller (`00515d10`, at `00515f3d`) saves the mouse position in
`CGameMode`+0xc4 and +0xc8, so each frame's movement is measured from the last.

**State 2, dragging:**
- **Turning:** the target yaw turns by the x movement (`AddToAngle`, `004cf230`).
  Indoors it stays within 20° of +0x270 instead, unless `ClientState` option 6 is set.
- **With Shift** (`GetAsyncKeyState`), the target pitch moves by half the y movement,
  within the latitude limits, and becomes the current latitude.
- **With Ctrl,** the target distance moves the same way, within the distance limits
  unless option 5 is set, and becomes the current distance.
- Half the movement is C's division, toward zero.

**State 4, resetting:**
- **Ctrl** sets the target distance to 300;
- **Shift** sets the target pitch to −45;
- **outdoors with neither**, the target yaw goes to 0.

## Checks

`tools/camera_vectors.py` writes the original's results in the emulation oracle, with the
x87 at single precision, to `tests/unit/camera_vectors.inc`:
- **`SetSize`** at ten sizes, from 1 × 1 to 1920 × 1080;
- **`GetHeight`** at 48 points of a 128 × 96 map, inside, on the edges and outside;
- **80 frames** of `CView::Update` following a wandering player:
  - with turns past the half circle;
  - with targets set as `CGameMode` sets them;
  - ten frames with `g_isFixedCamera`;
  - one frame off the map.

  Some frames put the eye below the map's ground.
- **`PlaceCamera` and `SaveViewSetting`** for eight saved settings, indoors and outdoors,
  inside and outside the limits;
- **60 calls to `ProcessCameraInput`** across both states, Shift, Ctrl, both kinds of
  map and both options.

The port matches every field bit for bit (`tests/unit/camera_test.cpp`).

**Not ported:** the shake. The sprites' scales from `SetSize` and `Step` are in
`src/render/actor` ([actor.md](actor.md#the-scales)).
