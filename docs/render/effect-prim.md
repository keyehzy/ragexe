# Effect ids and primitives

The server asks for an effect by id. The client turns the id into a `CRagEffect`, which may
play a `.str` ([effect.md](effect.md)) and may add primitives (`CEffectPrim`): rays,
cylinders, sprites flying out. The port is `src/render/effect_table` (which id plays what)
and `src/render/effect_prim` (the primitives and the effects that add them).

## Launching an effect

`CAbleToMakeEffect::LaunchEffect` (`00501e10`) takes the id, an offset and an angle:
1. **Nothing**, when effects are off (`00687ea8`) or the id is outside 0 to 60000. Some
   ids from 0x23 to 0xa4 (the byte table at `00502008`) play even with effects off.
2. **A new `CRagEffect`** (0x17be8 bytes), the offset turned by the angle.
3. **`InitEffect`** (`0054eee0`) with the id as its type: it keeps the actor at +0xa0,
   the actor's position at +4, the id at +0xac and the duration at +0xc0.
4. **The world's object list:** the effect joins its end.

## Which id plays what

`InitEffect` picks a `.str` by a switch over the ids 10 to 0x150 (the byte table
`0054f854` into the jump table `0054f690`); the names are strings at `006184..` to
`00618b74`. Ids without a case, and 0 to 9, play no `.str`.

**Minimal effects.** With `g_ClientState+0x1840` (`00687eb0`) set, ten ids play a
lighter file:

| Id | Normal | Minimal |
|---:|--------|---------|
| 0xa | `Maemor.str` | `memor_min.str` |
| 0x42 | `Cure.str` | `cure_min.str` |
| 0x4b | `Gloria.str` | `gloria_min.str` |
| 0x4c | `Magnificat.str` | `magnificat_min.str` |
| 0x4d | `Resurrection.str` | `resurrection_min.str` |
| 0x55 | `LexAeterna.str` | `lexaeterna_min.str` |
| 0x58 | `Suffragium.str` | `suffragium_min.str` |
| 0x67 | `WeaponPerfection.str` | `WeaponPerfection_min.str` |
| 0x68 | `MaximizePower.str` | `maximize_min.str` |
| 0x70 | `Kyrie.str` | `kyrie_min.str` |

**Numbered variants.** Seven ids pick a file with the C runtime's `rand` (`005c2553`),
`rand() % n + 1`:

| Id | File | n |
|---:|------|--:|
| 0x19 | `Firewall%d.str` | 2 |
| 0x31 | `FireHit%d.str` | 3 |
| 0x34 | `WindHit%d.str` | 3 |
| 0x5c | `Meteor%d.str` | 3 |
| 0x6d | `Bubble%d.str` | 4 |
| 0x8d | `Pneuma%d.str` | 3 |
| 0xc7 | `Pong%d.str` | 3 |

**CP949 names.** The potions (0xcc to 0xd1, `빨간포션.str` "red potion" to `초록포션.str`
"green potion") and three skills (0xda `집중.str`, 0xdb `각성.str`, 0xdc `버서크.str`)
have Korean file names.

**The duration** is the dword at `00617148` + 4 × id, 0x152 of them: the frame
`CRagEffect::vf01` ends the effect at. Many are 9999 or more.

## The world's order

`CWorld::UpdateGameObjects` (`004c4a10`) runs each world step:
1. **vf01 of every object** in the list at +8, in order.
2. **A primitive an effect adds** (`CRagEffect::CreatePrimitive`, `0054f9a0`) joins the
   end of the list. So it runs in the same step as the effect that made it.
3. **An object whose vf01 answers false** is deleted.
   - A deleted effect (`~CRagEffect`, `0054d370`) sends message 0x18 to each of its
     primitives (`CEffectPrim::vf02`, `00586050`), which clears their owner.
   - A primitive without an owner answers false at its next vf01, later in the same pass.

**`CRagEffect::vf01`** (`0054d4c0`):
1. the id's own processing;
2. the frame +0xbc counts on;
3. true while it is below the duration.

## Primitives

A `CEffectPrim` is 0x15990 bytes:
- **The constructor** (`0057cfa0`) sets the defaults.
- **`InitPrimitive`** (`00586380`) takes the owner, the id and an offset:
  - the owner goes to +0xa4 and the id to +0x130;
  - the owner's position goes to +0x2ec and +4;
  - the offset is added to +0x2f8.
- **vf01** (`0057dce0`) and **vf03** (`00586070`) switch on the id, 0 to 0x43; the
  names are in `analysis/labels`.

The fields ids 0 to 2 use:

