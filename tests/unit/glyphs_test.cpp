#include "render/glyphs.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace ragexe::render;

namespace {

constexpr std::int32_t kWidth = 160;
constexpr std::int32_t kHeight = 120;
constexpr std::size_t kPixels = std::size_t{160} * 120;
// "Ragnarok 1.$ fps" at (3, 40) in iRO's default font, regular and bold.
constexpr std::size_t kRegularPixels = 272;
constexpr std::int64_t kRegularSumX = 13672;
constexpr std::int64_t kRegularSumY = 12960;
constexpr std::size_t kBoldPixels = 389;
constexpr std::int64_t kBoldSumX = 20592;
constexpr std::int64_t kBoldSumY = 18541;

using Pixels = std::set<std::pair<std::int32_t, std::int32_t>>;

Pixels DrawOne(const GlyphRenderer& glyphs, const TextRun& run, std::uint32_t* color = nullptr) {
  std::vector<std::uint32_t> argb(kPixels, 0);
  glyphs.Draw(run, kWidth, kHeight, argb);
  Pixels set;
  for (std::int32_t y = 0; y < kHeight; ++y) {
    for (std::int32_t x = 0; x < kWidth; ++x) {
      const std::uint32_t texel =
          argb[static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)];
      if (texel == 0) continue;
      set.emplace(x, y);
      if (color != nullptr) *color = texel;
    }
  }
  return set;
}

}  // namespace

TEST_CASE("Glyphs measure as GDI's cells", "[glyphs]") {
  const auto glyphs = GlyphRenderer::Create(1);
  REQUIRE(glyphs != nullptr);
  // iRO's service makes language 0 Arial three pixels taller.
  CHECK(glyphs->Extent({0, 12, 400, false}, "").cx == 0);
  CHECK(glyphs->Extent({0, 12, 400, false}, "").cy == 15);
  CHECK(glyphs->Extent({1, 12, 400, false}, "x").cy == 12);
  CHECK(glyphs->Extent({1, -13, 400, false}, "x").cy == 13);

  const std::string text = "Total : 0 - 0 fps";
  for (const std::int32_t weight : {400, 700}) {
    const FontSpec font{0, 12, weight, false};
    std::int32_t sum = 0;
    for (const char c : text) sum += glyphs->Extent(font, std::string(1, c)).cx;
    CHECK(glyphs->Extent(font, text).cx == sum);
    CHECK(sum > 0);
  }
  CHECK(glyphs->Extent({0, 12, 700, false}, "Wednesday").cx >
        glyphs->Extent({0, 12, 400, false}, "Wednesday").cx);
  CHECK(glyphs->Extent({0, 12, 551, false}, "Wednesday").cx ==
        glyphs->Extent({0, 12, 700, false}, "Wednesday").cx);
  CHECK(glyphs->Extent({0, 12, 550, false}, "Wednesday").cx ==
        glyphs->Extent({0, 12, 400, false}, "Wednesday").cx);
  CHECK(glyphs->Extent({0, 16, 400, false}, "Wednesday").cx >
        glyphs->Extent({0, 12, 400, false}, "Wednesday").cx);
}

TEST_CASE("Glyphs step a byte at a time", "[glyphs]") {
  const auto glyphs = GlyphRenderer::Create(1);
  REQUIRE(glyphs != nullptr);
  const std::string text("ab\0c", 4);
  CHECK(glyphs->Next(text, 0) == 1);
  CHECK(glyphs->Next(text, 2) == 2);
  CHECK(glyphs->Next(text, 4) == 4);
  CHECK(glyphs->Previous(text, 3) == 2);
  CHECK(glyphs->Previous(text, 0) == 0);
}

TEST_CASE("Glyphs are drawn in their cell, in the run's colour", "[glyphs]") {
  const auto glyphs = GlyphRenderer::Create(1);
  REQUIRE(glyphs != nullptr);
  const FontSpec font{0, 12, 400, false};
  const TextRun run{20, 30, "Hdg|", 0x00ffff, font};
  std::uint32_t color = 0;
  const Pixels set = DrawOne(*glyphs, run, &color);
  REQUIRE(!set.empty());
  CHECK(color == 0xffffff00u);
  const TextSize size = glyphs->Extent(font, run.text);
  std::int32_t top = kHeight, bottom = 0;
  for (const auto& [x, y] : set) {
    CHECK(x >= run.x);
    CHECK(x < run.x + size.cx);
    CHECK(y >= run.y);
    CHECK(y < run.y + size.cy);
    top = std::min(top, y);
    bottom = std::max(bottom, y);
  }
  // The ascenders and the descenders reach near the cell's edges.
  CHECK(top <= run.y + 3);
  CHECK(bottom >= run.y + size.cy - 3);

  // Moved by a pixel, the same pixels a pixel over.
  Pixels moved;
  for (const auto& [x, y] : DrawOne(*glyphs, {21, 29, run.text, run.color, font}))
    moved.emplace(x - 1, y + 1);
  CHECK(moved == set);

  // Off the edges, only what is on the screen.
  const Pixels clipped = DrawOne(*glyphs, {-5, -4, run.text, run.color, font});
  for (const auto& [x, y] : set)
    CHECK(clipped.contains({x - 25, y - 34}) == (x - 25 >= 0 && y - 34 >= 0));
  for (std::int32_t edge = 1; edge <= 24; ++edge) {
    INFO("at " << edge << " from the right and bottom edges");
    const std::int32_t dx = kWidth - edge - run.x;
    const std::int32_t dy = kHeight - edge - run.y;
    const Pixels far = DrawOne(*glyphs, {run.x + dx, run.y + dy, run.text, run.color, font});
    std::size_t inside = 0;
    for (const auto& [x, y] : set) {
      const bool on = x + dx < kWidth && y + dy < kHeight;
      CHECK(far.contains({x + dx, y + dy}) == on);
      inside += on ? 1 : 0;
    }
    CHECK(far.size() == inside);
  }
}

