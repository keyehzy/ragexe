# The server's moves and its clock

The server moves actors with two packets: 0x0087 moves the own player, and 0x0086 moves any
other actor. Each carries the move's time by the server's clock, so the client keeps a clock in
step with the server. The port is `src/world/server_moves`.

## The clock

`ClientState` (`00686670`) keeps the server's time as an offset from `timeGetTime`:

| Offset | What |
|-------:|------|
| +0x504 | the offset |
| +0x51c | how many times in a row a packet's time came over 72 ms behind the clock |

**`ClientState::GetServerTime`** (`00593200`) is `timeGetTime` + (−72 − the offset). Every
path's times are reckoned by it ([path.md](path.md#the-callers)).

**`ClientState::SetServerTime`** (`005931d0`) clears +0x51c and sets the offset to `timeGetTime`
less the time it is given. The clock then reads 72 ms behind that time. The login's packet
0x0073 sets it (`0053e5d3`), and so do the game's 0x0073 (`00527953`) and 0x007f (`005272fc`).

**`ClientState::SyncServerTime`** (`00593170`) takes a packet's time *t*. Let *a* be
`GetServerTime` + 72, where the clock will be in 72 ms. All comparisons are unsigned, so they
wrap:
- *t* after *a*: `SetServerTime(t)`.
- *a* past *t* + 144: +0x51c goes up by one (`inc`, wrapping). Once it is 4 or more as a signed
  number, `SetServerTime(a − 144)`, which sets the clock back 72 ms. A count wrapped negative
  never resets the clock.
- Otherwise +0x51c is 0.

So a time over 72 ms ahead of the clock moves it forward at once, and one over 72 ms behind it
moves it back only after four in a row. The handlers of packets 0x007b, 0x007f, 0x0086, 0x0087 and 0x01da call it,
and so does the mini-game's `mg::mgType1::CMonster::EnterState01` (`004efc7f`).

## The packets

Both packets pack two cells and the sixteenths into 6 bytes, *b0* to *b5*. Each coordinate takes
10 bits and each sixteenth 4:

| Field | Bits |
|-------|------|
| start x | *b0* << 2 \| *b1* >> 6 |
| start y | (*b1* & 0x3f) << 4 \| *b2* >> 4 |
| goal x | (*b2* & 0xf) << 6 \| *b3* >> 2 |
| goal y | (*b3* & 3) << 8 \| *b4* |
| sixteenths x | *b5* >> 4 |
| sixteenths y | *b5* & 0xf |

The handler builds message 0's order from them ([states.md](states.md#the-move)): the time, the
start, the sixteenths, then the goal.

**`CGameMode::HandlePacket_0087`** (`00526d60`), 12 bytes, the own player's move:
- the time at +2 and the cells at +6;
- `SyncServerTime` with the time;
- message 0 to the own player (`CWorld`+0x3c, vf02), with no sender and no other words. The
  player moves at its own speed ([orders.md](orders.md#the-move)).

**`CGameMode::HandlePacket_0086`** (`00526ca0`), 16 bytes, another actor's move:
- the actor's id at +2, the cells at +6 and the time at +0xc;
- the actor found by `CWorld::GetActorById` (`004c4cf0`);
- `SyncServerTime` with the time, whether the actor is found or not;
- message 0 to the actor, if found, as above.

## In the port

`src/world/server_moves.{h,cpp}`, in `ragexe_world`:
- `ServerClock`: +0x504 and +0x51c, with `Time` (`GetServerTime`), `Set` and `Sync`.
- `UnpackMoveCells`: the 6 bytes to a `MoveOrder`.
- `ReadOwnMove` and `ReadActorMove`: the packets.
- `ReceiveOwnMove` and `ReceiveActorMove`: each handler's reading and its clock's step. They
  return the order for the caller to send as message 0, and `ReceiveActorMove` returns the id
  for it to find.

`ragres draw-frame --move` sends the own player its move as packet 0x0087
([frame.md](../render/frame.md#in-the-port)).

## Checks

`tools/server_move_vectors.py` runs both handlers in the emulation oracle on 800 packets. It
varies:
- cells at each byte's edges and at random;
- times about the clock's bounds, 72 ms either side of where it will be, and at random;
- the offset and the count, the count also at 2^31 − 1 and −1;
- `timeGetTime` about 72 and 2^32;
- for 0x0086, an actor found or not.

The own player and the actor found are stand-ins whose vf02 records its words and the order.
`tests/unit/server_moves_test.cpp` matches the clock afterwards, the ids asked for and every
message.
