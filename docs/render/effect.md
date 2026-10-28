# Effects: playing a .str

Most skill and item effects are `.str` files ([str.md](../formats/str.md)): layers of
keyframed, textured quads laid out in a 640 × 480 frame. A `CRagEffect` plays one where an
actor stands: each world step it moves every layer's keys on, then draws the layers as
triangles in front of everything. The port is `src/render/effect`.

## The player's state

`CRagEffect::LoadStrFile` (`0054fae0`) keeps the `CEZeffectRes` at +0x758. `ResetStr`
(`0054fb40`) then starts it.

| Field | What | At the start |
|------:|------|--------------|
| +0xbc | the frame | 0 |
| +0x4160 | the layers that have run out of keys | 0 |
| +0x4164 | how many must run out for the effect to end | the layer count, one fewer when layer 0 has no keys |
| +0x41e8 | each layer's current key, −1 once it has run out | 0 |
| +0x760 | each layer's 29 values, 0x74 bytes | |
| +0x4168 | whether `AdvanceStr` moved the layer this step | |

**A layer that has no keys** never runs out. So an effect with an empty layer, other than
layer 0, never ends.

### The values

Each key holds 29 values; most are floats. `AdvanceStr` and `DrawStrLayer` read them as
follows:

| Value | Meaning |
|------:|---------|
| 0, 1 | the position in the 640 × 480 frame |
| 2, 3 | the texture's corner, u and v |
| 4, 5 | its extent |
| 6 to 9 | not read |
| 0xa to 0xd | the corners' x |
| 0xe to 0x11 | the corners' y |
| 0x12 | the texture's index, a float |
| 0x13 | how a key moves the index (an integer) |
| 0x14 | the index's step |
| 0x15 | the angle, in 1/1024 turns |
| 0x16 to 0x19 | red, green, blue and alpha, 0 to 255 |
| 0x1a, 0x1b | the source and destination blends (D3DBLEND, integers) |
| 0x1c | the stage mode (integer) |

## Each step

`CRagEffect::vf01` runs each world step (16 ms, [architecture.md](../architecture.md)):
1. the effect's own processing (for most effects `ProcessGenericType`, `005671c0`), which
   calls `AdvanceStr`;
2. then frame +1.

When `AdvanceStr` answers false, the effect is over.

**`AdvanceStr`** (`0054fba0`) takes x, y and z offsets; `ProcessGenericType` gives
(0, −80, 0).

**False** without a `.str`, or once enough layers have run out.

**The screen point.** The actor's position (+0xa0) goes through `CView`'s matrix:
- **Depth:** z is at least 10.
- **x and y:** (scale_x / z) × x + the origin, and y × scale_y / z + the origin.
- **The depth value:** (1 / (1/z) − 10) × 1500 / 1490 × (1/z).
- **Offsets:** the three are added; +0xdc, +0xe0 and +0xe4 keep the results.

**Each layer** with keys, and not run out, whose current key's frame has come:
1. **On to the next key** when the frame is past the current key's and the next key is
   at the frame. Reaching the last key sets the layer to −1 and counts it as run out.
2. **A type 0 key** sets the 29 values. When the key after it has the same frame, the
   layer moves on to that one too, unchecked.
3. **Any other key adds to them:**
   - values 0 to 0x11, 0x15 and 0x16 to 0x19 are added;
   - the texture index moves by the key's mode (value 0x13) and step (value 0x14),
     against the layer's texture count n:

     | Mode | The index |
     |-----:|-----------|
     | 1 | plus the key's value 0x12 |
     | 2 | plus the step, held below n at n − 1 |
     | 3 | plus the step, wrapped below n: i − n × ⌊i / n⌋ |
     | 4 | less the step; below 0, i − n × q, plus n if still below 0 |
     | 5 | (frames since the key × step + i), bounced between 0 and n − 1 |
     | others | unchanged |

   **The q of mode 4** is a stack slot the compiler shares with the z offset. It is the
   z offset until a mode 3 or 4 in an earlier layer of the same step leaves its quotient
   there. Mode 5 with one texture would divide by zero.
4. **The layer is flagged** as moved.

## Drawing

`CRagEffect::Render` (`vf03`, `0054ee50`) draws each moved layer from layer 1 on, never
layer 0. It uses `DrawStrLayer` (`00550070`).

**The four corners** take their x from values 0xa to 0xd and their y from 0xe to 0x11:
- which goes where depends on how the pairs (0xa, 0xb) and (0xf, 0x10) compare;
- u and v go along: u for one side, u + its extent for the other, and the same for v.

**Placing the corners.** Each corner is:
1. turned by the angle, × 0.3515625 (a double, `005e9df8`) × π / 180, through `SinFloat`
   and `CosFloat`;
2. moved to the layer's position less (320, 240);
3. moved by the screen point.

The effect editor's frame centre thus lands on the actor, 80 pixels up for generic
effects.

**The vertices** come from a static template (`00686500`):
- **Depth:** z 10⁻⁵ and rhw 0.99999, at the near plane, so effects draw over the world.
- **Colour:** the four colour values truncated, a byte each.
- **Specular:** 0xffffffff.

**Two triangles,** v0 v1 v2 and v2 v1 v3, in the effect's `RenderQuad`s at +0x43e8:
- **Texture:** the layer's texture at the index (`GetEffectEntryTexture`, `004289a0`); a
  negative index is 0.
- **Blends and stage mode:** values 0x1a to 0x1c, copied as they are.
- **Flags:** 5 (translucent, its own stage mode), so they draw last, in the order added,
  each with its own blends ([scene.md](scene.md#drawscene)).

## In the port

`src/render/effect.{h,cpp}`:
- `StrPlayer`: `Reset`, `Advance`, `Step` and `Draw`;
- `StrLayerState`, the values as stored;
- `EffectTriangle`, a triangle with its layer, texture index, blends and stage mode.

**ragview:** `--effect NAME` plays `texture\effect\NAME.str` where the actor stands:
- a step each world step, counted by `FrameTimer`;
- again from the start when it ends;
- a screenshot shows it after `--effect-step` steps.

Textures are uploaded as triangles first ask for them.

## Checks

`tools/effect_vectors.py` runs the original in the emulation oracle and writes
`tests/unit/effect_vectors.inc`:
- **Three synthetic effects:**
  - 5 to 8 layers, some empty;
  - keys of both types, every texture mode, equal frames;
  - assorted colours, corners, angles and blends.
- **Playback:** 240 frames under changing views, actor positions and offsets. The last
  effect has no empty layer and plays past its end.
- **What is recorded each frame:** what `AdvanceStr` answers and leaves, and every
  triangle `DrawStrLayer` adds.

`tests/unit/effect_test.cpp` matches all 66,406 values.

**Not ported:**
- the effects' own processing (`CRagEffect::vf01`'s 200-odd cases), beyond the generic
  offset and the hit spark ([effect-prim.md](effect-prim.md));
- most of `CEffectPrim`, the particles and shapes effects add: ids 0 to 2 are ported
  ([effect-prim.md](effect-prim.md));
- the sounds;
- the texture cache's use counts.