TEST_CASE("Glyphs are the pinned FreeType's and Liberation Sans's", "[glyphs]") {
  const auto glyphs = GlyphRenderer::Create(1);
  REQUIRE(glyphs != nullptr);
  // An L: its stem the fullest column, at the left; its foot the fullest row, at the bottom.
  const Pixels l = DrawOne(*glyphs, {10, 10, "L", 0, {1, 20, 400, false}});
  REQUIRE(!l.empty());
  std::map<std::int32_t, int> columns, rows;
  for (const auto& [x, y] : l) {
    ++columns[x];
    ++rows[y];
  }
  const auto fullest = [](const std::map<std::int32_t, int>& counts) {
    return std::max_element(counts.begin(), counts.end(),
                            [](const auto& a, const auto& b) { return a.second < b.second; })
        ->first;
  };
  CHECK(fullest(columns) == columns.begin()->first);
  CHECK(fullest(rows) == rows.rbegin()->first);

  // The baseline: the share of the cell usWinAscent (1854 of 1854 + 434) takes, rounded; an
  // H stands on it.
  for (std::int32_t height = 8; height <= 24; ++height) {
    INFO("height " << height);
    const Pixels h = DrawOne(*glyphs, {10, 10, "H", 0, {1, height, 400, false}});
    REQUIRE(!h.empty());
    const std::int32_t ascent = (2 * 1854 * height + 2288) / (2 * 2288);
    CHECK(h.rbegin()->second == 10 + ascent - 1);
  }

  // A line in each face, summed.
  for (const std::int32_t weight : {400, 700}) {
    INFO("weight " << weight);
    const Pixels set = DrawOne(*glyphs, {3, 40, "Ragnarok 1.$ fps", 0, {0, 12, weight, false}});
    std::int64_t sum_x = 0, sum_y = 0;
    for (const auto& [x, y] : set) {
      sum_x += x;
      sum_y += y;
    }
    CHECK(set.size() == (weight == 400 ? kRegularPixels : kBoldPixels));
    CHECK(sum_x == (weight == 400 ? kRegularSumX : kBoldSumX));
    CHECK(sum_y == (weight == 400 ? kRegularSumY : kBoldSumY));
  }
}

TEST_CASE("Vertical glyphs are the cell turned a quarter clockwise", "[glyphs]") {
  const auto glyphs = GlyphRenderer::Create(1);
  REQUIRE(glyphs != nullptr);
  const TextRun across{40, 20, "Ragnarok", 0xff, {0, 12, 700, false}};
  const TextRun down{40, 20, "Ragnarok", 0xff, {0, 12, 700, true}};
  Pixels turned;
  for (const auto& [x, y] : DrawOne(*glyphs, across))
    turned.emplace(across.x - (y - across.y) - 1, across.y + (x - across.x));
  CHECK(!turned.empty());
  CHECK(DrawOne(*glyphs, down) == turned);
}

TEST_CASE("Code page 1252 and COLORREFs", "[glyphs]") {
  CHECK(Cp1252ToUnicode('A') == U'A');
  CHECK(Cp1252ToUnicode(0x7f) == 0x7f);
  CHECK(Cp1252ToUnicode(0x80) == 0x20ac);
  CHECK(Cp1252ToUnicode(0x81) == 0x81);
  CHECK(Cp1252ToUnicode(0x85) == 0x2026);
  CHECK(Cp1252ToUnicode(0x9f) == 0x178);
  CHECK(Cp1252ToUnicode(0xa0) == 0xa0);
  CHECK(Cp1252ToUnicode(0xe9) == 0xe9);
  CHECK(Cp1252ToUnicode(0xff) == 0xff);
  CHECK(ColorRefToArgb(0x00000000) == 0xff000000u);
  CHECK(ColorRefToArgb(0x000000ff) == 0xffff0000u);
  CHECK(ColorRefToArgb(0x0000ff00) == 0xff00ff00u);
  CHECK(ColorRefToArgb(0x00ff0000) == 0xff0000ffu);
  CHECK(ColorRefToArgb(0xff123456) == 0xff563412u);
}
