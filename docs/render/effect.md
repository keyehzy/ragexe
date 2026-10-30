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

## Which cases play their .str

`CRagEffect::vf01` (`0054d4c0`) runs one case per effect id (its jump table is at
`0054e658`). 97 of the cases do no more than play the effect's `.str`, sounds aside, or
not even that.

**Without a `.str`** (+0x758 null), `AdvanceStr` answers false at once. A case that ends
on the answer then ends on its first step, before its frame counts on.

**`ResetStr`** sets the frame to 0 and reads the `.str`'s header (+0x75c) unchecked.

| Ids | Function | Each step |
|-----|----------|-----------|
| 0x34, 0x40, 0x46, 0x6d, 0x6e, 0x7e, 0x87, 0x8b, 0x8d, 0x8f, 0x90, 0x92 to 0x98, 0x9c to 0x9e, 0xa7 to 0xa9, 0xc7, 0xd0, 0xd1, 0xd3, 0xd5, 0xd6, 0x137, 0x14f, 0x150 | `ProcessGenericType` (`005671c0`; vf01 at `0054e5d4`) | `AdvanceStr` (0, −80, 0); over when it answers false |
| 0x17, 0x19, 0x1d, 0x1e, 0x29, 0x31, 0x41 to 0x45, 0x4b to 0x4e, 0x53 to 0x55, 0x57, 0x58, 0x5b, 0x5f to 0x61, 0x65, 0x67, 0x68, 0x6a to 0x6c, 0x6f to 0x71, 0x7c, 0x7f, 0x81, 0x83, 0x99 to 0x9b | one each, `00558270` to `00566a50` | its sounds, then the same |
| 0xcc to 0xcf, 0xd2, 0x111, 0x131, 0x132 | vf01's own (`0054e571`, `0054e03f`, `0054e402`, `0054e463`) | on the first frame to the actor as a sound plays, then the same |
| 0x5a | `005653b0` | the same; the view shaken at frames 50, 100, 150, 200 and 250 |
| 0x5c | `00565ec0` | the same; the view shaken at frame 40 |
| 0x66 | `005660f0` | the same; the view shaken at frame 35 |
| 0x82, 0xfa | `005668f0` | on the first frame the duration to 9999; `AdvanceStr`, its answer unread |
| 0x85, 0x86 | `00566800`, `00566830` | `AdvanceStr`; when it answers false, the frame to 9999 and over |
| 0x91 | `005668d0` | `AdvanceStr`; when it answers false, `ResetStr`, so it never ends |
| 0xfb | vf01's own (`0054e0b8`) | `AdvanceStr` (0, −20, 0); over when it answers false |
| 0x69 | `00566340` | nothing |
| 0x127, 0x128 | `0057ccb0` | on the first frame a joke (1 for 0x127, 0 for 0x128); no `.str` |
| 0x12d | `00568e70` | no `.str`; `effect\폭죽.wav` (`00619e94`) at frame 300, and past frame 2300 back to 299 |
| 0xda | `ProcessConcentrationDA` (`00566df0`) | to the actor; on the first frame a Concentration column (rising 2, 50 steps); `AdvanceStr` |
| 0xdb | `ProcessConcentrationDB` (`00566e60`) | to the actor; at frame 30 a column (rising 1.9, 30 steps); `AdvanceStr` |
| 0xdd | vf01's own (`0054e1d3`) | a column (rising 1, 50 steps) every step; no `.str` |

