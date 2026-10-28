# Player characters

A player character (`CPc`) is drawn from up to eight sprites at once: the body, the head,
three pieces of headgear, the weapon, its glow and the shield. Each frame, an `.imf` file
([imf.md](../formats/imf.md)) says whether the body or the head goes first, and the
frames' anchors ([act.md](../formats/act.md#actanchor)) seat the head on the body and the
headgear on the head. Everything else is placed as an NPC's sprite is
([actor.md](actor.md)). The port is `src/render/player`.

## The layers

`CPc` keeps each layer's actions and sprite in two vectors: `+0x2f8` and `+0x2e8`, read
through `GetLayerAct` and `GetLayerSprite` (`00546490`, `00546270`).

| Layer | What | Set by |
|------:|------|--------|
| 0 | body | `UpdateBody` (`vf16`, `00542320`) |
| 1 | head | `SetHair` (`vf17`, `00542400`) |
| 2, 3, 4 | headgear | `vf21`, `vf22`, `vf23` |
| 5, 6 | weapon and its glow | `SetWeapon` (`vf20`, `005464b0`) |
| 7 | shield | `vf24` |

A layer is drawn only when both its actions and its sprite have a name (`GetLayerActName`,
`GetLayerSpriteName`).

### The names

`ClientState::InitNameTables` (`00588ba0`) fills tables of names at start-up:
- **The jobs' names:** 45 of them; 0 is the novice, 초보자. +0x674 holds them, and +0x6b4
  the same again for the `.imf` files.
- **The sexes:** 0 is female, 1 male. Their folders are 여 and 남 (+0x684), their suffixes
  _여 and _남 (+0x6e4).
- **The hair styles:** 20 for each sex (+0x694 male, +0x6a4 female). Each maps a style to
  the number of a head sprite: the male style 2 is head 1.

