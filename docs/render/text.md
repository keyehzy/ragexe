# Text

The client draws text with GDI onto DirectDraw surfaces. It asks a surface for a device
context with a font selected (`GetSurfaceDCWithFont`, `00407680`), sets the colour with
`SetTextColor`, and calls `TextOutA`. Measuring uses `GetTextExtentPoint32A`. Stepping over
characters uses `CharNextExA` and `CharPrevExA` on the client's code page (`0067c998`). The
port is `src/render/text`: which font each call asks for, and the `TextOutA` runs it makes.
How the glyphs look is the font's, so no oracle can match it bit for bit; the port draws
them with FreeType ([the glyphs](#the-glyphs)).

## Fonts

`GetSurfaceDCWithFont` takes a language, a height, a weight and a flag for vertical text.
It finds or creates the font in `g_FontCache` (`00623c48`) and selects it into the surface's
DC; `ReleaseSurfaceDC` (`00407770`) puts the previous font back.

**The record.** `FontEntry::FontEntry` (`004077d0`) keeps the four values. When the language
is 0, it is set from the service language (`0067c9d8`, which `SelectLanguage` sets:
[clientinfo.md](../formats/clientinfo.md#selectlanguage)):

| Service language | Font language | Height |
|-----------------:|--------------:|--------|
| 1, 6, 7 | 1 | 3 more |
| 2, 3, 4 | the same | as asked |
| 5 | 5 | 3 more |
| any other | 0 | as asked |

So on iRO (service type 1) a request in language 0 gets language 1, three pixels taller.

**Creating it.** `FontEntry::CreateFont` (`00407870`) calls `CreateFontA` with:
- the height and weight;
- an escapement of −900 for vertical text, 0 otherwise;
- the language's charset (`GetCharSet`, `00407a30`, from `00605520`);
- the face from `GetInstalledFaceName` (`00407980`), or none;
- 0 for everything else.

`GetInstalledFaceName` asks `EnumFontFamiliesExA` for the language's face (`00605508`), then
its alternative (`006054f0`), and returns the first installed:

| Language | Face | Alternative | Charset |
|---------:|------|-------------|--------:|
| 0 | 굴림 | Gulim | 0x81 (Hangul) |
| 1 | Arial | Arial | 0 |
| 2 | ＭＳ Ｐゴシック | MS Gothic | 0x80 (Shift JIS) |
| 3 | 宋体 | SimSun | 0x86 (GB2312) |
| 4 | 細明體 | MingLiU | 0x88 (Big5) |
| 5 | Tahoma | Tahoma | 0xde (Thai) |

The faces are in each language's own code page.

**The cache** keeps its records sorted (the inlined `std::sort` of `FontCache::GetFont`,
`00407ac0`). The order changes nothing that is drawn.

## The calls

A length of 0 stands for the whole string (`strlen`).

| Function | What it draws |
|----------|---------------|
| `SceneRenderer::DrawText` (`00410df0`) | the text on the back buffer, in language 0, height 12, weight 400 |
| `SceneRenderer::DrawOutlinedText` (`00410eb0`) | the text four times in the outline colour, a pixel off on each diagonal (up left, up right, down right, down left), then in its colour; the weight given |
| `UIWindow::DrawText` (`004a5cb0`) | the text, weight 400 |
| `UIWindow::DrawShadowedText` (`004a5ea0`) | the text a pixel down and right in the shadow colour, then in its own |
| `UIFrameWnd::DrawRightAlignedText` (`004a5d20`) | the text ending at x: it moves left by the width of the whole string, whatever length it draws |
| `UIListBox::DrawColorCodedText` (`004a5dc0`) | the text in pieces between colour codes, each in the colour last given, each after the one before |
| `DrawVerticalText` (`004a6010`) | the text a character a line, each centred on the widest (C's division) |
| `UINameBalloonText::DrawOutlinedText` (`004a5f30`) | the text four times in the outline colour, a pixel left, right, up and down, then in its colour; weight 700 when bold, else 400 |
| `UIVerticalNameBalloonText::DrawOutlinedText` (`004a6130`) | the same in the vertical font, moved right by the text's height |

**The measures:**
- `UIWindow::GetTextWidth` (`004a6280`) and `UIBalloonText::GetTextHeight` (`004a6310`):
  the text's extent, weight 700 when bold, 0 for no text.
- `UIVerticalNameBalloonText::GetVerticalTextHeight` (`004a63a0`): the heights of the
  characters summed.
- `FitTextWithEllipsis` (`004a6460`): the text when it is narrower than the width.
  Otherwise characters come off the end while the rest and "..." (`00606730`) are wider
  than the width. Then "..." is added. When none is left, the result is "..." alone.

**Unused:** nothing in this client calls `DrawVerticalText` or `FitTextWithEllipsis`; they
are ported as they stand.

**Colour codes.** `UIWindow::ParseColorCode` (`004a5bf0`) reads a `^` and six hex digits as
red, green and blue, into a `COLORREF`. Each digit's value comes from the table at
`00609f84`, indexed with a signed char, so a byte above 0x7f reads one of the 128 bytes before
the table. A code at the end of the text reads past it. `UIWindow::FindColorCode`
(`004a5c70`) steps to the end or the next `^`. The list box's colour is the caller's,
which it leaves as the last code's.

## The stamp and the frame rate

Two callers of `SceneRenderer::DrawText` draw over the whole screen.

**The screenshot's stamp.** When a screenshot is due (`CMode`+0x2c), `CMode::SaveScreenshot`
(`004cba20`) stamps the back buffer before saving it:
- **The text:** `GetLocalTime`'s date and time, `"%d/%02d/%02d/%s  %02d: %02d: %02d"`
  (`0060c164`), with the day of the week by its English name (`0060c1c4` Sunday to
  `0060c188` Saturday).
- **Where:** at x 4, 16 above the bottom of the renderer's height (+0x28 of `00625e98`).
- **How:** in black a pixel right, left, down and up, then in white: an outline.

**The frame rate.** `SceneRenderer` counts frames:
- `00410cd0`, at the renderer's start (`0040d1a0`), zeroes the count (+0x3c) and keeps
  `timeGetTime` (+0x40) as the time it counts from;
- `Present` (`00410c50`) adds one to the count each frame.

`00410cf0` draws them, though nothing in this client calls it:
1. `"Total : 0 - 0"` at (0, 0) and `"Unused : %d - %d"` at (0, 16), both black. The
   numbers are the textures queued to be deleted (+0x25c) and that count shifted left by
   17, as signed ints.
2. The rate: the count times 1000.0 (`005e26c4`), over the milliseconds since the start as
   a signed int, each step in single precision. It is printed `"%.01f fps"` in yellow
   (`0xffff`), 40 left of the right edge (+0x24) and 14 above the bottom (+0x28).
3. When more than a second has passed (unsigned) and the count is even, both the count and
   the time since the start are halved.

**`"%.01f"`** is the C runtime's of the client:
- the exact value is rounded to tenths with ties away from zero, so 2.25 prints "2.3";
- a value a little under zero prints "-0.0", but a negative zero (no frames over a time
  gone backwards) prints "0.0";
- an infinity (frames in no time) prints "1.$", its "1.#INF" rounded at the '#';
- the x87's indefinite NaN (no frames in no time) prints "-1.$".

## The glyphs

GDI is replaced by FreeType 2.15.0 (`src/render/glyphs`, library `ragexe_glyphs`):
- **The face:** Liberation Sans, embedded in the build, metric-compatible with Arial, the
  face iRO's language asks for. Every language is drawn in it. A weight above 550 takes its
  bold, as GDI picks the nearer of Arial's two weights.
- **The height** is the cell's, ascent and descent together, as `CreateFontA` reads a
  positive height. The baseline is the share of it the face's `usWinAscent` takes, rounded.
- **The extents:** the sum of the characters' hinted advances, and the cell's height. No
  kerning, as `TextOutA` has none.
- **The pixels:** hinted glyphs without smoothing, each covered pixel set to the run's
  colour; nothing else is touched, the background being transparent (`SetBkMode`,
  `00407680`).
- **Vertical fonts** (escapement −900) run down the screen: the cell is turned a quarter
  clockwise about the run's point, so it lies to the point's left. That is why the vertical
  balloon moves its text right by the text's height.
- **The characters:** code page 1252, the system's on an English Windows, which iRO uses
  (code page 0, [clientinfo.md](../formats/clientinfo.md#selectlanguage)). A byte is a
  character.

## In the port

`src/render/text.{h,cpp}`:
- `FontSpec`, `ResolveFont` (the record) and `RequestFont` (`CreateFontA`'s arguments,
  given which faces are installed);
- `TextMetrics`, the extents and the character steps;
- `TextRun`, one `TextOutA`, and the calls above, which add runs: `DrawSceneText`,
  `DrawSceneOutlinedText`, `DrawWindowText`, `DrawShadowedText`, `DrawRightAlignedText`,
  `DrawColorCodedText` with `ParseColorCode`, `DrawVerticalText`, `DrawNameBalloonText`,
  `DrawVerticalNameBalloonText`;
- the measures `TextWidth`, `TextHeight`, `VerticalTextHeight` and `FitText`;
- `DrawScreenshotStamp`, and `FrameCount`, `StartFrameCount`, `DrawFrameRate` and
  `FormatTenths` for the frame rate.

`src/render/glyphs.{h,cpp}`: `GlyphRenderer`, the `TextMetrics` of FreeType's faces, which
draws runs into ARGB pixels; `Cp1252ToUnicode` and `ColorRefToArgb`. `ragview` draws the
frame rate and the stamp with it ([gpu.md](gpu.md#ragview)).

## Checks

`tools/text_vectors.py` runs the original in the emulation oracle and writes
`tests/unit/text_vectors.inc`:
- **Fonts:** `FontEntry`'s record for every service language 0 to 8, font language 0 to 5
  and both flags. Then `CreateFont`'s arguments with both faces installed, only the
  alternative, or neither.
- **400 calls** of the thirteen functions:
  - on random text, with high bytes and, for the list box, colour codes, some ending early
    or with junk digits;
  - at random positions, colours, fonts, lengths and weights;
  - `FitTextWithEllipsis` at, just under and just over the text's width and its width with
    "...".
- **The stamp:** 120 runs of `SaveScreenshot`, stopped after its last `DrawText` before it
  makes the folder and the file. The time comes from a stand-in `GetLocalTime`, some with
  fields out of range; some runs have no screenshot due.
- **The frame rate:** 308 calls of `00410cf0` with `timeGetTime` stood in for:
  - random counts and times, with the time gone backwards, none, and just over a second;
  - counts of none;
  - rates of exactly n.25 and n.75, the ties at the tenths;
  - eight rates within 2^-56 of a midpoint between two floats beside an n.25 or n.75,
    which a division through a double would round to the tie, printing the next tenth.

The stamp and the frame rate are run with the back buffer's `AddRef`-like slot popping its
argument, as a COM method does, so that `DrawText` returns with its registers kept.

**The stand-ins:**
- `GetSurfaceDCWithFont` records the font asked for.
- `GetTextExtentPoint32A` answers with a function of the bytes and the font, which the test
  shares.
- `TextOutA` and `SetTextColor` are recorded.
- The code page is 1252, so characters are single bytes.

`tests/unit/text_test.cpp` matches all 23,933 values.

`tests/unit/glyphs_test.cpp` checks the glyphs:
- cell heights, and extents that are the sums of their characters';
- glyphs inside their cell, in the run's colour, moved with it and clipped at the edges;
- the baseline at every height from 8 to 24, and an L's stem and foot where they belong;
- a line's pixels in each face, summed, which pins FreeType's and the font's releases;
- vertical text being the horizontal turned;
- the code page and the colours.

**Not ported:**
- the other languages' faces (Gulim, MS Gothic, SimSun, MingLiU, Tahoma), all drawn in
  Liberation Sans;
- double-byte stepping on the Korean, Japanese and Chinese code pages, which the
  `TextMetrics` given supplies;
- the cache's sort.
