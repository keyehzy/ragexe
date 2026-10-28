# Effect ids and primitives

The server asks for an effect by id. The client turns the id into a `CRagEffect`, which may
play a `.str` ([effect.md](effect.md)) and may add primitives (`CEffectPrim`): rays,
rings, sprites flying out. The port is `src/render/effect_table` (which id plays what)
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

## In the port

**`src/core/crt_rand`:** `CrtRand`, `srand` and `rand`.

**`src/render/effect_table`:**
- `EffectStrName(id, minimal, rand)`;
- `EffectDuration(id)`.

**`src/render/effect_prim`:**
- `EffectPrim` with `MakePrimitive`, `UpdatePrimitive` (vf01) and `DrawPrimitive` (vf03),
  for ids 0 to 2;
- `ProcessHitSpark`;
- `EffectWorld`, the world's list as effects use it:
  - `Launch` adds an effect;
  - `Step` runs a world step in the list's order;
  - `Draw` gives the primitives' triangles.

**`ragview --effect-id ID`** plays the id's `.str` ([gpu.md](gpu.md)), and launches the id
again when its effect and primitives are done. `--minimal-effects` picks the `_min`
files.

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

`tests/unit/prim_test.cpp` matches all of it: 98,957 values, the CRT's first `rand` values among
them.

**Not ported:**
- the other 65 primitive ids;
- the other effects' processing;
- the sounds.
