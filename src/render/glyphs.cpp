#include "render/glyphs.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include "embedded_fonts.h"

namespace ragexe::render {

namespace {

constexpr std::int32_t kBoldAbove = 550;

// Code page 1252's bytes 0x80 to 0x9f (the Unicode Consortium's CP1252.TXT).
constexpr std::array<char16_t, 32> kCp1252High = {
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008d, 0x017d, 0x008f, 0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
    0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
};

// A rendered glyph: where its bitmap lies from the pen on the baseline, and a byte a pixel.
struct Glyph {
  std::int32_t left = 0;  // right of the pen
  std::int32_t top = 0;   // above the baseline
  std::int32_t advance = 0;
  std::int32_t width = 0;
  std::int32_t rows = 0;
  std::vector<std::uint8_t> bits;  // 1 where the glyph covers the pixel
};

}  // namespace

struct GlyphRenderer::Faces {
  FT_Library library = nullptr;
  std::array<FT_Face, 2> faces{};  // regular, bold
  std::array<std::int32_t, 2> sized{};
  std::array<std::pair<std::int32_t, std::int32_t>, 2> win_metrics{};  // usWinAscent, usWinDescent
  std::int32_t service = 0;
  std::map<std::tuple<std::size_t, std::int32_t, char32_t>, Glyph> glyphs;

  Faces() = default;
  Faces(const Faces&) = delete;
  Faces& operator=(const Faces&) = delete;
  Faces(Faces&&) = delete;
  Faces& operator=(Faces&&) = delete;
  ~Faces() {
    for (FT_Face face : faces)
      if (face != nullptr) FT_Done_Face(face);
    if (library != nullptr) FT_Done_FreeType(library);
  }

  // The face and cell height a font asks for.
  std::pair<std::size_t, std::int32_t> Pick(const FontSpec& asked) const {
    const FontSpec font = ResolveFont(asked, service);
    return {font.weight > kBoldAbove ? 1 : 0, std::max(1, std::abs(font.height))};
  }

  std::int32_t Ascent(std::size_t face, std::int32_t height) const {
    const auto [ascent, descent] = win_metrics[face];
    const std::int32_t total = ascent + descent;
    if (total <= 0) return height;
    return static_cast<std::int32_t>((2 * static_cast<std::int64_t>(ascent) * height + total) /
                                     (2 * static_cast<std::int64_t>(total)));
  }

  const Glyph& Get(std::size_t face_index, std::int32_t height, char32_t code) {
    const auto key = std::make_tuple(face_index, height, code);
    const auto found = glyphs.find(key);
    if (found != glyphs.end()) return found->second;
    Glyph& glyph = glyphs[key];
    FT_Face face = faces[face_index];
    if (sized[face_index] != height) {
      FT_Size_RequestRec request{};
      request.type = FT_SIZE_REQUEST_TYPE_CELL;
      request.height = static_cast<FT_Long>(height) * 64;
      if (FT_Request_Size(face, &request) != 0) return glyph;
      sized[face_index] = height;
    }
    if (FT_Load_Char(face, code, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO | FT_LOAD_MONOCHROME) != 0)
      return glyph;
    const FT_GlyphSlot slot = face->glyph;
    const FT_Bitmap& bitmap = slot->bitmap;
    glyph.left = slot->bitmap_left;
    glyph.top = slot->bitmap_top;
    glyph.advance = static_cast<std::int32_t>(slot->advance.x >> 6);  // whole pixels, hinted
    if (bitmap.pixel_mode != FT_PIXEL_MODE_MONO) return glyph;
    glyph.width = static_cast<std::int32_t>(bitmap.width);
    glyph.rows = static_cast<std::int32_t>(bitmap.rows);
    glyph.bits.resize(static_cast<std::size_t>(glyph.width) * static_cast<std::size_t>(glyph.rows));
    for (std::int32_t y = 0; y < glyph.rows; ++y) {
      const unsigned char* row = bitmap.buffer + static_cast<std::ptrdiff_t>(y) * bitmap.pitch;
      for (std::int32_t x = 0; x < glyph.width; ++x)
        glyph.bits[static_cast<std::size_t>(y) * static_cast<std::size_t>(glyph.width) +
                   static_cast<std::size_t>(x)] =
            static_cast<std::uint8_t>((row[x >> 3] >> (7 - (x & 7))) & 1);
    }
    return glyph;
  }
};

std::unique_ptr<GlyphRenderer> GlyphRenderer::Create(std::int32_t service) {
  auto faces = std::make_unique<Faces>();
  faces->service = service;
  if (FT_Init_FreeType(&faces->library) != 0) return nullptr;
  const std::array<std::pair<const unsigned char*, std::size_t>, 2> files = {
      std::pair{kSansRegular, kSansRegular_size}, std::pair{kSansBold, kSansBold_size}};
  for (std::size_t i = 0; i < files.size(); ++i) {
    if (FT_New_Memory_Face(faces->library, files[i].first, static_cast<FT_Long>(files[i].second), 0,
                           &faces->faces[i]) != 0)
      return nullptr;
    const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(faces->faces[i], FT_SFNT_OS2));
    if (os2 == nullptr) return nullptr;
    faces->win_metrics[i] = {os2->usWinAscent, os2->usWinDescent};
  }
  return std::unique_ptr<GlyphRenderer>(new GlyphRenderer(std::move(faces)));
}

