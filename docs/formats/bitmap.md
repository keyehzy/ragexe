# Bitmaps: CBitmapRes

`CBitmapRes` (RTTI) holds one 32-bit image. It is registered for `bmp`, `tga` and `jpg`
under `texture\` ([resources.md](resources.md#types)), and `CEmblemRes` keeps one at
`+0x110` for guild emblems ([ebm.md](ebm.md)).

## Members

| Offset | Field |
|-------:|-------|
| +0x110 | has alpha: 1 after a TGA, 0 after a BMP, a JPEG or `Create`; `TextureCache` passes it to `C3dDevice::CreateTexture` |
| +0x114 | width |
| +0x118 | height |
| +0x11c | pixels: width × height 32-bit values, top row first, each `A << 24 \| R << 16 \| G << 8 \| B` |

## Methods

| Method | Address | What it does |
|--------|---------|--------------|
| constructor | `004277d0` | `CRes`, then clears `+0x110` and `+0x11c` |
| `Load(path)`, slot 3 | `004277f0` | `DataFile::Open(path)`, then `LoadFromMemory(path, data, size)` |
| `LoadFromMemory(name, data, size)`, slot 1 | `004278a0` | dispatches on the name's extension: everything from its last `.`, compared with `lstrcmpiA` to `.bmp`, `.tga` and `.jpg`. Anything else shows "Unsupport fileformat!" (`00605f24`) and fails; a name with no `.` fails silently |
| `Reset`, slot 4 | `00427940` | frees the pixels and zeroes width, height and pointer |
| `Create(width, height)` | `00427b30` | allocates pixels filled with `0xffffffff` and clears the alpha flag. `CEmblemRes::Load` uses it |
| `GetPixel(x, y)` | `00427980` | the pixel, or `0x00ff0000` when out of range |
| `ApplyAlphaBitmap(name)` | `004279d0` | puts another bitmap into this one's alpha; nothing calls it |

**`GetPixel`'s range check is broken:** it tests `y >= 0 || y < height`, which is always
true. So `x` is checked but `y` is not, and a row past the image reads past the pixels.
`CSkinMgr::GetColorChipColor` calls it with in-range coordinates. In the port it checks
both coordinates.

## BMP

`LoadBmp` (`00427b90`, given data and size):

1. **Reset** through slot 4, then check that the first two bytes are `BM`; anything else
   fails.
2. **Read the headers** without checking any field against the data's size: the 14-byte
   file header and the 40 bytes after it (copied at `00427bd3`). The client uses only
   these fields:

   | Field | Offset |
   |-------|-------:|
   | `bfOffBits`, where the pixels start | 10 |
   | `biWidth` | 18 |
   | `biHeight` | 22 |
   | `biBitCount` | 28 |
   | `biClrUsed` | 46 |

   Compression and the header size are ignored.
3. **Bit count.** 24 and 8 are supported. Anything else shows "Only 16M or 8bit color
   format supported" (`00605f70`) and fails. That rejects the 3 four-bit BMPs in
   `data.grf`.
4. **Allocate** a width × height × 3 staging buffer and the pixel array.
5. **Fill the staging buffer.** Rows are read bottom-up, since the first stored row is
   the bottom of the image, and a negative height is not handled.
   - **8-bit:**
     - **Palette:** `biClrUsed` entries, or 256 when that is 0, copied from offset 54,
       always 14 + 40, into a 256-entry stack array. More than 256 overruns the stack.
       Entries past the count keep whatever the stack held.
     - **Rows:** each stored row takes `(width + 3) & ~3` bytes. It is copied to a
       4096-byte stack buffer first, so a width above 4093 overruns that buffer too.
     - **Pixels:** each byte selects a palette entry, whose bytes 0, 1 and 2 (B, G, R)
       are staged.
   - **24-bit:** each row's `width × 3` bytes are copied as they are (B, G, R).
     - **Row padding:** when `width % 4 != 0`, the source then advances by
       `width − ((width + 3) & ~3) + 4` bytes.
     - **Why that works:** the 8-bit stride appears in a 24-bit formula, but it gives
       the same padding as the usual `(4 − 3·width % 4) % 4`.
6. **Convert** each staged pixel `b, g, r` to `0xff000000 | r << 16 | g << 8 | b`.
7. **Finish:** free the staging buffer, clear the alpha flag, succeed.

In `data.grf`: 5,013 8-bit, 455 24-bit and 3 4-bit BMPs, all stored bottom-up and none
wider than 4093.

## TGA

`LoadTga` (`00427e30`):

1. **Check the header:** color map type (byte 1) must be 0 and image type (byte 2) must
   be 2 (raw) or 10 (run-length). Anything else fails, without resetting first.
2. **Read the size:** width is the 16-bit value at 12, height at 14. Pixel depth is byte
   16. The descriptor's origin bits are ignored: rows are always read bottom-up.
3. **Locate the pixels** at 18 + the ID length (byte 0).
4. **Read each pixel** as B, G, R, plus A when the depth is 32. Any other depth reads 3
   bytes, so 16-bit images decode as garbage. An alpha that is not read keeps its last
   value, which starts at `0xff`.
5. **Run-length packets** (type 10): a header byte `c` covers `(c & 0x7f) + 1` pixels,
   repeated when bit 7 is set, literal otherwise.
   - **A packet that runs past the end of a row** keeps writing: in memory that is the
     next row up, which was already written, since rows go bottom to top.
   - **On the first row,** it is past the end of the array.
   - **The next row** starts at its own beginning anyway.
6. **Finish:** set the alpha flag to 1 and succeed.

Nothing is bounds-checked against the data. In `data.grf`: 116 TGAs, all type 2, 32-bit,
bottom-up.

## JPEG

`LoadJpeg` (`004280d0`), through `ijl15.dll`:

1. **Read the header** (`ijlInit` #2; `ijlRead` #4 with `IJL_JBUFF_READPARAMS`) for
   width and height.
2. **Decode** with `IJL_JBUFF_READWHOLEIMAGE`: 3 channels, BGR. The DIB pad is set to the
   24-bit row padding, but the staging buffer is width × height × 3 with no padding, and
   is read back without padding.
   - **For widths that are not a multiple of 4,** that overruns the buffer and skews
     the rows.
   - **In `data.grf`:** all 144 JPEGs are baseline and multiples of 4 wide (12 use 2×2
     chroma subsampling).
3. **Convert** each pixel, `b, g, r`:
   - if `r ≥ 0xfe`, `g ≤ 3` and `b ≥ 0xfe` (magenta), it becomes `0x000a0a0a`: dark,
     with alpha 0;
   - otherwise `0xff000000 | r << 16 | g << 8 | b`.
4. **Finish:** clear the alpha flag.

When the header cannot be read, the code skips to `ijlFree`, deletes the caller's data
pointer and reports success.

## ApplyAlphaBitmap

`ApplyAlphaBitmap(name)` (`004279d0`):

1. Load a second `CBitmapRes` from `name`. Failure shows "Error reading alpha bitmap"
   (`00605f54`).
2. For every pixel (x, y) of this bitmap, take `m = mask.GetPixel(x, y)`. The pixel's
   alpha becomes the low byte of:

   ```
   s = ((m | 0xff00) >> 8) + ((m | 0xff0000) >> 16) + (m | 0xff)   // 32-bit, wrapping
   q = (int32_t)s / 3                                               // signed, toward zero
   ```

   Its colour stays (`00427a9a`–`00427adf`). It reads as an average of the mask's
   channels that ORs where it should mask.

## In the port

`src/res/bitmap.{h,cpp}`:
- `Bitmap`, the decoders `DecodeBmp`, `DecodeTga` and `DecodeJpeg`, and `BitmapRes`
  (registered by `RegisterBitmapTypes`).
- **BMP and TGA give the original's pixels exactly.** `tools/bitmap_oracle.py` runs the
  original decoders in the emulation oracle over every image in `data.grf` and compares
  success, size, alpha flag and every pixel:
  - all 5,471 BMPs (214,703,692 pixels) and 116 TGAs are identical;
  - the 3 four-bit BMPs are refused by both, with the same message.
- **JPEG is decoded with stb_image** instead of IJL. The oracle compares it with an
  independent decoder, Pillow, with the magenta key applied to both:
  - all 144 JPEGs are within 3 levels per channel, where the tolerance is 4;
  - no pixel keys differently.
  - The key is applied to stb_image's values, so a pixel near the threshold could in
    principle key differently from the original.
- **In `ctest`:** when the data is present, `bitmap_oracle_sample` runs the oracle on 30
  images of each kind.

Where the original reads or writes out of bounds, the port stays in bounds:
- **BMP and TGA data** that ends early fails.
- **A palette of more than 256 entries** fails.
- **Palette entries past the count** are 0.
- **A run-length packet that runs past a row** writes where the original would, while
  that is inside the image, and drops what falls outside.
- **JPEG rows** are decoded without the skew.
- **`GetPixel`** checks `y`.
- **A JPEG whose header cannot be read** fails.
