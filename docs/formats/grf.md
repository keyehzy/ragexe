# GRF archives

The client reads all its game data through one archive object: the `CGPak` (RTTI) at
`0067cd00`, built by the static initializer `004dd750` on `"data.grf"` (`006125f0`)
and published through the pointer `0067cd84`. The `.gpf` patches in the install have
the same format (version 0x102); the client never opens them, since the patcher merges
them into `data.grf`. `CSakGPak` (RTTI, constructor `004ded50`) would stack one
archive over another, but nothing constructs it.

All numbers are little-endian. Offsets in the table are relative to the end of the
header (0x2e), not to the start of the file.

## Header

`CGPak::Open` (`004ddae0`) maps the first 0x2e bytes (`004ddb2b`):

| Offset | Size | Field |
|-------:|-----:|-------|
| 0x00 | 16 | `"Master of Magic"` and its NUL, compared as four dwords with `006126e8` (`004ddb44`) |
| 0x10 | 14 | not read by the client |
| 0x1e | 4 | table offset; the table starts at 0x2e + this (`004ddb7f`) |
| 0x22 | 4 | seed |
| 0x26 | 4 | entry count + seed + 7: the count is this − seed − 7 (`004ddb58`) |
| 0x2a | 4 | version |

- **Versions.** The header check accepts 0x100 to 0x105 (`004ddb67`, `004ddb73`). Each
  record then rejects anything above 0x103 with "Fuck off! FileVer is %x.."
  (`00612650`, `004ddd02`), so 0x100 to 0x103 load. `data.grf` is 0x103; the nine
  `.gpf` patches are 0x102.
- **The table** runs from its offset to the end of the file (`004ddb82`: file size −
  offset). If it cannot be read, the client shows "data.grf Error !!! Please reinstall
  client." (`00612678`) and exits (`004ddba5`).

## Table records

### Versions 0x101 to 0x103

`004ddcc2` to `004dde43`, one record after the other:

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 4 | `L` |
| 4 | `L` | name field |
| 4 + L | 4 | compressed size + size + 0x2cb |
| 8 + L | 4 | stored size + 0x92cb |
| 12 + L | 4 | size |
| 16 + L | 1 | flags |
| 17 + L | 4 | offset |