GlyphRenderer::GlyphRenderer(std::unique_ptr<Faces> faces) : faces_(std::move(faces)) {}

GlyphRenderer::~GlyphRenderer() = default;

TextSize GlyphRenderer::Extent(const FontSpec& font, std::string_view text) const {
  const auto [face, height] = faces_->Pick(font);
  std::int32_t cx = 0;
  for (const char c : text)
    cx += faces_->Get(face, height, Cp1252ToUnicode(static_cast<unsigned char>(c))).advance;
  return {cx, height};
}

std::size_t GlyphRenderer::Next(std::string_view text, std::size_t pos) const {
  return pos < text.size() && text[pos] != '\0' ? pos + 1 : pos;
}

std::size_t GlyphRenderer::Previous(std::string_view, std::size_t pos) const {
  return pos > 0 ? pos - 1 : 0;
}

void GlyphRenderer::Draw(const TextRun& run, std::int32_t width, std::int32_t height,
                         std::span<std::uint32_t> argb) const {
  const auto [face, cell] = faces_->Pick(run.font);
  const std::int32_t ascent = faces_->Ascent(face, cell);
  const std::uint32_t color = ColorRefToArgb(run.color);
  std::int32_t pen = 0;
  for (const char c : run.text) {
    const Glyph& glyph = faces_->Get(face, cell, Cp1252ToUnicode(static_cast<unsigned char>(c)));
    for (std::int32_t row = 0; row < glyph.rows; ++row) {
      for (std::int32_t col = 0; col < glyph.width; ++col) {
        if (glyph.bits[static_cast<std::size_t>(row) * static_cast<std::size_t>(glyph.width) +
                       static_cast<std::size_t>(col)] == 0)
          continue;
        // u along the text, v down its cell from the top.
        const std::int32_t u = pen + glyph.left + col;
        const std::int32_t v = ascent - glyph.top + row;
        const std::int32_t x = run.font.vertical ? run.x - v - 1 : run.x + u;
        const std::int32_t y = run.font.vertical ? run.y + u : run.y + v;
        if (x < 0 || y < 0 || x >= width || y >= height) continue;
        argb[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)] = color;
      }
    }
    pen += glyph.advance;
  }
}

char32_t Cp1252ToUnicode(unsigned char c) {
  if (c >= 0x80 && c < 0xa0) return kCp1252High[c - 0x80];
  return c;
}

std::uint32_t ColorRefToArgb(std::uint32_t color) {
  return 0xff000000u | ((color & 0xff) << 16) | (color & 0xff00) | ((color >> 16) & 0xff);
}

}  // namespace ragexe::render
