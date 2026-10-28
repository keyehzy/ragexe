# The Direct3D 7 device

`C3dDevice` (the global `g_C3dDevice`, `006239d0`) owns DirectDraw 7, the Direct3D 7
device and the surfaces the client draws into. `InitAudioAndDevice` (`005b1d20`)
creates it at start-up. `CreateRenderer` then hands the device to the `SceneRenderer`,
which does all the drawing ([texture-stages.md](texture-stages.md)).

All values are the DirectX 7 SDK's: `DDSCL_`, `DDSCAPS_`, `D3DRENDERSTATE_`, `D3DTSS_`
and the rest. Interface methods are named by their slot in the SDK's vtable order.

## Fields

| Offset | Field |
|-------:|-------|
| +0 | the window |
| +4, +8 | width and height of the render target |
| +0xc | the window's client area on screen, the `Blt` destination (`RECT`) |
| +0x1c | the source rectangle of the `Blt` to the screen (`RECT`) |
| +0x2c | the primary surface |
| +0x30 | the back buffer, or null when the 3D device draws to the primary |
| +0x34 | the render target, +0x30 or +0x2c, with a reference of its own |
| +0x38 | the z-buffer |
| +0x3c | `IDirect3DDevice7` |
| +0x40 | `IDirectDraw7` |
| +0x44 | `IDirect3D7` |
| +0x48 | the device's `D3DDEVICEDESC7`, from `GetCaps` |
| +0x134 | the z-buffer's memory: `DDSCAPS_VIDEOMEMORY` or `DDSCAPS_SYSTEMMEMORY` |
| +0x138 | the z-buffer's `DDPIXELFORMAT` |
| +0x158 | the primary surface's `DDSURFACEDESC2` |
| +0x1d4 | full screen |
| +0x1d8 | when set, a full-screen `Present` blits instead of flipping |
| +0x1dc | the desktop's bits per pixel |
| +0x1e0 | a system-memory copy of the render target |
| +0x1e4 | point filtering, read by `SceneRenderer` (`00410620`); `InitAudioAndDevice` sets it to 1 after `Init` |
| +0x1f0 to +0x20c | shift and loss of red, green, blue and alpha (below) |
| +0x210 | the primary's bits per pixel |
| +0x214 to +0x220 | its red, green, blue and alpha masks |
| +0x224 to +0x234 | texture limits from the caps: minimum and maximum width and height, maximum aspect ratio |
| +0x238 | `DDCAPS_BLTSTRETCH` of the DirectDraw driver |
| +0x239 | the maximum texture size is at least 1024 × 1024 |

## Init

`C3dDevice::Init` (`00404410`) takes (window, driver GUID, device GUID, display mode,
flags).

**`InitAudioAndDevice`'s arguments:**
- **GUIDs:** the registry's `GUIDDRIVER` and `GUIDDEVICE`.
- **Display mode:** a `DDSURFACEDESC2` with `WIDTH`, `HEIGHT` and `BITPERPIXEL`.
- **Flags:** 6, or 7 when `ISFULLSCREENMODE` is set.

| Flag | Meaning |
|-----:|---------|
| 1 | full screen |
| 2 | draw into a back buffer: the flip chain's, or an off-screen surface in a window |
| 8 | a z-buffer with stencil |
| 0x10 | leave out `DDSCL_FPUSETUP` |

Flag 4 is always passed but nothing reads it. Flags 8 and 0x10 are never passed.

**Preconditions.** A null window or device GUID, or full screen without a display mode,
returns `E_INVALIDARG`.

**Steps.**
1. Set +0x1d4 from flag 1.
2. Run `CreateObjects`, below.
3. **If it fails,** call `Release`. An `E_FAIL` becomes `0x82000000`.
4. **If a device exists,** set the states below.

### CreateObjects

`00404760`:
1. **Choose the z-buffer's memory:**
   - video memory for the HAL (`005ec118`) and T&L HAL (`005ec0d8`) devices;
   - system memory for any other.
2. `CreateDirectDraw`.
3. `CreateSurfaces`.
4. With a device GUID, `CreateD3DDevice`, then `CreateZBuffer`.

The first failure returns its error.

#### CreateDirectDraw

`00404810`:
1. `DirectDrawCreateEx` with the driver GUID and `IID_IDirectDraw7` (`005ec478`).
2. `GetDisplayMode` for the desktop's bit count (+0x1dc).
3. `SetCooperativeLevel`:

   | Mode | Flags |
   |------|-------|
   | full screen | `FULLSCREEN \| ALLOWREBOOT \| EXCLUSIVE` |
   | windowed | `NORMAL` |

   Both add `MULTITHREADED`, and `FPUSETUP` unless flag 0x10 is given. So the client
   always runs with `FPUSETUP`, and Direct3D leaves the x87 at single precision.

