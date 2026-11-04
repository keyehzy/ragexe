# The sprite texture cache

An 8-bit sprite image has no texture of its own: `SceneRenderer` draws it through its palette
into a slot of a shared 256 × 256 page, and keeps the pages to find it there again.
`AddSpriteQuad` ([actor.md](actor.md#the-quad)) and `DrawActFrame`
([actor.md](actor.md#on-the-screen)) ask for one, as do the windows that show sprites
(`UISpriteButton` and seven more `vf20`s): `FindCachedSprite` first, then `CacheSpriteImage`
when it answers no texture. The port is `src/render/sprite_cache`.

## The pages

`SceneRenderer`+0x194 holds 16 lists of pages, one for each **size class**. An image's width
and height (`CSprRes` image +0 and +2, compared as signed 16-bit) each fall in a band: up to
32, up to 64, up to 128, or more. The class is the width's band times 4 plus the height's, so
an image of 0 or negative width goes with the smallest. Each class's slots are the size its
bands allow (`006055c8`): 32 × 32 for the first, 256 × 256 for the last.

**The spare textures.** The `SceneRenderer` constructor makes 50 page textures up front
(`MakeSpareSpritePages`, `0040deb0`, from `0040d454`): each a `CTexture(256, 256, 0)` at the
end of the list at +0x254 (its head +0x258, its count +0x25c), referenced once
(`IncrementUseCount`, `004167e0`, +0x120). They are the first 50 textures the client makes,
before any map's.

A **page** (0x2c bytes, in a list node of 0x34) is:

| Offset | Field |
|-------:|-------|
| +0 | its `CTexture` |
| +4, +8 | the columns and rows of slots, 256 over the slot's width and height |
| +0xc, +0x10 | the slots' width and height |
| +0x14 | the slots, a vector of 0x24-byte records, row by row |
| +0x28 | when a slot of it was last used (`timeGetTime`) |

A **slot**:

| Offset | Field |
|-------:|-------|
| +0 | the image (its record's address) |
| +4 | the palette (its address) |
| +8 | when it was last used, 0 at first |
| +0xc, +0x10 | u0, v0: the slot's corner over 256 (`005e2668`) |
| +0x14, +0x18 | u1, v1: the image's far corner over 256 |
| +0x1c, +0x20 | the slot's corner in texels |

## Finding an image

`FindCachedSprite` (`0040e540`) takes the image, the palette and a record to fill. It looks
through the pages of the image's class in order, and each page's slots in order, for the
first slot holding both the image and the palette. Found, the slot and its page are stamped
with `timeGetTime`, the slot's u0, v0, u1 and v1 go to the record's +0xc to +0x18, and the
page's texture is the answer. Otherwise the answer is 0 and the record is left alone.

## Caching an image

`CacheSpriteImage` (`0040e130`) takes the same arguments:
1. **A slot:** the first, page by page, used before `timeGetTime` − 3750, a quarter of the
   15 s at `00612710`. The comparison is unsigned: in the first 3.75 s after the clock wraps,
   every slot is old enough, and the first slot of the first page is taken whatever it holds.
   An image already cached is not looked for: a second copy can go in another slot.
2. **Without one, a page:** a new node at the end of the class's list.
   - Its texture is the last spare in the queue, taken off it (`0040e2c3`), or once the queue
     is empty a new `CTexture(256, 256, 0)`. So the first page takes the 50th spare. Either
     way it is cleared with `FillColor(0, 0)` (`vf02`).
   - The slots are laid out row by row at multiples of the class's size, each with its corner
     over 256 as u0 and v0 and its time 0.
   - Then the search starts again from the first page, and finds the new page's first slot.
3. **The image** goes in the slot with the palette: u1 and v1 are its width and height past
   the slot's corner, over 256. Both sums are loaded with `fild` from a quadword whose high
   half is 0, so a negative one reads as unsigned and lands past 2²⁴ (`0040e47f`).
   `CTexture::DrawRleImage` ([texture.md](texture.md#drawrleimage)) then draws the image at
   the slot's corner and zeroes the rest of the slot.
4. The slot and its page are stamped with `timeGetTime`, read again, and the record and
   answer are filled as `FindCachedSprite` fills them.

At a time of exactly 3750, no slot is older than 0, so step 2 makes pages without end and
never returns.

## Letting pages go

`CGameMode::ReleaseSpritePagesPeriodically` (`0050d3d0`), early in each `OnFrame`
(`0050d436`), runs `ReleaseExpiredPooledTextures` (`0040e030`) once `timeGetTime` is past
`g_nextSpritePageRelease` (`00685ee0`, 0 at the start), then sets that to 15 s later
(`0x3a98`).

`ReleaseExpiredPooledTextures` goes through the 16 lists in order and lets go each page last
used before `timeGetTime` − 15000 (unsigned again), freeing its slots. A texture still
referenced (+0x120, [texture.md](texture.md)), which only a spare is, goes back at the end of
the queue (`0040e090`); any other is deleted. So the 50 spares are never deleted while the
renderer lives (`DeleteQueuedTextures`, `0040df90`, deletes them with it), and pages beyond
them come and go.

## The port

`SpriteTextureCache` holds the 16 lists:
- **`MakeSpareTextures`** is `0040deb0`: the spares from a function that makes a texture.
- **`Find`** is `FindCachedSprite`.
- **`Cache`** is `CacheSpriteImage`. It takes a function that makes a page's texture once the
  spares run out, and it answers the page made (to be cleared), whether its texture was made,
  and the slot to draw the image into. It reads the
  time once, where the original reads it twice. At the time that makes the original loop,
  it answers none.
- **`ReleaseExpired`** answers the textures deleted, in order.
- **`ReleaseIfDue`** is `0050d3d0`.

Images and palettes are keys, as the original tells them apart by address.

## Checks

`tools/sprite_cache_vectors.py` runs the original's functions in the emulation oracle, with
the x87 at single precision, on a `SceneRenderer` made by its own constructor, spares and
all. It runs 32 scripts of frames, each frame at one time. Each frame's operations
are:
- **draws,** as `AddSpriteQuad` asks: `FindCachedSprite`, then `CacheSpriteImage` on a miss;
- **`CacheSpriteImage` alone,** the image cached or not;
- **`0050d3d0`'s release,** some at just its time.

The scripts vary:
- **the images:** 3 to 150 of them, with sides at each band's edges, 0, negative and up to
  32,767; four scripts draw 150 large images 40 at a frame, which use up the spares;
- **the palettes:** one to three;
- **the starting time,** about where `timeGetTime` − 3750 and − 15000 wrap and near 2³²;
- **the steps between frames,** from milliseconds to past 15 s.

`CTexture`'s constructor, `FillColor`, deleting destructor and `DrawRleImage` are stood in
for and recorded, the textures numbered in the order made. Each operation's texture, record,
page made and whether its texture was, slot drawn and textures deleted are kept.

`tests/unit/sprite_cache_test.cpp` matches all 14,730 operations: 6,578 images found, 1,630
pages made (702 of them with a new texture), 5,915 slots taken again, and 607 releases
deleting 264 textures. It also checks that the port gives up at the time where the original
never returns. 29 mutants are killed.
