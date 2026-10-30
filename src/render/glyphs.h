#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "render/text.h"

namespace ragexe::render {

// The system's side of the text calls, with FreeType in place of GDI
// (docs/render/text.md#the-glyphs): the extents GetTextExtentPoint32A gives, the steps CharNextExA
// and CharPrevExA take, and the pixels TextOutA sets.
//
// - Every language is drawn in Liberation Sans, embedded, metric-compatible with Arial, the
//   face iRO's language asks for; a weight above 550 takes its bold, as GDI picks the nearer
//   of Arial's two weights.
// - A font's height is its cell's, ascent and descent together, as CreateFontA reads a
//   positive height (a negative one is taken by its size, and 0 as 1); the ascent is the share
//   of it the face's usWinAscent takes, rounded.
// - Glyphs are hinted and drawn without smoothing, in one colour, as GDI draws Arial at these
//   sizes; nothing else is touched (SetBkMode TRANSPARENT, 00407680).
// - Bytes are characters of code page 1252, the system's on an English Windows, which the
//   iRO client uses (code page 0).
class GlyphRenderer final : public TextMetrics {
 public:
  // nullptr when FreeType cannot open the embedded faces. service is the language a font of
  // language 0 takes (0067c9d8, ResolveFont).
  static std::unique_ptr<GlyphRenderer> Create(std::int32_t service);
  ~GlyphRenderer() override;
  GlyphRenderer(const GlyphRenderer&) = delete;
  GlyphRenderer& operator=(const GlyphRenderer&) = delete;
  GlyphRenderer(GlyphRenderer&&) = delete;
  GlyphRenderer& operator=(GlyphRenderer&&) = delete;

  // The sum of the characters' advances, and the cell's height.
  TextSize Extent(const FontSpec& font, std::string_view text) const override;
  std::size_t Next(std::string_view text, std::size_t pos) const override;
  std::size_t Previous(std::string_view text, std::size_t pos) const override;

  // TextOutA: the run's glyphs from its point, the top left of its cell, into width × height
  // ARGB pixels (top row first), each covered pixel set to the run's colour with alpha 0xff.
  // A vertical font (escapement -900) runs down the screen, its cell's top to the right: the
  // cell turned a quarter clockwise about the point, so it lies to the point's left.
  void Draw(const TextRun& run, std::int32_t width, std::int32_t height,
            std::span<std::uint32_t> argb) const;

 private:
  struct Faces;
  explicit GlyphRenderer(std::unique_ptr<Faces> faces);
  std::unique_ptr<Faces> faces_;
};

// A byte of code page 1252 as Unicode: Latin-1, with the printable characters at 0x80 to 0x9f
// and the five bytes it leaves undefined kept as their C1 controls.
char32_t Cp1252ToUnicode(unsigned char c);

// A COLORREF (0x00bbggrr) as an opaque ARGB dword.
std::uint32_t ColorRefToArgb(std::uint32_t color);

}  // namespace ragexe::render
