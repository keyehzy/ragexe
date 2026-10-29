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

## Motes

Three primitive ids draw small squares facing the camera, each record holding up to four
of them ("motes"). They are 0x1b (`RenderRisingSquares` `0056e920`), 0x1e
(`RenderPulsingSquares` `0057ab90`) and 0x21 (`RenderOrbitingSquares` `005708f0`).

**The records**, as these ids read them:

| Offset | What |
|-------:|------|
| +0 | active (a byte) |
| +4 | 0x1b: how fast the motes rise |
| +8, +0xe | the age and the alpha |
| +0x10 | the camera angles, as for [the facing squares](#facing-squares) |
| +0x14 | 21 floats, each id's own, below |
| +0x6c | the size, half the square's width |
| +0x88, +0x94, +0xa0, +0xac | the four motes' positions |

**The motes' order.** The updates and the drawing take the motes in the order +0xa0,
+0x88, +0xac, +0x94.

**Rising motes** (0x1b, `UpdatePrim_27` `0056e0d0`):
- **Walking angles.** Eight angles at +0x14 walk towards the goal after each, 3 degrees a
  step up and 2 down.
- **Drifting.** A mote above the ground drifts by 0.15 of the sines of two angles: for the
  motes in order, x and z by angles 0 and 2 forward, 4 and 6 back, 8 forward and 10 back,
  and 12 back and 14 forward.
- **Rising.** Each mote rises by the speed. Past 30 up it starts again at the centre, up to
  99 down.
- **Drawing.** White squares from 0 degrees, each drawn twice, only while the first
  corner is above the ground. Their alpha is 250, fading from 20 up to nothing at 30 up.

**A pulsing mote** (0x1e, `UpdatePrim_30` `0057a900`), the first record:
- **Brightening** by 1 a step for 200 steps.
- **Position.** Three angles walk a degree a step. The mote sits at their sines (x, z
  and y), plus 7 along the phase (+0x2c, a degree a step), plus the climb (+0x30).
- **The swell phase** (+0x34) turns 5 to 9 degrees a step by `rand()`.
- **Drawing.** A blue square of the size, swollen by a twentieth of the swell's sine, then
  a yellow one of half the size plus the same swell.

**Popping motes** (0x21, `UpdatePrim_33` `0056e4a0`):
- **The values.** Each mote has 16 bytes from +0x14: a count, the alpha, the image and the
  angle.
- **Brightening.** The count runs to 10 while the alpha grows by 25 a step.
- **Fading.** Then the alpha falls by 12 a step. Below 12 the mote starts again with a
  random angle and image, 3 to 10 up.
- **Drawing.** Each mote is a (100, 100, 255) square from 45 degrees, of its image, 4 out
  along its angle. The drawing writes the motes' x and z back into the record.
- **Corner order.** The corners go B, A, D, C instead of A, B, C, D when either of these
  holds, but not both:
  - the image is 0 or 2;
  - the angle less the camera's is between −80 and 70, or beyond 270 either way.

**The effects:**

| Effect ids | Function | Id | What |
|-----------|----------|---:|------|
| 0xca | `AddRisingMotes` (`0056db60`) | 0x1b | four records of four motes of `freezing_circle.bmp`, size 0.8, rising 0.15 a step; starts again at frame 99999998 |
| 0xe4 | `AddPulsingMotes` (`0057a6c0`) | 0x1e | as many primitives as the target's x rounds up to (none unless above 0), motes of `thunder_center.bmp` of size 2 circling 15 up, their phases 72 degrees apart |
| 0xcb, 0xfe | `AddPoppingMotes` (`0056d5c0`) | 0x21 | one and four records of motes of `super1` to `super5.bmp`, size 3.5. 0xfe plays `effect\mon_금강불괴.wav` |

## Single squares and rectangles

Six primitive ids draw one shape per record, each through `DrawQuad`:

| Id | Drawing | Shape |
|---:|---------|-------|
| 0x29 | `RenderTiltedSquare` (`0056fb50`) | an upright square flying out |
| 0x2a | `RenderRotatedRectangle` (`0056fea0`) | a ray |
| 0x32 | `RenderColoredFacingSquares` (`0057c620`) | a firework, facing the camera |
| 0x34 | `RenderFacingSquareLayers` (`0057c0f0`) | a beam, facing the camera |
| 0x35 | `RenderFacingRectangles` (`0057c3b0`) | a line, facing the camera |
| 0x41 | `RenderRotatedSquares` (`0056c4a0`) | ten nested squares, facing the camera |

**The records**, as these ids read them:

| Offset | What |
|-------:|------|
| +0 | active (a byte) |
| +2 | the pitch, degrees up from the ground of the direction out |
| +4 | 0x32: the squares' half-diagonal; 0x35: the half-width |
| +8, +0xe | the age and the alpha |
| +0xc | the turn, a square's own rotation |
| +0x10 | the camera angles of 0x32, 0x34, 0x35 and 0x41, as for [the facing squares](#facing-squares) |
| +0x14 | the growth, the distance's change a step; 0x35 and 0x41: an offset |
| +0x18 | 0x29: the square's half-diagonal, then its tilt (+0x1c); 0x2a: the half-width; 0x41: ten alphas |
| +0x68 | the yaw, degrees round the ground |
| +0x6c | the distance out; 0x32: the speed; 0x41: the size |
| +0x70 | the colour; 0x41: the image |
| +0x71 | 0x2a: the half-length, in half-widths; 0x32: the stage |
| +0x72, +0x73 | 0x32: the sparkle's delay and image |
| +0x88 | the centre of 0x29 and 0x32, then 0x32's trail at +0xa0, +0x94 and +0xac |

**Bursting squares** (0x29, `UpdatePrim_41` `00571d80`):
- **Flying out.** After a negative age, the distance grows by the growth each step.
- **Fading.** In by 15 a step (colour 0) or 25 for 10 steps, out by 7 after 25.
- **The tilt** turns 5 degrees a step.
- **Drawing.** An upright square of the half-diagonal, corners a quarter turn apart from
  the turn, turned round the vertical by the tilt. It sits the distance out from the
  centre along the pitch and yaw, in the world. It is (100, 150, 255), or (250, 250, 155)
  for colour 1.

**Rays** (0x2a, `UpdatePrim_42` `00571e30`):
- **Moving out.** After a negative age, the distance grows by the growth each step.
- **Fading** in by 15 a step from step 11 to 20, and out by 5 otherwise.
- **Drawing.** A rectangle twice the half-width across, from the half-length short of the
  distance to as far past it, along the pitch and yaw, relative to the primitive. It is
  white for colour 1, otherwise (200, 200, 255) onto ONE.

**Fireworks** (0x32, `UpdatePrim_50` `0057b440`), following the owner, after a negative
age:
- **The trail.** Every third step the centre moves to +0xa0, that to +0x94 and that to
  +0xac.
- **Sparkling.** From the delay plus 160 to plus 180, image 1 on steps 15 and 19 of every
  20 and image 2 on step 17; image 0 otherwise.
- **Turning** 5 degrees a step.
- **Stage 0** rises a quarter a step. Past age 154 it bursts: stage 1, alpha 250.
- **Stage 1** moves along the pitch and yaw at the speed and falls 0.02 more each step.
  The speed falls to 0.98 of itself; the alpha falls by 1, and by 1 more when `rand()` is
  even.
- **Starting again.** After 2000 steps it starts again at stage 0, 15 back along x from
  the owner, with a new yaw and speed.
- **Drawing.** Two squares facing the camera from the turn, in the world: one at the
  centre, and one half a unit smaller at +0xa0 at half the alpha. Colours 0 to 3 are
  blue, red, green and yellow, and the others magenta. Sparkling, they are white, of the
  sparkle's image, at a quarter of the alpha.

**Beams and lines** (0x34, `UpdatePrim_52` `00572c40`; 0x35, `UpdatePrim_53`
`00572ce0`):
- **Fading** in by 18 a step for 10 steps. 0x34 fades out by 15 after 20; 0x35 never
  fades.
- **0x35 follows its owner.** 0x34 stays where it started.
- **0x34's drawing.** Two wedges from the primitive out to the distance along the yaw,
  facing the camera: 3 degrees either way in white at half the alpha, then 5 either way
  in (130, 130, 255).
- **0x35's drawing.** A line twice the distance long and twice the half-width across,
  along the yaw, moved the offset aside towards the yaw less 90 degrees, facing the
  camera; white onto ONE.

**Asura squares** (0x41, `UpdatePrim_65` `0056b300`), after a negative age:
- **Brightening.** For the first 19 steps the ten alphas grow by 13 a step, to 70, 90, ...
  250.
- **Fading.** Then the first nine fall by 5 a step. After 140 steps the last does too,
  and the size grows by a hundredth each step it stays above 0.
- **Shrinking.** For the first 49 steps the size falls by a tenth.
- **Drawing.** Ten squares facing the camera, relative to the primitive:
  - the size plus 0, 3, ... 27 out to 45, 135, 225 and 315 degrees;
  - moved the offset along x, and 30 up;
  - each at its own alpha, the largest the first, and drawn only above 0;
  - of the record's image, black onto ONE.

  The drawing writes the corners into the record at +0x88.

**The effects:**

| Effect ids | Function | Id | What |
|-----------|----------|---:|------|
| 0xe3 | `AddBurstSquares` (`005752b0`) | 0x29 | 20 a step: four squares of `white01.bmp` from the target, half-diagonal 3.5, from 12 out by 1.5 a step, ages −70 to −99; directions, turns and tilts by `rand()`. `effect\EF_Bash.wav` on the first step, `effect\rog_intimidate.wav` at step 35 |
| 0x106, 0x10a | `AddRays` (`005756b0`) | 0x2a | 20 and 24 a step: four rays of `lens1.tga`, colours 0 and 1; half-width 0.7, 24 half-widths long, from −10 by 3 a step, ages −10 to −34 |
| 0x114 | `AddShortRays` (`00575ed0`) | 0x2a | 20 a step: four rays of `lens2.tga`, colour 2; half-width 1, 16 half-widths long, from −10 by 2 a step, ages −20 to −29 |
| 0x129 | `AddFireworks` (`00568ba0`) | 0x32 | four fireworks of `pok1`, `pok3` and `pok2.tga` from 15 back along x from the player, ages −300 to −349; colours, delays (0 to 60), speeds (0.4 to 0.796) and directions by `rand()` |
| 300 | `AddBeams` (`00575870`) | 0x34 | twice, from 30 and 60 degrees: four beams of `shockwave_c.bmp` 100 long, a quarter turn apart |
| 0x12f | `AddSlashLines` (`005759b0`) | 0x35 | twice, at frame 100: four lines of `blue_ivy.bmp` at 30 degrees, 100 long, half-width 1, offsets −15 to −22.5 and then 15 to 22.5; flags 0x201 |
| 0x148 | `AddAsuraStrike` (`00567740`) | 0x1c, 0x41 | at least 280 steps; `effect\EF_BeginSpell.wav` and two `AddSpreadingRings` of `ring_white` (alphas 45 and 25, preset 2) on the first step; at steps 1 and 21, three squares of size 18 each: `asura3` to `asura1` (ages −40, −20, 0; offsets −6, −18, −30), then `asura4` to `asura6` (ages −40, −60, −80; offsets 6, 18, 30) |

**`AddSpreadingRings`** (`005730c0`, id 0x1c) makes its effect last at least 60 steps. It
adds four rings of radius 4.1 and tilt 80 at 180, 270, 0 and 90 degrees:
- **Alphas:** the alpha given plus 135, 90, 45 and 0.
- **Heights:** 25 to 22 for preset 2, otherwise 20 to 17.
- **Scale:** 8 for preset 1, otherwise 5.
- **An effect of exactly 60 steps** starts them at alpha 0 and ages 0, −5, −10 and −15.

**Notes on the effects:**
- **Positioning.** The effects keep themselves at the actor each step, except:
  - `AddBurstSquares` and `AddShortRays`, which do so on the first step only;
  - `AddFireworks`, which keeps itself at the player.
- **After adding**, the effect moves its own y up 9 (the fireworks: x back 15), which
  nothing reads after.
- **Late fireworks.** They burst 455 to 504 steps after the effect starts.
- **Never called.** `AddCoinBurst` (`005754a0`) is `AddBurstSquares` with `coin_a.bmp`,
  colour 1, ages −18 to −47 and 12 up instead of 9. The port leaves it out.

## Walls and bands

Eight more primitive ids keep four records each at +0x156b0. Three draw rings of walls
through `DrawPresetColorQuad`, like [the ring walls](#ring-walls); five draw boxes and
tubes through `DrawQuad`:

| Id | Drawing | Shape |
|---:|---------|-------|
| 0x20 | `RenderSlantedRingWalls` (`0056cc90`) | quarter rings whose walls lean their own way |
| 0x3a | `RenderOffsetRingWalls` (`0056c2d0`) | rings, each about its own centre |
| 0x3b | `RenderRaisedRingWalls` (`0056caa0`) | rings lifted off the ground |
| 0x23 | `RenderBox` (`0056f2b0`) | a box: four walls and a floor |
| 0x3d | `RenderBoxWalls` (`0056f770`) | a box's four walls |
| 0x3f | `RenderPentagonBand` (`00570750`) | a five-sided tube between two points |
| 0x42 | `RenderArcBands` (`005704e0`) | two five-sided tubes, one inside the other |
| 0x43 | `RenderBandSegments` (`0056c6c0`) | a trail of 20 three-sided tubes, bouncing |

**The rings' records** are those of [the ring walls](#the-ring-records), with:
- **0x20:** the first two flag bytes (+0x70, +0x71), signed, move the ring along x and z.
- **0x3a:** the ring's centre at +0x88, and how far out it circles at +0xac.
- **0x3b:** the height (+4) lifts the whole ring.

**The other records**, as 0x23, 0x3d, 0x3f, 0x42 and 0x43 read them:

| Offset | What |
|-------:|------|
| +0 | active (a byte; 0x42 tests it signed, above 0) |
| +2 | 0x3d: the phase, turning a degree a step |
| +4 | 0x23 and 0x3d: the full height; the others: the tube's radius |
| +8, +0xc, +0xe | the age, the angle and the alpha |
| +0x10 | 0x3d: the age it starts fading |
| +0x14 | 0x23 and 0x3d: the half-length, half-width, height and (0x3d) colour; 0x3f: the radius it pulses about; 0x42: the lift, then how far back it starts; 0x43: 21 values, below |
| +0x68 | 0x43: the heading, degrees |
| +0x6c | 0x42: the tube's length; 0x43: the first bounce's |
| +0x70 | 0x43: 21 signed bytes, below |
| +0x88 | 0x42: its two ends (+0x88, +0x94); 0x3f: its ends at +0x94 and +0xac; 0x43: where it starts |

**Closing walls** (0x20, `UpdatePrim_32` `00571bb0`):
- **Closing in.** The radius falls by a hundredth (`005e9f24`) a step.
- **Fading** in by 10 a step for 10 steps, then out by 1. Faded out, the ring goes back to
  radius 20 and tilt 90, and once past age 30 it stops.
- **The walls** all stand at the height times the sine of the age.
- **Drawing.** As `RenderRingWalls`, with these differences:
  - the span goes in ninths, not 21sts;
  - there are no low walls;
  - the ring is moved along x and z by its flag bytes;
  - each wall's top leans out by the tilt not along the wall's angle but along a lean of
    450, 270, 90 or 630 (rings 0 to 3) less it, reduced once past 360. Each ring's walls
    so mirror their angles about 225, 135, 45 and 315 degrees.

  The drawing writes its corners into the record at +0x88.

**Orbiting rings** (0x3a, `UpdatePrim_58` `0056b7f0`), at the owner:
- **The centre** is the orbit along the angle (its y 0). The angle is reduced past 360
  but never turns.
- **Fading** in by 5 to 180 for 10 steps, and out by 2 from the age in the kind.
- **The walls.** Wall i stands at the height times sin 9i, and for 90 steps times the
  sine of the age too; never below 0.
- **Drawing.** As `RenderRingWalls` about the ring's centre, without low walls. The tops
  are not lifted with the centre: their y is that of a ring on the ground.

**Raised rings** (0x3b, `UpdatePrim_59` `0056ba70`), at the owner, after a negative age:
- **Turning** 5 degrees a step; the span opens by 3 to 120.
- **Fading** in by 10 to 250 for 11 steps, and out by 3 after 1400.
- **Drawing.** As `RenderRingWalls`, without low walls, the whole ring lifted by its
  height. The drawing writes its corners into the record at +0x88.

**Boxes** (0x23, `UpdatePrim_35` `00571c40`; 0x3d, `UpdatePrim_61` `00571cb0`):
- **0x23** fades in by 9 for 10 steps, then out by 1, and stops once faded past age 30.
  Its half-length and half-width grow by 0.005 a step, and its height is the full height
  times the sine of the age.
- **0x3d** waits out a negative age. Its phase turns a degree a step, and from the age in
  +0x10 it fades by 2. Its height is 0.65 of the full height plus 0.35 of it on the
  phase's sine, and for 90 steps times the sine of the age too.
- **The corners** lie 1 up, the half-length along the angle and the half-width across:
  (+, +), (+, −), (−, −), (−, +).
- **The walls** each run from a corner to the next and up the height, relative to the
  primitive.
- **0x23's drawing.** The walls are white onto ONE. The floor follows, both ways, at half
  the alpha.
- **0x3d's drawing.** The walls are white for a colour of 0 and (235, 255, 235) onto ONE
  for 1. For any other the look stays the last box's; the original's is unset before the
  first.

  Both drawings write the corners into the record at +0x88.

**The pentagon band** (0x3f, `UpdatePrim_63` `0056bc30`):
- **Every record**, active or not, ages, and has its angle reduced past 360.
- **The second and fourth** pulse their radius by half the sine of half their age (within
  720) about the radius in +0x14.
- **Drawing.** A five-sided tube of the radius from +0x94 to +0xac, its sides 72 degrees
  apart from the angle; (230, 255, 230), in the world.

**Arc bands** (0x42, `UpdatePrim_66` `005722d0`), after a negative age:
- **Fading** in by 8 for 5 steps, then out by 4.
- **The far end** lies the length on from the near one along the angle.
- **Drawing.** Two five-sided tubes from end to end, lifted by +0x14, in the world. Their
  sides lie 72 degrees apart about the line across the angle (the angle plus 90, within
  360). The first is of the radius, (255, 255, 55) onto ONE. The second is a quarter
  wider, (255, 255, 200).

**Bouncing bands** (0x43, `UpdatePrim_67` `0056b460`), after a negative age:
- **Fading** by 5 after 35 steps.
- **Bouncing.** The newest segment (+0x14) goes 8 degrees further through its bounce.
  Past 180 it starts the next: less 180, and its bounces (+0x70) one more.
- **The trail.** Every value and byte then takes the one before's place, so the 21 are
  the last 21 steps.
- **A point of the trail** is out along the heading and up:
  - **out:** twice the length for each bounce made, each 0.7 (`005e89c4`) of the last,
    plus twice the share of the next its degrees make (over 180, `005e2690`);
  - **up:** 1.5 lengths times the sine of its degrees (truncated), shrunk by 0.7 a bounce.
- **Drawing.** Twenty segments from each point to the next, each a three-sided tube of the
  radius:
  - the tube's sides lie about the line across their direction: `VectorToDegrees` of the
    first less the second, plus 90;
  - segment i is at the alpha less 5i, while that is above 0;
  - its colour is (255, 200, 10 + 7i), in the world.

**The effects:**

| Effect ids | Function | Id | What |
|-----------|----------|---:|------|
| 0xe2 | `AddGrandCross` (`00574da0`) and `AddBox` (`00575100`) | 0x20, 0x23 | four quarter rings of `ring_white.tga` at 180, 90, 0 and 270 degrees, moved 23 along each diagonal, radius 19.9, height 120, tilt 90; and two boxes of `ring_yellow.tga` 24 by 3.2, 60 high, crossed. `effect\cru_grand cross.wav` |
| 0xe0 | `AddCloudRings` (`005734b0`) | 0x3b | four rings of `cloud11.tga` 8 up: spans 90 to 30, angles 0 to 30, radius 3 to 3.6, walls 3 to 1.5; scale 4; `_heal_effect.wav` |
| 0x13d | `AddOpeningPortal` (`005784a0`) | 0x39, 0x3c, 0x3b | three rings of `magic_violet.tga` 50 high at random angles, radius 4 to 2, fading from 1400; at step 1, three low rings of `ring_blue.tga` as `AddReadyPortal`'s, kind 3111; at step 2, four 30-degree arcs of `cloud11.tga` a quarter turn apart, 1.5 to 7.5 up and radius 9 to 4.5; scale 4 |
| 0x13e, 0x13f | `AddBoxWalls` (`00573d60`) | 0x3d | one box of `ring_red.tga` (0) or `alpha_down.tga` (1): 10 by 10 and 50 high at alpha 30, fading from 864; or 5 by 5 and 16 high at 120, from 1880, green. The phase by `rand()` |
| 0x133, 0x142 to 0x144 | `AddPentagonBand` (`00569330`) | 0x3f | four tubes of `cloud11.tga` from the actor to 300 up and 70 back along x and z (60 and 15 for variant 10), angles 25 apart; radius 2 to 4 (1: 4 to 8; 2: 1 to 2), the fourth's fixed and aged 180; alpha 40 (2: 30; 10: 25) |
| 0x149 | `AddArcBands` (`00579da0`) | 0x42 | at steps 0 and 1: four tubes of `cloud11.tga`, radius 0.25, towards the target from up to 3 about the actor, started up to 15 back, lifted 8 to 12 and 20 to 40 long; ages 0 to −19; `effect\EF_hit2.wav` at steps 10 and 30, `effect\EF_hit4.wav` at 20 |
| 0x14a | `AddBouncingBands` (`00567bd0`) | 0x43 | at steps 0 to 3: four trails of `white02.bmp`, radius 0.1, alpha 125 to 175, headings by `rand()`, first bounces 6 to 13 long; ages 0 to −25 |

**Notes on the effects:**
- **Positioning.** The effects keep themselves at the actor each step, except:
  - `AddPentagonBand`, on the first step only;
  - `AddOpeningPortal`, at step 1 only.
- **The pentagon band's target.** `AddPentagonBand` writes each tube's top end to the
  effect's target (+0xe8) as it goes.
- **Unused.** Nothing adds id 0x3a; the port checks it on synthetic records only.

## Sprite trails

Six more primitive ids draw like [particles](#a-particle-id-5) through
`RenderActFrameHistory`: the sprite's current frame at each record of their history.

| Id | Update | What it does |
|---:|--------|--------------|
| 7 | `UpdatePrim_7` (`0057f4a0`) | circles its owner at a distance along a heading |
| 8 | `UpdatePrim_8` (`0057f6d0`) | a shot along its matrix, circling an axis |
| 0x15 | `UpdatePrim_21` (`0057edd0`) | a particle left where it started |
| 0x26 | `UpdatePrim_38` (`00568300`) | a leaf drifting down |
| 0x2c | `UpdatePrim_44` (`00568580`) | stands at a point, its travel there too |
| 0x31 | `UpdatePrim_49` (`00568630`) | stands at a point |

`vf01` ends 7 and 0x15 when `AdvanceActionFrame` answers false, as it does 5 and 6. It
ignores the answer for 8, 0x26, 0x2c and 0x31, which then draw a frame past the action's
last; the port draws nothing there.

**Id 7, circling.** Each step:
1. **The start** is the owner, plus the offset (+0x304).
2. **Speeds and rates.** The distance's speed (+0x27c) grows by its rate (+0x280), the
   fall (+0x21c) by its rate (+0x220), and the heading's speed (+0x260) by its rate
   (+0x26c). The distance and the heading grow by their speeds, and the offset's y by the
   fall.
3. **The angle** turns by the spin.
4. **Changes.** At `change_at` the fall and its rate take +0x1cc and +0x1d0.
5. **Distance keys.** With +0xbc set, at each key frame the distance's speed and rate take
   the next of the key values and limits. These are the same lists as
   [the alpha keys](#vf01).
6. **The point.** The matrix is the heading's turn about y, and the travel the distance
   along its z. The primitive is the travel from the start.
7. **The roll.** The scale grows by its speed, the pitch by its speed, and the matrix turns
   about z by the pitch. The pitch's degrees are passed as radians.

**Id 8, a circling shot.** Each step:
1. **The start** is the owner plus the offset.
2. **Speeds and rates.** The speed grows by the acceleration, the pitch's speed by its rate
   (+0x270), and the fall by its rate; the pitch by its speed.
3. **The flight.** The step is the speed less the fall along the matrix's z, and the travel
   falls back by it.
4. **The circle.** A second matrix (+0xf4) turns about the axis (+0x1c0) by the angle,
   through `Matrix::MakeAxisRotation` (`004099d0`): the quaternion of half the angle's sine
   along the axis and its cosine. The drift is the pitch along that matrix's y.
5. **The point** is the start plus the travel, plus the drift.

**Id 0x15** is id 5's update with one difference. It is left where it started: its point
is the start and offset it was given, not its owner's, and it has no `kEndNear`.

**Id 0x26, a leaf.** The first record holds the leaf, read as [the motes'](#motes): the
fall at +4, two angles each walking to the goal after it at +0x14, a mode at +0x70 and the
leaf at +0xa0. With the record active, each step:
1. **Walking.** The angles walk 3 up or 2 down, as [the clouds'](#the-updates-1) do.
2. **Falling.** The leaf falls by the fall.
3. **Drifting.** It drifts by the angles' sines: 0.24 along x and 0.3 along z, or 0.12
   and 0.15 in mode 1.
4. **Starting again.** Once below its owner, the leaf starts again within 150 of it along
   x and z (by `rand()`), 200 above.
5. **The point.** The travel and the primitive stand at the leaf.

**Ids 0x2c and 0x31** stand at the owner, or with the first record active at its point
(+0xa0); 0x2c moves its travel there too.

**After the move**, all four turn the angle to the camera under `kFaceCamera`, as id 5
does.

**The effects:**

| Effect ids | Function | Ids | What |
|-----------|----------|---:|------|
| 7 | `AddHealingColumn` (`00552420`) | 0xd, 7 | a cylinder of `alpha_down.tga`, radius 4.5, 35 high, fading in to 90 over 30 steps and out over the last 20; unless effects are minimal, every 9 steps a `particle1` 3 out at a random heading, rising 1.2 a step and slowing; the effect lasts 100 steps; `_heal_effect.wav` |
| 0xe | `AddBlueColumns` (`00554700`) | 0xd, 7 | three cylinders coloured (32, 176, 232), radius 2.5 to 4.5, 15 to 35 high, limits 16 to 64; every other step a `particle2` as above, life 30 |
| 0xf | `AddSoulStrike` (`00554cc0`) | 8 | one to five shots by the level, at steps 6, 17, 28, ... and angles from −90 on by 180, 90, 60 or 45; `effect\EF_SoulStrike.wav` with each |
| 0x47 | `AddSpiralSpark` (`0055c5d0`) | 7 | every 130 steps a `particle1` spiralling out, 30 down its matrix's y; the effect lasts 200 steps |
| 0x48 | `AddWideningSpirals` (`0055c770`) | 7 | every 10 steps the same, a distance key at age 95 turning its widening to −0.15; the effect ends at 500 less the step |
| 100 | `AddRemoveTrap` (`0055f760`) | 7 | at steps 0 and 7 twelve `particle1`s thrown up at 2 a step and falling back; `effect\hunter_removetrap.wav` |
| 0x56 | `AddAspersio` (`00565bd0`) | 7 | every 5 steps from 65 to 135 a `particle1` 1.5 out, 30 down; the effect lasts 400 steps, its `.str` 80 up; `effect\priest_aspersio.wav` at 70 |
| 0x75 | `AddWaterBall` (`005607e0`) | 8 | a shot of `Waterball`; `effect\wizard_waterball_chulung.wav` |
| 0xd8, 0xd9 | `AddRedTripleShot` (`00564160`), `AddBlueTripleShot` (`005643e0`) | 8 | three shots at twice the speed, angles −90, 0 and 90, coloured (250, 100, 100) or (100, 100, 250) |
| 0xa2 | `AddSnowfall` (`00557600`) | 0x15 | while more than 160 steps are left, two flakes of `ef_snow` a step, 100 above the ground up to 300 out about the player, falling 0.5 a step |
| 0xa3, 0x14d | `AddCherryBlossoms` (`00567e10`), `AddMapleLeaves` (`00568090`) | 0x26 | every other step to 300 (200) a leaf of `sakura01` (`단풍`), falling 0.2 or 0.3 (0.06 to 0.15) a step; action `rand()` % 3 (0); start again at step 99999998 |
| 0x10b | `AddTargetSprite` (`0057a190`) | 0x2c | `침투경_` at the target, scale 2.5; its action by the quarter the actor lies in from it, turned by the camera; `effect\mon_침투경.wav` |
| 0x12e | `AddDemonstration` (`0057a400`) | 0x31 | `데몬스트레이션` 1 above the actor, scale 1.2 |

**The shots** (8) start 10 up at the effect:
- **Aim.** They face the target: `CRenderObject::TurnTowards` (`005005b0`), the heading
  its negation.
- **Speed.** They cover the ground distance (`CRenderObject::GroundDistanceTo`,
  `00500530`) over their life. The fall starts at 3.5 (2 for the triple shots) and the
  pitch at −2, each shrinking by twice itself over the life.
- **The axis** is the matrix's z plus its translation.
- **Life and size.** 40 steps, 12 history records (60 and 7 for the triple shots); scale
  2.75 (0.75 for the water ball, 1.7 for the triple shots).

**Notes on the effects:**
- **Positioning.** `AddSoulStrike`, `AddSpiralSpark`, `AddRemoveTrap`, `AddAspersio`,
  `AddWaterBall` and the triple shots never place their effect; `AddSnowfall` keeps it at
  the player; `AddDemonstration` places it on the first step only; the others keep it at
  the actor each step.
- **The sprites** are 이팩트\ and the names given, a `.spr` and an `.act` each.
  `SetAction` (`00500de0`, `vtable+0x1c`) starts the action given from its first frame.
- **The fade.** A primitive's [fade to its owner's end](#vf01) divides by the steps left,
  which the x87 loads exactly. The leaves' effects last 99999999 steps, past single
  precision, so the port rounds that quotient once (`core::RoundedQuotient`).
- **Never called:** `AddRingSprite` (`0057a560`), `AddDemonstration` with `ring` 5 above.

## Screen wedges and rings

Five more primitive ids. Three stand on the screen like [ids 0 to 2](#primitives), at the
owner's point on screen (+0xdc, +0xe0) plus the offset (+0x304); id 0xc rings the world
about its history; id 0x14 is a hairline.

| Id | Update | Drawing | Shape |
|---:|--------|---------|-------|
| 3 | `UpdatePrim_3` (`0057e660`) | `RenderScreenWedge` (`00581820`) | a wedge on screen |
| 0x13 | `UpdatePrim_19` (`0057e7a0`) | `RenderScreenWedge2` (`00581990`) | the same, a copy of each |
| 4 | `UpdatePrim_4` (`0057e5e0`) | `RenderScreenRing` (`00581d00`) | a ring on screen |
| 0xc | `UpdatePrim_12` (`005809a0`) | `RenderHistoryRings` (`005841b0`) | a ring at each history record |
| 0x14 | `UpdatePrim_20` (`00581290`) | `RenderScreenLine` (`00581b00`) | a line 10 up, falling |

`vf01` ignores what these updates answer; none of them ends the primitive.

**A wedge** (3 and 0x13). Each step:
1. **The point** is the owner's on screen plus the offset.
2. **Stopping.** With flag 1, once the height (+0x290) is at most 0, its speed and rate stop,
   and `DebugLog` writes `m_heightSize  = %d`.
3. **Changes.** At `change_at` the height's speed, the heading's speed and its rate take
   +0x1fc, +0x1cc and +0x1d0, whatever they hold: no check for 0.0001.
4. **Bouncing.** Below 0, the height goes back to 0 and its speed turns round.
5. **Growing.** The heading's speed and the height's take their rates, the heading and the
   height their speeds, and the sweep (+0x1b8) grows by +0x288, the width's speed.
6. **`UpdateAlphaAndColor`.** The image never changes.

It draws one triangle (flags 1, at z 10⁻⁶ and rhw 0.999999) from the point out to the
height at the heading less and plus the sweep, up being −y. u is ½ at the point and 1 and 0
at the ends; v is 0 at the point.

**A ring on screen** (4). Each step the point follows the owner on screen; the radius
(+0x278) takes its speed (+0x27c), which takes its rate (+0x280). The width (+0x248) is the
radius, held to +0x224 unless flag 1. `RenderScreenRing` draws it like
[a cylinder's side](#a-cylinder-id-0xd), flat on the screen:
- **The ring** lies between the width short of the radius and the radius.
- **The sides.** One every +0x1b8 degrees from 0, while the angle is at most 360 less
  that, the tables indexed directly.
- **Two triangles a side:** inner, outer at the next angle, outer; then inner, inner at the
  next angle, outer at the next angle. Flags 1.
- **The texture's u** steps a quarter a side, back to 0 at 1; v is 1 inside.

**History rings** (0xc). Each step:
1. **The start** is the owner plus the offset.
2. **Changes.** At `change_at` the radius's rate and speed take +0x1d0 and +0x1cc, whatever
   they hold.
3. **Speeds.** The speed, the heading's speed, the radius's and the bottom radius's (+0x22c)
   take their rates (+0x234 for the bottom radius); the heading, radius and bottom radius
   take their speeds.
4. **The matrix** is rebuilt as a cylinder's: about x by the pitch, then y by the heading.
   The step is the speed along its z; the primitive stands at the start plus the travel.
5. **The width** is the radius, held to the bottom radius unless flag 1.
6. **`UpdateAlphaAndColor`**, then **`ShiftHistory`**.

`RenderHistoryRings` draws a ring at each record:
- **The ring** lies about the record's position, between the width short of the record's
  radius and the radius, in the plane of the primitive's matrix's x and y rows.
- **The sides** as `RenderScreenRing`'s, the u running on from one record to the next. Each
  is coloured by its record and drawn with `AddPrimitive`'s flags +0x128.
- **The first texture**, whatever the image.
- **Projection.** The view transform and `ProjectPoint` are written out several ways: the
  first triangle's corners sum from z, then from x and z, then from x, z and y; the second
  triangle calls `TransformPoint` and scales its first corner's x before dividing by z. The
  port follows each.

**A line** (0x14). Each step it falls 5 (y + 5). `RenderScreenLine` projects the position
and the point 10 above, and draws a triangle of the position, a pixel right of it and the
point above:
- untextured, flags 5;
- rhw 0.999999 throughout;
- colours 0x9cffffff then 0x9bffffff.

Only effect 0xa1 adds it, beside id 0x16, which is not ported. Nothing adds 0x13.

**The effects:**

| Effect ids | Function | Ids | What |
|-----------|----------|---:|------|
| 8 | `AddSparkRipples` (`00552720`) | 0xc, 7 | every 20 steps a flat ring of `RING_YELLOW.TGA` spreading from radius 2, at most 10 wide, its first matrix built from π itself; every other step a `particle1` circling 1 or 1.7 out, rising or falling |
| 0xc, 0x38, 0x3a, 0x3b | `AddBeginSpell` (`00552a80`, `00552fe0`, `00553360`, `005537c0`) | 0xc, 0xd | at the actor, a flat ring of `alpha_down.tga` of radius 15 fading in to 128; every 10 steps a cylinder of `ring_blue`, `Magic_Green`, `Magic_Sky` or `Magic_Violet.tga` widening and rising; for 0xc and 0x3a every 8 steps a lower one of `ring_yellow` or `ring_blue`; `effect\EF_BeginSpell.wav` |
| 0x2a | `AddBlessing` (`00553e80`) | 0xc, 5 | a ring as above in (32, 176, 232), radius 10; at steps 0, 3, 6 and 9 the sprite `축복` 25 up; every 4 steps a `particle6` 2 to 8 out and 25 up, sinking; `effect\EF_Blessing.wav` |
| 0x10 | `AddBash` (`00554ff0`) | 4, 3 | unless effects are minimal, 40 up from the actor on screen: a ring of `alpha_down.tga` of radius 100, and twenty wedges of `alpha_center.tga` at random headings, turning; `effect\EF_Bash.wav` |
| 0x23 | `AddReadyPortalRipple` (`00559f60`) | 0xc | every 14 steps a ripple of `ring_blue.tga` 1 up, spreading flat, at most 8 wide; `effect\EF_ReadyPortal.wav` |
| 0x24 | `AddPortalColumns` (`0055a100`) | 0xd, 0xc | two turning cylinders of `ring_blue.tga`, 40 and 50 high, and the same ripples |
| 0x3d | `AddRipplingColumn` (`0055b940`) | 0xc, 7 | a ring of `alpha_down.tga`, ripples of `ring_blue` closing in every 28 steps to 116, and a `particle1` circling up every 10; starts again at step 145 |
| 0x3e | `AddSightrasher` (`0055be40`) | 5, 0xc | every 5 steps to 15, eight `Sight`s and eight `Shadow`s flying out 45 degrees apart; at step 10 a ring of `ring_yellow.tga`; the actor gets message 0x6d; `effect\wizard_sightrasher.wav` |
| 0x49 | `AddRingSwirl` (`0055cae0`) | 0xd, 0xc | at steps 0 and 5 a cylinder of `ring_blue.tga` turned the way the actor faces, 20 degrees on the second time; a ring of `ring_yellow.tga` whose spreading changes at step 20 |
| 0x62 | `AddAdrenalineRush` (`0055ede0`) | 3, 7 | twenty wedges of `alpha_center.tga` growing from nothing, changing at age 40; four `particle1`s circling the actor; the effect lasts 300 steps; `effect\black_adrenalinerush_a.wav`, `_b.wav` at 30 |
| 0x79, 0x80 | `AddStoneCurse` (`00560e40`, `00561c70`) | 4 | 20 up on screen, a ring of `alpha_center.tga` widening at 7 a step and slowing, fading from the start; for 0x80 the effect lasts 9999 steps and its ring 40, `effect\black_overthrust.wav` first; `effect\EF_StoneCurse.wav` |
| 0xd7 | `AddWedgeBurst` (`00563eb0`) | 3 | Bash's twenty wedges; at step 35 the actor gets message 0x3f |
| 0x28 | `AddSignum` (`00564b00`) | 4, 3 | at step 30 Bash's ring and twenty longer, faster wedges; `effect\EF_Signum.wav` and `effect\EF_Bash.wav`; its `Cross.str`, through `AdvanceStr`, decides its end |
| 0x141 | `AddRingedColumn` (`00578d90`) | 0xc, 0x3e, 7 | a ring of `alpha_down.tga`; at steps 1 and 2 four closing rings (id 0x3e) of the texture `vf01` passes (`ring_blue.tga`); every 10 steps a `particle1` circling up; starts again at step 99999998 |

**The point on screen.** The effects with ids 3 and 4, and 0x3e, put the actor on screen
each step, as `ProcessHitSpark` does: the view transform, z at least 10, and `ProjectPoint`.
Each writes it out its own way: the transform called (0x10, 0xd7, 0x28) or with its sums
from z (0x3e, 0x62, 0x79, 0x80); the projection called, or written out with x scaled first
(0x79, 0xd7) or last (0x80, 0x28).

**Notes on the effects:**
- **Positioning.** `AddBeginSpell` and `AddBlessing` keep the effect at the actor each
  step, `AddRingedColumn` at steps 1 and 2; the others never place it.
- **Messages.** 0x3e and 0xd7 send their actor a message through its vf02 (`0x6d` with
  0x16, and `0x3f`); the port's effect world has no actor to send them to.
- **The `.str`.** `AddSignum` answers what `AdvanceStr` does; the port's effect world
  leaves `Cross.str` to the `.str` player, and keeps the effect to its duration.
- **Keys.** 0x3d's ripples get a key frame (20) and value (0) without +0xb4 or +0xb8, so
  `UpdateAlphaAndColor` never reads them.
- **Out of the tables.** 0x141's second closing rings start at radius 10.2, so their
  tilt falls to −1 and [the ring drawing](#drawing) reads just before `g_cosTable` (a
  zero) and the last of the negated cosines; the port reads the tables as they lie in
  memory there.

## Flying quads

Three more primitive ids fly along their matrix, sharing one update:

| Id | Update | Drawing | Shape |
|---:|--------|---------|-------|
| 0xf | `UpdatePrim_15` (`0057fb90`) | `RenderTransformedQuad` (`00582fd0`) | a quad in the plane of the second matrix |
| 0x10 | `UpdatePrim_15` | `RenderCrossedQuads` (`00583910`) | two crossed quads ([below](#crossed-quads)) |
| 0x16 | `UpdatePrim_22` (`005802b0`) | `RenderBillboard` (`00583660`) | a quad on the screen at the point's depth |

`vf01` ignores what `UpdatePrim_15` answers; it ends 0x16 when `UpdatePrim_22` answers
false.

**The update.** Each step:
1. **The start.** `UpdatePrim_15` puts it at the owner plus the offset; `UpdatePrim_22`
   keeps the start it was given and stands at the start plus the offset plus the travel.
2. **Changes.** At `change_at`, each of the width's speed (+0x1f8), the height's (+0x1fc),
   the speed (+0x1cc), the width's rate (+0x1f4), the height's (+0x200), the acceleration
   (+0x1d0) and the step (+0x1d4 to +0x1dc) that does not hold 0.0001 takes hold.
   `UpdatePrim_22` takes the step's speed (+0x1e0 to +0x1e8) too; `UpdatePrim_15` instead
   sets +0xa0 and puts 0.0001 back in all twelve.
3. **Speeds.** The step takes its speed (+0x31c); the width's, height's, heading's and
   pitch's speeds their rates; the speed the acceleration.
4. **The step** is the speed along the matrix's z, unless flag 0x100 keeps it as it is
   (but not with 0x400). The travel takes it.
5. **Seeking** (0x400): within 3 of the owner's target along the ground (from the last
   step's point), `DebugLog("Hit!!")`, +0xb0 set and false; otherwise the heading takes
   the facing (+0x38).
6. **Sizes.** The width, height, heading and pitch take their speeds. Past +0x29c the
   height's speed and rate stop.
7. **The second matrix** (+0xf4):
   - flag 0x200: about z by the camera's pitch, its degrees passed as radians, then y by
     the facing;
   - flag 0x40: the heading becomes the camera's yaw plus 90 and the angle its pitch;
     about x by the pitch, z by the angle, y by the heading;
   - otherwise about z by the angle, x by the pitch, y by the heading.
8. **The flags after:**
   - 2: v runs from a tenth of a counter (+0x134, 0 to 9) to one more (+0x214, +0x218);
   - 0x10: the owner's target becomes the offset plus the travel;
   - 4: below the ground's height (`C3dAttr::GetHeight`), the speed stops, the age jumps
     to 25 before the life, the flag clears and the owner's +0xb4 is set;
   - 0x20: every +0x12c frames the height's speed turns round.
9. **Colours.** The red, green and blue take their speeds (ints). With flag 0x800, once any
   of them is 254 they step only every +0x194 frames (counting in +0x15c), and each
   wraps: 255 or more to 0, below 0 to 254. Without it, red wraps 256 to 0 and below 0 to
   255, and green and blue are held to 0 to 255.
10. **`UpdateAlphaAndColor`**, then **`AdvanceFrameByCount`**.

**A transformed quad** (0xf) lies about the point in the plane of the second matrix's y
and z rows:
- **Its corners** are the width either side along z, and −height to +height along y;
  with flag 0x80 from 0 to −2 × height, and with flag 8 down to 0 only.
- **Two triangles** of the current image: top-right, top-left, bottom-right, then
  bottom-left, bottom-right, top-left. u is 0 on the right; v runs from +0x214 at the top
  to +0x218 at the bottom.
- **Projection.** Each corner through `TransformPoint` and `ProjectPoint`, the second
  triangle's first written out. AddPrimitive's flags are +0x128.

**A billboard** (0x16) stands on the screen:
- **The point.** The position through the view (its sums from z), projected with
  `SceneRenderer` +0x1c and +0x20 as the centre.
- **Its size.** Up and right of the point, the width and height a seventh of a pixel per
  unit at that depth.
- **Two triangles** at z 0.8 and rhw 1: the point, right, up; then right, up, up-right.

**The effects:**

| Effect ids | Function | Ids | What |
|-----------|----------|---:|------|
| 0xd | `ProcessGlassWall` (`00553ac0`) | 0xf | on the first frame four upright walls of `ring_blue.tga` round the actor, two 2.6 out along its facing (3 wide) and two 3 out across it (2.6 wide), 40 high, rocking every 50 frames, fading in to 180; `effect\EF_GlassWall.wav`; it also plays its `.str` |
| 0x72, 0x89 | `AddWallsOfMagic` (`0055ff60`, `005602d0`) | 0xf | the same four walls 15 high, 5 out and wide in `magic_violet.tga` fading in, or 2.5 in `magic_green.tga` at 180; the effect lasts 9999 frames |
| 0x30 | `AddPondRing` (`00556710`) | 0xf, 0xc | eight panels of `pond_water.bmp` 12.2 out, 2 up, their texture scrolling; every 10 frames eight flat panels of `alpha_down.tga` 13.8 out; every 20 a ring of `alpha_center.tga` |
| 0x3c | `AddLockOn` (`0055b5d0`) | 0xf | two squares of `LockOn128.tga` under the actor, 45 degrees apart, turning, shrinking for 20 frames and cycling their colours; the effect 2 over the ground |
| 0x9f, 0xa0 | `AddMarker` (`005627f0`, `005629c0`) | 0xf | a square of `LockOn128.tga` turning, from white at level 0 to red at 10 (green at level 11), or of `party.tga` in blue; starts again at frame 9998 |
| 0x52 | `AddCollapsingRing` (`0055d630`) | 4, 3, 0xf | on screen a ring of radius 370 closing in at 18.5 a frame and five wedges 350 long shrinking; at frame 50 a square of `ring_liner.tga` growing; `effect\EF_Bash.wav` at 37 |
| 0x5d | `AddShockwaveBall` (`0055e610`) | 0xf | a ball of `thunder_center.bmp` and one of the six `thunder_ball` images thrown at the target, facing the camera; `effect\hunter_shockwavetrap.wav` |
| 0x5e | `AddThunderBlast` (`0055eaa0`) | 0xf | at the actor, every 20 frames a burst of `thunder_pang.bmp` growing for 10 frames; at frame 10 a plasma blast of five images lasting 300 |
| 99 | `AddFlasher` (`0055f290`) | 0xf, 3 | a flash of `thunder_center.bmp` 10 up with alpha keys at 40 and 100, and twenty wedges on screen; `effect\hunter_flasher.wav` |
| 0x74, 0x76 | `AddFallingSpell` (`005605f0`, `00560a60`) | 0xf | a square of the three `water_out` images or of `fire_ivy.bmp` sinking 25 over the effect, facing the camera; `effect\wizard_waterball_chulung.wav` or `wizard_fire_ivy.wav` |
| 0x77 | `AddDetecting` (`00560be0`) | 0xf | a flat square of `fashasha.tga` growing and sinking; `effect\hunter_detecting.wav` |
| 0xa1 | `AddRain` (`00557140`) | 0x14, 0x16 | while more than 40 frames are left, a drop 200 above the ground within 100 of the actor and a splash of `rain_gnd.tga` where it lands; while more than 300 are left, a second whose splash slides; the effect at the player |

**Notes on the effects:**
- **Positioning.** `AddLockOn`, `AddMarker`, `AddThunderBlast` and `AddCollapsingRing`
  (after its first frame's primitives) keep the effect at the actor; the lock-ons 2 above
  the ground under it. `AddRain` keeps it at the player, moving it to each splash to make
  it.
- **The point on screen.** `AddCollapsingRing` writes the projection out with x scaled
  first; `AddFlasher` calls it.
- **Unread fields.** The walls of 0x72 and 0x89 set +0x198, which nothing ported reads.
- **The `.str`.** `ProcessGlassWall` plays its `.str` through `AdvanceStr` but ends the
  effect at its duration whatever that answers.

## Crossed quads

Id 0x10 flies by the flying quads' `UpdatePrim_15` (`vf01`'s jump table `0057e06c` sends
0xf and 0x10 to the same call, `0057dd9b`); `RenderCrossedQuads` (`00583910`, `vf03`'s
table `00586268`) draws it.

**Its two quads** cross along the z row of the primitive's own matrix (+0xc4, not the
second at +0xf4), about the position:
- **Corners.** The first quad runs from −height to +height along the matrix's x row, the
  second along its y row; both the width either side along z. A corner is the matrix's
  rows times its three values plus the matrix's last row, plus the position.
- **Sums.** The assembly sums each quad's first corner from y (y, z, then x), its next
  two from z (z, y, x), and its last's x from z, x then y and its y and z from z. Every
  corner has a 0 among its three values, so each order rounds as the others would.
- **Two triangles** each: u 0 and v 1 at −height and +width, u 1 and v 1 at −height and
  −width, u 0 and v 0 at +height and +width; then u 1 and v 0 at +height
  and −width, and the second and third again. All take the colour +0x124 and the current
  image, with AddPrimitive's flags 1, not +0x128.
- **Projection.** Every corner goes through `TransformPoint` and the near clamp. Each
  first triangle's first corner is projected inline, as `ProjectPoint` does; each second
  triangle's first inline with x scaled before the 1/z; the rest through `ProjectPoint`.

**The effects** (`CRagEffect::vf01`):

| Effect ids | Function | Ids | What |
|-----------|----------|---:|------|
| 9 | `AddRisingLight` (`00556d10`) | 0xc, 0xd, 0x10 | at frame 0 a ring of `ring_yellow.tga` (sides of 22.5 degrees, flat, of radius 0: its width 7 is overwritten by its update) and a column of `alpha_down.tga` 6 across rising to 50, both for the whole effect at alpha 128; every 5 frames (15 with minimal effects) a fleck of `alpha_down.tga` 7 wide rising at 2 |
| 0x2b, 0x25, 0x26 | `AddAgilityRays` (`00554310`, `0055a4a0`, `0055a890`) | 0, 0x10 | at frame 0 a ray of `dex_agi_up.bmp`, `agi_up.bmp` or `slow.bmp` (0x26's 80 lower, turned about and 10 high) for the whole effect; every other frame a fleck of `ac_center2.tga` 3 to 9 wide, rising (0x26: 20 lower and sinking); `effect\EF_IncAgiDex.wav`, `EF_IncAgility.wav`, `EF_DecAgility.wav` |
| 0x1a, 0x1f | `AddArrows` (`00558520`, `00559180`) | 0x10, 0xc, 5 | from frame 12 an arrow every 10 frames, the level many, of `icearrow.tga` or the six `불화살` ("fire arrow") images, falling onto the actor; where each lands a ring of `ring_blue.tga` or `ring_yellow.tga`; for 0x1f every fourth frame a spark of `particle4` from the arrow's tip; `effect\EF_IceArrow%d.wav` or `EF_FireArrow%d.wav` (1 to 3) at frame 12 |
| 0x73 | `AddBlitzBeat` (`0055f950`) | 0x10, 0xd | at frame 0 ten flecks of `ac_center2.tga` 15 ahead of the actor, sweeping on and widening; from frame 36 every third frame a beat of `magic_green.tga` (id 0xd) before it, the level many; `effect\hunter_blitzbeat_1st.wav` at 5 and `hunter_blitzbeat.wav` at 30 |
| 0xdc | `ProcessConcentrationDC` (`00566ed0`) | 0xd, 0x10 | at frame 0 a column of `alpha_down.tga` (`method_005647c0`); from frame 13 to 109 every fourth frame a red fleck of `ac_center2.tga` rising; `effect\ac_concentration.wav` at 18; it ends once the frame passes 150 |

**The flecks.** Each is a crossed quad turned a random heading (`rand() % 360`), 2 and up
to 4, 6 or 8 more out along it (`rand() % 5` or `% 7`), then its matrix rebuilt as a turn
about x alone, 90 degrees (−90 for 0x26), so that its speed along z carries it along y.
They are thin (0.18 to 0.25 high); 0x2b's and 0x25's rise at 0.2 to 0.69 a step, 0x26's
start still and gain 0.015 a step each step, and Concentration's rise at 1/3 to
1.15.

**The arrows.** An arrow starts 60 up, 25 to 34 out along x and 15 to 24 along z, aimed
by `CRenderObject::GetAngleBetween` (`00500610`):
- **Heading:** the negated angle from its x and z to a point within 2 of the actor
  (`(rand() & 3) − 2` each way).
- **Pitch:** the angle from (−60, its distance along the ground) to (0, 0).
- **Its matrix** is a turn about x by the pitch, then y by the heading; it flies at −2.75
  along z for 70 frames, fading over the last 20.
- **Flags 4 and 0x10:** it leads the effect's target along with it, and on landing sets
  the effect's +0xb4, which the setup clears when it adds the ring. The ring (id 0xc,
  30 frames) widens from 1.2 a step, slowing by 1.2 × 2 / 70 a step.
- **The level** is multiplied by 10 at frame 12, after which an arrow comes every 10
  frames while the frames since 12 are below it.
- **The aim** is kept at the effect's +0xd4 (heading) and +0xd8 (pitch); 0x1f's sparks
  leave from the effect's target (the arrow's tip) along it, up to 20 degrees either way
  in heading and 10 in pitch.

**Blitz beat.**
- **The flecks** face the actor's way, each 15 out along it at once (the step, added to
  the travel), 7 up and 3.5 out round a random angle. They fly back along it at 1.2, 0.1
  faster each step; at age 10 their speed becomes −0.3 and their acceleration 0.001 (the
  changes at +0x1cc and +0x1d0).
- **The beats** are cylinders (id 0xd) pointing down, at the matrix's last row less twice
  its y row plus its z row, each 0 to 2 across and 2 to 12 up (`rand() % 10` and
  `rand() % 20`, a third `rand` unused), 12 frames each.
- **The count** of beats is a global (`DAT_0068658c`) that each 0x73 effect sets to 0 at
  its frame 0; it stops them at the effect's level.

**Concentration.** It also sends its actor's vf02 message 0x7e with 250, 120 and 120 at
frame 0 and 0x7f when it ends, and at frame 18 the game mode's vf05 message 0x87 with 1.
`AdvanceStr` plays its `.str` 80 up every frame; whatever that answers, the effect ends
after frame 150, long before its duration of 9999. `method_005647c0` (`005647c0`) makes
its column: id 0xd, 4.5 across and 10 high, rising by the speed it is given (1 here) and
0.01 a step more each step, fading in to 90.

## Pyramids and spheres

Four more ids, two drawn as solids and two never created:

| Id | Update | Drawing | Shape |
|---:|--------|---------|-------|
| 9 | `UpdatePrim_9` (`0057f8c0`) | `RenderPyramid` (`00582990`) | a four-sided pyramid in the second matrix |
| 0xe | `UpdatePrim_14` (`005811f0`) | `RenderSphere` (`005857e0`) | a sphere of cells every +0x1b8 degrees |
| 0x11 | none (`vf01` only ages it) | none | |
| 0x12 | `UpdatePrim_18` (`00580bc0`) | none | |

No `CreatePrimitive` call in the binary passes 0x11 or 0x12 (nor 0xa or 0xb), and only
`CreatePrimitive` calls `InitPrimitive`.

**A pyramid's update** (id 9):
1. **The start.** At the owner plus the offset.
2. **Changes.** At `change_at` the speed takes change 2 (+0x1cc) and the acceleration
   change 5 (+0x1d0), with no 0.0001 test.
3. **Moving.** The speed takes the acceleration; the step is the speed along the matrix's
   z; the travel takes the step; it stands at the start plus the travel.
4. **Flag 1** stops it, speed and acceleration, at age +0x134.
5. **Flag 2** skips the alpha's update. Instead, 30 frames before its life, it takes the
   speed from +0x1d8 and the acceleration from +0x1e4 (step changes 1 and 4), so that a
   spike can rise, stop and sink again.

**A sphere's update** (0xe) stands at the owner plus the offset. The distance (its radius)
and the heading take their speeds, the distance's speed its rate, then the alpha's update
runs.

**0x12's update** is the cylinder's (`UpdatePrim_13`) without the first turn: the heading's
and pitch's speeds neither take their rates nor turn it before its matrix is built.
`vf01` ignores what it answers, as it does for 0xd.

**A pyramid** (`RenderPyramid`) has four sides about the position in the second matrix
(+0xf4):
- **Shape.** Each side runs from a base corner (the scale +0x248 either way along x and
  y) up to the tip (the height +0x290 along z) and back to the next base corner.
- **Texture.** Each side is one triangle of the first texture, with AddPrimitive's flags 1.
  u steps 0.2 a side (`005e9f40`); v is 1 at the base and 0 at the tip.
- **The view** is written out with every sum from x, then z, then y, and each side's last
  corner is projected inline.
- **Sum orders.** Every corner has a 0 among its three values, so the second matrix's
  sums round alike in any order.

**A sphere** (`RenderSphere`) has a cell every +0x1b8 degrees:
- **The grid.** The latitude runs from −90 while not past 90 less the sweep, the
  longitude from 0 while not past 360 less it.
- **A corner** is the z row, times the distance, of a turn about x by the latitude, then
  about y by the longitude plus the heading. The next longitude adds a quarter degree
  (`005e653c`).
- **Texture.** u steps 1/(360/sweep) a cell and v 1/(180/sweep) a row.
- **Two triangles** of the first texture per cell, with AddPrimitive's flags 1:
  - (lat, lon), (lat + sweep, lon), (lat, lon + sweep);
  - then the far corner, the third again and the second again.
- **Projection.** The first triangle's view transforms are written out, x's sum from z
  and y's and z's from x, then z. The second triangle's first corner is written the same
  way; its second goes through `TransformPoint` and is projected inline with x scaled
  first; its third goes through `TransformPoint`.
- **Size.** A sweep below about 16 would give more triangles than the primitive's buffer
  (+0x1eb0, 512) holds; the setups use the default 36, 100 triangles.

**The effects:**

| Effect ids | Function | Ids | What |
|-----------|----------|---:|------|
| 0x1b | `AddIcicleTrail` (`00558980`) | 9 | unless minimal effects are on, from frame 0 an icicle of `ice.tga` each frame, 5 out and then 2 more each frame towards the target, until one lands within 2.5 of it |
| 0x1c | `AddIcicleBurst` (`00558e10`) | 9 | at frame 0 eight icicles of `ice.tga` round the actor, 20 to 30 high; `effect\EF_FrostDiver2.wav` |
| 0x4a | `AddIceWall` (`0055ce30`) | 9 | at frame 0 three icicles of `ice.tga` 30 to 40 high in a row, the effect and they lasting 9999 frames; `effect\wizard_icewall.wav` |
| 0x4f | `AddEarthSpike` (`0055d110`) | 9 | at frame 0 a spike of `stone.bmp` that rises for 12 frames, drops back for 3 and stops, not fading (flag 2 with no step changes), and six stones round it, all for 240 frames; `effect\wizard_earthspike.wav` |
| 0x7b | `AddStoneTrail` (`00561350`) | 9 | every third frame a stone of `stone.bmp`, 2 further each frame towards the target; `effect\EF_FrostDiver.wav` |
| 0x84 | `AddStoneTriad` (`00561750`) | 9 | at frame 0 three stones of `stone.bmp` a third of a turn apart, rising for 10 frames and lasting 1000, as the effect does |
| 0x8e | `AddSpikeField` (`005624a0`) | 9 | at frame 0 a five by five field of spikes of `stone.bmp`, 5 apart, that rise, stop at 14 and sink again; the effect 50 frames longer; `effect\wizard_earthspike.wav` |
| 0x59 | `AddStormGust` (`00565600`) | 9 | unless minimal effects are on, two icicles of `ice.tga` at frames 58, 87, 116 and 145, living until frame 215; `effect\wizard_stormgust.wav` |
| 0x11 | `AddBlast` (`005553e0`) | 0xc, 0xe | at frame 0 a widening sphere of `대폭발.tga` ("great explosion") turning 3 a frame, a ring of `ring_yellow.tga` for the effect and one for 30 frames; `effect\EF_MagnumBreak.wav` |
| 0x3f | `AddExplosionSphere` (`0055c4f0`) | 0xe | at frame 0 a sphere of `대폭발.tga` of radius 13.5, 10 up, still, at alpha 100, for 250 frames |
| 0xaa | `ProcessMagnumBreak` (`00566ac0`) | 0xc, 0xe | at frames 7 and 20 a ring and a sphere as 0x11's, 20 frames each; `effect\EF_MagnumBreak.wav`; it ends after frame 299 |

**The trails** (0x1b and 0x7b) turn the effect (a `CRenderObject`, +0x38) towards the
target with `TurnTowards`:
- **The walk.** They step along the z row of a turn about y by the negated facing,
  keeping the step at +0xf4 and the walk so far at +0x100.
- **0x1b** steps 5 at first, then 2 a frame. Before each icicle it measures
  (`CRenderObject::GroundDistanceTo`, `00500530`) from the effect moved by the walk; within
  2.5 it sets +0xb4 and adds no more after this one.
- **0x7b** steps 2 a frame and measures from where the effect stands. Its stones stop
  only if the target is within 2.5 of the effect to begin with.

**Notes on the effects:**
- **Pyramids.** Every one is tipped by its pitch and turned by its heading, its second
  matrix a copy of the first. The icicles of 0x1b, 0x1c and 0x4a rise at 3 a frame,
  slowing to a stop at age 20.
- **Unused calls.** Several setups call `rand`, `SinDegrees` and `CosDegrees` for values
  they never use. 0x4a's scale adds 2.5 to a whole number of hundredths below 40, always 0.
- **Messages.** 0x4f sends the game mode's vf05 0x87 with 1 and 0x94 with pointers to 1,
  0.24 and 0.2; 0x8e sends 0x87 with 0.
- **Endings.** 0x59 answers `AdvanceStr`'s result and 0xaa false after frame 299, both
  tested by `vf01`; the other setups' results are not. In the port, 0x59 plays to its
  duration, as the oracle's `AdvanceStr` answers true.

## Ribbons, the tube and panels

The last ids:

| Id | Update | Drawing | Shape |
|---:|--------|---------|-------|
| 0xa, 0xb | `UpdatePrim_10` (`00581050`) | `RenderHistoryRibbon` (`00585220`) | a ribbon of quads, one at each history record |
| 0x24 | `UpdatePrim_36` (`00572190`) | `RenderTubeBetweenPoints` (`005701e0`) | two tubes between two actors |
| 0x2e | `UpdatePrim_46` (`0057b8e0`) | `RenderVerticalPanels` (`0057af70`) | up to four swaying upright panels |

`vf01` ignores what each update answers.

**A ribbon** (0xa and 0xb, which nothing creates):
- **The update** takes four points about the owner plus the offset:
  - the distance out at the heading (x by its sine, z less its cosine);
  - the same at the heading plus its speed, raised by +0x294;
  - both of those again, the scale (+0x248) lower.

  It then runs the alpha's update, shifts the history (whose records keep the points),
  turns the heading by its speed and raises the offset by +0x294.
- **The drawing** puts a quad at each record: (A, B, C) and (D, C, B), of the first
  texture in the record's colour, with AddPrimitive's flags 1. u runs 0 to 1 from A to B;
  v is 1 at the top. The view transform is written out, x's sum from x, then z, and y's
  and z's from z, then y.

**The tube** (0x24) uses the first of the four 0xb8-byte records at +0x156b0, laid out
as the bands' (`PrimBand`):
- **The record.** The radius is at +4, the age at +8 and the alpha at +0xe. +0x14 holds
  the id of the actor it runs to, as a float. The start is at +0x94 and the end at +0xac.
- **The update** keeps the start 8 above the owner and counts the age. The alpha rises 2
  a frame for five frames, then falls 2 for five.
- **The end.** While the id is above 0 and names an actor, the end is 8 above that actor.
  The id names the player (the game mode's world +0x3c) when it equals
  `g_ClientState+0xcdc` (`0068734c`); otherwise `CWorld::GetActorById` (`004c4cf0`) looks
  it up, truncated. When no actor is named, the record goes out and its alpha falls to 0.
- **The drawing** runs while the record is lit and its alpha is above 0. The line goes
  from the start to the end, drawn in over the first 15 frames by the sine of six times
  the age. Round it are two tubes:
  - the radius, in yellow (255, 255, 0) at the alpha;
  - twice the radius, in blue (0, 0, 255) at twice the alpha (a 16-bit shift).

  Each tube is ten quads (`DrawQuad`, in the world). Its ring points are every 36
  degrees, at right angles to the line (`VectorToDegrees` of the start less the end, a
  quarter turn on).

**The panels** (0x2e) read the same records their own way:
- **The record.** A sway is at +2, the height at +4, the age at +8 and the alpha at +0xe.
  The kind is at +0x14. The edges' x and z are at +0x88/+0x90 and +0xa0/+0xa8.
- **The update** keeps the primitive at the owner. For each lit record it counts the age
  and turns the sway 2 degrees a frame (4 for kind 1), wrapping past 359. The height
  becomes 20 plus 10 times the sway's sine.
- **The drawing** puts an upright quad (`DrawQuad`, relative to the primitive) at each lit
  record with its alpha above 0. The quad runs from one edge to the other, from the ground
  up the height. Its colour comes from the kind:
  - kind 1 is red (255, 155, 155) and kind 2 green (155, 255, 155), both added;
  - kind 3 is yellow (255, 255, 100) and kind 4 pale red (255, 150, 150), blended;
  - any other kind is white, blended.

**The effects:**

| Effect ids | Function | Ids | What |
|-----------|----------|---:|------|
| 0xe8 | `AddActorLink` (`00569de0`) | 0x24 | at frame 0 a tube of `cloud11.tga`, radius 0.4, alpha 45, from the actor to the actor whose id is the target's y (+0xec); out if none; the effect at the actor |
| 0x115, 0x122 | `AddPanels` (`00574150`), kind 0 | 0x2e | at frame 0 four panels of `ring_blue.tga` or `ring_red.tga` from the actor out to a random point up to 20 away each way, alpha 15 |
| 0x11f, 0x124 | `AddPanels`, kinds 1 and 2 | 0x2e | four panels of `ring_red.tga` or `magic_green.tga` through the actor, 3 either way at random slants, two across x and two across z, alpha 50 |
| 0x126 | `AddPanels`, kind 4 | 0x2e | two crossed panels of `safeline.bmp` 6 either way, alpha 100 |

**Notes on the effects:**
- **The tube's end.** `AddActorLink` puts the end at the actor itself; its update moves it
  8 above.
- **The kind.** `AddPanels` writes it only for kinds 1 to 4, so kind 0 keeps the record's
  0 (white).
- **Unread fields.** It also zeroes the scale (+0x248), and `AddActorLink` the record's
  +0x6c; nothing ported reads either.

## In the port

**`src/core/crt_rand`:** `CrtRand`, `srand` and `rand`.

**`src/render/effect_table`:**
- `EffectStrName(id, minimal, rand)`;
- `EffectDuration(id)`.

**`src/render/effect_prim`:**
- `EffectPrim` with `MakePrimitive`, `ResetHistory`, `UpdatePrimitive` (vf01) and
  `DrawPrimitive` (vf03), for every id: 0 to 0x16, 0x24, 0x2e, the twelve ring ids, the
  eight facing-square ids, the three flat-square ids, the three mote ids, the six ids of single
  squares and rectangles, the eight of walls and bands and 0x26, 0x2c and 0x31.
  `PrimScene` carries the camera's yaw and pitch, the ground, the view, the projection
  and the sprite slope, and the actors an id can name (`EffectActors`); `PrimDrawing` gets the triangles and the sprite quads, each with
  the sprite it is of;
- `ProcessHitSpark`, `ProcessRingBurst` and `ProcessHit3`; `AddRingWaves` (with `slow`
  for `AddSlowRingWaves`), `AddRisingRings`, `AddFadingRings`, the eight setups of
  [More rings](#more-rings), the eleven of [the square effects](#the-square-effects) and
  `AddTwinSquares`, `AddSwellingSquare`, `AddRisingMotes`, `AddPulsingMotes`,
  `AddPoppingMotes`, the seven setups of
  [single squares and rectangles](#single-squares-and-rectangles) (`AddRays` with
  `short_rays` for `AddShortRays`), the eight of [walls and bands](#walls-and-bands) and
  the thirteen of [sprite trails](#sprite-trails) (`AddTripleShot` for both triple shots,
  `AddFallingLeaves` for both leaves) and the fourteen of
  [screen wedges and rings](#screen-wedges-and-rings) (`AddBeginSpell` for four ids,
  `AddStoneCurse` with `overthrust` for 0x80) and the twelve of
  [flying quads](#flying-quads) (`AddWallsOfMagic`, `AddMarker` and `AddFallingSpell`
  for two ids each) and the five of [crossed quads](#crossed-quads) (`AddAgilityRays`
  for three ids, `AddArrows` for two), the eleven of
  [pyramids and spheres](#pyramids-and-spheres) and the two of
  [ribbons, the tube and panels](#ribbons-the-tube-and-panels) (`AddPanels` for five
  ids);
- `EffectWorld`, the world's list as effects use it:
  - `Launch` adds an effect;
  - `Step` runs a world step in the list's order;
  - `Draw` gives the primitives' triangles and quads;
  - `SetParticle`, `SetSprite`, `SetMinimal` and `SetPlayer` give it the sprites effects
    load by name (`kParticleSprite` and `kEffectSprites`), the minimal flag and where the
    player stands; `SetPlayerId` and `SetActor` the actors an id names, for the tube.

**`src/core/angles`:** `DegreesToRadians`.

**`src/core/x87`:** `RoundedQuotient`, a float over a double rounded once, and
`RoundedProduct`, a float times a double rounded once to single
precision, shared with the `.str` player.

**`ragview --effect-id ID`** plays the id's `.str` ([gpu.md](gpu.md)), and launches the id
again when its effect and primitives are done. `--minimal-effects` picks the `_min` files,
and `--effect-level N` gives the effect a level (0 by default), which the arrows and blitz
beat count by. The effects thrown at a target aim 50 out either way along the ground. The
installed 2003 data lacks many of the effects' textures, and ragview draws those blank:
- the clouds, `cloud11.tga` among them;
- the `pok` sparks;
- `pikapika2.bmp`;
- the two boomerangs;
- `ring_white.tga` and `ring_red.tga`;
- `white01.bmp`, `white02.bmp` and the `asura` squares.

The melody notes, the item pictures, `alpha_down.tga`, `magic_violet.tga`, the `lens`
rays and `shockwave_c.bmp` are there. Of the trails' sprites only `particle1` and
`particle2` are; ragview draws nothing for `ef_snow`, `sakura01`, `단풍`, `침투경_`,
`데몬스트레이션` and `Waterball`. The screen effects' sprites (`축복`, `particle6`, `Sight`
and `Shadow`) and textures are all there, as are the crossed-quad effects' (`particle4`,
the six `불화살` images, `icearrow.tga`, `ac_center2.tga` and the rest).

## Checks

`tools/prim_vectors.py` runs the original in the emulation oracle and writes
`tests/unit/prim_vectors/`, one file per family of primitives so that none grows too large
for git:
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
- **12 synthetic mote primitives**, four of each mote id, and the four mote effects as for
  the squares. The motes are below, at and above their limits, and every image of 0x21 is
  seen from both sides. 0x21's motes' x and z are checked through the triangles drawn from
  them.
- **24 synthetic single squares and rectangles**, four of each id, 24 steps each, and
  the eight effects that add them, as for the squares:
  - ages around the updates' thresholds: 0x32 rising, bursting, sparkling and starting
    again, and 0x41 brightening, fading and growing;
  - every colour, the rays' lengths (negative too) and 0x29's turns past 360;
  - 0xe3 for 104 steps, until its squares fly, and 0x12f for 112, past frame 100.
  0x41's corners are checked through the triangles drawn from them.
- **32 synthetic walls and bands**, four of each id, 24 steps each, and the eleven effects
  that add them, as for the squares:
  - ages around the updates' thresholds, negative ages, and angles and phases past 360
    and below 0;
  - 0x20's quarter rings within the first quarter turn, where every ring's lean stays
    inside the tables, and its offsets either way;
  - 0x3d's colours 0, −0 and 1, and 0x43's bounces from −2 to 7 and degrees from 0 to
    180;
  - 0xe2 for 112 steps, until its walls and boxes have faded.

  The corners the drawings of 0x20, 0x23, 0x3b and 0x3d write are checked through the
  triangles drawn from them.
- **24 synthetic sprite trails**, four of each id, 40 steps each, as for the shapes:
  - every field their updates read at random: id 7's distance keys and changes, id 8's
    axis, id 0x15's flags, and the leaves' angles, modes and restarts;
  - ids 8, 0x26, 0x2c and 0x31 loop their actions, since one that runs out goes on past
    its frames;
  - the state adds the further fields, the second matrix and the first record.
- **The 15 trail effects and six more runs of 0x10b**, as for the squares, with the
  stand-in sprite for every name:
  - a stand-in act of four actions (the two, twice), for the actions the leaves and 0x10b
    pick;
  - the views of the snow, the leaves and 0x10b looking where their sprites are;
  - the quads checked with the triangles.
- **20 synthetic screen and history rings**, four of each of ids 3, 4, 0xc, 0x13 and 0x14,
  up to 40 steps each under a moving point on screen, as for the first primitives:
  - every field their updates and drawings read at random: changes holding 0.0001 or not,
    heights below 0 with and without flag 1, the width held or not, sweeps whole and not
    (400, drawing nothing), one to six history records of 0xc and its flags +0x128;
  - the state adds the sweep, the radii, the step, start and travel, the matrix and the
    history; the triangles are checked at steps 0, 3, 9, 20 and the last.
- **The 18 screen effects**, as for the trail effects:
  - the actor given a vf02 (`CEffectPrim`'s, which ignores 0x6d and 0x3f) for the messages
    0x3e and 0xd7 send;
  - `AdvanceStr` answering true, so that Signum plays past step 30 without its `.str`;
  - 0x3d for 152 steps, past its restart; Bash, 0x3e, 0xd7 and Signum ending their runs
    while their last primitives still draw.
- **20 synthetic flying quads**, ten each of ids 0xf and 0x16, up to 40 steps each, as for
  the shapes:
  - every flag but 1 at random, with the target 12 or so away and the ground below, so
    that some reach it and some land;
  - changes holding 0.0001 or not, colours at 254 for 0x800, and the second matrix
    rebuilt every way;
  - the state adds the changes, the second matrix and the owner's target and landed flag;
    the triangles are checked every frame.
- **The 15 flying-quad effects and 0x9f again at level 11**, as for the trail effects, 0x30,
  0x52 and 99 sampled densely.
- **Ten synthetic crossed quads** (id 0x10), as for the flying quads.
- **The eight crossed-quad effects**, sampled densely, as for the screen effects:
  - the arrows again at levels 5 and 10 (where ten times the level first differs from
    nine times), and blitz beat at level 2;
  - the game mode given a vtable whose vf05 is `C3dGround::vf03` (`004bc290`, an empty
    function of four arguments), for Concentration's message;
  - Concentration for 160 steps, past its end.
- **40 synthetic pyramids, spheres and primitives of ids 0x11 and 0x12**, ten of each, as
  for the flying quads:
  - flags 1 and 2, changes and ages around 30 before the life;
  - the second matrix at random, sweeps of 20 to 100 (none small enough to overflow the
    buffer; 26 among them, where 1/(360/sweep) and sweep/360 round apart), and radii,
    rates and colours at random;
  - the state is the position, image, colour, age and fade, colours, speeds, angles, alpha,
    distance, radii and height with their speeds, the step, start and travel and the
    matrix; the triangles are checked at steps 0, 3, 9, 20 and the last.
- **The eleven pyramid and sphere effects and three more runs**, sampled densely, as for
  the crossed-quad effects:
  - 0x1b twice more, once with the target 1.8 from the actor, so that its first step ends
    3.2 from it, just past the 2.5 it stops within;
  - 0x7b a second time with the target within 2.5, where its stones stop;
  - 0x8e for all 300 of its frames, until its spikes sink; 0x59 for 230, until its last
    icicles are gone; Magnum Break for 305, past its end.
- **Twenty synthetic ribbons**, ten each of ids 0xa and 0xb, as for the flying quads, with
  one to six history records, headings far outside 0 to 360 and heights either way; the
  state adds the offset and the four points.
- **Eight synthetic tubes and panels**, four of each, as for the walls, with random
  records:
  - the tube's age about its 15;
  - its id the player's, the other actor's (whole and not), an unknown one, 0 or below;
  - every kind of panel, and kinds 1.5 and 5.
- **The six tube and panel effects** (0xe8 three times), as for the screen effects:
  - the player's id written at `g_ClientState+0xcdc`;
  - `CWorld::GetActorById` answering one other actor;
  - 0xe8 run to the player, to that actor and to no one.

`tests/unit/prim_test.cpp` matches all of it: 5,745,547 values, the CRT's first `rand` values
among them.

**Not ported:**
- the other effects' processing;
- the sounds.
