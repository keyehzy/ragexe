# The own player's messages

What the user asks of the world's own player (a **`CPlayer`**, [pursuit.md](pursuit.md)) and what
its pursuit asks of it arrive as messages: **`CPlayer::SendMsg`** (`0054c5d0`, vf02). Its switch
takes messages 0 to 0x67 by the bytes at `0054cf18` into the table at `0054cec4`; 21 of them it
handles itself, and the rest, index 20, it leaves to `CPc::SendMsg` (`0054ceb5`). The port is
`SendOwnPlayerMessage` in `src/world/player_orders`.

Messages take three words after the id (`[ebp+0x10]`, `+0x14`, `+0x18`). Message 0's first
word points at a move's order, and 0x67's second at a skill's record. What goes to the game mode
is `CGameMode::SendMsg` (vf05) with its id and three words.

| Message | Words | What it does |
|---------|-------|--------------|
| 0 | the order | the move (below) |
| 8 | | stands (`SetState(0)`) |
| 9 | x, y | PreMove set (+0x31c 1, +0x314 `timeGetTime`), the goal (+0x308, +0x30c) the cell |
| 0xa | id, mode | the attack (below) |
| 0xd | | the pursuit ends: +0x318 and +0x324 0, the game mode 0x1c |
| 0xf | | an attack (pursuit 1 or 5) with +0x320 set: +0x320 2, then with +0x334 set 0xd and the game mode 0x50; a cast on an actor (3): the game mode 0x1c |
| 0x10 | x, y | standing, sliding or fallen (0, 4, 6), it looks at the cell (below) |
| 0x11 | x, y | 0xd; then while attacking, flinching or falling (2, 5, 6) the walk waits (+0x32c 1, +0x358, +0x35c the cell), otherwise +0x358, +0x35c -1, +0x32c 0 and 0x5b to the cell |
| 0x12 | id | the item pursued (2) |
| 0x1d | id, job | the game mode 0x96 with 0 and the id for jobs 0x8d, 0x8f and 0x98, otherwise +0x330 |
| 0x1e, 0x1f | | not hiding: standing the game mode 0x96 with 2 and +0x330, fallen with 3 |
| 0x28 | id, job | the game mode 0x96 with 7 and the id for those jobs; otherwise with +0x330, unless the actor `CWorld::GetActorById` finds for it has +0x1f4 20 |
| 0x29 | id, skill, range | not fallen, and hiding only for skill 0x89: the cast on an actor (3), +0x344 the skill, +0x330 the id, +0x348 the range; logs "AM_PROCEEDSKILL" to `DebugLog` (`004060f0`), empty in this build |
| 0x2a | | the game mode 0x4c with +0x344, +0x330 and +0x34c |
| 0x3c | id | the attack again: pursuit 5 after 1, otherwise 1; +0x338 1200 ms before `timeGetTime`, so the first ask goes at once; +0x324, +0x334 0, +0x330 the id, the goal -1 |
| 0x41 | skill, x, y | the cast on a cell (4): +0x344, +0x350, +0x354, +0x348 `CGameMode`+0x364, +0x330 0 |
| 0x42 | | the game mode 0x4d with the address of {+0x344, +0x350, +0x354, +0x34c}; for skill 0x7d window 0x54 (`UIWindowMgr` `004ad670`) is sent 0x44 with it instead (its vf37) |
| 0x5a | level | +0x34c |
| 0x5b | x, y | the game mode 0x97 to the cell, unless +0x328 is set and `timeGetTime` is not past +0x33c + 1000 (unsigned, wrapping); then +0x328 1 and +0x33c the time |
| 0x67 | id, the record | the cast on a skill's object (6): +0x344, +0x348 and +0x34c the record's +4, +8 and +0xc, +0x330 the id |

"Hiding" is bit 1 of the effect state (+0x21c, `IsBit1Set`, `0059d350`). Messages 0xa, 0x12,
0x3c, 0x41 and 0x67 do nothing while hiding or fallen (state 6); 0x29 as the table says.
`CGameMode`+0x364 is the range of the skill being aimed: `CGameMode::SendMsg` takes it from the
skill's record (+0x18, `00521cde`), and packet 0x019e sets it to 5.

