#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace ragexe::render {

// The text the client draws with GDI onto surfaces (docs/render/text.md): which font each
// call asks for and the TextOutA runs it makes. The glyphs themselves are the font's.

// A font as GetSurfaceDCWithFont (00407680) asks for it: the language, height, weight and
// whether it is turned for vertical text.
struct FontSpec {
  std::int32_t language = 0;
  std::int32_t height = 0;
  std::int32_t weight = 0;
  bool vertical = false;
};

// FontEntry::FontEntry (004077d0): a language of 0 becomes the service's (0067c9d8): 1, 6 and
// 7 the language 1 with the height 3 more, 2 to 4 their own, 5 its own with the height 3 more,
// and any other stays 0.
FontSpec ResolveFont(const FontSpec& request, std::int32_t service);

// What FontEntry::CreateFont (00407870) passes CreateFontA.
struct FontRequest {
  std::int32_t height = 0;
  std::int32_t escapement = 0;  // -900 for vertical text, 0 otherwise
  std::int32_t weight = 0;
  std::uint8_t charset = 0;  // FontEntry::GetCharSet (00407a30), 00605520
  // FontEntry::GetInstalledFaceName (00407980): the language's face (00605508) when it is
  // installed, else its alternative (006054f0), else none.
  const char* face = nullptr;
};

FontRequest RequestFont(const FontSpec& font, const std::function<bool(const char*)>& installed);

// The extent of text in a font, GetTextExtentPoint32A's.
struct TextSize {
  std::int32_t cx = 0;
  std::int32_t cy = 0;
};

// What the text functions ask of the system: extents, and stepping over characters on the
// client's code page (0067c998) with CharNextExA and CharPrevExA.
class TextMetrics {
 public:
  virtual ~TextMetrics() = default;
  virtual TextSize Extent(const FontSpec& font, std::string_view text) const = 0;
  // The offset after the character at pos, pos itself at the end.
  virtual std::size_t Next(std::string_view text, std::size_t pos) const = 0;
  // The offset of the character before pos, 0 at the start.
  virtual std::size_t Previous(std::string_view text, std::size_t pos) const = 0;
};

// One TextOutA: where, what and in which colour (a COLORREF), and the font asked for.
struct TextRun {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::string text;
  std::uint32_t color = 0;
  FontSpec font;
};

// A length of 0 stands for the whole text (strlen) in the calls below that take one.

// SceneRenderer::DrawText (00410df0): text on the back buffer in the default font (language 0,
// height 12, weight 400).
void DrawSceneText(std::int32_t x, std::int32_t y, const std::string& text, std::uint32_t color,
                   std::vector<TextRun>& out);

// SceneRenderer::DrawOutlinedText (00410eb0): the text four times in the outline colour, a pixel
// off on each diagonal (up left, up right, down right, down left), then in its colour.
void DrawSceneOutlinedText(std::int32_t x, std::int32_t y, const std::string& text,
                           std::uint32_t color, std::uint32_t outline, const FontSpec& font,
                           std::vector<TextRun>& out);

// UIWindow::DrawText (004a5cb0): text in a window, weight 400.
void DrawWindowText(std::int32_t x, std::int32_t y, const std::string& text, std::size_t length,
                    std::int32_t language, std::int32_t height, std::uint32_t color,
                    std::vector<TextRun>& out);

// UIWindow::DrawShadowedText (004a5ea0): the text a pixel down and right in the shadow's colour,
// then in its own.
void DrawShadowedText(std::int32_t x, std::int32_t y, const std::string& text, std::size_t length,
                      std::uint32_t color, std::uint32_t shadow, std::int32_t language,
                      std::int32_t height, std::vector<TextRun>& out);

// UIFrameWnd::DrawRightAlignedText (004a5d20): the text ending at x. The width is the whole text's,
// whatever the length drawn.
void DrawRightAlignedText(std::int32_t x, std::int32_t y, const std::string& text,
                          std::size_t length, std::int32_t language, std::int32_t height,
                          std::uint32_t color, const TextMetrics& metrics,
                          std::vector<TextRun>& out);

// UIWindow::ParseColorCode (004a5bf0): at a '^', the six hex digits after it as red, green
// and blue into color, and the offset past them; otherwise pos.
std::size_t ParseColorCode(const std::string& text, std::size_t pos, std::uint32_t& color);

// UIListBox::DrawColorCodedText (004a5dc0): text with colour codes, each piece between them drawn
// in the colour last given, each after the last; color starts as the caller's and is left as the
// last code's.
void DrawColorCodedText(std::int32_t x, std::int32_t y, const std::string& text, std::size_t length,
                        std::uint32_t& color, std::int32_t language, std::int32_t height,
                        const TextMetrics& metrics, std::vector<TextRun>& out);

// DrawVerticalText (004a6010): the text a character a line, each centred on the widest.
void DrawVerticalText(std::int32_t x, std::int32_t y, const std::string& text, std::size_t length,
                      std::int32_t language, std::int32_t height, std::uint32_t color,
                      const TextMetrics& metrics, std::vector<TextRun>& out);

// UINameBalloonText::DrawOutlinedText (004a5f30): the text four times in the outline colour, a
// pixel left, right, up and down, then in its colour; weight 700 when bold, else 400.
void DrawNameBalloonText(std::int32_t x, std::int32_t y, const std::string& text,
                         std::size_t length, std::uint32_t color, std::uint32_t outline,
                         std::int32_t language, std::int32_t height, bool bold,
                         std::vector<TextRun>& out);

// UIVerticalNameBalloonText::DrawOutlinedText (004a6130): the same in the vertical font, moved
// right by the text's height.
void DrawVerticalNameBalloonText(std::int32_t x, std::int32_t y, const std::string& text,
                                 std::size_t length, std::uint32_t color, std::uint32_t outline,
                                 std::int32_t language, std::int32_t height, bool bold,
                                 const TextMetrics& metrics, std::vector<TextRun>& out);

// UIWindow::GetTextWidth (004a6280) and UIBalloonText::GetTextHeight (004a6310): the text's width
// and height.
std::int32_t TextWidth(const std::string& text, std::size_t length, std::int32_t language,
                       std::int32_t height, bool bold, const TextMetrics& metrics);
std::int32_t TextHeight(const std::string& text, std::size_t length, std::int32_t language,
                        std::int32_t height, bool bold, const TextMetrics& metrics);

// UIVerticalNameBalloonText::GetVerticalTextHeight (004a63a0): the heights of the text's characters
// summed.
std::int32_t VerticalTextHeight(const std::string& text, std::size_t length, std::int32_t language,
                                std::int32_t height, bool bold, const TextMetrics& metrics);

// FitTextWithEllipsis (004a6460): the text, or when it is at least width wide, as many of its
// characters as fit with "..." (00606730) after them, or "..." alone.
std::string FitText(const std::string& text, std::int32_t width, std::int32_t language,
                    std::int32_t height, const TextMetrics& metrics);

}  // namespace ragexe::render