#### CreateSurfaces

`004049e0`.

**Full screen:**
1. Set the window's style to `WS_POPUP | WS_CLIPCHILDREN`.
2. Make the screen rectangle the mode's size.
3. `SetDisplayMode(width, height, bits, refresh, flags)`. The flags are
   `DDSDM_STANDARDVGAMODE` for 320 × 200 × 8, 0 otherwise.
4. Create the primary:
   - **With flag 2:** `PRIMARYSURFACE | 3DDEVICE | FLIP | COMPLEX` with one back
     buffer, taken with `GetAttachedSurface(BACKBUFFER)`.
   - **Without:** `PRIMARYSURFACE | 3DDEVICE`.

**Windowed:**
1. Take the window's client area and its screen position.
2. Create the primary, `PRIMARYSURFACE`, and give it a clipper on the window.
3. **With flag 2,** draw into an off-screen surface of the client area's size,
   `OFFSCREENPLAIN | 3DDEVICE`.
4. **Without it,** the primary also gets `3DDEVICE` and the client draws straight into
   the window's rectangle.

**Then, in both modes:**
1. `GetSurfaceDesc` of the primary into +0x158.
2. Set the masks, the bit count and the channels (below).
3. The render target (+0x34) is the back buffer, or the primary without flag 2, with an
   `AddRef`.
4. **The copy** (+0x1e0): an `OFFSCREENPLAIN | SYSTEMMEMORY` surface of the target's size.
   `CopyRenderTargetToBackup` (`00405e80`) and `CopyBackupToRenderTarget` (`00405ed0`)
   blit between the two around a fade. Each tries `DDBLT_ASYNC` first, then `DDBLT_WAIT`.

#### CreateD3DDevice and CreateZBuffer

**`CreateD3DDevice`** (`004048c0`):
1. `QueryInterface` for `IID_IDirect3D7` (`005ec148`).
2. `CreateDevice` with the device GUID, on the render target.
3. `SetViewport`: the whole target, z from 0 to 1.
4. **Find the z-buffer format** with `EnumZBufferFormats`.
   - `EnumZBufferFormatCallback` (`004049a0`) copies each format whose flags are
     `DDPF_ZBUFFER`, or `DDPF_ZBUFFER | DDPF_STENCILBUFFER` with flag 8.
   - It stops at the first 16-bit one, so a 16-bit z-buffer wins when there is one.
     Otherwise the last match is kept.
   - **No match** fails.

**`CreateZBuffer`** (`00404e70`):
1. `GetCaps` into +0x48.
2. **A device with `D3DPRASTERCAPS_ZBUFFERLESSHSR`** gets no z-buffer.
3. **Otherwise:**
   1. Create a `ZBUFFER` surface of the target's size, in the memory chosen above.
   2. Attach it to the target.
   3. Call `SetRenderTarget` again.

The emulation oracle confirms both modes' sequences:

| | Windowed (flags 6) | Full screen (flags 7) |
|---|---|---|
| cooperative level | `0xc08` | `0xc13` |
| primary | `0x200` | `0x2218`, one back buffer |
| target | off-screen `0x2040` | the back buffer |
| copy | `0x840` | `0x840` |
| z-buffer | `0x24000`, the 16-bit format | the same |

### The states Init sets

Render states:

| State | Value |
|-------|-------|
| `CULLMODE` | `NONE` |
| `ZFUNC` | `LESSEQUAL` |
| `TEXTUREPERSPECTIVE` | 1 |
| `ALPHAFUNC` | `GREATEREQUAL` |
| `ALPHAREF` | `0xf0` |
| `COLORKEYENABLE` | 1 |
| `ZENABLE`, `ZWRITEENABLE` | 1 |
| `SRCBLEND`, `DESTBLEND` | `SRCALPHA`, `INVSRCALPHA` |

`ALPHATESTENABLE` and `ALPHABLENDENABLE` are left alone, so the device's defaults hold
until a draw sets them.

Texture stages:

| Stage | Colour | Alpha | Other |
|------:|--------|-------|-------|
| 0 | `MODULATE(TEXTURE, DIFFUSE)` | `MODULATE(TEXTURE, DIFFUSE)` | coordinates 0; `ADDRESS` `CLAMP` |
| 1 | `MODULATEALPHA_ADDCOLOR(TEXTURE, CURRENT)` | `DISABLE` | coordinates 1; `ADDRESS` `BORDER`, border `0xff000000` |
| 2 | `DISABLE` | `DISABLE` | |

