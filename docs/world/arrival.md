# Actors coming into view

The game mode brings an actor into view with a message to it, `CGameActor::SendMsg`
(`00504460`, `vf02`): 3 and 4 when it stands, 5 and 6 when it walks. Each carries a record of
the actor, built by the game mode from a packet, and with it the actor's three status words,
which `CGameActor` `00506c80` (here `SetStatus`) takes. The port is `src/world/actor_arrival`.

`CPc` has its own messages 3 to 6 (`CPc::SendMsg`, `00543540`). They also set the player's look
(`vf14`), its state (dead or sitting) and +0x294: [look.md](look.md).

## The records

Messages 3 and 4 share one record, and 5 and 6 another. The offsets that `CGameActor` reads:

| Standing (3, 4) | Walking (5, 6) | What |
|-----------------|----------------|------|
| +0 | +0 | the id, to +0x1b4 (4 and 6 only) |
| +4, +8 | +4, +8 | the job and a second value, for `vf13`, which `CGameActor`'s ignores (`004eb060`, `ret 8`) |
| +0xc, +0x10 | +0x10, +0x14 | the cell |
| +0x14 | | the direction |
| | +0xc | the move time |
| | +0x18, +0x1c | the sixteenths in the cell |
| | +0x20, +0x24 | the goal |
| +0x18 | +0x28 | the speed, to +0xc8 (3 leaves it alone) |
| +0x1c, +0x20 | | to +0x1ec and +0x1f0 |
| +0x24 | +0x2c | to +0x1f4 |
| +0x34, +0x3c, +0x38 | +0x38, +0x40, +0x3c | the status words, in `SetStatus`'s order |
| +0x40 | +0x44 | to +0x224 (`SetField224`, `00506be0`; 3 leaves it alone) |
| +0x5c, +0x60 | +0x60, +0x64 | the guild and its emblem's version, for `vf38` |
| +0x64, +0x68 | +0x68, +0x6c | for `vf37`, which `CGameActor`'s ignores (`004eb1d0`) |

The job is first passed through `RemapValueByFlagBit5` (`00504190`, the mounted jobs). On a
`CGameActor` only message 4 reads it again, to test for 0x2d2, which the remapping never makes
or unmakes.

## The messages

**Message 3** (`0050597b`):
1. the actor at the cell's centre (`CGameMode::GetCellCentre`, `00511de0`), at the height
   there (`C3dAttr::GetHeight`);
2. the facing (direction + 4) × 45°, multiplied as integers and then made a float;
3. +0x1ec, +0x1f0, the goal (+0xc0, +0xc4) the cell itself, and +0x1f4;
4. the status, `SetStatus` (below), with 0 for its last argument;
5. standing, `SetState(0)` ([states.md](states.md));
6. the emblem, `vf38` (below).

**Message 4** (`00505a33`) is message 3 with the id, the speed and +0x224, but no emblem, except
for job 0x2d2. That job's guild is the record's +0x48 shifted up 16 bits, ORed with +0x4c, and its
version is +0x44. The message's next argument (`how`) then counts:
- **1:** the colour becomes `0x44ffffff`;
- **2:** effect 6 is launched (`LaunchEffect`) unless bit 6 of the effect state is set. This is
  not ported.

**Message 5** (`00505d11`):
1. the speed and the move time;
2. +0x1f4, the status, +0x224 and the emblem;
3. `CGameActor::FindMovePath` from the start cell and sixteenths to the goal, at the record's
   speed, with the actor left where it is;
4. **found:** the goal kept, walking (`SetState(1)`), the step left (+0x194) 0, as message 0
   does. **Not found:** nothing more.

**Message 6** (`00505bc9`) is message 5 with the id, and the actor placed before the search:
- **The point:** the start cell's centre plus, along each axis, the cell size × 1/16
  (`005e8a7c`) × (the sixteenths − 8), each product rounded to single precision.
- **The height:** the height at that point.
- **Not found:** the actor stands.

## The status

`SetStatus(body, effect, health, announce)` keeps the three words at +0x218, +0x21c and
+0x220, then shows what changed in the effect word, bit by bit, against the old one. Messages
3 to 6 pass `announce` 0. With 1, other callers also launch effect 0x10 and play a sound for
each health bit; that is not ported.

