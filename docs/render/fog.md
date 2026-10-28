# The fog

Some maps are fogged: each one's record in `fogParameterTable.txt` gives the fog's start,
end, colour and density. The client's Direct3D 7 device draws already transformed
vertices, so the client works out each vertex's fog itself and passes it to the device in
the specular colour's alpha. The port is `src/render/fog`.

## The switch

The fog flag is `ClientState`+0x182c (`00687e9c`):
- **At start-up,** `ClientState` `00587d90` reads it from the registry value `FOG`. It is
  1 when the value is missing or the key does not open, so the fog is on by default.
- **`/fog`** (the chat command `ClientState` `00596210` maps to message 0x3c) flips it.
  `CGameMode` `00516a80` then applies the fog again and prints "Fog On" or "Fog Off".

## The map's fog

`CGameMode::OnInit` calls `CGameMode` `00516570` with the flag, after setting the clear
colour. With the flag on, it looks up the map's `.rsw` name (`CGameMode`+0x50, such as
`gef_fild12.rsw`):
- **The lookup** (`ClientState` `005991f0`) is a `std::lower_bound` over the records
  `LoadFogParameterTable` sorted ([client_tables.md](../formats/client_tables.md)), by
  `StringLess` (`00599400`). The record found must equal the name as `strcmp` sees it.
- **A record found** fills four globals from its values, in order:

  | Value | Global | Read with | Example |
  |-------|--------|-----------|---------|
  | 1 | start, `00625eb0` | `atof`, as a float | `0.2` |
  | 2 | end, `00625eb4` | `atof`, as a float | `0.45` |
  | 3 | colour, `00625ea4` | `ParseHexDigits` | `0x2275A9FF` |
  | 4 | density, `00625eac` | `atof`, as a float | `0.5` |

  Then `SetFogEnabled(1)` (`SceneRenderer` `00411010`).
- **Otherwise,** with the flag off or no record, `SetFogEnabled(0)`, and the four
  globals keep what they had.

The start and end are fractions of the depth range, from the near plane, 10 (`005e269c`),
to the far one, 1500 (`005e2698`). The density is set on the device but unused in linear
mode.

**`ParseHexDigits`** (`004e4cc0`) reads from the last character back to the third, so
the first two, normally `0x`, are skipped whatever they are:
- **Each character** takes four bits, the last one the lowest. A character that is not a
  hexadecimal digit adds nothing but still takes its bits.
- **The shift wraps at 32,** so a ninth digit lands on the first.
- **A text shorter than 3** gives 0.

**`SetFogEnabled`** (`00411010`) runs on a hardware device (`C3dDevice`+0x1ec clear):
- **+0x4c** is set when the device has vertex fog, `D3DPRASTERCAPS_FOGVERTEX` in its
  triangle raster caps (`C3dDevice`+0x8c).
- **+0x44** takes the argument, and so does `FOGENABLE`.

On a software device both are cleared. Either way it sets:
- **+0x48,** so that the next `DrawScene` sets up the projection;
- **the mode `00625ea0`** to 3, `D3DFOG_LINEAR`;
- **range fog `00625ea8`** to 1.

## Each vertex

`AddPrimitive` (`00412550`) runs `ComputeVertexFog` (`0040c2e0`) on every primitive when
+0x4c is set. That is whether or not the fog is on. For each vertex, at single precision:
1. **The depth** 1/rhw is placed between near and far: d = (1/rhw − 10) / (1500 − 10).
2. **Then between the fog's start and end:** f = (d − start) / (end − start).
3. **Clamped:** below 0, or unordered, gives 0; above 1 gives 1.
4. **The fog factor** (1 − f) × 255 (`005e26a8`), truncated by `__ftol`, is shifted into
   the top byte. The whole specular colour is replaced, so its other channels become 0.

255 is no fog, and 0 is the fog's colour.

## The device's states

`SceneRenderer::SetupProjection` (`00411090`) runs at the start of `DrawScene` while +0x48
is set, then clears it. With the fog on (+0x44) on a hardware device, it sets up the
following.

**The transforms:**
- the world and view are set to identity;
- the projection comes from `MakePerspectiveMatrix` (`00402a70`): a 15° field of view
  (`005e26a4` times `005e26c8`), +0x10 as the aspect, and 10 to 1500.

**The render states:**
1. **`FOGCOLOR`** (34) takes the colour.
2. **In linear mode, with vertex fog:**
   - `FOGSTART` (36) and `FOGEND` (37) as depths, (1500 − 10) × fraction + 10;
   - `FOGDENSITY` (38);
   - `FOGTABLEMODE` (35) 0;
   - `FOGVERTEXMODE` (140) the mode.
3. **In linear mode, without vertex fog:** `FOGENABLE` 0.
4. **In any other mode, with vertex fog:** `FOGTABLEMODE` 0 and `FOGVERTEXMODE`, then
   `FOGDENSITY`.
5. **In any other mode, without it:** the message "Your graphic accelerator does not
   support fog effect.", then `FOGDENSITY`.
6. **With vertex fog and range fog:** `RANGEFOGENABLE` (48) 1.

`SetFogEnabled` always leaves the mode linear, so in the game only steps 2 and 3 occur.

**Applying it.** `DrawScene` then sets `FOGENABLE` from +0x44 for the opaque and
alpha-blended passes ([scene.md](scene.md#drawscene)). For vertices with an rhw, Direct3D
blends each pixel toward the fog colour by the specular alpha it interpolates. The
transforms and the start and end above play no part. The shader does the same
([gpu.md](gpu.md)).

## In the port

`src/render/fog.{h,cpp}`:
- `FogState`, the six globals;
- `ParseHexDigits`;
- `FindMapFog` (`00516570` with the flag on, and `005991f0`);
- `VertexFog` (`ComputeVertexFog` for one vertex);
- `SetupFog`, the render states of `SetupProjection`.

The `atof` it shares with the `.rma` reader is `res::AtoF` (`src/res/c_runtime.h`).

**`ragview`:**
- reads the map's record unless `--no-fog`;
- gives every vertex of the ground and the models its fog factor;
- puts `SetupFog`'s states before the first `DrawScene`, and turns `DrawScene`'s fog on
  when a record was found.

## Checks

`tools/fog_vectors.py` runs the original in the emulation oracle, with the x87 at single
precision, and writes `tests/unit/fog_vectors.inc`:
- **`ParseHexDigits`** on 27 strings: the table's colours, short, long, mixed-case,
  junk, and bytes above 0x7f;
- **a synthetic table** of 13 records read by `LoadFogParameterTable`. `00516570` then
  runs for 19 names, found and not, with the flag on and off. The values cover signs,
  spaces, `d` exponents, junk, overflow, underflow, long digit strings, and equal and
  inverted ranges;
- **`ComputeVertexFog`** on 394 depths, under 9 ranges;
- **`SetupProjection`** under 16 settings: the fog on and off, with and without vertex
  fog, linear and not, range fog on and off.

`tests/unit/fog_test.cpp` matches all 3,719 values.

**Not ported:**
- the transforms `SetupProjection` sets;
- the software device's path, which turns the fog off;
- the `/fog` command.