**Filtering.** `MINFILTER` and `MAGFILTER` on stages 0 and 1 are `LINEAR`, or `POINT`
when +0x1e4 is set. At `Init` it is still 0, since the global starts zeroed, so the
filter is `LINEAR`.

**Addressing.** Stage 0 gets `ADDRESSV` `WRAP` and then `ADDRESS` `CLAMP`. `ADDRESS` sets
both `ADDRESSU` and `ADDRESSV`, so stage 0 clamps.

## Pixel format channels

`CreateSurfaces` derives, from each of the primary's masks:
- **The shift:** the zero bits below the mask.
- **The loss:** 8 minus the bits from there to its top.

A zero mask has shift 0 and loss 8.

`ExpandRed`, `ExpandGreen` and `ExpandBlue` (`00405df0`, `00405e20`, `00405e50`) scale
a pixel's channel to 8 bits:
```
((mask & pixel & 0xffff) >> shift) << loss
```
**The `0xffff`** keeps only the low 16 bits. In a 32-bit format, red therefore reads as
0.

| Format | Shifts (R, G, B, A) | Losses |
|--------|---------------------|--------|
| 565 | 11, 5, 0, 0 | 3, 2, 3, 8 |
| 555 | 10, 5, 0, 0 | 3, 3, 3, 8 |
| X888 | 16, 8, 0, 0 | 0, 0, 0, 8 |
| 8888 | 16, 8, 0, 24 | 0, 0, 0, 0 |

## Clearing and presenting

**Clears** go through `IDirect3DDevice7::Clear` on the whole target, with z 1.0:

| Function | Address | Clears |
|----------|---------|--------|
| `Clear(color)` | `00405140` | target and z-buffer |
| `ClearDepth` | `00405180` | the z-buffer |
| `ClearTarget(color)` | `004051c0` | the target |

**`Present`** (`00405040`) takes one byte. It returns `0x8200000e` without a primary.
1. **No back buffer:** return the primary's `IsLost`.
2. **When the byte is 0:** take the window's position again, and `Blt` the back buffer
   into it with `DDBLT_WAIT`.
3. **Full screen with +0x1d8 clear:** `Flip` with `DDFLIP_WAIT`.
4. **Otherwise,** as step 2. When the `0x61ec58` flag is set, `WaitForVerticalBlank`
   comes first ([architecture.md](../architecture.md)).

`RestoreSurfaces` (`00405200`) calls `IDirectDraw7::RestoreAllSurfaces`.

## CreateRenderer

`004054b0`.

1. **The pixel mode** comes from the primary's red mask:

   | Red mask | Mode |
   |----------|-----:|
   | `0xf800` | 2 |
   | `0x7c00` | 3 |
   | `0x1f` | 2, after "BGR 565 MODE. NOT SUPPORTED" (`006054a0`) |
   | any other, with 32 bits | 4 |
   | any other | −1, after "NOT SUPPORTED MODE" (`0060548c`) |

2. **Caps:**
   - `IDirectDraw7::GetCaps` gives the stretch blit flag (+0x238).
   - `IDirect3DDevice7::GetCaps` gives the texture limits (+0x224 to +0x234).
3. **Two-stage blending:** `006053f0` is set when the device has at least two
   texture blend stages and `D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR`.
4. **Mipmaps:** `SelectMipmapFlags`, below.
5. **Large textures:** +0x239 is set when the maximum texture size is at least
   1024 × 1024.
6. **The renderer** (`operator new(0x2a0)`) gets the mode, the device and the size.
   - **Guard band:** a guard band of 0 on the left gives −640, −480 and the size plus
     640 and 480. Otherwise the caps' guard band is used.
7. **`CalibrateTexelOffset`,** below.

### Textures

**`FitTextureSize`** (`004056e0`) is what `CTexture::CreateSurface` asks for a texture
of (width, height):
1. Each side goes up to a power of two, at least 1.
2. A side at or below the minimum becomes the minimum; at or above the maximum, the
   maximum.
3. **With an aspect ratio limit `a`:**
   - if `a < w / h`, then `h = w / a`;
   - then, if `a < h / w`, the result is `(h / a, h)`.

   The division is unsigned.
4. **What never ends:**
   - **A side above 2³¹:** the doubling wraps to 0 and never ends.
   - **A side limited to 0, with an aspect ratio limit:** the original divides by zero.