The paths are relative to `sprite\` (`imf\` for the `.imf`):

| Function | Path |
|----------|------|
| `GetBodyActName`, `GetBodySpriteName` (`00593920`, `00593960`) | `인간족\몸통\<sex>\<job><suffix>.act` and `.spr` |
| `GetHeadActName`, `GetHeadSpriteName` (`005939a0`, `00593a20`) | `인간족\머리통\<sex>\<style><suffix>.act` and `.spr` |
| `GetImfName` (`00593b70`) | `<job><suffix>.imf` |

A hair style outside 0 to 19 becomes 13, and the head functions write that back to the
character (+0x298).

## Drawing

`CPc::Render` (`vf03`, `00544380`) runs in three steps:
1. **The shadow:** `DrawShadow`, unless +0x58 is 3 or 6.
2. **The body,** while +0x85 is set:
   - `RenderActFrameItems` (`00544be0`), the layered drawing below;
   - `DrawBody` instead, with +0x2c8 set;
   - `00545540` instead, for a few special ids on the Korean service.
3. **Otherwise** it only works out the screen position.

**`RenderActFrameItems`** first sets up as `DrawBody` does: the projection, the pixel
scale (+0x48), the x offset and the depth bias. Two things differ:
- **The x offset** multiplies the object's scale in before the pixel scale. That is a
  different rounding.
- **The object's colour** channels are divided by 255 up front. Red, green and blue are
  also tinted by the ground.

### The slots

Eight slots, from 7 down to 0, each draw one layer. The direction (the action less the
base action) picks the mapping:

| Direction | Slot 7 | Slot 6 | Slot 5 | Slots 4 to 0 |
|-----------|--------|--------|--------|--------------|
| 0, 1, 6, 7 | the `.imf` layer of order 1 | order 0 | layer 2 | layers 3 to 7 |
| 2 to 5, turned away | layer 7 | order 1 | order 0 | layers 2 to 6 |

**The `.imf` lookup** is `CImfRes::FindEntryWithValue` (`0042a220`), with the order, the
action and the frame. It answers the first layer whose order there is the value. When
the action or frame is beyond the counts of that layer, it answers −1, and −1 draws the
body.

**Two adjustments follow:**
- **The head-first swap.** Once the head has come up, a body (layer 0) slot draws layer 2
  instead, which the swap below turns into layer 4. The next layer-2 slot then draws the
  body.
- **Layers 2 and 4 swap,** and anything outside 0 to 7 draws the body.

### Each layer

1. **Skips:**
   - a layer without names;
   - a weapon or shield (5 to 7) outside the attack actions, 0x20 to 0x27 and 0x50 to
     0x5f;
   - a frame beyond the layer's frame count.
2. **The item:** item 0 of the frame, item 1 for the head. An item beyond the frame's is
   `ActFrame::GetItem`'s static one at `00605b70`, which shows image 0 at the origin. An
   item without an image (−1) is skipped.
3. **The colour:** each channel is the item's / 255 × the object's (255 for blue, −255
   for the others, truncated). They are then combined as b − (((a·256 + r)·256 + g)·256).
   That gives `A << 24 | R << 16 | G << 8 | B` without masking: a channel past 255
   carries into the next.
4. **The anchors** use each frame's first anchor, and only when both frames have one of
   the same kind:
   - **The head:** its offset is the body frame's anchor less its own. It is kept in
     `006864f0` and `006864f4`.
   - **The headgear:** its offset is the head frame's anchor less its own, plus those
     globals.

   **The globals** are never reset. A head whose anchors do not match leaves them as an
   earlier frame, or an earlier player, set them.
5. **Placing:** the edges, the check against the screen and the rhw are as in
   [actor.md](actor.md#the-body), with the anchor offset added to x and y:
   - **The layer step** counts the layers drawn so far, not the slot.
   - **The flat depth** (+0x86) uses the slot.
   - **Keyed** (+0x83, with +0x68 not 0xffff0000), the colour is +0x68 itself.
6. **The palette:**
   - the layer's own sprite's;
   - the body, with +0x34, takes the resource named by +0x14's;
   - the head, with +0x29c, takes +0x2d0's.
7. **The flags:** the headgear (2 to 4) goes in as 1 (translucent, without writing depth),
   everything else as 0x101.
8. Before each quad goes in, it sends the character message 0x78 through `vf02`.

## In the port

`src/render/player.{h,cpp}`:
- **Layers:** `PlayerLayer`, the layers, and `PlayerHeadOffset`, the two globals.
- **Drawing:** `DrawPlayer` and `FindImfLayer`. Each quad carries its layer, so the
  caller knows which sprite's texture it needs.
- **Names:** `PlayerBodyName`, `PlayerHeadName` and `PlayerImfName`, with the tables as
  the original fills them, in CP949.

**ragview:** `--player JOB SEX HAIR` draws a player where the player stands:
- the body and the head of the job, sex and hair style given;
- ordered by the job's `.imf`;
- with the shadow, idle, animated.

## Checks

`tools/player_vectors.py` runs the original in the emulation oracle and writes
`tests/unit/player_vectors.inc`. It records:
- **Every name:** the 45 jobs and both sexes for the body and the `.imf`, and hair styles
  −1 to 21 for the head, with the style they leave.
- **300 `.imf` lookups** on a synthetic `.imf` of 104 actions.
- **320 players:**
  - **layers:** 6 sets of synthetic layers, some missing, with heads of one to three
    items and anchors that match or not;
  - **actions:** idle, walking and attack, every direction;
  - **settings:** assorted flags and colours, under 3 views, from assorted head offsets.

  That is 782 quads in all layers, with the head offset each player leaves.

`tests/unit/player_test.cpp` matches all 11,006 values.

**Not ported:**
- the headgear's, the weapon's and the shield's names (`vf21` to `vf24`, `SetWeapon`);
- the palettes of other resources (+0x34, +0x29c);
- the message `vf02` sends;
- `00545540`, the special drawing for a few ids on the Korean service;
- the jobs' shadow scales (`ClientState` `0059b750`). ragview uses 1.