The column is `AddConcentrationColumn` (`005647c0`,
[effect-prim.md](effect-prim.md#crossed-quads)).

**The cases ported with their primitives** that step the `.str`:
- `ProcessGlassWall` (0xd, `00553ac0`), `UpdateGlassWall` (0x13b, `00579790`) and
  `ProcessConcentrationDC` (0xdc, `00566ed0`) leave the answer unread;
- `AddAspersio` (0x56, `00565bd0`) and `ProcessMagnumBreak` (0xaa, `00566ac0`) step it
  only while they go on, and leave the answer unread;
- `AddCoins` (0xa, `00557850`) steps it each step, leaves the answer unread, and is over at
  frame 100;
- `AddSignum` (0x28, `00564b00`), `AddStormGust` (0x59, `00565600`) and vf01's case 0xf8
  are over when it answers false.

Every other ported case leaves its `.str` unplayed.

### The sounds

Each plays through `PlayWave` (`004352a0`), mostly on the first frame; the function
gives the frames. None of them is ported.

| Ids | Sounds |
|-----|--------|
| 0x1e | `effect\magician_thunderstorm.wav` |
| 0x29 | `effect\EF_Angelus.wav` |
| 0x31 | the name `ClientState` `0059a290` gives for 3 |
| 0x41, 0x42, 0x43 (twice), 0x44, 0x45 | `effect\thief_invenom.wav`, `effect\Acolyte_cure.wav`, `effect\swordman_provoke.wav`, `effect\st_mvp.wav`, `effect\hunter_skidtrap.wav` |
| 0x4b, 0x4c, 0x4d, 0x4e, 0x53, 0x54, 0x55, 0x57 (five times), 0x58, 0x5b | `effect\priest_` and `gloria`, `magnificat`, `resurrection`, `recovery`, `sanctuary`, `impositio`, `lexaeterna`, `lexdivina`, `suffragium`, `benedictio`, `.wav` |
| 0x5a | `effect\wizard_meteo.wav` and `effect\hunter_blastmine.wav` |
| 0x5c, 0x5f, 0x60, 0x61, 0x66 | `effect\wizard_` and `meteor`, `quagmire`, `fire_pillar_a`, `fire_pillar_b`, `fire_pillar_b`, `.wav` |
| 0x65 (twice), 0x67, 0x68 | `effect\black_weapon_repair_a.wav`, `effect\black_weapon_perfection.wav`, `effect\black_maximize_power_circle.wav`, `_sword.wav` (twice) and `_sword_bic.wav` |
| 0x6a, 0x6b, 0x6c, 0x6f | `effect\hunter_` and `blastmine`, `claymoretrap`, `freezingtrap`, `springtrap`, `.wav` |
| 0x70, 0x71 | `effect\priest_kyrie_eleison_b.wav` and `_a.wav`; `effect\priest_magnus.wav` |
| 0x7c, 0x7f, 0x81 | `effect\assasin_poisonreact.wav` (both), `effect\assasin_venomsplasher.wav` |
| 0x82, 0xfa, 0x83 | `effect\knight_twohandquicken.wav`, `effect\knight_autocounter.wav` |
| 0x99, 0xda, 0xdb | `effect\ac_concentration.wav` |
| 0x9a, 0x9b | `effect\bs_refinesuccess.wav`, `effect\bs_refinefailed.wav` |
| 0xcc to 0xcf, 0xd2 | `_heal_effect.wav` (`0061817c`, without `effect\`) |
| 0x111, 0x131, 0x132 | `effect\mon_연환.wav` (`00618230`), `effect\p_success.wav`, `effect\p_failed.wav` |

### Messages to the game mode

Some cases send the game mode a message through its vf05 (`CGameMode::SendMsg`,
`0051d990`), with three values:

| Message | What | Sent by |
|--------:|------|---------|
| 0x87 | `CView::StartShake` (`004cf450`): the view shakes for 200 ms with value 1, else 650 ms | 0x4f with 1 and 0x8e with 0 on the first frame; 0xdc with 1 at frame 18; 0x5a, 0x5c and 0x66 with 0 |
| 0x94 | `CView::SetShakeAmplitudes` (`004cf4b0`): three floats, by pointer | 0x4f, after its 0x87: 1, 0.24 and 0.2 |
| 0xb3 | a line of `BA_frostjoke.txt` (1) or `DC_scream.txt` (0) said aloud | 0x127 and 0x128 |

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
- `StrPlayer`: `Reset`, `Advance`, `Step`, `SetFrame` and `Draw`;
- `StrLayerState`, the values as stored;
- `EffectTriangle`, a triangle with its layer, texture index, blends and stage mode.

**ragview:** `--effect NAME` plays `texture\effect\NAME.str` where the actor stands:
- a step each world step, counted by `FrameTimer`, at most 8 a frame;
- again from the start when it ends;
- a screenshot shows it after `--effect-step` steps, all taken on the first frame.

Textures are uploaded as triangles first ask for them.

`src/render/effect_prim.{h,cpp}`:
- **`EffectStr`:** an effect's `.str`, as `AdvanceStr` and `ResetStr` play it. Without
  one (`RagEffect::str`), `AdvanceStr` answers false.
- **`ProcessStrEffect`:** the 97 cases above.
- **`ModeMessage`:** `EffectWorld` keeps the messages its effects send the game mode until
  `TakeMessages`.

**ragview `--effect-id`:** the `.str` the id names is the launched effect's, played by its
case at the effect's frame, from its start each launch. The game mode's messages are
dropped: neither the view's shake nor the jokes are ported.

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

`tools/prim_vectors.py` checks the cases above: what each asks of its `.str` and the
game mode, step by step ([effect-prim.md](effect-prim.md#checks)).

**Not ported:**
- the 6 cases of `CRagEffect::vf01` that add primitives of their own and are not yet
  ported ([PLAN.md](../PLAN.md));
- the sounds, the view's shake and the jokes;
- the texture cache's use counts.