Two fields join the pursuit's ([pursuit.md](pursuit.md#the-fields)). `CPlayer::ResetPlayerFields`
(`0054b4a0`, [own.md](own.md)) starts +0x328 at 0 and +0x33c 1200 ms back:

| Offset | What |
|--------|------|
| +0x328 | set once message 0x5b asks the game mode to walk; a move (0) clears it |
| +0x33c | when 0x5b last asked |

## The move

Message 0 (`0054ca73`) is `CGameActor`'s ([states.md](states.md)) at the player's own speed:
`ClientState`+0xd94 (`00687404`), which packet 0x00b0 sets, not +0xc8. First +0x328 and PreMove
(+0x31c) are cleared and +0x18c takes the order's time. `CGameActor::FindMovePath` from the
order's cell; found, the goal (+0xc0, +0xc4) is the order's, the player walks (`SetState(1)`,
through `CPlayer::SetState`) and +0x194 is 0. Not found, `FindMovePath` again from the cell and
sixteenths under the player (`CGameMode::GetCellPoint`, `00511c30`); found, the goal and walking
as before, but +0x194 stays as it was. Found neither time, nothing more: the player does not
stand, as `CGameActor`'s does.

## The attack

Message 0xa (`0054cd77`): not hiding or fallen, the actor `GetActorById` finds for the id; none,
nothing. The pursuit is 5 when its speed (+0xc8) is past four thirds of the player's, 1
otherwise. Four thirds is the speed × 4 (`shl`, wrapping) divided by 3 (signed, by
0x55555556), and the comparison is unsigned (`jbe`): a negative speed is past every limit.
Then +0x324 and +0x334 0, +0x330 the id, +0x320 the second word.

## The look

Message 0x10 (`0054c898`) turns the player toward a cell's centre (`CGameMode::GetCellCentre`).
From the angle to it (`CRenderObject::GetAngleBetween`), the turn is `(int)(facing − angle)`
(`fsub`, `__ftol`), 360 more when below 0, and the sector `(int)((turn + 22.5) × (1/45))` (`fild`,
`fadd` and `fmul` by doubles, rounded once each to single precision), 8 more when below 0 and 8
less when 8 or more. The head (+0x294) is 0 ahead, 1 and 2 turned aside:

| Sector | The body (+0x38) | The head |
|--------|------------------|----------|
| 0, 4 | faces the cell | 0 |
| 1 | faces the cell when the head is 1 | 0 then; otherwise 1, the body left |
| 7 | faces the cell when the head is 2 | 0 then; otherwise 2, the body left |
| 2, 3 | the angle + 45, less 360 when 360 or more | 1 |
| 5, 6 | the angle − 45, plus 360 when below 0 or unordered | 2 |
| any other | left | left |