- **The next record** starts at 21 + L (`004dde10`).
- **The name** is bytes 2 to L − 4 of the name field: L − 6 bytes from record offset 6
  (`004ddd0d`). The first 2 and the last 4 bytes of the field are not read. Decoding
  takes two steps:
  1. Swap the nibbles of every byte (`SwapNibblesCopy` `004dd7f0`, calling
     `SwapNibbles` `004dd790`).
  2. Apply the block function to every 8-byte block (`DecryptBlocksMixed` with a cycle
     of 1, `004dddb5`; see [the cipher](#the-cipher)).

  The name is the result up to its first NUL (`strcpy` at `004dddcb`). Names are CP949,
  with `\` as the separator; `data.grf` names start with `data\`.
- **The entry** the client stores for each record is 0x114 bytes, in the vector at
  `CGPak+0x58`:

  | Offset | Field |
  |-------:|-------|
  | +0 | compressed size |
  | +4 | stored size |
  | +8 | size |
  | +0xc | offset |
  | +0x10 | flags, one byte |
  | +0x14 | the name as a [hashed name](#names-and-lookup) |

- **`CGPak+0x3c`** keeps the largest stored size, the size of the decode buffer at
  `+0x78` (`004dde46`).

### Version 0x100

The name is a NUL-terminated string at record offset 4, with its nibbles swapped and no
block function (`004ddcd4`: `SwapNibblesCopy` of strlen + 1 bytes). The client logs
"OldVer??" (`0061266c`) for each one. The fields start at record offset `L`, not
4 + L: `004dde01` adds `L` to the record start, which is never moved past the length
on this path. The next record is at 17 + L. None of our archives is 0x100.

### What the fields hold

- **Compressed size** is the zlib stream's length. The client uses it only to choose
  the decode cycle.
- **Stored size** is the compressed size rounded up to a multiple of 8. Files are packed
  one after another: each offset is the previous offset plus its stored size.
- **Flags.** The client tests only for 1 (compressed and encoded). In our archives
  every other entry has flags 0, and all of those are directory names (`data\effect`)
  carrying the same placeholder numbers: compressed 1097, stored 1812, size 1372,
  offset 1372.

After reading the table, the client sorts the entries with an inlined `std::sort`
(the rest of `004ddae0`, through `std::MedianOfThree`, `std::PartitionRange` and
`std::ShiftInsertSorted`), ordered by `HashedFileName::IsLess` (`004df7a0`).

## Names and lookup

- **Hashed names.** `HashedFileName::Set` (`004df750`) copies the name, lower-cases it
  with `_strlwr` (`005d3d6a`; the C locale, so only A–Z), and hashes it
  (`004df8a0`): `h = 0x1505`, then `h = h * 33 + c` for each byte `c`, taken as a
  **signed** char.
- **Order.** `IsLess` compares the hashes as unsigned numbers, then the names byte by
  byte as unsigned chars. `NotEqual` (`004df830`) compares the same way.
- **Archive lookup.** `004de830` and `004deb20` binary-search for the first entry not
  less than the key (lower bound), then check that it is equal.
- **Path building.** `MakeDataPath` (`004dd5a0`) turns a resource name into the key:
  - it prefixes `data\` (`0061ebc8`);
  - if the result contains a `:`, it keeps only the part from the character before the
    first `:` on, so an absolute `C:\...` path drops the prefix;
  - it collapses every `//` to `/`.

  There is no other normalization: `/` and `\` are different characters to the archive.
- **Search order.**
  - **Loading** (`CGPak::vf01`, `004dd9a0`, under the critical section at `+0x14`): the
    archive first (`004de830`), then the loose file of that name (`004dea90`:
    `CreateFileA`, then the whole file into `VirtualAlloc` memory).
  - **Checking that a file exists** (`CGPak::vf02`, `004dda10`): the archive
    (`004deb20`), then `FindFirstFileA` (`004debc0`).
  - The archive always wins over a loose file.

## Reading an entry

`004de830`, given the key:

1. **Allocate** size + 8 bytes (`VirtualAlloc`, `004de916`).
2. **Map** stored-size bytes at 0x2e + offset (`004de921`).
3. **Decode.** Look up the extension of the full name (`strrchr` `.`, then `_stricmp`;
   `004dec80`) in the list at `006125dc`: `.gnd`, `.gat`, `.act`, `.str`.
   - **Listed extension:**
     - flags 1: apply the block function to the first 20 blocks
       (`DecryptFirstBlocks`), then `uncompress`;
     - otherwise: copy size bytes of the stored data, with nothing decoded.
   - **Any other extension:** `DecryptBlocksMixed`, with the digit count taken from the
     compressed size written in decimal (`_itoa`, at least 1); then:
     - flags 1: `uncompress`;
     - otherwise: copy size bytes of the decoded data.
4. **`uncompress`** is `cps.dll`'s export. Its source length is the stored size, and its
   output length becomes the entry's length.

### cps.dll

`cps.dll` is zlib 1.0.4: it carries "deflate 1.0.4 Copyright 1995-1996 Jean-loup
Gailly" and "inflate 1.0.4 Copyright 1995-1996 Mark Adler", and exports only
`compress` and `uncompress`. The port uses current zlib.
- **Every compressed entry** of `data.grf` and the nine patches inflates with it to its
  recorded size (`ragres check`).
- **One exception:** one damaged entry, described under [In the port](#in-the-port).

## The cipher

`DesCipher` holds a DES key schedule (16 subkeys of 8 bytes) and runs one DES round
over 8-byte blocks. Its tables are the standard DES tables:

| Table | Address |
|-------|---------|
| IP | `005e8b00` |
| IP⁻¹ | `005e8b40` |
| E | `005e8bf8` |
| P | `005e8c28` |
| PC-1 | `005e8b90` and `005e8bac` |
| PC-2 | `005e8bc8` |
| shifts | `005e8b80` |
| S | `005e8c48` |

- **Bit order.** Bits are numbered from the most significant bit of the first byte (the
  mask table `005e8af8` is 0x80, 0x40, …).
- **The S table** is 8 tables of 64 bytes, each indexed directly by a 6-bit group.

### The key has no effect

- **What `SetKey` does** (`004dc1a0`): it clears C and D (`004dc1b3`, `004dc1b8`),
  then for each PC-1 bit that is set in the key, it **ANDs** the mask into C or D
  (`004dc207`, `004dc245`).
- **Why that makes every subkey zero:** C and D stay zero, so the rotations leave them
  at zero and every subkey is zero, whatever the key. The emulation oracle confirms it.
- **The keys the client builds, for the record:**
  - each table record uses `"Pr"` + 5 decimal digits + `"e"` (`004ddd88`);
  - each file uses `"n"` + the first 6 characters of its file name + `"k"`
    (`004decd0`).
- **Consequence:** only the zero-key round function matters.

### Block function

`DecryptBlocksMixed` (`004dc96b`–`004dcad8`) and `DecryptFirstBlocks` share the same
code. On an 8-byte block:

1. IP (`DesInitialPermutation` `004dcfa0`). Call the first 4 bytes L and the last 4 R.
2. Expand R by E into 48 bits, as eight 6-bit groups (`004dc99a`).
3. XOR the groups with the last subkey (`+0x78`), which is zero (`004dc9f7`).
4. Look group *i* up in S table *i*; the eight 4-bit results fill 4 bytes, high nibble
   first (`004dca12`).
5. Permute the 4 bytes by P and XOR them into L (`004dca6e`, `004dcab8`). There is no
   swap of L and R.
6. IP⁻¹ (`DesFinalPermutation` `004dd020`).

With a zero key and no swap, applying the function twice gives back the block, so it
encodes as well as decodes.

### DecryptBlocksMixed

`004dc820` (src, n, dst, out_n, key, digits). It sets out_n = n.

**The cycle** comes from `digits`:

| digits | cycle |
|--------|-------|
| below 3 | 1 |
| 3 or 4 | digits + 1 |
| 5 or 6 | digits + 9 |
| 7 or more | digits + 15 |

**Each 8-byte block i**, from 0, is handled as follows:
- **i < 20, or i a multiple of the cycle:** the block function.
- **Any other block** is a plain block. A counter of plain blocks, starting at 0,
  decides what happens to it:
  - **counter is 7:** the block is shuffled, and the counter becomes 1 (`004dc959`,
    `004dc963`);
  - **otherwise:** the block is copied, and the counter goes up by 1.

**The shuffle** (`004dc8bb`) of input bytes `s` into output bytes `d`:

| Output | Input |
|--------|-------|
| `d[0..1]` | `s[3..4]` |
| `d[2]` | `s[6]` |
| `d[3..4]` | `s[0..1]` |
| `d[5]` | `s[2]` |
| `d[6]` | `s[5]` |
| `d[7]` | `s[7]`, substituted |

The substitution swaps these byte pairs and leaves every other byte alone (jump tables
`004dcb14`/`004dcb34` and `004dcba4`/`004dcbc0`): `00`↔`2b`, `01`↔`68`,
`48`↔`77`, `60`↔`ff`, `6c`↔`80`, `b9`↔`c0`, `eb`↔`fe`.

**The loop** stops after the block that reaches or passes n (`004dcadb`), so it always
handles whole blocks: it handles ⌈n/8⌉ blocks, and at least one.

### DecryptFirstBlocks

`004dcc40` (src, n, dst, out_n). It copies n bytes, then applies the block function to
blocks 0 to 19, stopping early after the block that reaches n (`004dce00`–`004dce1c`).

## In the port

`src/res/grf.{h,cpp}` and `src/res/grf_cipher.{h,cpp}`:

- **Blocks.** Like the original, a decode handles whole blocks. When the stored size is
  not a multiple of 8, the reader reads the bytes that follow it in the file, as the
  mapped view would. In our archives every name field and every compressed entry is a
  whole number of blocks; only the flag-0 directory entries (stored size 1812) are not.
- **Broken streams.** The client ignores `uncompress`'s result (`004de947`,
  `004dea1d`) and keeps the recorded size, so the bytes past the break are the zeros of
  the fresh `VirtualAlloc` memory. One entry needs this:
  - `data\wav\worm_tail_damage.wav`, in `data.grf` and two of the patches.
  - Its stream is a single stored block of 1410 bytes, but only 1416 bytes are stored,
    so the last data byte and the checksum are missing.
  - The reader gives the 1409 bytes followed by a zero, and reports the entry as
    damaged.
- **Duplicate names.** The original's `std::sort` would leave the order of duplicates
  unspecified; `ragres check` reports them, and our archives have none.
- **Test vectors.** `tests/unit/grf_cipher_vectors.inc` holds the original's outputs,
  from `tools/grf_vectors.py`, which runs `DecryptBlocksMixed`, `DecryptFirstBlocks`
  and `HashedFileName::Set` in the emulation oracle.
