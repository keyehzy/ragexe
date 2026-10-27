# Palettes: CPaletteRes

`CPaletteRes` (RTTI, 0x510 bytes) is a 256-colour palette, registered for `pal` under
`palette\` ([resources.md](resources.md#types)). Sprites are drawn through palettes, and
the alternative palettes swap a sprite's colours.

## File

A `.pal` file is 256 entries of 4 bytes: red, green, blue, and a fourth byte the client
ignores. Only the first 1024 bytes are read. All 502 `.pal` files in `data.grf` are
exactly 1024 bytes, in two folders under `data\palette\`.

## Load

`CPaletteRes::Load` (slot 3, `004320e0`):

1. `DataFile::Open(path)`.
   - **If that fails,** the client shows "CPaletteRes :: Cannot find File : " (`006063f4`)
     followed by the path, and returns false.
   - Because `GetResource` then tries the `ResNameTable` name and finally calls
     `OnLoadError`, a missing palette shows two or three message boxes.
2. `DataFile::Read` of 1024 bytes into a stack buffer. Its result is ignored: a shorter
   file leaves the buffer as the stack held it.
3. Convert the 256 entries into the palette at `+0x110` (below), then succeed.

`Reset` (slot 4, `004320d0`) does nothing; the palette lives in the object.

## Colour conversion

`C3dDevice::ConvertColorsToArgb1555(dst, src, count)` (`00405440`) is called on 256
entries. `CSprRes` uses it too, for the palettes inside sprite files. Each source entry
`r, g, b, x` becomes one 32-bit value:

- **Magenta,** `r ≥ 0xfe`, `g ≤ 3` and `b ≥ 0xfe`, becomes the value at `006053fc`.
  That value is `0x0421`, and nothing writes it: 1 in each 5-bit channel, with the alpha
  bit clear. The JPEG key, `0x000a0a0a` ([bitmap.md](bitmap.md#jpeg)), is the same
  colour in 32 bits.
- **Every other colour** becomes `0x8000 | (r & 0xf8) << 7 | (g & 0xf8) << 2 | b >> 3`:
  ARGB1555 with the alpha bit set.

The device itself is not consulted (`ECX` is `g_C3dDevice` but unused), so the result
does not depend on the display mode.

## In the port

`src/res/palette.{h,cpp}`:
- `ConvertColorsToArgb1555`, and `PaletteRes`, registered by `RegisterPaletteTypes`.
- **Short files:** where the original would convert stack garbage for a file under
  1024 bytes, the port fails.
- **The oracle:** `tools/palette_oracle.py` runs the original conversion in the
  emulation oracle on every `.pal` in `data.grf`, plus every byte pattern a colour can
  take near the key, and compares each value.