A turn below −787 or past 697 leaves the sector outside 0 to 7 once wrapped, and nothing turns.
Then the frame (+0x28) is the head, and the game mode hears 0x98 with the facing's direction,
`((int)facing + 180) / 45 % 8` (C's, so negative for a facing below −180), and the head.

## In the port

`src/world/player_orders.{h,cpp}`, in `ragexe_world`: `SendOwnPlayerMessage` takes an
`OwnMessage` (the id, the three words, message 0's order and 0x67's record) and returns false
for the messages it leaves to `CPc`. `OwnPlayer` has +0x328 (`walk_asked`), +0x33c
(`walk_asked_at`) and +0x34c (`level`); `StateContext` the player's speed (`own_speed`),
`CGameMode`+0x364 (`skill_range`) and what 0x42 asks for (`cell_skills`). The messages the player
sends itself while handling one (0xd, 0x5b) are handled at once; those `SetState` and the
pursuit send go through `StateContext::send_own` when it is set, otherwise into
`actor_messages`. Each message is kept in `actor_messages` as it arrives, as the oracle records
it at the switch.

`ragres find-paths` takes `m`: a player as the own player sent a script of these messages.

**Found on the way:**
- Message 0's second search keeps the segment (+0x194) the player was on, and a move found
  neither way leaves the state alone.
- 0xa's four thirds wraps for speeds past 2^29, and its unsigned comparison puts a negative
  speed past any limit.
- 0x1d and 0x28 act on their first word only for jobs 0x8d, 0x8f and 0x98 in their second; the
  pursuit sends them with no words, so for it they act on +0x330.
- `PathFinder::BuildPath` (`004ccda0`) and `GetFirstStepTime` (`004cced0`) load the speed
  exactly (`fild`, `fimul`) and round each product once before `__ftol`; the port rounded the
  speed to a float first, the same below 2^24 but not past it, and past 2^31 undefined. It now
  rounds once and wraps as `__ftol` does.

## Checks

`tools/walk_oracle.py` sends own players messages on every map's `.gat` (287).
- **The oracle:** `CPlayer::SendMsg` runs on a `CPlayer` as `o`'s ([pursuit.md](pursuit.md#checks))
  does, but every message, its own included, goes through: they are recorded at the switch
  (`0054c5fb`), 0x67's record word as 0. The game mode's 0x4d and window 0x54's 0x44 are recorded
  with the four words their address points at; `UIWindowMgr` `004ad670` gives a stand-in for
  window 0x54. One stand-in is what `GetActorById` finds, with its speed and +0x1f4.
- **What varies:** P's fields with the facing anywhere in −3000 to 3000, the states, hiding,
  +0xd0 and the head; o's own fields; +0x328, +0x33c, +0x34c, the player's speed about four
  thirds of the target's and wrapping, `CGameMode`+0x364; one to 16 messages of the 21, the
  time between them about 450, 1000 and 1200 ms; moves from the player's cell, another or off
  the map, to cells near, far or not walkable; looks at the cells about the player.
- **What is hashed:** after each message, the fields, the path, the game mode's messages, p's
  player fields, o's (with what was recorded at the switch), +0x328, +0x33c, +0x34c and what
  0x42 asked for.

The facings stay finite and modest: `CGameActor::GetDirection` (`00502b00`) takes an unordered
`fcom` as below 0 and adds 360 to a NaN facing forever, and a facing too large for 360 to change
it never comes within 0 to 360 either.

The hashes equal `ragres find-paths`: **11,480 own players** (`m`) sent 71,024 messages. The
full run, 294,010 queries on all 287 maps, keeps the older counts, and the older vector files
regenerate byte-identical.

`tools/own_orders_vectors.py` writes `tests/unit/own_orders_vectors.inc` (1,398), which
`tests/unit/walk_test.cpp` replays: the three maps' scripts, then messages that each hang on one
thing:
- each message in each state, hiding or not;
- the attack's pursuit about four thirds of the speed, wrapping and negative;
- 0xf by pursuit, +0x320 and +0x334;
- the look at the cell straight ahead (0°), its turn at each sector's edge, below 0 and at the
  limits, each head; the cells all around at each sector's middle; where the angle is exactly
  45° and 315° (π is `0x40490fd8`, a little short), so 5 and 6 reach 0 and 2 and 3 reach 360;
  a negative direction;
- a walk queued or asked; the walk asked about its second and wrapping;
- the jobs 0x1d and 0x28 act on as given, a target's +0x1f4, found or not;
- 0x29 while hiding; 0x3c's pursuit and +0x338; 0x42's window; 0x41, 0x5a, 0x67 and 0x2a in turn;
- moves found from the order, from the player's cell or not at all, at speeds about and past
  2^24, and a move that leaves the state as it was, with the queued move `SetState` then sends.

**Not ported:** what the game mode does with these (0x1c, 0x4c, 0x4d, 0x50, 0x96, 0x97, 0x98,
the requests to the server), and the messages `CPc::SendMsg` takes for the own player.
