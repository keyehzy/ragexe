#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ragexe::platform {

// The video settings the original reads from the registry in LoadVideoSettings
// (ISFULLSCREENMODE, WIDTH, HEIGHT, BITPERPIXEL). Here they live in a text file of
// KEY=value lines in the user's data directory; '#' starts a comment.
struct Settings {
  bool fullscreen = false;
  int width = 640;
  int height = 480;
  int bits_per_pixel = 16;
};

// Unknown keys and malformed lines are skipped; missing keys keep their defaults.
Settings ParseSettings(std::string_view text);
std::string FormatSettings(const Settings& settings);

// A missing or unreadable file gives the defaults.
Settings LoadSettings(const std::filesystem::path& path);
bool SaveSettings(const std::filesystem::path& path, const Settings& settings);

}  // namespace ragexe::platform