| Field | What | Default |
|------:|------|--------:|
| +4, +8 | the point on screen | |
| +0x28 | the image, an index into the textures at +0xa8 | 0 |
| +0x84 | visible (`CRenderObject`'s) | 1 |
| +0x124 | the colour, alpha and red, green, blue | |
| +0x134 | a period, for flags 2 and 8 | 0 |
| +0x13c | flags: 1 anchored, 2 stop growing, 4 absolute, 8 wander, 0x80 bounce | 0 |
| +0x140 | the age | 0 |
| +0x144 | the life | 100 |
| +0x148, +0x164, +0x1b4 | frames per image, images, whether they loop | 4, 1, 1 |
| +0x150 | the age the fade-out starts | −1 |
| +0x160 | the age the six changes take hold | −1 |
| +0x19c to +0x1a4, +0x1a8 to +0x1b0 | red, green, blue (ints) and their speeds | 0xff, 0 |
| +0x1ec, +0x1f0 | speed and acceleration along the heading | 0 |
| +0x204, +0x208 | the alpha's limits | 254, 0 |
| +0x23c | the heading, in degrees | 0 |
| +0x244, +0x268, +0x274 | the drawing's angle, its spin and the spin's rate | 0 |
| +0x254, +0x258, +0x25c | the alpha, its speed and the fade's scale | 254, 0, 1 |
| +0x284 to +0x28c | width, its speed and rate | 0 |
| +0x290 to +0x298 | height, its speed and rate | 0 |
| +0x2f8, +0x2fc | the distance travelled | 0 |
| +0x304, +0x308 | the offset from the owner's point | 0 |

### vf01

1. **Without an owner**, false.
2. **The id's update** (below); false if it ends the primitive.
3. **The age** counts on; true while it is at most the life.

**Ids 0 and 2** (`UpdatePrim_0`, `0057e180`) fly from the owner's point:
1. **The changes.** At age +0x160, each of the six values at +0x1f8, +0x1fc, +0x1cc,
   +0x1f4, +0x200 and +0x1d0 that is not 0.0001 replaces, in that order:
   - the width's speed, the height's speed and the speed;
   - the width's rate, the height's rate and the acceleration.
2. **The point.** The owner's screen point (+0xdc, +0xe0) plus the offset, or the offset
   alone with flag 4.
3. **The speeds** take their rates; flag 2 zeroes the four size speeds and rates once the
   age reaches the period.
4. **Bounce** (flag 0x80), below.
5. **The angle, width and height** take their speeds.
6. **The travel:**
   - x gains `SinDegrees(heading) × speed`;
   - y loses `CosDegrees(heading) × speed`;
   - the point gains the travel.
7. **The colours** take their speeds and are clamped:
   - green and blue to 0 and 255;
   - red wraps the other way: above 255 it becomes 0, below 0 it becomes 255.
8. **Wander** (flag 8): every period, the heading moves by `rand() & 15` less 8, and the
   period becomes `rand() % 10 + 10`.
9. **`UpdateAlphaAndColor`**, then **`AdvanceFrameByCount`**, below.

**Id 1** (`UpdatePrim_1`, `0057e4a0`) circles the owner's screen point plus the offset:
1. the heading, distance, spin and sizes take their speeds, which take their rates;
2. the point sits at that distance along the heading.

It has no changes, flags or colour speeds.

**Bounce.** With the alpha's speed negative, it turns when the alpha is at or below the
lower limit; otherwise when the alpha is at or above the upper one.

**`UpdateAlphaAndColor`** (`0057d6c0`):
1. **Keys.** With +0xb8 set, at each key frame (+0x2a4, a vector of floats) the alpha
   (+0x2b4) and its upper limit (+0x2c4) are set. With +0xb4 set instead, the alpha's
   speed is set.
   - A key index beyond the vector goes back to 0 with +0xb8.
   - With +0xb4 the index is not checked, and the frame read past the vector's end.
2. **Bounce** with flag 0x80.
3. **The owner's end.** The first time the primitive would outlive its effect (its life
   less its age above the effect's frames left plus one):
   - bounce stops;
   - the alpha falls to 0 over the effect's frames left.
4. **The fade-out.** At age +0x150, bounce stops and the alpha falls over the life left,
   times +0x25c.
5. **The alpha** takes its speed, held between the limits.
6. **The colour** packs the truncated alpha with red, green and blue, a byte each.

**`AdvanceFrameByCount`** (`0057d680`): every +0x148 frames of age, the next image. It
wraps when +0x1b4 is set, and otherwise ends the primitive on running out.

### vf03

Nothing unless visible. Both shapes are two screen-space triangles with flags 1 (the
translucent list sorted by the first vertex's rhw, [scene.md](scene.md#addprimitive)):
- **z** 10⁻⁶ and **rhw** 0.999999, so they draw over the world;
- **the texture** is the current image's.

**A ray**, ids 0 and 1 (`RenderScreenRay`, `00581520`):
- The width either side, from −height to +height, or from 0 to −2 × height with flag 1
  (anchored, growing up from the point).
- Turned by the angle about the point.
- Moved half a pixel up and left.
- The texture's v runs from the far end (0) to the near one (1).

**A diamond**, id 2 (`RenderRotatedScreenQuad`, `005812a0`):
- Half the width either side and half the height up and down.
- The side tips 5/6 of the way out (`005e9f90`).
- Turned by the angle.
- The bottom tip opaque white, the rest the colour.

## The hit spark

Effect id 1, `CRagEffect::ProcessHitSpark` (`00550c60`), each world step:
1. **The actor's point on screen:**
   - `TransformPoint` (`00550ff0`) by the view;
   - z at least 10;
   - `SceneRenderer::ProjectPoint` (`00551050`): x × (1/z) × scale_x + the origin, and
     y × scale_y × (1/z) + the origin.

   The result goes to +0xdc and +0xe0.
2. **On frame 0:**
   - `PlayWave` of `effect\EF_hit2.wav`;
   - eight rays (id 0, anchored), one every 45 degrees from 0, each with its own `rand`
     calls in turn:

| Field | Value |
|-------|-------|
| life | `rand() % 20 + 10` |
| angle and heading | its direction − 15 + `rand() % 30` |
| speed | (`rand() % 45` + 5) × 0.1 |
| acceleration | −½ × speed / life |
| width | `rand() % 15 + 5`, shrinking to 0 over the life |
| height | `rand() % 20 + 20`, growing by 1.5 a frame, plus 0.25 more each frame |
| offset | (sin, −cos) of the direction × `rand() % 5`, then 20 up |
| alpha | 0, rising by 254 / 8 a frame to 254 |
| texture | `effect\lens1.tga` for even rays, `lens2.tga` for odd |

`vf01` passes 0 for the argument that would tint the rays blue (red and green 120), so
they are always white. The effect lasts 50 frames (`00617148`); its rays live 10 to 29.

## Particles and cylinders

Ids 5, 6 and 0xd move in the world, not on the screen. Each step they start from the
owner's position plus their offset (+0x2ec), add their step to the distance travelled
(+0x2f8) and stand at the sum.

**The matrix** (+0xc4, the identity at first) carries their direction. Headings and
pitches turn into it through `DegreesToRadians` (`0040b380`), which reads the radian
table at `006252fc` the way `SinDegrees` reads the sines: a negative angle reads back
from the table's end, 361 + k.

### A particle, id 5

`UpdatePrim_5` (`0057e8e0`):
1. **The changes.** At age +0x160, the step takes the three values at +0x1d4 and its speed
   the three at +0x1e0.
2. **Speeds.** The speed takes the acceleration, the scale's speed its rate.
3. **The step.** With flag 0x400, the step takes its own speed (+0x31c). Otherwise, with
   flag 1 or past age +0x14c (−1 by default), it is the speed along the matrix's z row,
   plus the matrix's translation. Without either, the particle stays put this step and
   neither turns nor scales.
4. **The travel** takes the step; the angle its spin, the scale its speed.
5. **Drift** (flag 2): every period (+0x134), a new period of `(rand() & 7) + 12` and two
   drifts of 0.05 to 0.09, each made negative on an even `rand()`. Only the first drift
   moves the particle, along x, every step.
6. **Wide wander** (flag 4): every period, a speed of 0.15 to 0.49, the heading and pitch
   moved by −40 to +39, the matrix rebuilt (about y by the heading, then x by the pitch)
   and a period of 5 to 14.
7. **Wander** (flag 8): every period, the heading and pitch moved by −5 to +4, the matrix
   rebuilt the other way (about x, then y) and a period of 5 to 19.
8. **The end** (flag 0x100): false within 3 (on the ground, x and z) of the effect's target
   (+0xe8, `InitEffect`'s last arguments; `CRenderObject` `00500530`).
9. **Facing** (flag 0x20): the angle becomes +0x38 less the camera's yaw.
10. **`UpdateAlphaAndColor`**, then the history, then **`AdvanceActionFrame`**
    (`0057d620`): `AdvanceFrameByCount` against the frame count of the sprite's action.

### A thrown particle, id 6

`UpdatePrim_6` (`0057f270`):
- **Along the ground.** The speed takes its acceleration and is held at 0 or more. The
  step along the matrix's z moves x and z.
- **Up and down.** y moves by a fall speed (+0x21c) that takes its rate (+0x220) first.
- **Bounce.** While falling, on reaching the ground (`C3dAttr::GetHeight` below it), the
  fall speed turns to −2/3 of itself. With +0x168 set, it also plays
  `effect\EF_Coin%d.wav`, one of three by `rand()`.

### The history

Ids 5 and 6 keep their last +0x154 places in records of 0x8c bytes at +0x358:

| Offset | What |
|-------:|------|
| +0 | the position |
| +0xc | the twelve floats at +0x328 |
| +0x3c, +0x40, +0x44 | the distance +0x278, the scale +0x248, the heading +0x23c |
| +0x48 | the matrix |
| +0x78 | red, green and blue |
| +0x84 | the alpha |
| +0x88 | the colour, packed |

- **`ResetHistory`** (`0057d4b0`) fills every record from the primitive as it is.
- **`ShiftHistory`** (`0057da50`), after each update, moves each record back one place.
  Record k of n then has:
  - alpha × (1 − k/n) and its colour;
  - the current scale less (scale / n) × k × ½.
  
  The first record becomes the primitive as it is now. Red, green and blue are never moved
  or rewritten: they stay as `ResetHistory` left them, and the ported ids never read them.

### Drawing a particle

`RenderActFrameHistory` (`00581ff0`) draws the sprite's current frame at each record, last
place included:
1. **The record's point.** Projected through the view, z at least 10, onto the screen centre
   (+0x1c, +0x20). A pixel is (1/z) × scale_x × 1/7.
2. **Each item of the frame**, as `CRenderObject` draws an actor's:
   - sized by the record's scale;
   - kept when it reaches the screen;
   - the rhw of its top and bottom from `g_spriteSlopeDown`, plus 10⁻⁵;
   - the record's colour, or the item's own with flag 0x200;
   - its angle plus the primitive's;
   - flags +0x128 (1, translucent).
3. **`AddSpriteQuad`** with the sprite's palette.

### A cylinder, id 0xd

`UpdatePrim_13` (`00580df0`):
1. **The speeds** take their rates: the speed, both radii's (+0x22c, +0x230), the
   height's, the heading's (+0x260) and the pitch's (+0x264).
2. **The matrix.** The heading and pitch take their speeds, and the matrix is rebuilt: about
   x by the pitch, then y by the heading.
3. **The step** is the speed back along the matrix's y row.
4. **The heading** takes its speed a second time.
5. **The sizes.** The radii (+0x224 bottom, +0x228 top) and the height take their speeds;
   the height stops at +0x29c (100).
6. **Narrowing** (flag 1): at age +0x150, the top radius closes to 0 over the life left
   plus one.
7. **The alpha and the image.** `vf01` ignores whether the images ran out, so a cylinder
   whose images do not loop goes on past its last one.

`RenderCylinder` (`00584a20`) draws the side between the two rings:
- **The rings.** The bottom ring is at the point and the top ring the height up the
  matrix's −y.
- **The sides.** One every +0x1b8 degrees (36) from 0, while the angle is at most 360 less
  that. The angles index `g_sinTable` and `g_cosTable` directly, without reduction.
- **Two triangles a side**, bottom-left, top-left, bottom-right, then bottom-right,
  top-left, top-right.
- **The texture's u** steps a quarter a side, back to 0 at 1; v is 1 at the bottom.
- **Projection.** The view transform and `ProjectPoint` are written out three ways in the
  function: the first corner's sums start from z, and the second triangle's top-left
  scales x before dividing by z. The port follows each.
- **Flags 1** for each triangle; the colour is +0x124.

## Effect ids 0 and 2

Both act on their first frame only, aiming by 180 less the actor's facing (+0x38),
wrapped into 0 to 359.

**Id 0** (`ProcessRingBurst`, `005505a0`):
- **A cylinder** of `effect\ring_blue.tga`:
  - life 10, rising at 0.7, slowing to half;
  - radii 5 and 10, height 3.5;
  - fading out from age 5;
  - 10 up from the actor, pitched −90 and headed by the facing.
- **Particles**, unless minimal effects are on. Each is aimed with `rand()`: a heading in
  a band of 80 degrees, a pitch of −50 to +49, then 4 to 7 out along its z and 10 up.
  - Two particles (id 5) ahead of the facing: speed 0.6 to 1.49, life 6 to 29.
  - Two (id 6) behind it, thrown up at 0.3 to 1.19 and pulled down at twice that over
    their life.
  - All four: scale 0.6 to 1.59, fading out over their life, `particle1`'s sprite, three
    history records.

**Id 2** (`ProcessHit3`, `005510b0`):
- **Sound:** `effect\EF_hit3.wav`.
- **Two cylinders** of `effect\lens2.tga`:
  - life 15, bottom radius 1.5, top radius 1.5 and 4;
  - growing in height by 0.5 and 0.25 a frame, plus 0.2 more each frame;
  - fading from age 10.
- **Eight particles** ahead, speed 0.8 to 1.99, shrinking to nothing over their life.

**The particle sprite** is `이펙트\particle1` (`00618b98`, `00618b80`; "이펙트" is
"effect"). The particles show its frames one every four steps, looping (+0x148, +0x1b4).

## Ring walls

Twelve primitive ids draw rings of walls with `RenderRingWalls`. All twelve are ported,
with the twelve effects that add them.

### The ring records

Each primitive keeps four records of 0xb8 bytes at +0x156b0:

| Offset | What |
|-------:|------|
| +0 | active (a byte) |
| +2 | the span: the degrees of ring drawn (a short) |
| +4 | the walls' height; 6.001 marks low walls |
| +8 | the age |
| +0xc, +0xe, +0x10 | the angle the ring starts at, its alpha and its kind (shorts) |
| +0x14 | each of the 21 walls' height |
| +0x68 | the tilt: how far the walls lean out, in degrees from flat (a short) |
| +0x6c | the radius |
| +0x70 | a byte per wall, below |
| +0x88 | the last wall's corners, kept by the drawing |

### The updates

**Id 0x1d** (`UpdatePrim_29`, `00571a50`), waves. Each step, at the effect's position,
each active ring:
- turns 5 degrees (3 when +0x1f0 holds 99);
- widens by 0.3 (0.1);
- leans in by a degree, down to 40 (30);
- fades in for ten steps, by 20, or by 10 for a kind above 0, then out by 2;
- swells its walls: wall i is the height × sin 9i, plus 0.15 of that times the sine of the
  age, a quarter turn later for each ring.

**Id 0x27** (`UpdatePrim_39`, `0056bb30`), rising rings. Each step, each active ring:
- turns a degree;
- raises its walls from age 200 to 290 along a quarter sine of the height, then stands
  them at full height. The sine is read as entry age + 161 of the negated cosine table at
  `00624210`, which runs on into the sine table;
- with kind 1, fades by 1 a step after 250;
- fades by 2 a step from 800, its height growing by 10 a step until the alpha is gone.

### Drawing

`RenderRingWalls` (`0056c0d0`) walks each active ring's span from its angle in steps of a
21st:
1. **The wall's foot** is on the circle at the radius, 2 up for low walls.
2. **Its top** leans out from the foot by the tilt (cosine out, sine up) to the wall's
   height, 2 more for low walls.
3. **The quad.** From the second step on, the foot and top join the last step's through
   `DrawPresetColorQuad`.

**Quirks:**
- A full ring's 21st step goes back to the angle it started at. A 22nd step follows
  (17 × 21 is 357, under 360) and draws a second, empty wall there.
- The angles index `g_sinTable` and `g_cosTable` directly, so a negative angle would read
  outside them.
- A span under 21 makes a step of 0 and never ends.

`DrawPresetColorQuad` (`00569fc0`) makes the quad two triangles:
- **Corners and texture:** the four corners are relative to the primitive. The first
  texture is used, its u the wall's 21st.
- **The colour** is a preset picked by the scale (+0x248) truncated:

| Preset | Red, green, blue |
|------:|------------------|
| 1 | 255, 175, 175 |
| 2 | 195, 195, 255 |
| 3 | 255, 255, 255, at alpha 80 |
| 4 | 100, 100, 255 |
| 6 | 255, 100, 100 |
| 7 | 100, 255, 100 |
| 8 | 255, 255, 170 |
| 9 | 140, 210, 140 |
| 10 | 255, 89, 182 |
| 11 | 170, 170, 255 |
| others | white |

  The alpha is the ring's.
- **Blends:** `SRCALPHA` onto `ONE` for a scale below 4 other than 1, onto `INVSRCALPHA`
  otherwise, stage mode 0, with `AddPrimitive`'s flags +0x128 (5: translucent, by its
  blends).
- **Projection:** the view transform is written out with x's sum starting from x. Two
  corners scale x before dividing by z; the port follows each.

### The effects

All four add one primitive on their first frame, lasting as long as the effect.
`AddRisingRings` places the effect at the actor on that frame only; the other three every
step.

| Effect ids | Function | Primitive | Rings |
|-----------|----------|----------:|-------|
| 0xe1, 0xec, 0xed, 0xee | `AddRingWaves` (`00573840`) | 0x1d | radius 1 to 4, height 25, tilt 80, white |
| 0x105 | `AddSlowRingWaves` (`005739f0`) | 0x1d | the same, slow |
| 0x107 | `AddRingWaves`, kind 1 | 0x1d | the same, fading in by 10 |
| 0xe7, 0xf7, 0x103, 0x104 | `AddRisingRings` (`00568ec0`) | 0x27 | aged 0, 30, 60, 90; height 120; tilt 89 |
| 0xf8 | `AddFadingRings` (`00569150`) | 0x27 | aged 200, kind 1, radius 6 up, tilt 83 down, preset 5 |

**The textures:**
- 0xe1 and 0x104: `ring_red.tga`;
- 0xe7, 0xec and 0xf7: `ring_blue.tga`;
- 0xed and 0x103: `magic_green.tga`;
- 0xee: `ring_white.tga`;
- 0xf8, 0x105 and 0x107: `ring_yellow.tga`.

**`AddRisingRings`' variants** (0, 1, 2, 3 for 0xe7, 0xf7, 0x103, 0x104):
- The radius starts at 2, or 11 for variant 1, plus half a step each.
- The alpha is 50, or 70 for variant 1.
- The preset is 4 for variants below 2, then 7 and 6.

### More rings

The other ten ids that draw ring walls each step their rings their own way. Each sets the
primitive at the effect's position first, except 0x36.

**The flag bytes.** Each record has a byte per wall at +0x70; the ids use them
differently:
- **0x17 and 0x19:** a wall settled at its full height.
- **0x39:** a wall's mode, 0 waving and 1 standing.
- **0x36:** the first byte marks the rise as done, and the second (signed) is the ring's
  peak height.

**Rising walls** (0x17, 0x19; `UpdatePrim_23` `0056b130`, `UpdatePrim_25` `0056bca0`):
- **Which rings.** From ring 3 alone when the scale is 3, otherwise every ring.
- **Turning.** Rings 0 to 2 turn by their index plus 3.
- **Fading in** for 20 steps: ring 3 by 8 (to 60, or 100 for 0x19), the others by 10 to
  180.
- **Fading out** over the last 40 steps of the life: by 5, ring 3 by 2 (3).
- **The walls** rise with the sine of the age to their share (sin 9i) of the height and
  settle on reaching it.
- **0x19's ring 3** settles its walls after age 90 without clamping them. From then on it
  waves at 0.55 of each wall's share plus 0.45 of it on the sine of the age.

**Turning rings** (0x18, `UpdatePrim_24` `0056bfa0`):
- **Turning.** The rings at 0 and 180 degrees turn 1 degree a step, the others 2.
- **Fading** in to 80 over 100 steps, then out by 5 over the last 20 of the life.
- **The walls** wave at their share plus 0.3 of it.
- **The wave's phase.** For the first two rings it is the age plus the ring's quarter
  turn. For the others it is twice the sum of the age and an eighth turn per ring.

**Spreading and growing rings** (0x1c, 0x22; `UpdatePrim_28` `005715b0`, `UpdatePrim_34`
`00571820`): a ring widens (0.07, 0.04) and leans in to 10 degrees, its alpha cut to 0 at
the limit. It fades in (by 10 under radius 4, by 20 under 2.5), then out (by 3, by 2).
Once faded it starts again: at radius 3.37 while the life has 30 steps left, and at
radius 2.14 every time; tilt 74. 0x1c waits for its first step. The walls wave at the full
height plus 0.3 of it.

**A wobbling ring** (0x30, `UpdatePrim_48` `00571940`): the kind turns a degree a step,
and the radius is 3 plus its sine. The angle turns 3 degrees a step; the walls wave as for
0x18.

**Lifting rings** (0x36, `UpdatePrim_54` `005716f0`), where the primitive stays put:
- **Turning.** Ring k turns 5 − k degrees a step.
- **The height**, from the first step: the second flag byte times the sine of twice the
  age.
- **Fading.** By 1 a step once twice the age passes 135, until 180. Then the first flag
  byte is set and the ring fades by 10.
- **The walls** wave at the height plus 0.3 of it. The phase runs three times faster for
  the last two rings.

**Waving walls** (0x39, `UpdatePrim_57` `0056b600`):
- **Turning.** A ring whose first wall is in mode 1 turns by its index plus 8.
- **Fading** in by 5 to 180 for 16 steps, and out by 2 from the age in its kind.
- **Mode 0 walls** wave at three quarters of the height plus a quarter on a sine. The sine
  runs 4, 3 or 2 times the age for rings 0, 1 and the others, plus 34 degrees a wall.
- **Mode 1 walls** stand at the height.
- **Both** rise along the sine of the age for 90 steps.

**A spreading portal** (0x3c, `UpdatePrim_60` `0056b930`), from the first step:
- **Spreading.** A ring widens by 0.5 a step. Past radius 7 it fades by 15 a step; for its
  first 10 steps it fades in by 24 to 240.
- **Starting again.** Once faded, the ring starts again from radius 0. It stops instead
  once ring 3's age passes 120 with ring 3's kind 1, or 1400 in any case. Ring 0 also
  counts ring 3's age, so an inactive ring 3 serves as the counter.
- **The walls** rise with the sine of 9 times the age for 10 steps.

**Closing rings** (0x3e, `UpdatePrim_62` `0056b500`):
- **Closing.** A ring closes in by 0.05 a step. At the centre it goes back to radius 10
  and alpha 0.
- **The height** is the radius, and the tilt is 90 less 9 times the radius.
- **Fading** in by 1 to 70, then out by 3 over the last 40 steps of the life.
- **The walls**, those without a flag, stand at the height.

**The effects:**

| Effect ids | Function | Id | Rings |
|-----------|----------|---:|-------|
| 200 | `AddRingTrio` (`0056d2c0`) | 0x17 | three open rings (span 315) of `ring_blue`, preset 4 |
| 0xde, 0xfc | `AddTurningRings` (`00572d90`) | 0x18 | four rings, radius 8 to 7.7, of `ring_black`, `ring_yellow` |
| 0xef to 0xf1 | `AddRandomRing` (`00573bb0`) | 0x22 | one ring, angle and alpha by `rand()` |
| 0x118, 0x11d | `AddTwirlRing` (`00573f80`) | 0x30 | one open ring of `twirl.bmp`, preset 8 or 4, height, angles and alpha by `rand()` |
| 0x130 | `AddTeleportationRings` (`00575b60`) | 0x36 | four rings of `Magic_Violet` peaking at 100, 70, 40 and 15 |
| 0x13a | `AddTeleportationWalls` (`005770d0`) | 0x39 | four rings of `ring_yellow`, heights 65 to 15, fading from age 60 |
| 0x13b | `UpdateGlassWall` (`00579790`) | 0x39 | three rings of `alpha_down`, angles by `rand()`, fading from 3080 |
| 0x13c | `AddReadyPortal` (`00578150`) | 0x3c | three low rings starting 10 steps apart |

**Notes on the effects:**
- **Positioning.** All but `AddTeleportationRings` keep the effect at the actor each step.
  `AddReadyPortal` does so every 14 steps, with `effect\EF_ReadyPortal.wav`.
- **Restart.** `AddRingTrio` starts again at frame 99999998 with a duration of 99999999.
- **The `.str`.** Id 0x13b also plays `SafetyWall.str` through `AdvanceStr`; the port's
  effect world leaves that to the `.str` player.
- **Quirks:**
  - The angles, tilts and kinds the updates read index the sine and cosine tables
    directly, without reduction; the check keeps them inside.
  - 0x36's height reads the sine of twice the age, past the table once the age passes
    180.

**Id 0xf8 never shows.** After `AddFadingRings`, `vf01` calls `AdvanceStr`, which answers
false without a `.str`, and `InitEffect` loads none for 0xf8. The effect ends on its first
step, and its rings, losing their owner, end later in the same pass.

## Facing squares

Eight primitive ids draw squares turned to face the camera with `RenderFacingSquares`
(`0057bab0`): 0x1f, 0x28, 0x2b, 0x2d, 0x2f, 0x33, 0x37 and 0x38. All eight are ported,
with the 30 effects that add them.

### The square records

The squares use the same four 0xb8-byte records at +0x156b0 as the rings, laid out their
own way:

| Offset | What |
|-------:|------|
| +0 | active (a byte) |
| +2 | the turn: the square's own rotation in degrees (a short) |
| +4 | 0x28 and 0x33: how far out the target is |
| +8 | the age |
| +0xc, +0xe | the heading it moves along, and its alpha (shorts); 0x1f keeps the age it starts fading at in +0xc |
| +0x10 | the camera angles, below (a short) |
| +0x18, +0x1c | the speed along the heading and the climb each step |
| +0x14 to +0x20 | 0x1f: two angles each walking towards a goal, below |
| +0x2c | 0x1f: the floor height |
| +0x30 | the mode; 2.0 draws the square red |
| +0x34 | the phase in degrees; the size swells by a twentieth of its sine |
| +0x6c | the size: the distance from the centre to a corner |
| +0x70 to +0x73 | bytes: the image index, a state, the colour and the shade |
| +0x88 | the centre |
| +0xa4 | the height it bobs or arcs about |

**The camera angles.** Every update starts at the game mode's `CView` (+0xb8). It writes
360 less the camera's yaw (+0x20) into the first record's +0x10, and 450 less its pitch
(+0x1c), reduced below 360, into the second's. Then it puts the primitive at its owner.

**Only the first record's colour and shade count.** They pick the colour of every
square in the primitive.

### Drawing

`RenderFacingSquares` draws each active record with an alpha above 0:
1. **The corners** sit at the turn and every 90 degrees on. Each step is reduced once
   past 360, but the turn itself is not.
2. **The size** is the size plus a twentieth of it times the sine of the phase.
3. **Facing the camera.** `RotateQuadPoints` (`0057caa0`) turns the corners about x by
   the second record's angle, then about y by the first's.
4. **The quad.** The centre is added, and `DrawQuad` (`0056a960`) draws it.

**The colours** (red, green, blue), and whether the quad blends onto ONE:

| Mode or colour | Colour | Onto ONE | Corner order |
|---|---|:-:|---|
| mode 2.0 | (200, 0, 60) | yes | A, B, C, D |
| 0 | white | yes | A, B, C, D |
| 1 | (150, 150, 250) | no | A, B, C, D |
| 2 | (130, 130, 250) | no | B, A, D, C |
| 3 | (200, 200, 100) | no | B, A, D, C |
| 4 | white | yes | B, A, D, C |
| 7 | white | no | B, A, D, C |
| 8 | (135, 255, 135) | yes | B, A, D, C |
| 9, shade 1 to 5 | (255, 135, 135), (255, 205, 135), (255, 255, 135), white, (135, 135, 255) | yes | B, A, D, C |
| 10 | (250, 250, 150) | no | B, A, D, C |
| 11 | (220, 250, 220) | no | B, A, D, C |

Any other colour or shade draws nothing.

**`DrawQuad`** is shared by 38 of the primitive renderers. It takes four corners, an
alpha, a blend, an image index, whether the corners are absolute, and a colour:
- **The corners** are relative to the primitive unless absolute; the squares' are
  absolute.
- **The triangles** are (A, B, C) and (D, C, A), with u and v from 0 to 1, starting at
  A's (0, 1).
- **The blend** is SRCALPHA onto ONE or onto INVSRCALPHA, with `AddPrimitive`'s flags at
  +0x128. The texture is the one at the image index.
- **The view transform** is written out four ways:
  - A's x is summed from z, and its y and z from x, then z, then y.
  - B, C and D are summed from z.
  - The second triangle's C is projected inline, with x scaled before the 1/z.

**The tables, read directly.** The squares index the angle tables without reducing the
angle. They are laid out in memory as:
1. the cosines (`00623c6c`);
2. their negations;
3. the sines (`006247b4`);
4. their negations;
5. the radians;
6. their negations.

Each is 361 entries long, so an angle past 360 reads on into the next table. `TableAt`
reads that layout. An index before the cosines would read the original's other globals,
and reads 0 in the port.

### The updates

**Clouds** (0x1f, `UpdatePrim_31` `0057b0f0`). Each active record whose mode is below 99:
- **Brightening.** For its first 80 steps it brightens by 3 a step in mode 1. In other
  modes it brightens by 2, but in mode 0 only while the owner is above the floor.
- **Fading.** Once the age passes +0xc, it fades by 1 a step.
- **Starting again.** Faded out, it starts at a new place: 25 to 225 out either way in
  mode 1, otherwise within 150 of the owner. In mode 0 with the owner below the floor, it
  starts at age 300.
- **Walking angles.** Two angles walk a degree a step towards their goals, between 0 and
  359. Past a goal, a new goal is drawn on the far side of it with `rand()`. A twentieth of
  each angle's sine drifts the centre in x and z.
- **The phase** turns a degree a step, for every active record.

**A boomerang** (0x28, `UpdatePrim_40` `00572350`), the first record:
- **Spinning.** It turns 15 degrees a step.
- **The trail.** Every step (colour 1) or every third step, the others take the record
  before's turn, size, phase, centre and image, with its alpha less 25 (the second record
  less 150). Each starts once the age passes 1, 2 or 3.
- **Out.** On its first step it heads for the target and climbs to its height until it has
  come its range.
- **Back.** Then colour 1 trebles its speed and swerves up to 44 degrees by `rand()`.
  Colour 0 instead turns 5 degrees a step until it heads within 4 degrees of the owner.
- **The end.** After that it fades by 12 a step and sinks.

**Thrown sparks** (0x2b, `UpdatePrim_43` `00571e90`). Each record:
- **Waiting.** It waits out its negative age, then turns 5 degrees a step backwards.
- **Heading.** On its first step it heads from the owner through the target, give or take
  30 degrees by `rand()`, reduced with C's remainder. An owner heading below 30 degrees can
  turn the heading negative, which reads before the cosine table.
- **Fading** in by 15 for 10 steps, then out by 3, until it goes out.
- **Moving.** It climbs by its climb and moves along its heading.

**A bobbing square** (0x2d, `UpdatePrim_45` `0057b6f0`), the first record:
- **The trail.** Every 6 steps each active record behind takes the one before's turn,
  phase, centre and image, with its size plus a half and its alpha less 50. It also takes
  colour 2 and mode 0.
- **Swelling and bobbing.** The phase turns 5 degrees a step. The square bobs 3 either way
  about 6 above the owner, on the sine of its heading, which turns a degree a step.
- **Colour 7** also turns the square 10 degrees a step.

**Sinking squares** (0x2f, `UpdatePrim_47` `0057b970`). Each record:
- **Fading.** It fades in by 25 for 10 steps, then out by 5, sinking a fifth a step.
- **Starting again.** Faded out, it starts again at the owner's height, or 5 above in
  state 1. Its first height is never set by `AddSinkingSquares`, so it starts at 0.
- **Turning.** The phase turns 5 degrees a step; in state 1 the square turns too.

**An arcing square** (0x33, `UpdatePrim_51` `005728c0`), the first record:
- **The trail**, as for 0x28, every other step.
- **Turning.** It turns 10 degrees a step and is never reduced past 360, so the corners
  read on into the negated cosines.
- **The arc.** It heads for the target over its steps (+0x71), climbing to the target's
  height. It is lifted on half a sine 10 high (colour 0) or 30.
- **Landing.** 5% past its steps it stays where it is and fades by 20 a step.

**A pulsing square** (0x37, `UpdatePrim_55` `005727d0`): the phase turns 3 degrees a step.
It brightens by 4 for 20 steps and fades by 1 after 100.

**Rising sparks** (0x38, `UpdatePrim_56` `00572020`). Each record:
- **Waiting.** It waits out its negative age, then turns 5 degrees a step backwards.
- **Moving.** It takes a random heading and rises by its climb.
- **Fading** in by 15 for 10 steps, then out by 3, until it goes out.
- **The flash.** In state 1 it shows the second image for two steps: at ages 35 and 36 for
  the first record, each next record 7 steps sooner.

### The square effects

All but the clouds keep the effect at the actor each step, and add their primitive on
the first frame unless noted. The primitives take flags 5, as long a life as the
effect, and +0x2f8 set to the effect's target where they head for it.

| Effect ids | Function | Id | What |
|-----------|----------|---:|------|
| 0xe5, 0xe6, 0xe9 | `AddClouds` (`005686c0`) | 0x1f | 40, 60 and 40 primitives of four clouds (`cloud4`, `cloud1` or `cloud2.tga`) about the player. 0xe5 is low, with a floor at −152; 0xe6 is high; 0xe9 is low and red. They start again at frame 99999998 |
| 0xf9, 0x109 | `AddBoomerang` (`00569610`, `00579af0`) | 0x28 | `shield_boomerang.bmp`, size 7, colour 0 (back to the owner); `blue_ivy.bmp`, size 2 + 0.4 × the level, colour 1 (off past the target) |
| 0x108, 0x113 | `AddThrownSparks` (`00576090`) | 0x2b | 5 and 20 primitives of four `thunder_center.bmp` sparks from the target on; 0x113 plays `effect\rog_back stap.wav` at frame 20 |
| 0x116 to 0x125 | `AddBobbingSquare` (`00574520`) | 0x2d | ten pictures over the actor, below |
| 0x11b | `AddSinkingSquares` (`00574890`) | 0x2f | two `safeline.bmp` squares |
| 0x12a, 299, 0x134 | `AddArcingSquare` (`00569ae0`) | 0x33 | an empty bottle (colour 8), an empty potion bottle (colour 9, shaded by the level) and a stone (colour 0), thrown to the target |
| 0x135 | `AddPulsingSquare` (`005698b0`) | 0x37 | `pikapika2.bmp` over the actor |
| 0x136 | `AddThunderSparks` (`00576310`) | 0x38 | 20 primitives of four `thunder_center.bmp` sparks |
| 0x14b, 0x14c | `AddHealingSparks` (`00576590`) | 0x38 | `pok1` and `pok3.tga` sparks every 4 frames to 20 |
| 0x138, 0x140 | `AddHealingWalls` (`005768c0`) | 0x39, 0x38 | two rings of `alpha_down.tga`, then sparks every 6 frames to 50 (25 for 0x140) |
| 0x139, 0x145, 0xfd | `AddHealingRings` (`00577480`) | 0x39, 0x38 | four rings of `ring_white` or `ring_blue`. Below 0xfd, sparks every 6 frames to 50; 0x145 adds four rings of `alpha_down.tga` on frame 1, and sparks every 9 frames from 2 |

**The bobbing squares** (variant, picture, colour):
- 0x116: variant 1, `zz.bmp`, size 5.
- 0x117: variant 3, `pocket.bmp`, colour 3.
- 0x119: variant 2, `melody_a` or `melody_b.bmp` by `rand()`, with a trail.
- 0x11a: variant 7, `twirl.bmp`, turning.
- 0x11c: variant 5, one of three stones (`레드젬스톤`, `블루젬스톤`, `옐로우젬스톤.bmp`),
  colour 4.
- 0x11e and 0x123: `melody_b` and `melody_a.bmp`.
- 0x120: variant 8, one of `spell_01` to `spell_08.bmp`.
- 0x121: variant 6, `idun_apple.bmp`, size 8, colour 4.
- 0x125: variant 4, `kiss.bmp`, size 15.

**The level** (+0xb8) is set through `CRagEffect::vf02` (`0054eba0`) from a message,
at most 50.

**Sounds** the effects play and the port leaves out:
- `effect\cru_shield boomerang.wav` (0xf9);
- `effect\mon_탄지신통.wav` (0x109);
- `_heal_effect.wav`, 150 up for 0x135;
- `effect\흡기.wav` (0x14c).

## Flat squares

Three primitive ids draw flat squares, lying level, with `RenderHorizontalSquares`
(`0056e6b0`): 0x1a, 0x25 and 0x40. They keep the ring records' layout and read:
- the angle (+0xc);
- the alpha (+0xe);
- the first wall's height (+0x14);
- the tilt (+0x68) as a swell phase;
- the radius (+0x6c).

**Drawing.** Each active record is one square of the first texture, white, relative to
the primitive:
- **The corners** sit 90 degrees apart, from the record's angle when the scale is 0, or
  otherwise from 23 degrees times the record's index.
- **Their distance out** is 0.8 of the radius plus a tenth of the radius times 1 plus the
  sine of the tilt. The 0.8 is a double (`005e9f10`), so the product is rounded once to
  single precision (`core::RoundedProduct`).
- **The height** is the first wall's.
- **The blend** is onto ONE when the scale is 0.

The original also writes the corners into the record from +0x88.

**The updates:**
- **0x1a and 0x25** (`UpdatePrim_26` `0056dfb0`, `UpdatePrim_37` `0056e010`) turn the
  first two records' tilts 3 degrees a step, active or not. 0x1a then steps its image
  (`AdvanceFrameByCount`).
- **0x40** (`UpdatePrim_64` `0056e070`) counts the first record's age. From its first step
  it fades by 1 every odd step.

**The effects:**

| Effect id | Function | Id | What |
|---:|----------|---:|------|
| 0xc9 | `AddTwinSquares` (`0056d990`) | 0x1a | two squares of `pikapika2.bmp`, radius 15, swelling half a turn apart, alpha 200, scale 4; starts again at frame 99999998 |
| 0xf2 | `AddSwellingSquare` (`00574ae0`) | 0x25 | one square of `effect\aaa copy.bmp`, radius 7, from a random swell phase, alpha 100, scale 5 |

**0x40 has no effect.** Its setup (`00574c40`) is never called. It would make a square of
radius 5 at alpha 150, heading by the target's x.

## In the port

**`src/core/crt_rand`:** `CrtRand`, `srand` and `rand`.

**`src/render/effect_table`:**
- `EffectStrName(id, minimal, rand)`;
- `EffectDuration(id)`.

**`src/render/effect_prim`:**
- `EffectPrim` with `MakePrimitive`, `ResetHistory`, `UpdatePrimitive` (vf01) and
  `DrawPrimitive` (vf03), for ids 0 to 2, 5, 6, 0xd, the twelve ring ids, the eight
  facing-square ids and the three flat-square ids. `PrimScene` carries the camera's yaw and pitch, the ground, the view,
  the projection and the sprite slope; `PrimDrawing` gets the triangles and the sprite
  quads;
- `ProcessHitSpark`, `ProcessRingBurst` and `ProcessHit3`; `AddRingWaves` (with `slow`
  for `AddSlowRingWaves`), `AddRisingRings`, `AddFadingRings`, the eight setups of
  [More rings](#more-rings), the eleven of [the square effects](#the-square-effects) and
  `AddTwinSquares` and `AddSwellingSquare`;
- `EffectWorld`, the world's list as effects use it:
  - `Launch` adds an effect;
  - `Step` runs a world step in the list's order;
  - `Draw` gives the primitives' triangles and quads;
  - `SetParticle`, `SetMinimal` and `SetPlayer` give it the particle sprite, the minimal
    flag and where the player stands.

**`src/core/angles`:** `DegreesToRadians`.

**`src/core/x87`:** `RoundedProduct`, a float times a double rounded once to single
precision, shared with the `.str` player.

**`ragview --effect-id ID`** plays the id's `.str` ([gpu.md](gpu.md)), and launches the id
again when its effect and primitives are done. `--minimal-effects` picks the `_min`
files. The effects thrown at a target aim 50 out either way along the ground. The
installed 2003 data lacks many of the squares' textures, and ragview draws those blank:
- the clouds;
- the `pok` sparks;
- `pikapika2.bmp`;
- the two boomerangs;
- `ring_white.tga`.

The melody notes, the item pictures and `alpha_down.tga` are there.

## Checks

`tools/prim_vectors.py` runs the original in the emulation oracle and writes
`tests/unit/prim_vectors.inc`:
- **The table:** `InitEffect` on a constructed `CRagEffect` for every id 0 to 0x151, both
  ways of the minimal flag, each with its own `srand` seed. It records the name handed to
  `LoadStrFile` and the duration.
- **36 synthetic primitives**, ids 0 to 2 in turn:
  - the real constructor and `InitPrimitive`;
  - every field above set at random, including the flags, changes, keys of both kinds,
    colours that overflow, and short owners whose end the primitives outlive;
  - vf01 and vf03 each frame under a moving owner point, until vf01 answers false.
- **Four hit sparks:** `InitEffect` with id 1, then each frame:
  - `CRagEffect::vf01`;
  - vf01 of every object in the world's list, in order;
  - vf03 of each.

  Two of the runs shorten the effect, so that it ends before its rays and tells them.

- **30 synthetic shapes**, ids 5, 6 and 0xd in turn:
  - every field their update and drawing read set at random, including every flag of id
    5, an arbitrary matrix, one to six history records, and the coin sound;
  - a stand-in sprite of 8-bit and 32-bit images, some halved, and two actions of
    mirrored, scaled, turned and coloured items;
  - views from the oracle's `CView`, with the ground of `tools/actor_vectors.py` to bounce
    on.

  The cylinders all loop their images, since one that does not reads past its textures.
- **Five runs of effect ids 0 and 2:** id 0 both ways of minimal effects, under changing
  views and facings, through the world's list as for the hit spark.

- **24 synthetic ring primitives**, two of each ring id:
  - four random records each: spans of 21 to 360, low walls, ages around the updates'
    thresholds, kinds, flag bytes and wall heights;
  - random presets, including 1.0, 3 and fractions, and random flags.

  The inputs stay where the original's direct table indexing stays inside its tables.
- **The 23 ring effects** through the world's list, 24 steps each, each with its own
  `srand` seed.

  The ring triangles (168 a frame for a full primitive) are recorded on some frames only:
  steps 0 and 10 for the synthetic rings, steps 0, 12 and 23 for the effects. The states
  are recorded every frame.
- **24 synthetic square primitives**, three of each square id, 12 steps each, each with
  its own `srand` seed:
  - four random records each: every colour and shade, mode 2, turns past 360 for 0x33,
    ages around the thresholds (negative for 0x2b and 0x38), and both images;
  - a target between 40 and 320 degrees round from the owner, so that 0x2b's spread stays
    inside the tables.
- **The 30 square effects** through the world's list, 24 to 56 steps each, with a target,
  a level and the player's position:
  - 0xe5 with the player above the clouds' floor;
  - every state recorded while an effect has one or two primitives;
  - otherwise every eighth step, the second and the last, and for the effects that add
    dozens only the second and the last.
- **12 synthetic flat squares**, four of each flat id, and the two flat-square effects,
  generated as for the rings.

`tests/unit/prim_test.cpp` matches all of it: 1,521,275 values, the CRT's first `rand` values
among them.

**Not ported:**
- the other 39 primitive ids;
- the other effects' processing;
- the sounds.
