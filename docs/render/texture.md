# Textures and surfaces

`CSurface` (RTTI) wraps a DirectDraw surface; `CTexture` and `CSurfaceWallpaper` derive
from it. The device that creates them is in [device.md](device.md).

## CSurface

| Offset | Field |
|-------:|-------|
| +0 | vtable (`005e26dc`) |
| +4, +8 | width and height |
| +0xc | the `IDirectDrawSurface7` |

**Virtual functions:**

| Slot | Function |
|-----:|----------|
| 0 | scalar deleting destructor |
| 1 | `UpdatePixels(x, y, width, height, pixels, flag, stride)` |
| 2 | `FillColor(rect, colorref)` |
| 3, 4 | drawing through `SceneRenderer` (`0040e610`, `0040e6a0`) |

**Construction.**
- `CSurface(width, height)` (`00416080`) creates the surface (`00416420`):
  - each side rounded up to a multiple of 4;
  - an off-screen plain surface in the primary's pixel format;
  - a source colour key of red | blue, which is magenta.
- `CSurface(width, height, surface)` (`004160e0`) wraps an existing surface.
- The destructor releases the surface.

### UpdatePixels

`CSurface::UpdatePixels` (`00416130`) writes 32-bit ARGB pixels into the surface at (x, y).
- **No sampling.** Rows are stride pixels apart; a stride of 0 means the width.
- **The lock** is `Lock(rect, DDLOCK_WAIT | DDLOCK_WRITEONLY)`.

