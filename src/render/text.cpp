#include "render/text.h"

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "core/x87.h"

namespace ragexe::render {

namespace {

constexpr std::int32_t kNormal = 400;
constexpr std::int32_t kBold = 700;  // 400 plus 300
constexpr std::int32_t kVerticalEscapement = -900;
constexpr std::int32_t kLanguages = 6;

// 00605508: the faces by language, their names in the language's own code page.
constexpr std::array<const char*, kLanguages> kFaces = {
    "\261\274\270\262",  // 굴림
    "Arial",
    "\202\154\202\162\040\202\157\203\123\203\126\203\142\203\116",  // ＭＳ Ｐゴシック
    "\313\316\314\345",                                              // 宋体
    "\262\323\251\372\305\351",                                      // 細明體
    "Tahoma",
};
// 006054f0: the same in Latin letters.
constexpr std::array<const char*, kLanguages> kAlternativeFaces = {
    "Gulim", "Arial", "MS Gothic", "SimSun", "MingLiU", "Tahoma",
};
// 00605520: SHIFTJIS 0x80, HANGUL 0x81, GB2312 0x86, CHINESEBIG5 0x88, THAI 0xde.
constexpr std::array<std::uint8_t, kLanguages> kCharsets = {0x81, 0, 0x80, 0x86, 0x88, 0xde};

// 00609f84: each hex digit's value, read with a signed char, so a byte above 0x7f reads the
// 128 bytes before the table (00609f04).
constexpr std::array<std::uint8_t, 256> kHexDigits = {
    0xb8, 0xae, 0xc1, 0xfd, 0xbf, 0xa1, 0x20, 0xb0, 0xa1, 0xb1, 0xee, 0xc0, 0xcc, 0x20, 0xbf, 0xc0,
    0xc1, 0xf6, 0xb8, 0xb6, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0xcb, 0x5e, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2e, 0x3f, 0x41, 0x56, 0x55, 0x49, 0x4d, 0x69, 0x6e, 0x69, 0x47, 0x61,
    0x6d, 0x65, 0x53, 0x63, 0x6f, 0x72, 0x65, 0x57, 0x6e, 0x64, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x25, 0x64, 0xc1, 0xa1, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xcc, 0x20, 0xc1, 0xa1, 0xbc, 0xf6, 0xb8,
    0xa6, 0x20, 0xc0, 0xfa, 0xc0, 0xe5, 0xc7, 0xcf, 0xbd, 0xc3, 0xb0, 0xda, 0xbd, 0xc0, 0xb4, 0xcf,
    0xb1, 0xee, 0x3f, 0x00, 0xc0, 0xd4, 0xb4, 0xcf, 0xb4, 0xd9, 0x2e, 0x00, 0xb4, 0xe7, 0xbd, 0xc5,
    0xc0, 0xc7, 0x20, 0xc1, 0xa1, 0xbc, 0xf6, 0xb4, 0xc2, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

std::uint8_t HexDigit(char c) {
  const std::int32_t index = static_cast<std::int32_t>(c) + 128;
  return kHexDigits[static_cast<std::size_t>(index)];
}

// The string's length up to its first NUL, as strlen reads it.
std::size_t Strlen(const std::string& text) { return std::strlen(text.c_str()); }

std::size_t LengthOr(const std::string& text, std::size_t length) {
  return length != 0 ? length : Strlen(text);
}

void AddRun(std::vector<TextRun>& out, std::int32_t x, std::int32_t y, std::string_view text,
            std::uint32_t color, const FontSpec& font) {
  out.push_back({x, y, std::string(text), color, font});
}

FontSpec Font(std::int32_t language, std::int32_t height, bool bold, bool vertical = false) {
  return {language, height, bold ? kBold : kNormal, vertical};
}

}  // namespace

FontSpec ResolveFont(const FontSpec& request, std::int32_t service) {
  FontSpec out = request;
  if (request.language != 0) return out;
  switch (service) {
    case 1:
    case 6:
    case 7:
      out.language = 1;
      out.height = request.height + 3;
      break;
    case 2:
    case 3:
    case 4: out.language = service; break;
    case 5:
      out.language = 5;
      out.height = request.height + 3;
      break;
    default: break;
  }
  return out;
}

FontRequest RequestFont(const FontSpec& font, const std::function<bool(const char*)>& installed) {
  FontRequest out;
  const auto language = static_cast<std::size_t>(font.language);
  out.height = font.height;
  out.escapement = font.vertical ? kVerticalEscapement : 0;
  out.weight = font.weight;
  out.charset = kCharsets[language];
  if (installed(kFaces[language])) {
    out.face = kFaces[language];
  } else if (installed(kAlternativeFaces[language])) {
    out.face = kAlternativeFaces[language];
  }
  return out;
}

void DrawSceneText(std::int32_t x, std::int32_t y, const std::string& text, std::uint32_t color,
                   std::vector<TextRun>& out) {
  AddRun(out, x, y, std::string_view(text).substr(0, Strlen(text)), color, {0, 12, kNormal});
}

void DrawSceneOutlinedText(std::int32_t x, std::int32_t y, const std::string& text,
                           std::uint32_t color, std::uint32_t outline, const FontSpec& font,
                           std::vector<TextRun>& out) {
  const std::string_view s = std::string_view(text).substr(0, Strlen(text));
  AddRun(out, x - 1, y - 1, s, outline, font);
  AddRun(out, x + 1, y - 1, s, outline, font);
  AddRun(out, x + 1, y + 1, s, outline, font);
  AddRun(out, x - 1, y + 1, s, outline, font);
  AddRun(out, x, y, s, color, font);
}

void DrawWindowText(std::int32_t x, std::int32_t y, const std::string& text, std::size_t length,
                    std::int32_t language, std::int32_t height, std::uint32_t color,
                    std::vector<TextRun>& out) {
  AddRun(out, x, y, std::string_view(text).substr(0, LengthOr(text, length)), color,
         Font(language, height, false));
}

void DrawShadowedText(std::int32_t x, std::int32_t y, const std::string& text, std::size_t length,
                      std::uint32_t color, std::uint32_t shadow, std::int32_t language,
                      std::int32_t height, std::vector<TextRun>& out) {
  const std::string_view s = std::string_view(text).substr(0, LengthOr(text, length));
  const FontSpec font = Font(language, height, false);
  AddRun(out, x + 1, y + 1, s, shadow, font);
  AddRun(out, x, y, s, color, font);
}

void DrawRightAlignedText(std::int32_t x, std::int32_t y, const std::string& text,
                          std::size_t length, std::int32_t language, std::int32_t height,
                          std::uint32_t color, const TextMetrics& metrics,
                          std::vector<TextRun>& out) {
  const FontSpec font = Font(language, height, false);
  const std::string_view whole = std::string_view(text).substr(0, Strlen(text));
  const TextSize size = metrics.Extent(font, whole);
  AddRun(out, x - size.cx, y, std::string_view(text).substr(0, LengthOr(text, length)), color,
         font);
}

std::size_t ParseColorCode(const std::string& text, std::size_t pos, std::uint32_t& color) {
  const auto at = [&](std::size_t k) { return pos + k < text.size() ? text[pos + k] : '\0'; };
  if (at(0) != '^') return pos;
  const auto pair = [&](std::size_t k) {
    return static_cast<std::uint8_t>(HexDigit(at(k)) * 16 + HexDigit(at(k + 1)));
  };
  color = static_cast<std::uint32_t>(pair(5)) << 16 | static_cast<std::uint32_t>(pair(3)) << 8 |
          pair(1);
  return pos + 7;
}

void DrawColorCodedText(std::int32_t x, std::int32_t y, const std::string& text, std::size_t length,
                        std::uint32_t& color, std::int32_t language, std::int32_t height,
                        const TextMetrics& metrics, std::vector<TextRun>& out) {
  const std::size_t end = LengthOr(text, length);
  const FontSpec font = Font(language, height, false);
  std::size_t pos = 0;
  while (pos < end) {
    const std::size_t start = ParseColorCode(text, pos, color);
    // UIWindow::FindColorCode (004a5c70): on to the end or the next '^'.
    std::size_t stop = start;
    while (stop < end && text[stop] != '^') stop = metrics.Next(text, stop);
    pos = stop;
    if (stop == start) continue;
    const std::string_view piece = std::string_view(text).substr(start, stop - start);
    AddRun(out, x, y, piece, color, font);
    x += metrics.Extent(font, piece).cx;
  }
}

void DrawVerticalText(std::int32_t x, std::int32_t y, const std::string& text, std::size_t length,
                      std::int32_t language, std::int32_t height, std::uint32_t color,
                      const TextMetrics& metrics, std::vector<TextRun>& out) {
  const std::size_t end = LengthOr(text, length);
  const FontSpec font = Font(language, height, false);
  std::int32_t widest = 0;
  for (std::size_t pos = 0; pos < end;) {
    const std::size_t next = metrics.Next(text, pos);
    const TextSize size = metrics.Extent(font, std::string_view(text).substr(pos, next - pos));
    if (widest <= size.cx) widest = size.cx;
    pos = next;
  }
  for (std::size_t pos = 0; pos < end;) {
    const std::size_t next = metrics.Next(text, pos);
    const std::string_view c = std::string_view(text).substr(pos, next - pos);
    const TextSize size = metrics.Extent(font, c);
    AddRun(out, (widest - size.cx) / 2 + x, y, c, color, font);
    y += size.cy;
    pos = next;
  }
}

void DrawNameBalloonText(std::int32_t x, std::int32_t y, const std::string& text,
                         std::size_t length, std::uint32_t color, std::uint32_t outline,
                         std::int32_t language, std::int32_t height, bool bold,
                         std::vector<TextRun>& out) {
  const std::string_view s = std::string_view(text).substr(0, LengthOr(text, length));
  const FontSpec font = Font(language, height, bold);
  AddRun(out, x - 1, y, s, outline, font);
  AddRun(out, x + 1, y, s, outline, font);
  AddRun(out, x, y - 1, s, outline, font);
  AddRun(out, x, y + 1, s, outline, font);
  AddRun(out, x, y, s, color, font);
}

void DrawVerticalNameBalloonText(std::int32_t x, std::int32_t y, const std::string& text,
                                 std::size_t length, std::uint32_t color, std::uint32_t outline,
                                 std::int32_t language, std::int32_t height, bool bold,
                                 const TextMetrics& metrics, std::vector<TextRun>& out) {
  const std::string_view s = std::string_view(text).substr(0, LengthOr(text, length));
  const FontSpec font = Font(language, height, bold, true);
  const std::int32_t across = metrics.Extent(font, s).cy + x;
  AddRun(out, across - 1, y, s, outline, font);
  AddRun(out, across + 1, y, s, outline, font);
  AddRun(out, across, y - 1, s, outline, font);
  AddRun(out, across, y + 1, s, outline, font);
  AddRun(out, across, y, s, color, font);
}

std::int32_t TextWidth(const std::string& text, std::size_t length, std::int32_t language,
                       std::int32_t height, bool bold, const TextMetrics& metrics) {
  return metrics
      .Extent(Font(language, height, bold),
              std::string_view(text).substr(0, LengthOr(text, length)))
      .cx;
}

std::int32_t TextHeight(const std::string& text, std::size_t length, std::int32_t language,
                        std::int32_t height, bool bold, const TextMetrics& metrics) {
  return metrics
      .Extent(Font(language, height, bold),
              std::string_view(text).substr(0, LengthOr(text, length)))
      .cy;
}

std::int32_t VerticalTextHeight(const std::string& text, std::size_t length, std::int32_t language,
                                std::int32_t height, bool bold, const TextMetrics& metrics) {
  const std::size_t end = LengthOr(text, length);
  const FontSpec font = Font(language, height, bold);
  std::int32_t sum = 0;
  for (std::size_t pos = 0; pos < end;) {
    const std::size_t next = metrics.Next(text, pos);
    sum += metrics.Extent(font, std::string_view(text).substr(pos, next - pos)).cy;
    pos = next;
  }
  return sum;
}

std::string FitText(const std::string& text, std::int32_t width, std::int32_t language,
                    std::int32_t height, const TextMetrics& metrics) {
  static constexpr std::string_view kEllipsis = "...";  // 00606730
  std::string out = text.substr(0, Strlen(text));
  const FontSpec font = Font(language, height, false);
  std::size_t end = out.size();
  const std::int32_t whole = metrics.Extent(font, out).cx;
  if (width > whole) return out;
  const std::int32_t ellipsis = metrics.Extent(font, kEllipsis).cx;
  std::int32_t total = ellipsis + whole;
  while (width < total) {
    const std::size_t previous = metrics.Previous(out, end);
    end = previous;
    if (previous == 0) break;
    total = ellipsis + metrics.Extent(font, std::string_view(out).substr(0, end)).cx;
  }
  out.resize(end);
  out += kEllipsis;
  return out;
}

void DrawScreenshotStamp(const LocalTime& time, std::int32_t screen_height,
                         std::vector<TextRun>& out) {
  // 0060c1c4 to 0060c188.
  static constexpr std::array<const char*, 7> kDays = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                                       "Thursday", "Friday", "Saturday"};
  const char* day = time.day_of_week < kDays.size() ? kDays[time.day_of_week] : "";
  std::array<char, 128> text{};
  std::snprintf(text.data(), text.size(), "%d/%02d/%02d/%s  %02d: %02d: %02d", time.year,
                time.month, time.day, day, time.hour, time.minute, time.second);
  const std::string stamp = text.data();
  const std::int32_t y = screen_height - 16;
  DrawSceneText(5, y, stamp, 0, out);
  DrawSceneText(3, y, stamp, 0, out);
  DrawSceneText(4, y + 1, stamp, 0, out);
  DrawSceneText(4, y - 1, stamp, 0, out);
  DrawSceneText(4, y, stamp, 0xffffff, out);
}

void StartFrameCount(FrameCount& count, std::uint32_t now) {
  count.frames = 0;
  count.start = now;
}

void DrawFrameRate(FrameCount& count, std::uint32_t now, std::uint32_t queued_textures,
                   std::int32_t width, std::int32_t height, std::vector<TextRun>& out) {
  static constexpr double kMillisecondsPerSecond = 1000.0;  // 005e26c4
  const std::uint32_t elapsed = now - count.start;
  // fild of the count, fmul by a float, fidiv by the time as a signed int.
  const auto scaled =
      static_cast<float>(static_cast<double>(count.frames) * kMillisecondsPerSecond);
  const auto divisor = static_cast<std::int32_t>(elapsed);
  float fps = 0.0f;
  if (divisor != 0) {
    fps = core::RoundedQuotient(scaled, static_cast<double>(divisor));
  } else {
    fps = scaled != 0.0f ? std::numeric_limits<float>::infinity()
                         : std::numeric_limits<float>::quiet_NaN();
  }
  if (elapsed > 1000 && (count.frames & 1) == 0) {
    count.frames >>= 1;
    count.start += elapsed >> 1;
  }
  std::array<char, 128> text{};
  std::snprintf(text.data(), text.size(), "Total : %d - %d", 0, 0);
  DrawSceneText(0, 0, text.data(), 0, out);
  std::snprintf(text.data(), text.size(), "Unused : %" PRId32 " - %" PRId32,
                static_cast<std::int32_t>(queued_textures),
                static_cast<std::int32_t>(queued_textures << 17));
  DrawSceneText(0, 16, text.data(), 0, out);
  DrawSceneText(width - 40, height - 14, FormatTenths(fps) + " fps", 0xffff, out);
}

std::string FormatTenths(float value) {
  if (std::isnan(value)) return "-1.$";
  const bool negative = value < 0.0f;
  if (std::isinf(value)) return negative ? "-1.$" : "1.$";
  // A float times ten is exact in a double, and so is its fraction.
  const double tenths = std::fabs(static_cast<double>(value)) * 10.0;
  double whole = std::floor(tenths);
  if (tenths - whole >= 0.5) whole += 1.0;
  // The tenths as an integer, exact in "%.0f", and the point put before its last digit.
  std::array<char, 64> text{};
  std::snprintf(text.data(), text.size(), "%02.0f", whole);
  std::string digits = text.data();
  digits.insert(digits.size() - 1, ".");
  return negative ? "-" + digits : digits;
}

}  // namespace ragexe::render