The actor sends itself messages through `vf02`, from no sender. Their handlers make and end the
actor's effects ([effects.md](effects.md)), and the port's do too. "The player" below is
`ClientState`+0xcdc, the player's id.

**A seer** sees hidden actors. `ClientState` `00597950` asked about 7 says so: on the Korean
service (`g_serviceType` 0) only, for the ids 0x186b4, 0x2933c9 and 0x326b70.

**Bit 0** newly set plays `effect\EF_Sight.wav` where the actor stands, when the world has a
player (`CWorld`+0x3c).

**Bits 1 and 2**, hiding and cloaking, each on a change:
1. **The shown effects end** (`00506bf0`): message 0x6d with 0xc8, then 0xc9, then 0xca five
   times.
2. **Set:**
   - +0x85 (the body drawn) is cleared, and for bit 2 +0x84 (drawn at all) too;
   - when the actor is not the player and the player is not a seer: +0x88 (the shadow) is
     cleared, and messages 0x6d with 0x9f and 0x23 go out (bit 2 sends them the other way
     round).
3. **Cleared:** +0x85 is set (for bit 2 +0x84 too), and for others than the player +0x88.
4. **Either way, the effects of bits 3, 7 to 10 and 4,** while they are set: put away when
   hiding (0x34, 0x66), back when shown again (0x33, 0x65).

**Bits 3 and 7 to 10** (any of them) on a change: message 0x34, then, while any is set, 0x33
with the whole effect word.

**Bit 4** on a change: 0x65 set, 0x66 cleared. **Bit 5**: 0x69 set, 0x6a cleared.

**Bit 6** on a change hides the actor from everyone, the player too. First the shown effects
end, then:
- **set, for a seer:** +0x84 and +0x85 set, +0x88 cleared;
- **set, for anyone else:** all three cleared, and message 0x6d with 0x9f goes out;
- **cleared:** all three set.

The effects of bits 3, 7 to 10 and 4 then follow, as for bits 1 and 2.

**The body word,** when it changes. First what the old one leaves:

| Old | What |
|----:|------|
| 1 | `_stone_explosion.wav` |
| 2 | messages 0x3b and 0x3e, `_frozen_explosion.wav`, +0x82 and +0x87 cleared, +0x81 set, the colour's red, green and blue 0xff |
| 3 | message 0x48 |
| 4 | message 0x46 |

Each of these four also clears `CGameMode`+0x2a0. Their sounds read the world's player without
a check.

Then what the new one takes:

| New | What |
|----:|------|
| 1 | +0x82, +0x87 and +0x81 set, the colour (0x40, 0x40, 0x40) |
| 2 | message 0x3a, +0x82, +0x87 and +0x81 set, the colour (0, 0x80, 0xff) |
| 3 | message 0x47 |
| 4 | message 0x45 |
| 6 | +0x87 set, the colour (0x80, 0x80, 0x80) |

**The health word,** each time:

| Bit | Set | Clear |
|----:|-----|-------|
| 0 | +0x87, the colour (0xff, 0x80, 0xff) | |
| 1 | message 0x43, +0x87, the colour (0xc8, 0x32, 0x32) | message 0x44 |
| 2 | 0x49 | 0x4a |
| 5 | 0x4b | 0x4c |
| 6 | 0x4e | 0x4f |
| 4 | `g_groundWaterFade` (`00679f9c`) 1, for the player | 0, for the player |

The colours are red, green and blue. `CRenderObject::SetColorChannels` (`00500720`) changes
those bytes of +0x7c and keeps the alpha.

**Both words 0** at the end: +0x87 and +0x82 cleared, and the colour's red, green and blue
0xff.

## The emblem

`CGameActor::vf38` (`00504390`) takes a guild and its emblem's version. It acts only when all
of these hold:
- the guild is above 0;
- the version is above 0, or the job (`GetJob`) is 0x2d2;
- `GetJob` is below 0x16, 0x505 to 0x507 (`ClientState` `005aa610`, `005aa6d0`), or 0x2d2.

