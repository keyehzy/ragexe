# Packet framing

How the client cuts the byte stream from the server into packets, and how long each
packet is. The per-id table is generated: `analysis/exports/Ragexe.exe.packets.tsv`
lists every id the client knows with its handler in each mode, its length and, for
handlers, how far into the packet they read (`tools/recover_packets.py`).

## Queues

The connection keeps four `PacketQueue`s. Each is a byte buffer (a `std::vector<char>`)
with a read and a write position: `Peek`, `Read` and `Skip` work at the read position,
and `Append` writes at the write position. When the data does not fit, `Append` moves the
unread bytes to the front, doubling the buffer first if that is still not enough room.

| Member | Holds | Filled by | Emptied by |
|--------|-------|-----------|------------|
| `CConnection+0x40` | received bytes | `FD_READ`: `recv` in 2 KB reads | `RecvPacket`, through the receive filter |
| `CRagConnection+0xa8` | received, filtered | `RecvPacket` | `CutPacket` |
| `CConnection+0x24` | bytes to send | `CConnection::Send` | `send`, at once in `Send` and again on `FD_WRITE` |
| `CConnection+0x5c` | held sends | `Send` while the hold flag `+0x22` is set | `SetHoldSends(0)`, which passes them to `Send` |

- **Sending.** `CRagConnection::SendPacket` copies a packet to a static buffer, runs the
  send filter over it and calls `Send`.
- **The hold flag.** `CLoginMode::OnChangeState` sets it with `SetHoldSends(1)` right
  after a send. `CGameMode::HandlePacket_0092` also calls `SetHoldSends`, and both
  `HandlePacket_0073` (game and login) clear it.
- **When the expected filter modules have all arrived,** `RecvPacket` also passes the
  held sends through the send filter into `+0x24`. It moves the filtered receive queue
  back into `+0x40`, so that it goes through the new receive filter.
- **The filters** are the server-sent code of [code-modules.md](code-modules.md). Before
  the server sends any, they pass the bytes through unchanged.

## Cutting a packet

`CRagConnection::RecvPacket` ends with `CutPacket` on the filtered receive queue:

1. Peek at the first 2 bytes, the id (little-endian).
2. Look the id up in the length map, a `std::map<int, int>` at `CRagConnection+0x7c`:
   - **−1:** the packet has a variable length. Peek at 4 bytes; the word at offset 2 is
     the packet's total length, including the id and the length word.
   - **anything else:** the packet has that total length, at least 2.
3. If the whole packet has arrived, copy it out and return true. Otherwise leave the
   queue untouched and return false; the dispatcher stops for this frame.

An id the map does not have is added to it with 0 by the lookup, so it is read as a
2-byte packet. The dispatchers then pass it to `CRagConnection::vf02`, which clears the
receive queue, so every packet already queued behind it is lost as well.

## The length table

`CRagConnection::InitPacketLengths` (`0x419400`), the constructor's last call, fills the
map: 371 ids from 0x64 to 0x1e0. Of these, 61 are variable, and the fixed lengths run
from 2 to 186 bytes. The table covers both directions: 135 of the ids have no handler in
either dispatcher, among them the ones the client only sends, such as 0x64, 55 bytes.

| | Fixed length | Variable |
|--|--:|--:|
| `CGameMode` handlers | 175 | 41 |
| `CLoginMode` handlers | 13 | 3 |
| ids the dispatchers skip | 7 | 1 |
| ids with no handler | 119 | 16 |

Every handled id has an entry. The tool checks each fixed-length handler against its
length:
- None of them reads a constant offset past the end of its packet.
- For 104 of the 166 that read the packet at a constant offset, the furthest read ends
  exactly at the declared length.
- The rest read less: their packets end in fields they do not use, or they read the
  packet indirectly.

## Consequences for the port

- **The table is part of the protocol.** The port needs the same lengths to cut the
  stream, since fixed-length packets carry no length of their own.
- **Keep the unknown-id behaviour,** at least as a compatibility mode: the client
  silently drops the whole receive queue on an unknown id. A server or proxy sending an
  id the 2003 client lacks desynchronises it in this way, not with an error.
- **Packet order is preserved.** A packet that has not fully arrived waits for the next
  frame, and nothing behind it is processed first.