**`CreateTexture`** (`00405350`) and **`UpdateTexture`** (`004053f0`) divide the size by
`g_textureDivisor` (`0061ec50`):

| `TEXTUREMODE` | Divisor |
|---------------|--------:|
| 0 | 4 |
| 1 | 2 |
| any other, or absent | 1 |

**`SelectMipmapFlags`** (`00405f20`) sets the global pair `00623c10`/`00623c14`, which
`CTexture::CreateSurface` adds to each texture's description:
- **`TRILINEARFILTER` set** (`00688504`) **and `D3DPTFILTERCAPS_LINEARMIPLINEAR` in the
  texture filter caps:** `DDSD_MIPMAPCOUNT` and `DDSCAPS_MIPMAP | DDSCAPS_COMPLEX`;
- **otherwise:** 0.

`UIDetailLevelWnd` calls it again when the option changes.

**`CreateBackgroundSurface`** (`00405290`) makes:
- a `CTexture` when the size fits the maximum texture size;
- otherwise a `CSurfaceWallpaper` of the screen's size.

### CalibrateTexelOffset

`00405790` measures where the hardware samples a texel. Its result sets the global
offsets `g_texelOffsetU` and `g_texelOffsetV` (`006053f4`, `006053f8`). `CTexture::DrawRegion`
and `DrawImage` add them to texture coordinates ([texture.md](texture.md#drawing)).

**Setup:**
- **The texture:** a 4 × 4 checker, black and white in 2 × 2 blocks.
- **The states:** alpha test, alpha blending and the z-buffer off; linear filtering.

**The search:**
1. For each offset `k` from 0 to 65535:
   - `du = (k & 0xff) / 256` and `dv = (k >> 8) / 256` of a texel.
   - Draw the texture as a 4 × 4-pixel quad of transformed vertices, shifted by
     (`du`, `dv`). The quads tile the screen; each pass clears and draws as many as fit.
2. **After each pass:** read the target back through a `CSurface` and, per quad, sum
   the 8-bit channels (`ExpandRed` and the rest) over the white and the black texels.
3. **The winner** is the offset with the largest white minus black, among those with
   white above `0x167f` and black below `0x181`.
4. **The result** is the winner's `du` and `dv`, or (0.5, 0.5) when no offset qualifies.

**In 32-bit modes,** red reads as 0 (above), so only green and blue count.

The result depends on the hardware's rasterizer, which the fakes do not have. So it
cannot be checked in the emulation oracle.

## Release

`Release` (`004042e0`) releases the device first. If references remain, it shows
" : D3D object is still referenced %d times".

Then it releases, in order:
1. the back buffer, unless full screen, since the flip chain owns it;
2. the z-buffer;
3. the target;
4. the primary;
5. `IDirect3D7`;
6. the copy.

Last, `SetCooperativeLevel(window, NORMAL)`, then DirectDraw, with the same check.
It returns `0x8200000b` when anything was still referenced.

## In the port

`src/render/device.{h,cpp}` (`ragexe_render`):

| Port | Original |
|------|----------|
| `DeviceState` | a device's render and texture stage states. `ADDRESS` sets both address modes. |
| `InitialDeviceState` | the states `Init` sets |
| `ChannelsFromFormat`, `ExpandChannel` | the channel shifts and losses, and the expanders |
| `SelectPixelMode` | `CreateRenderer`'s choice |
| `FitTextureSize` | `FitTextureSize` |
| `SelectMipmapFlags` | `SelectMipmapFlags` |
| `CanBlendTwoStages` | the two-stage test |
| `TextureDivisor` | `TEXTUREMODE`'s divisor |

**Where the port differs:** `FitTextureSize` fails where the original never returns or
divides by zero.

**Checks:**
- **The vectors.** `tools/device_vectors.py` runs the original in the emulation oracle
  and writes `tests/unit/device_vectors.inc`:
  - `Init`, windowed in four pixel formats and full screen in two, on the HAL and the
    T&L HAL device: its state calls and the channels it derives;
  - the expanders on sample pixels;
  - `FitTextureSize` on 845 sizes and limits;
  - `SelectMipmapFlags` for each setting and caps.
- **The unit test** (`tests/unit/device_test.cpp`) replays `Init`'s calls through
  `DeviceState` and compares the result with `InitialDeviceState`.
- **Unchecked:** the pixel mode, the two-stage test and the divisor come from the code
  alone, since the emulator does not run `CreateRenderer` (it would need the
  calibration).
