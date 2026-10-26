#include "platform/settings.h"

#include <catch2/catch_test_macros.hpp>

using ragexe::platform::FormatSettings;
using ragexe::platform::ParseSettings;
using ragexe::platform::Settings;

TEST_CASE("Missing settings keep the defaults") {
  const Settings s = ParseSettings("");
  CHECK_FALSE(s.fullscreen);
  CHECK(s.width == 640);
  CHECK(s.height == 480);
  CHECK(s.bits_per_pixel == 16);
}

TEST_CASE("The registry's value names are read, with comments and spacing") {
  const Settings s = ParseSettings(
      "# video\n"
      "ISFULLSCREENMODE = 1\r\n"
      "WIDTH=1024   # wide\n"
      "HEIGHT=768\n"
      "BITPERPIXEL=32\n");
  CHECK(s.fullscreen);
  CHECK(s.width == 1024);
  CHECK(s.height == 768);
  CHECK(s.bits_per_pixel == 32);
}

TEST_CASE("Malformed lines and out-of-range values are skipped") {
  const Settings s = ParseSettings("WIDTH\nHEIGHT=tall\nWIDTH=-5\nBITPERPIXEL=24\nUNKNOWN=3\n");
  CHECK(s.width == 640);
  CHECK(s.height == 480);
  CHECK(s.bits_per_pixel == 16);
}

TEST_CASE("Format and parse round-trip") {
  Settings s;
  s.fullscreen = true;
  s.width = 800;
  s.height = 600;
  s.bits_per_pixel = 32;
  const Settings back = ParseSettings(FormatSettings(s));
  CHECK(back.fullscreen == s.fullscreen);
  CHECK(back.width == s.width);
  CHECK(back.height == s.height);
  CHECK(back.bits_per_pixel == s.bits_per_pixel);
}
