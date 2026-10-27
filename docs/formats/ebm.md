# Emblems: CEmblemRes

`CEmblemRes` (RTTI, 0x230 bytes) is a guild emblem: a BMP, zlib-compressed. It is
registered for `ebm` under `..\_tmpEmblem\` ([resources.md](resources.md#types)), so
`x.ebm` is read from `data\..\_tmpEmblem\x.ebm`: `_tmpEmblem` beside `data`.

## Members

| Offset | Field |
|-------:|-------|
| +0x110 | a `CBitmapRes` ([bitmap.md](bitmap.md)): its width is at +0x224, its height at +0x228 |

| Slot | Method | Address |
|-----:|--------|---------|
| 2 | `Clone`: `operator new(0x230)` and the constructor `00428360` | `004283d0` |
| 3 | `Load` | `00428500` |
| 4 | `Reset`: slot 4 of the bitmap | `00428670` |
| 5 | an empty override of `CRes::OnLoadError` | `00428430` |

## Where the files come from

`data.grf` holds no `.ebm` files: the client writes them from what the server sends.

**`SaveEmblemFile`** (`0059c800`) takes (data, size, guild, version):
1. It makes `%s\_tmpEmblem\` (`0061d958`) under the current directory.
2. It deletes the files that match `%s_%d_*.ebm` (`0061d94c`), with a string of
   `ClientState` (`+0x4e4`) and the guild.
3. It writes the data, as it is, to `%s%s_%d_%d.ebm` (`0061d93c`), adding the version.

**`GetEmblemFileName`** (`0059c9f0`) takes (out, guild, version) and gives the name to
load:
- **version above 0:** `%s_%d_%d.ebm` (`0061d994`);
- **otherwise:** `..\data\texture\effect\flagGraffiti%d.ebm` (`0061d968`). No such file
  is in `data.grf`.

## File

A zlib stream ([RFC 1950](https://www.rfc-editor.org/rfc/rfc1950)) of a BMP. Nothing else
is checked.

## Load

`CEmblemRes::Load` (slot 3, `00428500`) takes the path.

1. **Open it** with `DataFile::Open`. If it does not open, fail.
2. **Read the whole file** into an 8 KB stack buffer (`ebp-0x20a0`). The size is not
   checked: a larger file runs over the buffer into the `DataFile` and the frame.
3. **Inflate it** into another 8 KB stack buffer (`ebp-0x40a0`), with the length
   (`ebp-0x10`) set to 0x2000 first. `Uncompress` (`00428680`) calls `cps.dll`'s
   `uncompress` and ignores its result.
   - **An exception inside it:** the handler (`004286f5`) logs "Failed - Emblem
     Uncompress!" (`00605fd8`), and the load fails.
   - **Otherwise:** it logs "Emblem Uncomp Success!" (`00605ff4`).
4. **A length still at 0x2000** (`004285dd`) is taken as no image. That covers both an
   error, which leaves the length alone, and an image that fills the buffer exactly.
5. **Otherwise decode the bytes:** slot 1 of the bitmap, `LoadFromMemory`, is called with
   the name `_emblem.bmp` (`00605fcc`), so it is always the BMP decoder.
   - **On success,** the width and height are each clamped to at most 24 (`00428604`).
     The pixels stay as decoded, so a wider image reads with the wrong stride.
6. **No image, or a BMP that fails:** `CBitmapRes::Create(24, 24)`, all white
   (`0xffffffff`).
7. **Succeed.**

The BMP decoder checks nothing against the length, so it reads whatever the 8 KB buffer
holds past it ([bitmap.md](bitmap.md#bmp)).

### cps.dll's inflate

`cps.dll` is zlib 1.0.4, by its copyright strings. Its exports are `compress` (`10001130`)
and `uncompress` (`10006a90`). `uncompress` updates the length only when the stream ends
properly. What it accepts:

- **The header:**
  - the method must be 8;
  - the window must be at most 32 KB;
  - the check bits must hold;
  - a preset dictionary is refused.
- **The Adler-32** after the data is checked. Bytes after it are ignored.
- **A distance that reaches before the start of the output** copies zeros. 1.0.4
  inflates through a 32 KB window that it allocates zeroed, and it never checks a
  distance against what it has written. Current zlib refuses this ("invalid distance too
  far back").
- **Length symbols 286 and 287 in a fixed-code block:** the fixed code has them, but no
  length is defined for them, and 1.0.4 takes them as a copy of length 0.
  - **On its fast path,** the copy loop counts down past zero (`10003b29`) and copies
    without end. On Windows it runs off cps.dll's heap into an access violation, which
    `Uncompress`'s handler catches, so the load fails.
  - **Where the fast path applies:** within the last 80 bits of input from the symbol,
    1.0.4 decodes symbol by symbol and reports a clean error. Past 91 bits it always runs
    away. In between, it depends on when its fast loop was entered.
- **Some invalid code-length codes** that current zlib refuses ("invalid code lengths
  set"): 1.0.4 builds tables from them, and then reads far out of bounds (`10004600`).

## In the port

`src/res/ebm.{h,cpp}`: `DecodeEmblem` (to a `Bitmap`), `InflateEmblem`, and `EmblemRes`,
registered by `RegisterEmblemTypes`.

**`InflateEmblem`** stands in for `uncompress`:
1. It checks the zlib header as 1.0.4 does.
2. It inflates the raw deflate data with current zlib, over a zeroed 32 KB preset
   dictionary, so that distances before the start copy zeros.
3. It checks the Adler-32 itself.
4. **A fixed-code length symbol 286 or 287** (current zlib's "invalid literal/length
   code" in a block whose header says fixed) is reported as running away, and
   `DecodeEmblem` then fails with the client's message.

**Where the port differs:**
- **A file larger than 8 KB fails.**
- **What the original builds from bytes nobody set** is not reproduced:
  - **A BMP that needs more bytes than were inflated** gives the blank emblem.
  - **Palette entries past the count** are zero. The original reads whatever its stack
    held.
  - **Sizes the bitmap port refuses** give the blank emblem: negative, or more than 2²⁶
    pixels.
- **A fixed-code length symbol 286 or 287 near the end of the input** fails in the port.
  The client reports a clean error there and shows the blank emblem.
- **Code-length codes that 1.0.4 builds tables from and current zlib refuses** give the
  blank emblem. Where 1.0.4 then faults, the client's load fails.

**The oracle:** `tools/ebm_oracle.py` runs the original `Load` in the emulation oracle
with the original `cps.dll` mapped in (`Emulator.load_cps`).
- **The inputs:** there are no real emblems, so:
  - 31 synthetic files: 8- and 24-bit BMPs, wider and taller ones, broken and refused
    headers and checksums, BMPs the bitmap refuses, hand-built streams with a distance
    before the start and with a symbol 286, inflated lengths of 8191, 8192 and more,
    and an 8 KB file;
  - 400 seeded mutations of valid streams. Where current zlib still inflates one, its
    Adler-32 is redone, so that only the inflating can differ.
- **What it compares,** with `ragres decode-ebms`:
  - the bitmap: its size, its pixel count and every pixel;
  - separately, the bytes inflated and the length passed on.
- **Where the original reads bytes nobody set,** only the inflated bytes are compared.
- **When cps.dll faults, or runs past 10 million instructions,** the oracle counts the
  load as failed, as `Uncompress`'s handler makes it on Windows. The port must not decode
  an image there.
- **Results:** at 3,000 mutations, 41 such runs; the port fails 40 of them and gives the
  blank emblem for 1, an invalid code-length code. Every other result matches.
