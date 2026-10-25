# Texture stage modes

Each primitive the renderer draws carries a mode at `+0x1c`. Before the draw,
`SceneRenderer::method_004112d0` turns the mode into Direct3D 7 texture stage states.
The table below comes from running that function in the emulation oracle for every
mode, against the recording `IDirect3DDevice7` fake (`tools/emu/directx.py`).
`tests/emu/test_oracle.py` checks the draw path that uses it.

Operations and arguments are the Direct3D 7 `D3DTOP_` and `D3DTA_` values: `DIFFUSE`
is the vertex colour, `CURRENT` is stage 0's result, and `COMPLEMENT` means 1 − x.

| Mode | Stage 0 colour | Stage 0 alpha | Stage 1 colour | Stage 1 alpha |
|--:|---|---|---|---|
| 0 | `MODULATE(TEXTURE, DIFFUSE)` | `MODULATE(TEXTURE, DIFFUSE)` | `DISABLE` | `DISABLE` |
| 1 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG1(TEXTURE)` | `DISABLE` | `DISABLE` |
| 2 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG1(TEXTURE)` | `DISABLE` | `DISABLE` |
| 3 | `ADD(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `DISABLE` | `DISABLE` |
| 4 | `BLENDTEXTUREALPHA(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `DISABLE` | `DISABLE` |
| 5 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `MODULATE(TEXTURE, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 6 | `MODULATE(TEXTURE\|COMPLEMENT, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `MODULATE(TEXTURE\|COMPLEMENT, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 7 | `SELECTARG1(TEXTURE)` | `SELECTARG1(TEXTURE)` | `MODULATE(TEXTURE\|ALPHAREPLICATE, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 8 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG1(TEXTURE)` | `ADD(TEXTURE, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 9 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `BLENDTEXTUREALPHA(TEXTURE, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 10 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `BLENDDIFFUSEALPHA(TEXTURE, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 11 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `ADD(TEXTURE, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 12 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `ADD(TEXTURE\|COMPLEMENT, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 13 | `ADD(TEXTURE, DIFFUSE)` | `SELECTARG1(TEXTURE)` | `MODULATE(TEXTURE, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 14 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `MODULATE2X(TEXTURE, CURRENT)` | `SELECTARG1(TEXTURE)` |
| 15 | `MODULATE(TEXTURE, DIFFUSE)` | `SELECTARG2(DIFFUSE)` | `ADDSIGNED(TEXTURE, CURRENT)` | `SELECTARG1(TEXTURE)` |

- **Texture coordinates.** Stage 0 always reads coordinate set 0. Modes 5 to 15 point
  stage 1 at set 1; modes 0 to 4 leave stage 1's set alone, since they disable it.
- **Higher modes.** The switch is bounded at 15 (`cmp eax, 0xf; ja`): a mode of 16 or
  more, unsigned, sets nothing, and the draw keeps the stages of the one before it.
- **No dependence on the previous draw.** A mode sets only some states. But every
  argument that one of its operations reads is among them (the test checks this), so
  the result does not depend on the mode drawn before. The port can treat each mode
  as a fixed combiner.
- **The draw.** Around the stage set-up, `SceneRenderer::method_00410560` flushes a
  list of primitives. For each one it:
  1. sets the texture, only when it differs from the last one (`+0x50`);
  2. applies the mode;
  3. sets `SRCBLEND` and `DESTBLEND` from the primitive's `+0x14` and `+0x18`;
  4. calls `DrawPrimitive` with FVF `0x1c4`: transformed position and `rhw`, diffuse,
     specular, and one texture coordinate set, 32 bytes per vertex.

  `0x40c280` first multiplies the texture coordinates by the texture's u and v scale
  (`+0x18`, `+0x1c`), in place in the primitive's vertex array.
- **Initial state.** `C3dDevice::Init` sets up the device once:
  - Render states:
    - no culling, and `ZFUNC` `LESSEQUAL`;
    - alpha function `GREATEREQUAL` with reference `0xf0`;
    - colour keying, z-buffer and z writes on;
    - blend factors `SRCALPHA` / `INVSRCALPHA`.

    It turns on neither the alpha test nor alpha blending.
  - Stages 0 and 1: linear filtering, or point filtering when the device's `+0x1e4` is
    set.
  - The test pins these values.
