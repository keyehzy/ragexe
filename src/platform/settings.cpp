#include "platform/settings.h"

#include <charconv>
#include <fstream>
#include <sstream>

namespace ragexe::platform {
namespace {

std::string_view Trim(std::string_view s) {
  const auto first = s.find_first_not_of(" \t\r");
  if (first == std::string_view::npos) return {};
  const auto last = s.find_last_not_of(" \t\r");
  return s.substr(first, last - first + 1);
}

bool ParseInt(std::string_view s, int& out) {
  int value = 0;
  const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (error != std::errc() || end != s.data() + s.size()) return false;
  out = value;
  return true;
}

}  // namespace

Settings ParseSettings(std::string_view text) {
  Settings settings;
  while (!text.empty()) {
    const auto newline = text.find('\n');
    std::string_view line = text.substr(0, newline);
    text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);

    if (const auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
    const auto equals = line.find('=');
    if (equals == std::string_view::npos) continue;
    const std::string_view key = Trim(line.substr(0, equals));
    const std::string_view value = Trim(line.substr(equals + 1));

    int number = 0;
    if (!ParseInt(value, number)) continue;
    if (key == "ISFULLSCREENMODE") {
      settings.fullscreen = number != 0;
    } else if (key == "WIDTH" && number > 0) {
      settings.width = number;
    } else if (key == "HEIGHT" && number > 0) {
      settings.height = number;
    } else if (key == "BITPERPIXEL" && (number == 16 || number == 32)) {
      settings.bits_per_pixel = number;
    }
  }
  return settings;
}

std::string FormatSettings(const Settings& settings) {
  std::ostringstream out;
  out << "ISFULLSCREENMODE=" << (settings.fullscreen ? 1 : 0) << '\n'
      << "WIDTH=" << settings.width << '\n'
      << "HEIGHT=" << settings.height << '\n'
      << "BITPERPIXEL=" << settings.bits_per_pixel << '\n';
  return out.str();
}

Settings LoadSettings(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream text;
  text << in.rdbuf();
  return ParseSettings(text.str());
}

bool SaveSettings(const std::filesystem::path& path, const Settings& settings) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << FormatSettings(settings);
  return static_cast<bool>(out);
}

}  // namespace ragexe::platform