**16-bit surfaces.** Each channel's byte is shifted right by the device's loss and left
by its shift ([device.md](device.md#pixel-format-channels)), and the three are added.
A result equal to magenta in that packing is not written.

**32-bit surfaces.** A pixel whose low 24 bits are `0xff00ff` is not written, whatever
its alpha. Every other pixel is copied.

Magenta pixels therefore keep whatever the surface held there. With the colour key, they
show what is behind.

**`CSurfaceWallpaper`** (`004164e0`) is a `CSurface` of the screen's size. Its
`UpdatePixels` (`00416550`) writes the pixels into a temporary `CSurface` of the image's
size, then blits that over its own surface.

### FillColor

`FillColor` (`00416330`, and `00416750` in `CTexture`) blits `DDBLT_COLORFILL |
DDBLT_WAIT` into a rectangle.
- **The argument is a `COLORREF`** (`0x00bbggrr`), packed into the screen's 16-bit
  format: red from the low byte, blue from the third, each with the device's shifts.
- **It always uses the screen's format,** and keeps the low 16 bits, even for a 32-bit
  surface or a texture of another format.

## CTexture

`CTexture` is `operator new(0x128)`:

| Offset | Field |
|-------:|-------|
| +4, +8 | the texture's width and height, after `FitTextureSize` |
| +0xc | the surface |
| +0x10 | the pixel format, below |
| +0x14 | black is transparent, from the last `UpdatePixels` |
| +0x18, +0x1c | the last image's size as texture coordinates (`u`, `v`) |
| +0x20 | a name, `WATERBUMPMAP` for the bump maps |
| +0x120 | references (`IncrementUseCount` `004167e0` also stamps +0x124 with `timeGetTime`) |
| +0x124 | the last use, which `TextureCache::PurgeUnused` compares |

### Pixel formats

`EnumTextureFormatCallback` (`004171f0`) takes the device's first texture format that
matches each of the client's formats. It fills the table at `00625fe8`, 32 bytes an
entry.

| Format | Needs | Use |
|-------:|-------|-----|
| 0 | alpha, 16 bits, alpha mask `0x8000` | ARGB 1555 |
| 1 | alpha, 16 bits, alpha mask `0xf000` | ARGB 4444 |
| 2 | no alpha, 16 bits, red mask `0xf800` | RGB 565 |
| 4 | alpha, 32 bits, alpha mask `0xff000000` | ARGB 8888 |
| 5 | | the water's bump maps, below |

**Errors.**
- **Any other format** shows "Create Texture::Unsupported pixelformat" (`0060573c`).
- **A format the device lacks** shows a second message (`00605710`), and the surface is
  created anyway. The message is in Korean (CP949) and starts with "GFC".

### CreateSurface

`CTexture::CreateSurface` (`00417070`) takes (width, height, format).
1. Store them.
2. Fit the size with `FitTextureSize` ([device.md](device.md#textures)).
3. Release the old surface.
4. Create a texture surface:

| Field | Value |
|-------|-------|
| caps | `DDSCAPS_TEXTURE`, with `DDSCAPS2_TEXTUREMANAGE` so Direct3D manages it |
| flags | `DDSD_TEXTURESTAGE` (stage 0) |
| pixel format | the table's |
| mipmaps | `SelectMipmapFlags`' description flags and caps |
| mipmap count | 1, plus one for each power of two from 2 that is below both the width and the height |
| 565 only | a source colour key of red \| blue, which is magenta |

The mipmap count only takes effect when `TRILINEARFILTER` adds `DDSD_MIPMAPCOUNT`.

**Constructors.**
- **`CTexture(width, height, format)`** (`004165e0`) calls `CreateSurface`, or makes a
  procedural bump map for format 5.
- **`CTexture(width, height, format, surface)`** (`004166b0`) wraps a surface, or derives
  a bump map from it for format 5.

### UpdatePixels

`CTexture::UpdatePixels` (vf01, `00416990`) takes (x, y, width, height, pixels, black is
transparent, stride).

1. **The texture coordinates** of the image (`00416810`, into +0x18 and +0x1c):
   - find the smallest whole `k` with `width / k` within the texture's width;
   - `u` is `(width / k) / texture width`, in single precision; `v` likewise.
2. **Store the flag** in +0x14.
3. **Fit the image to the surface.** Take the surface's own width and height from
   `GetSurfaceDesc`.
   - `kx` and `ky` are the smallest whole factors that bring the image's width and
     height within them (signed division).
   - The image is written at `(x / kx, y / ky)`, as `width / kx` by `height / ky`
     texels.
   - **Sampling:** each texel takes source pixel `(c · kx, r · ky)`, with no filtering.
   - An empty result writes nothing.
4. **`Lock`** the rectangle with `DDLOCK_WAIT | DDLOCK_WRITEONLY`, and convert each pixel:

   | Format | Texel | Becomes 0 | Not written |
   |-------:|-------|-----------|-------------|
   | 0 | `a>>7 · 0x8000 \| r>>3 · 0x400 \| g>>3 · 0x20 \| b>>3` | opaque magenta, `0xfc1f`; with the flag, black (any alpha) | |
   | 1 | `a>>4 · 0x1000 \| r>>4 · 0x100 \| g>>4 · 0x10 \| b>>4` | magenta (`0xf0f`, any alpha); with the flag, black | |
   | 2 | `r>>3 · 0x800 \| g>>2 · 0x20 \| b>>3` | | with the flag, magenta |
   | 4 | the pixel | | |

   - **Format 0:** magenta with its alpha below `0x80` stays `0x7c1f`.
   - **Format 2:** without the flag, magenta is written. The surface's colour key hides
     it either way.
   - **Formats 3 and 5** write nothing.
5. **`Unlock`,** then build the mipmaps (`004168a0`).

**The mipmaps.** For each level attached to the surface:
1. Halve the rectangle.
2. If it is not empty, `Blt` it from the surface: `DDBLT_ASYNC`, and if that fails,
   `DDBLT_WAIT`.

So each level comes from the top one, scaled by the driver.

**`CalibrateTexelOffset`** calls `UpdatePixels` for its checker
([device.md](device.md#calibratetexeloffset)). The other callers reach it through the
vtable.

### DrawRleImage

`CTexture::DrawRleImage` (`00416d80`) takes (x, y, width, height, image, palette). It
draws a sprite's run-length image ([sprite.md](../formats/sprite.md)) into a 16-bit
texture.

**The image:** width at +0, height at +2, and the data at +0xc. The palette has 4-byte
entries; `CSprRes` keeps its palette that way, converted to ARGB 1555.

1. **`Lock`** the whole surface.
2. **For each row of the image,** starting at (x, y + row):
   - **A byte of 0** writes a 0. The next byte, minus 1, is how many more 0s follow. The
     run carries over into the next row.
   - **Any other byte** writes the low 16 bits of its palette entry.
   - Then the rest of the area's width is zeroed.
3. **Rows below the image,** down to the area's height, are zeroed.
4. **`Unlock`,** then build the mipmaps of the area.

**Edge cases:**
- **A run of length 0** counts down from −1 and never reaches 0 again, so every later
  texel of the image is 0.
- **An area narrower than the image** makes the zeroing count negative, and it then
  writes far past the row.
- **Unchecked:** nothing checks the area against the texture or the data's end.

`SceneRenderer::method_0040e130` is the one caller.

### Drawing

`CTexture`'s slots 3 and 4 (`00416f10`, `00416fc0`) draw the texture as a screen
rectangle (x, y, width, height) through `SceneRenderer::method_00410ab0`. The texel
offsets from the calibration go to both corners.
- **Slot 3** maps the rectangle 1:1 to texels:
  - `u` runs from `offset_u / texture width` to `(width + offset_u) / texture width`;
  - `v` likewise.

  It passes the colour argument.
- **Slot 4** maps the whole image:
  - `u` runs from `offset_u / texture width` to that plus `u scale` (+0x18);
  - `v` likewise.

  The colour is `0xffffffff`.

### CopyRectFrom and the bump maps

**`CopyRectFrom`** (`00417940`) blits a rectangle of another `CSurface` into this one with
`DDBLT_WAIT`, then builds the mipmaps. `CGraffiSkill::vf41` calls it.

**The bump maps** are format 5, with 16 bits a texel: two signed bytes, `du` and `dv`
(`DDPF_BUMPDUDV`).
- **Without `D3DTEXOPCAPS_BUMPENVMAP`** or **`BUMPENVMAPLUMINANCE`,** the client shows
  "notsuppot" boxes, then goes on.
- **The procedural map** (`004172f0`) fills each row with the same value: sums of three
  cosines and sines of the row's offset from the middle (0.5, `005e25b8`).
- **The derived map** (`00417640`) takes horizontal and vertical byte differences of a
  32-bit surface.

They belong with the water, which is ported later.

## In the port

`src/render/texture.{h,cpp}`:

| Port | Original |
|------|----------|
| `TexelImage` | the texels of a texture or surface |
| `ConvertTexel` | `UpdatePixels`' conversion of one pixel |
| `ImageUvScale` | `00416810` |
| `UpdateTexturePixels` | `CTexture::UpdatePixels`, without the mipmaps |
| `DrawRleImage` | `CTexture::DrawRleImage` |
| `UpdateSurfacePixels` | `CSurface::UpdatePixels` |
| `FillValue` | `FillColor`'s value |
| `SurfaceSide` | a `CSurface`'s size |
| `MipmapCount` | `CreateSurface`'s mipmap count |

The mipmaps are left to the renderer.

**Where the port differs:**
- **`UpdateTexturePixels` and `UpdateSurfacePixels`** fail on an area outside the
  target, where DirectDraw refuses the lock.
- **`DrawRleImage`** fails where the original writes outside the area or the texture,
  or reads past the data.

**The vectors.** `tools/texture_vectors.py` runs the original in the emulation oracle
and writes `tests/unit/texture_vectors.inc`. `tests/unit/texture_test.cpp` compares the
port with every texel:
- **`UpdatePixels`,** 40 cases:
  - each format, with and without the flag;
  - images that fit, need sampling down, sit at an offset, or have a wider stride;
  - and the uv scale of each.
- **`DrawRleImage`,** 8 cases: runs across rows, a zero-length run, areas larger than
  the image.
- **`CSurface::UpdatePixels`** on 565, 555 and X888 screens.
- **`FillColor`'s value** for eight colours on each screen.

The emulator's surfaces start filled with `0xa5`, so a texel the original leaves alone
shows. The fake `Lock` returns the rectangle's top-left, as DirectDraw does.
