#include "render/text.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace ragexe::render;

namespace {

struct FontVector {
  int service, language, height, weight, flag;
  int record[4];  // language, height, weight, flag
  int installed;  // 0 both faces, 1 the alternative, 2 neither
  int args[13];   // CreateFontA's height to pitch and family
  int face;       // 0 the language's, 1 its alternative, -1 none
};

struct RunVector {
  int x, y;
  const char* text;
  std::size_t length;
  std::uint32_t color;
  int font[4];
};

struct TextVector {
  int kind;
  const char* text;
  std::size_t length;
  int args[9];  // x, y, length (or width), language, height, colour, other colour, bold, weight
  std::uint32_t result;
  const RunVector* runs;
  std::size_t run_count;
  std::uint32_t color_after;
};

struct StampVector {
  int time[7];  // year, month, day of the week, day, hour, minute, second
  int height;
  int enabled;
  RunVector runs[5];
};

struct FrameRateVector {
  std::uint32_t frames, start, now, queued;
  int width, height;
  RunVector runs[3];
  std::uint32_t frames_after, start_after;
};

#include "text_vectors.inc"

// tools/text_vectors.py's GetTextExtentPoint32A, and single-byte characters (code page 1252).
class TestMetrics : public TextMetrics {
 public:
  TextSize Extent(const FontSpec& font, std::string_view text) const override {
    std::int32_t sum = 0;
    for (const char c : text) sum += static_cast<unsigned char>(c) % 7 + 4;
    const auto count = static_cast<std::int32_t>(text.size());
    return {sum * font.height / 12 + (font.weight > 400 ? count : 0),
            font.height + 2 + (font.vertical ? 3 : 0) + font.language};
  }
  std::size_t Next(std::string_view text, std::size_t pos) const override {
    return pos < text.size() && text[pos] != '\0' ? pos + 1 : pos;
  }
  std::size_t Previous(std::string_view, std::size_t pos) const override {
    return pos > 0 ? pos - 1 : 0;
  }
};

void CheckRuns(const std::vector<TextRun>& got, const RunVector* runs, std::size_t count) {
  REQUIRE(got.size() == count);
  for (std::size_t i = 0; i < got.size(); ++i) {
    INFO("run " << i);
    const RunVector& want = runs[i];
    CHECK(got[i].x == want.x);
    CHECK(got[i].y == want.y);
    CHECK(got[i].text == std::string(want.text, want.length));
    CHECK(got[i].color == want.color);
    CHECK(got[i].font.language == want.font[0]);
    CHECK(got[i].font.height == want.font[1]);
    CHECK(got[i].font.weight == want.font[2]);
    CHECK(got[i].font.vertical == (want.font[3] != 0));
  }
}

}  // namespace

TEST_CASE("Fonts are resolved and created as the original's", "[text]") {
  static const char* const kFaces[] = {
      "\261\274\270\262",
      "Arial",
      "\202\154\202\162\040\202\157\203\123\203\126\203\142\203\116",
      "\313\316\314\345",
      "\262\323\251\372\305\351",
      "Tahoma"};
  static const char* const kAlternatives[] = {"Gulim",  "Arial",   "MS Gothic",
                                              "SimSun", "MingLiU", "Tahoma"};
  std::size_t n = 0;
  for (const FontVector& v : kFonts) {
    INFO("case " << n++ << ": service " << v.service << ", language " << v.language);
    const FontSpec font = ResolveFont({v.language, v.height, v.weight, v.flag != 0}, v.service);
    CHECK(font.language == v.record[0]);
    CHECK(font.height == v.record[1]);
    CHECK(font.weight == v.record[2]);
    CHECK(font.vertical == (v.record[3] != 0));
    const auto language = static_cast<std::size_t>(font.language);
    const auto installed = [&](const char* face) {
      if (v.installed == 2) return false;
      if (v.installed == 1) return std::strcmp(face, kAlternatives[language]) == 0;
      return true;
    };
    const FontRequest request = RequestFont(font, installed);
    CHECK(request.height == v.args[0]);
    CHECK(v.args[1] == 0);
    CHECK(request.escapement == v.args[2]);
    CHECK(v.args[3] == 0);
    CHECK(request.weight == v.args[4]);
    CHECK(request.charset == v.args[8]);
    if (v.face < 0) {
      CHECK(request.face == nullptr);
    } else {
      REQUIRE(request.face != nullptr);
      CHECK(std::string(request.face) ==
            std::string(v.face == 0 ? kFaces[language] : kAlternatives[language]));
    }
  }
}

