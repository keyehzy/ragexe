# Server-supplied packet filter code (packet 0x01C7)

The client can receive x86 code from the server and run every packet it sends and
receives through it. This is the only place where Ragexe.exe creates or changes code at
run time (`tools/runtime_code_check.py`, report in `analysis/exports/Ragexe.exe.runtime_code.txt`).
Names below are from `analysis/labels/inferred.*.tsv`.

## Pieces

| Address | Name | Role |
|---|---|---|
| `0041ea70` | `PacketFilterStub(buffer, length, out_length)` | the code that gets patched |
| `CRagConnection+0x8c` | `PacketCodeModules` | holds the received code and the patch slot |
| `0041ee10` | `PacketCodeModules::PatchStub` | rewrites the stub in `.text` |
| `0041eb50` / `0041ec10` | `AddSendModule` / `AddRecvModule` | store one received module |
| `0041ecd0` / `0041ed20` | `RunSendModules` / `RunRecvModules` | run the modules over a buffer |
| `00418fc0` | `CRagConnection::RecvPacket` | reads modules from the stream, filters data |
| `00419250` | `CRagConnection::SetExpectedModules(recv, send)` | stores the counts at `+0xc4` / `+0xc8` |
| `00537a40`, `0053eba0` | `HandlePacket_01C7` | game- and login-mode handlers of packet 0x01C7 |
| `00614d74`, `00614d78` | `g_recvModuleCount`, `g_sendModuleCount` | both initialised to 2 |

## The patched stub

Unpatched, `PacketFilterStub` copies the buffer to a 1 KB stack area and back and stores
the length in `*out_length`: a filter that changes nothing. Its body contains the 6-byte
marker `33 c0 33 db 33 c9` (`xor eax,eax; xor ebx,ebx; xor ecx,ecx`) twice, at `0041ea7c`
and `0041eabd`; the marker is also stored at `00605b08`.

`PatchStub` (called from `CRagConnection::CRagConnection` and `CRagConnection::vf01`, through
`Reset`):

1. Follows `e9` jumps from the function address, then finds the first marker (`FindStubMarker`).
2. `VirtualProtect(marker + 6, 10, PAGE_READWRITE, ...)`. The ten bytes stay non-executable
   under this protection; the patch works only where data execution prevention is off,
   as on the Windows versions of 2003.
3. Writes `e8` (call) at `marker + 6` = `0041ea82` and keeps the address of its rel32
   operand at `PacketCodeModules+0x18`.
4. Writes `e9` (jmp) at `marker + 11` = `0041ea87` to just past the second marker,
   `0041eac3`, which is the function's epilogue.

Patched, the stub saves registers, calls the current module with the stub's own frame
(`[ebp+8]` buffer, `[ebp+0xc]` length, `[ebp+0x10]` out length) and returns.

## Receiving modules

Packet 0x01C7 is handled in both dispatchers: game mode `00524d80` (index table
`00526188`, entry for `id - 0x73`) and login mode `CLoginMode::method_0053cfa0` (index
table `0053d1d8`, entry for `id - 0x187`). Both handlers only call
`SetExpectedModules(g_recvModuleCount, g_sendModuleCount)`, that is (2, 2). The packet's
own fields are not read by the handler.

While either count is non-zero, `RecvPacket` reads modules from the receive stream
instead of packets. Each module is:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `a` |
| 2 | 2 | `b`; the code length is `(short)(a ^ b)` |
| 4 | length | code bytes, each XORed with the low byte of `b` |

Receive modules come first (`AddRecvModule`, count at `+0xc4`), then send modules
(`AddSendModule`, count at `+0xc8`). Each is decoded into a 4096-byte buffer, padded with
`_rand()` bytes; the last four bytes are `5d eb 41 00`. The buffer is stored in a
`0x41008`-byte list node (next, prev, a 256 KB area, then the 4 KB code at node `+0x40008`).
The code runs from the heap, which was executable at the time.

## Running them

`RunSendModules(buffer, length)` and `RunRecvModules(buffer, length)` walk their list. For
each module they point the patched call at its code (`WriteRel32` into the slot at `+0x18`)
and call `PacketFilterStub(buffer, length, node + 8, 1)`, so the modules are applied in
the order received, in place.

- Sending: `CRagConnection::method_00418f70`, used by every send, copies the packet to
  `006260d0`, runs `RunSendModules` on it and passes it on to `CConnection::method_00418690`.
- `RecvPacket`, once no modules are pending, moves data between four byte queues of
  `CRagConnection` (appended to with `0041fb60`, consumed with `0041fd00`):
  1. `+0x5c` → `RunSendModules` → `+0x24`.
  2. `+0xa8` → `+0x40` unchanged, so data already taken from the stream goes through the
     receive modules too.
  3. `+0x40` → `RunRecvModules` → `+0xa8`, from which `00419370` extracts a packet.

  The 0x24 / 0x5c pair are outgoing queues and 0x40 / 0xa8 incoming ones; which `CConnection`
  functions fill and drain them is not traced yet.

`Reset` frees both lists and patches the stub again, on construction and from
`CRagConnection::vf01`.

## For the port

The port cannot run server-supplied x86 natively. It keeps the stream format exactly: it
recognises 0x01C7, reads and stores the modules, and passes buffers through a
`PacketFilter` interface. The default filter is the identity, which is what the client
does before any 0x01C7. A server that does send modules can be served by running them in
the emulator used for the oracle (`tools/emu/`), where each module is a function called
with the stub's frame. Whether servers of the time sent 0x01C7 is not known from the
client; a stub server for the port does not need to.
