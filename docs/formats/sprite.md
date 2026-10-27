# Sprites: CSprRes

`CSprRes` (RTTI, 0x534 bytes) holds the images of one `.spr` file: 8-bit images drawn
through a palette, and 32-bit images. It is registered for `spr` under `sprite\`
([resources.md](resources.md#types)). Actors are drawn from sprites, and their `.act`
files say which images make up each frame.

## Members

| Offset | Field |
|-------:|-------|
| +0x110 | the file's palette: 256 ARGB1555 values, as [palette.md](palette.md#colour-conversion) converts them |
| +0x510 | `vector<SpriteImage*>`: the 8-bit images |
| +0x520 | `vector<SpriteImage*>`: the 32-bit images |
| +0x530 | the number of 8-bit images, from the header |

An image record (operator new 0x10) holds:

| Offset | Field |
|-------:|-------|
| +0 | width (16-bit) |
| +2 | height (16-bit) |
| +4 | "width halved" flag (16-bit) |
| +6 | "height halved" flag (16-bit) |
| +8 | a `CTexture*`, for 32-bit images |
| +0xc | the run-length data, for 8-bit images |

The client keeps 8-bit images run-length encoded and decodes them when it draws.

## File

All numbers are little-endian. **Header:**

| Offset | Size | Field |
|-------:|-----:|-------|
| 0 | 2 | `SP` |
| 2 | 2 | version: minor byte, then major (0x0201 is 2.1) |
| 4 | 2 | number of 8-bit images |
| 6 | 2 | number of 32-bit images, only when the version is 0x200 or later |

**The 8-bit images**, in order, each starting with a 2-byte width and a 2-byte height:
- **Versions below 0x201:** `width × height` palette indices, top row first.
- **Version 0x201:** a 2-byte size, then that many bytes of
  [run-length data](#run-length-encoding).

**The 32-bit images** (version 0x200 and later), each:
- a 2-byte width and a 2-byte height, then `width × height` pixels of 4 bytes;
- the bytes of a pixel are alpha, blue, green, red;
- rows are stored bottom row first.

**The palette** (versions above 0x100) is the file's last 1024 bytes, in the `.pal` layout.

In `data.grf`, 2,054 sprites are version 0x201 and 32 are 0x200. Together they hold 92
32-bit images, and every file ends exactly at its palette.

## Load

`CSprRes::Load` (slot 3, `004341d0`). `DataFile::Read` results are never checked, so a
truncated file leaves whatever the buffers held.

1. **Open.** If `DataFile::Open(path)` fails, show "Spr :: Cannot find File : "
   (`006064e4`) followed by the path, and fail.
2. **Header.** Read the first 6 bytes.
   - **Not `SP`:** show "Illegal File Format" (`006064d0`), then a second box with the
     path, and fail.
   - **Version above 0x201:** show "Unsupported Version. Content may be corrupted"
     (`00605bf0`) and fail.
3. **Image count.** Store the count of 8-bit images at `+0x530`.
4. **Palette** (version above 0x100): seek to 1024 bytes before the end, read them,
   seek back to offset 6, and convert the palette into `+0x110` with
   `C3dDevice::ConvertColorsToArgb1555`.
5. **32-bit count** (version 0x200 and later): read it now, the 2 bytes at offset 6.
6. **Each 8-bit image:** read width and height into a new record, then:
   - **Not shrinking** (both shrink globals 0):
     - below 0x201: read the raw indices and keep `EncodeRle` of them;
     - 0x201: read the size and keep the data as it is.
   - **Shrinking** (see [Detail level](#detail-level)):
     - get the raw indices: read them, or `DecodeRle` the stored data;
     - `ShrinkImage`, then keep `EncodeRle` of the result;
     - halve the record's width and/or height (signed division) and set the halved flags.
7. **Each 32-bit image:** read width and height, then the pixels into an ARGB buffer,
   filling it from its last row up.
   - Create a `CTexture`, halved in each direction that is shrinking, and upload the full
     buffer to it (slot 1, at 0,0).
   - Free the buffer, then halve the record and set its flags as for 8-bit images.
8. **Done:** succeed.

`Reset` (slot 4, `00434110`) frees each 8-bit image's data and record, destroys each
32-bit image's texture and frees its record, and empties both vectors. `Load` does not
reset first.

### Detail level

`00688a7c` (halve the width) and `00688a80` (halve the height) are set by
`LoadVideoSettings` (`005b1910`), the detail-level window (`UIDetailLevelWnd::vf37`) and
`ClientState::PreloadSpriteResources`.

### Run-length encoding

**`EncodeRle(pixels, width, height, &size)`** (`00434cb0`) walks the pixels in order:
- **A zero** starts a run: emit `0`, and the count is 1. Further zeros raise the count;
  when it passes 254, emit it (255) and end the run.
- **A non-zero pixel** emits the pending count (when in a run), then the pixel.
- **At the end,** emit the count when it is not 0.

Two quirks come from 16-bit arithmetic:
- **The pixel index is masked to 16 bits** (`local_14 & 0xffff`), so an image of more
  than 65,536 pixels re-reads from its start.
- **The size is 16 bits:** longer output is cut to its low 16 bits' worth.

No sprite in `data.grf` has a raw image that large; the largest has 46,492 pixels.

**`DecodeRle(data, width, height)`** (`00434e50`) gives `width × height` indices:
- **While a count is pending,** emit 0 and lower it.
- **Otherwise read a byte.** A non-zero byte is emitted as it is. A `0` is followed by a
  count byte:
  - **count not 0:** emit 0 and keep `count − 1` pending;
  - **count 0:** emit the 0 alone.
- **Runs continue across rows.** Nothing checks the end of the data. All 0x201 sprites in
  `data.grf` decode exactly to their size, with no `0 0` pairs.

**`ShrinkImage(pixels, width, height, halve_x, halve_y)`** (`00434ed0`) keeps every
second column and/or row:
- the result is `width / 2` (when halving x) by `height / 2` (when halving y);
- pixel (x, y) is the source at `(halve_y + 1)·y·width + (halve_x + 1)·x`.

## In the port

`src/res/sprite.{h,cpp}`:
- **What it keeps:** `Sprite`, with 8-bit images held as the client holds them (run-length
  data and the halved flags) and 32-bit images as top-down ARGB pixels at their stored
  size. Also the codec functions, and `SprRes`, registered by `RegisterSpriteTypes` with
  the detail level its loads use.
- **Encoder and decoder:** `EncodeRle` keeps the 16-bit quirks; `DecodeRle` gives 0 for
  data that runs out instead of reading past it.
- **What fails in the port:**
  - truncated files, where the original would read past the end;
  - negative sizes;
  - run-length images claiming more pixels than their data can cover (128 per byte),
    which the original would decode from past its buffer.
- **The oracle:** `tools/sprite_oracle.py` runs the original `Load` in the emulation
  oracle, behind a stand-in archive (`Emulator.serve_data_files`) and with `CTexture`
  replaced to capture the 32-bit pixels. It compares every sprite in `data.grf`, at each
  detail level:
  - the palette;
  - each 8-bit image's record and its pixels, decoded by the original `DecodeRle`;
  - each 32-bit image's record and pixels.
