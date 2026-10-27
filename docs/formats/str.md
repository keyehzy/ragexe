# Effects: CEZeffectRes

`CEZeffectRes` (RTTI, 0xd3b4 bytes) is an animated effect: layers of textured quads
driven by keyframes. It is registered for `str` under `texture\effect\`
([resources.md](resources.md#types)). `CRagEffect::LoadStrFile` (`0054fae0`) fetches one,
and `CRagEffect::method_0054fba0` plays it.

## Members

As with grounds ([gnd.md](gnd.md)), the loader keeps the file's bytes in its own
`DataFile` and points into them.

| Offset | Field |
|-------:|-------|
| +0x110 | the file's fourth dword |
| +0x114 | its third dword |
| +0x118 | layer count |
| +0x120 | 60 layers of 0x380 bytes |
| +0xd320 | int, zeroed by the constructor |
| +0xd324 | the `DataFile` |

**A layer:**

| Offset | Field |
|-------:|-------|
| +0 | texture count, as read |
| +8 | 110 `CTexture*`, filled on demand |
| +0x1c0 | 110 name pointers |
| +0x378 | key count, as read |
| +0x37c | the keys |

`GetEffectEntryTexture` (`004289a0`) loads texture `n` of a layer and caches it at
`+8`:
- **The name:** `effect\` and the name, with `.bmp` added when it has no `.`.
- **The fetch:** `TextureCache::GetTexture`, with its use count raised.

**A key** is 0x7c bytes: a frame, a type, then 29 dwords.
- **Type 0:** the player takes the 29 values as they are.
- **Any other type:** it adds them to its current ones.
- **How the texture index moves:** one value chooses it, through a `switch` on cases 1
  to 5 that use the layer's texture count.

The meanings of the values come with the effect player.

## File

All numbers little-endian.

| Field | Size |
|-------|-----:|
| 4 bytes, never checked (`STRM` in `data.grf`) | 4 |
| version, `0x94` | 4 |
| `+0x114` | 4 |
| `+0x110` | 4 |
| layer count | 4 |
| reserved, skipped | 16 |
| each layer | |

**Each layer:**

| Field | Size |
|-------|-----:|
| texture count | 4 |
| the names | 128 each |
| key count | 4 |
| the keys | 0x7c each |

### In data.grf

All 137 `.str` files are `STRM` version `0x94`, have zeroed reserved bytes, and end
exactly after their last layer.

| Field | Values |
|-------|--------|
| `+0x114` | 60 in every file |
| `+0x110` | 15 to 619 |
| layers | up to 50 |
| textures per layer | up to 31 |
| keys per layer | up to 221 |

## Load

`CEZeffectRes::Load` (slot 3, `00428b50`). No read is checked, and the pointers are
never compared with the file's size.

**Refusals:**
- **The file does not open:** show "Str::Cannot find File : " (`00606070`) with the
  path, `Reset`, and fail.
- **The version is not 0x94:** after the header and the 16 skipped bytes, show
  "LoadAniClip: Version is incorrect" (`0060604c`), `Reset`, and fail.

**Each layer:**
1. **The texture count** into `+0`.
2. **Its names:** point the next slots at the 128-byte names, advancing the read
   position.
3. **The other slots:** point every slot from the count to 110 at "1.bmp" (`00606044`).
4. **The key count** into `+0x378`. When it is above 0, point `+0x37c` at the keys and
   advance past them.

**Nothing is bounds-checked:**
- **Layers:** a layer count above 60 runs past the layer array.
- **Textures:** a texture count above 110 runs its names into `+0x378`.
- **Negative counts** read nothing, but are stored as they are.

`Reset` (slot 4, `00428b30`):
- zeroes everything from `+0x110` to the `DataFile`, texture pointers included, without
  releasing the textures;
- closes the `DataFile`.

The constructor calls it too.

## In the port

`src/res/str.{h,cpp}`: `Str` (layers of texture names and `StrKey`s), `DecodeStr` and
`StrRes`, registered by `RegisterStrTypes`.
- **Names and keys:** each layer keeps the names it has. The slots the client points at
  "1.bmp" are left implicit, and the keys are kept as stored.

**Where the port differs:**
- **Array overflows fail:** more than 60 layers, or more than 110 textures in a layer.
- **Truncated files fail,** as do names with no NUL in their 128 bytes.

**The oracle:** `tools/str_oracle.py` runs the original `Load` on every `.str` in
`data.grf` and on 6 synthetic files.
- **What it compares:** the header values, every layer's names (read through the
  original's pointers) and keys.
- **What else it checks:** that every slot past a layer's count points at "1.bmp".
- **The synthetic files cover:** a full 60 layers, a full 110 textures, negative counts,
  no layers and a refused version.
