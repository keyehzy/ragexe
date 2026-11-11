# The own player's making

The world's own player (`CWorld`+0x3c, a **`CPlayer`**, [pursuit.md](pursuit.md)) is made by
**`CWorld::CreatePlayer`** (`004c4e60`, [lists.md](lists.md#joining)). `LoadWorld` calls it
(`004c41dc`), and `CGameMode` `0050e390` does after `ClearGameObjects`. It takes the player as
the server last described it from `ClientState` (`00686670`). The port is `src/world/own_player`.

## The constructor

**`CPlayer::CPlayer`** (`0054b1d0`, 0x3dc bytes) runs these steps:
1. `CPc`'s constructor runs ([arrival.md](arrival.md#the-constructors)). It names the body
   `NPC\` and the name of job 46 (`00686dc8` +0xb8), as `CRenderObject::ResetObjectFields`
   (`00500400`) names every object's.
2. Three `XmlElement`s named `XMLDocument` are built at +0x360, +0x388 and +0x3b0.
3. The vtable is stored (`005e9cf4`).
4. **`CPlayer::ResetPlayerFields`** (`0054b4a0`) runs, below.
5. `006864f8` is cleared. Nothing reads it.

## The reset

`ResetPlayerFields` (`0054b4a0`) runs these steps, at `timeGetTime` *now*:
1. **The place:**
   - Its point is the centre of the cell at +0x620, +0x624 (`CGameMode::GetCellCentre`,
     `00511de0`), on the ground (`C3dAttr::GetHeight`, `00427150`).
   - Its goal (+0x308, +0x30c) is the attribute cell under it (`CGameMode::GetAttrCell`,
     `00511d50`).
   - +0x350 and +0x354 are 0.
2. **The facing** (+0x38) is (+0x628 × 5 + 20) × 9 as a 32-bit integer, made a float
   (`0054b52b`): (direction + 4) × 45°.
3. **Who it is:**
   - the id is +0xcdc;
   - the guild (+0x23c) and its emblem's version (+0x240) are `0067cb38` and `0067cb3c`, which
     packets 0x0150, 0x015c, 0x016c and 0x01b6 set;
   - the step left (+0x194) is −1.
4. **Its own fields** ([pursuit.md](pursuit.md#the-fields)):
   - 0 for +0x314, +0x31c, +0x318, +0x320, +0x334, +0x324, +0x330, +0x344, +0x34c, +0x32c
     and +0x328;
   - +0x348 is 1;
   - +0x358 and +0x35c are −1;
   - +0x338, +0x310 and +0x33c are *now* − 1200, each from its own `timeGetTime`.
5. **Pickable** (+0x89) is set. Its point on screen (+0x90, +0x94) is `g_pSceneRenderer`'s
   origin (+0x2c, +0x30).
6. **vf10 with 0:** `CPlayer::SetState` ([pursuit.md](pursuit.md#its-states-and-reports)). It
   forgets the goal (−1) and stands. The body's actions are still the constructor's (step 1
   above), so the action's delay (+0x44) is theirs. Dressing doesn't play an action again.
7. **The speed** (+0xc8) is +0xd94 (`00687404`). +0x8c is 1, as `CPc`'s constructor already left
   it.
8. **The sex:** `CGameActor::SetSex` (`00503020`) with **`ClientState::GetSex`** (`005a4bd0`).
   That gives +0x18d0, which it first copies to +0x5b8 when they differ.
9. **The dress:**
   - The status words (+0x218, +0x21c, +0x220) become the session's for a while: +0x614,
     +0x61c and +0x618.
   - vf14 ([look.md](look.md#the-dress)) is given these:
     - **`ClientState::GetDressJob`** (`0059d2e0`): +0xcec, or 0xd and 0x15 for 7 and 0xe while
       bit 5 of +0x61c is set;
     - the sex, as above;
     - the hair (+0x5f4) and the weapon's kind (+0x5f8);
     - the headgear: +0x600, +0x604 and +0x608;
     - the shield (+0x5fc);
     - the hair's colour (+0x610) and the clothes' (+0x60c).
   - The words go back to what they were.
10. **`CGameActor::SetStatus`** (`00506c80`, [arrival.md](arrival.md#the-status)) with +0x614, +0x61c and
    +0x618, announcing nothing. Each bit set in the session's words takes effect as it would
    for any actor. The player is its own (`ClientState`+0xcdc), so it keeps its shadow while
    hiding.
11. **The status icons:**
    - It sends itself message 0x6d with 0xa4 (vf02), which ends any status icons it shows.
    - Effect 0xa4 is launched on it (`CAbleToMakeEffect::LaunchEffect`, `00501e10`, always made).
      That is `ProcessStatusIcons` ([effect-prim.md](../render/effect-prim.md#the-status-icons)).
      The effect is kept at +0x3d8. It joins the end of the world's list, as every launched
      effect does ([effect-prim.md](../render/effect-prim.md#launching-an-effect)), so it is there
      before the player, which `CreatePlayer` appends after the constructor.
12. **`CPlayer::LaunchStatusEffects`** (`0054cf80`): for each of the player's statuses (the
    vector at `00641f98`, which packet 0x0196 fills), in order:

    | Status | Effect | Then |
    |--------|--------|------|
    | 2 | 0xa6, the after-images | message 0x2c with 0xffff and 0x82 |
    | 0x19 | 0xa6 | 0x2c with 0xffff and 0x80 |
    | 0x1f | 0xa6 | 0x2c with 0xffff and 0xa9 |
    | 0x44 | 0xa6 | 0x2c with 0xffff and 0xfa |
    | 0x56 | 0xcb | |
    | 0x57 | 0xfe | |

    An effect is sent its message only when it was made: with effects off (`00687ea8`), none of
    these is.

## In the port

`src/world/own_player`:
- `OwnSession`: the session's fields above.
- `OwnDressJob` (`GetDressJob`) and `OwnDress`: vf14's words.
- `LaunchOwnStatusEffects`.
- `MakeOwnPlayer`: the constructor and the reset. It makes a `PlayerActor` with `own` set, and
  `reports` false: a `CPlayer` reports neither its cell nor its walking. `OwnPlayer::status_icons`
  keeps the status icons' effect by its number in `StateContext::launched`; the effect isn't
  kept in the world's list, nor drawn (it draws only icons for the statuses). The caller gives
  the constructor's body in `context.act` and the player's id in `context.own_id`.

`ragres draw-frame --own` makes it as `CWorld::CreatePlayer` does
([frame.md](../render/frame.md#in-the-port)).

## Checks

`tools/own_make_vectors.py` runs `CPlayer`'s constructor in the emulation oracle 600 times,
across walk_vectors' three small maps. It varies:
- cells at the edges, on walls and on hills;
- directions from −100 to 100;
- jobs about the mounted ones, and sexes;
- each bit of the status words;
- the statuses that show effects, with effects on or off;
- times about 1200 ms and 2^32.

As the constructor calls the reset, the player's vtable becomes a copy whose vf02 and vf14
record their words and return. `tests/unit/walk_test.cpp` matches every field, the game mode's
messages, `CGameMode`+0x2a0, the messages it sends itself, vf14's words, the effects launched
and what those made are sent. It doesn't compare +0x1bc for the bard and the dancer, whose
sex vf14's `CPc::UpdateBody` sets ([look.md](look.md)).

The directions stop at 100 for a reason. `CGameActor::GetDirection` (`00502b00`) takes 360 off
a facing until it is below 360, and a facing past 2^24 never gets there: the original never
returns.