TEST_CASE("The text calls make the original's runs", "[text]") {
  const TestMetrics metrics;
  std::size_t n = 0;
  for (const TextVector& v : kTexts) {
    INFO("case " << n++ << ", kind " << v.kind);
    const std::string text(v.text, v.length);
    const auto x = v.args[0];
    const auto y = v.args[1];
    const auto length = static_cast<std::size_t>(v.args[2]);
    const auto language = v.args[3];
    const auto height = v.args[4];
    const auto color = static_cast<std::uint32_t>(v.args[5]);
    const auto other = static_cast<std::uint32_t>(v.args[6]);
    const bool bold = v.args[7] != 0;
    const auto weight = v.args[8];
    std::vector<TextRun> runs;
    switch (v.kind) {
      case 0: DrawSceneText(x, y, text, color, runs); break;
      case 1:
        DrawSceneOutlinedText(x, y, text, color, other, {language, height, weight, false}, runs);
        break;
      case 2: DrawWindowText(x, y, text, length, language, height, color, runs); break;
      case 3: DrawShadowedText(x, y, text, length, color, other, language, height, runs); break;
      case 4:
        DrawRightAlignedText(x, y, text, length, language, height, color, metrics, runs);
        break;
      case 5: {
        std::uint32_t c = color;
        DrawColorCodedText(x, y, text, length, c, language, height, metrics, runs);
        CHECK(c == v.color_after);
        break;
      }
      case 6: DrawVerticalText(x, y, text, length, language, height, color, metrics, runs); break;
      case 7:
        DrawNameBalloonText(x, y, text, length, color, other, language, height, bold, runs);
        break;
      case 8:
        DrawVerticalNameBalloonText(x, y, text, length, color, other, language, height, bold,
                                    metrics, runs);
        break;
      case 9:
        CHECK(static_cast<std::uint32_t>(
                  TextWidth(text, length, language, height, bold, metrics)) == v.result);
        continue;
      case 10:
        CHECK(static_cast<std::uint32_t>(
                  TextHeight(text, length, language, height, bold, metrics)) == v.result);
        continue;
      case 11:
        CHECK(static_cast<std::uint32_t>(
                  VerticalTextHeight(text, length, language, height, bold, metrics)) == v.result);
        continue;
      default: {
        REQUIRE(v.run_count == 1);
        CHECK(FitText(text, v.args[2], language, height, metrics) ==
              std::string(v.runs[0].text, v.runs[0].length));
        continue;
      }
    }
    CheckRuns(runs, v.runs, v.run_count);
  }
}

TEST_CASE("The screenshot's stamp is the original's", "[text]") {
  std::size_t n = 0;
  for (const StampVector& v : kStamps) {
    INFO("case " << n++);
    std::vector<TextRun> runs;
    if (v.enabled != 0) {
      const LocalTime time{
          static_cast<std::uint16_t>(v.time[0]), static_cast<std::uint16_t>(v.time[1]),
          static_cast<std::uint16_t>(v.time[2]), static_cast<std::uint16_t>(v.time[3]),
          static_cast<std::uint16_t>(v.time[4]), static_cast<std::uint16_t>(v.time[5]),
          static_cast<std::uint16_t>(v.time[6])};
      DrawScreenshotStamp(time, v.height, runs);
    }
    CheckRuns(runs, v.runs, v.enabled != 0 ? 5 : 0);
  }
}

TEST_CASE("The frame rate is drawn as the original's", "[text]") {
  std::size_t n = 0;
  for (const FrameRateVector& v : kFrameRates) {
    INFO("case " << n++ << ": " << v.frames << " frames in " << v.now - v.start << " ms");
    FrameCount count{v.frames, v.start};
    std::vector<TextRun> runs;
    DrawFrameRate(count, v.now, v.queued, v.width, v.height, runs);
    CheckRuns(runs, v.runs, 3);
    CHECK(count.frames == v.frames_after);
    CHECK(count.start == v.start_after);
  }
  FrameCount count{7, 7};
  StartFrameCount(count, 1234);
  CHECK(count.frames == 0);
  CHECK(count.start == 1234);
}

TEST_CASE("Tenths are printed as the client's C runtime prints them", "[text]") {
  CHECK(FormatTenths(0.0f) == "0.0");
  CHECK(FormatTenths(-0.0f) == "0.0");
  CHECK(FormatTenths(-0.01f) == "-0.0");
  CHECK(FormatTenths(-0.05f) == "-0.1");
  CHECK(FormatTenths(0.25f) == "0.3");
  CHECK(FormatTenths(-0.25f) == "-0.3");
  CHECK(FormatTenths(0.05f) == "0.1");  // just over 0.05
  CHECK(FormatTenths(0.35f) == "0.3");  // just under 0.35
  CHECK(FormatTenths(9.96f) == "10.0");
  CHECK(FormatTenths(std::numeric_limits<float>::infinity()) == "1.$");
  CHECK(FormatTenths(-std::numeric_limits<float>::infinity()) == "-1.$");
  CHECK(FormatTenths(std::numeric_limits<float>::quiet_NaN()) == "-1.$");
  CHECK(FormatTenths(3.4e38f) == "339999995214436424907732413799364296704.0");
}