Then:
1. both are kept, at +0x23c and +0x240;
2. the emblem's file is named by `ClientState::GetEmblemFileName` (`0059c9f0`):
   - `<server>_<guild>_<version>.ebm` for a version above 0, the server's name being
     `ClientState`+0x4e4, the character server chosen at login (`CLoginMode::SendMsg`,
     `0053d3c6`);
   - `..\data\texture\effect\flagGraffiti<guild>.ebm` otherwise;
3. when `ResourceManager::Exists` (`004e0190`) does not find the file, the game mode is told:
   message 0x8b with the guild (`CGameMode::SendMsg`, through `0067c9ec`).

## In the port

`src/world/actor_arrival.{h,cpp}`, in `ragexe_world`:
- `StandingArrival` and `WalkingArrival`, the records, with the offsets above;
- `ArriveGameActor` for messages 3 and 4, and `ArriveWalkingGameActor` for 5 and 6;
- `SetGameActorStatus`, `SetStatus` with `announce` 0, which also sends its messages through
  `SendGameActorEffectMessage`;
- `SetGameActorEmblem` (`vf38`), `EmblemFileName` and `SeesHiddenActors`.

`StateContext` gained what these read or write:
- the actor's messages to itself (`actor_messages`);
- the sounds, by name and place from the player (`Sound`);
- the world's player, the player's id and `g_groundWaterFade`;
- the server's name, and `resource_exists` for the emblem's file.

The other fields:
- `GameActor` gained +0x1ec, +0x1f0, the body and health words (+0x218, +0x220; `CPc`'s +0x218
  moved here) and the emblem's version (+0x240).
- `RenderObject` gained +0x84, +0x85 and +0x87, set as `CRenderObject`'s constructor sets them
  (`00500400`): drawn, the body drawn, untinted.

`ragres find-paths` takes `A`, a `G` actor coming into view: the message and its `how`, the
words and flags before, the player's id, whether the emblem's file is there, the world's player,
then the record's 28 words, then the updates.

## Checks

`tools/walk_oracle.py` also brings actors into view on every map's `.gat` (287), through the
original's `SendMsg` with a record in memory, then runs `ProcessState` update by update.
- **What the oracle records:**
  - the actor's messages to itself, through a copy of its vtable whose `vf02` is a recorder;
  - `PlayWave`'s calls;
  - the files `ResourceManager::Exists` is asked about, answered by the query;
  - the game mode's messages.
- **What varies:** the message, the words before and in the record (often equal, often one bit
  apart), the flags before, the player's id (often the actor's, or a seer's) and the service
  type, the job by `GetJob` and in the record, the guild and version, the emblem's file there
  or not, and a player in the world or none (always one when body state 1 or 2 is left).
- **What is hashed:** after the message and after each update, every field the moves and
  states write, plus the path, +0x1b4, +0x1ec to +0x240's fields, the speed, the four flags,
  `g_groundWaterFade`, `CGameMode`+0x2a0, the messages, the sounds and the files asked about.

The hashes equal `ragres find-paths` for 11,480 arrivals over 1,462,447 updates: 2,099
changed state (a walker that stood), 639 faded and 8,742 stayed.

`tools/arrival_vectors.py` writes `tests/unit/arrival_vectors.inc`. It holds arrivals on
`tools/walk_vectors.py`'s three maps, then ones that each hang on one thing:
- each effect bit set and cleared, by the player, by others and by seers on both services;
- the grouped bits changing while one stays set;
- every body state left for every other;
- each health bit, the player's own and another's;
- the emblem by job, guild, version and file;
- message 4's job 0x2d2, its fade-in and its hidden effect;
- odd directions, and a cell off the map;
- message 6's sixteenths;
- the four flags before;
- held or finished before, through a message 5 that finds no path, so that no `SetAction`
  follows the status;
- a record's id that messages 3 and 5 leave alone.

`tests/unit/walk_test.cpp` replays them, and checks `EmblemFileName`'s two forms.

**Not ported:**
- message 4's effect 6;
- `SetStatus` with `announce` set;
- the guild war map's work: `005078e0` and `005078a0`, run when `ClientState::IsGuildWarMap`
  (`CWorld`+0x60) is set;
- the launched effects that 0x6d ends ([effects.md](effects.md#the-rest)).
