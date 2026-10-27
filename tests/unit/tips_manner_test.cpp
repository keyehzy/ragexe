#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "res/data_files.h"
#include "res/manner.h"
#include "res/tips.h"

using namespace ragexe::res;

namespace {

std::vector<std::uint8_t> Bytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

using Lines = std::vector<std::string>;

}  // namespace

TEST_CASE("tips: text mode, then '\\n' ends a line and '#' a tip") {
  CHECK(ReadAsText(Bytes("a\r\nb\rc\r\r\nd\x1a"
                         "e")) == Bytes("a\nb\rc\r\nd"));
  const auto tips = ParseTips(ReadAsText(Bytes("first\r\n#\r\nsecond\r\n  more\r\n#\r\nopen\r\n")));
  REQUIRE(tips.size() == 2);
  CHECK(tips[0] == Lines{"first", ""});
  // The line break after a '#' starts the next tip with an empty line.
  CHECK(tips[1] == Lines{"", "second", "  more", ""});
  CHECK(GetTip(tips, 1) == &tips[1]);
  CHECK(GetTip(tips, 5) == &tips[0]);
  CHECK(GetTip({}, 0) == nullptr);
  CHECK(TipFileName(1) == "tipOfTheday_e.txt");
  CHECK(TipFileName(2) == "tipOfTheday_j.txt");
  CHECK(TipFileName(0) == "tipOfTheday.txt");
}

TEST_CASE("tips load from beside the client, matched without regard to case") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_tips";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  {
    std::ofstream f(dir / "tipOfTheDay_e.txt", std::ios::binary);
    f << "one\r\n#\r\ntwo\r\n#\r\n";
  }
  const DataFiles files(dir);
  std::vector<Tip> tips;
  REQUIRE(LoadTips(files, 1, tips));
  CHECK(tips.size() == 2);
  CHECK_FALSE(LoadTips(files, 2, tips));
  std::filesystem::remove_all(dir);
}

TEST_CASE("manner: words as the client keeps them") {
  MannerFilter filter;
  filter.Parse(
      Bytes("Fuck\r\n  ass\r\n\t \r\nword \r\none\rtwo\r\nA\xb0"
            "B\r\nlast"));
  // Leading blanks and trailing spaces stay; a blank line is skipped; a CR ends the line;
  // the byte after a lead byte is not lower-cased; the line with no '\n' is dropped.
  const std::string kept = std::string("a\xb0") + "B";
  CHECK(filter.words() == Lines{"fuck", "  ass", "word ", "one", kept});
}

TEST_CASE("manner: IsClean looks for each word at each character") {
  MannerFilter filter;
  filter.Parse(Bytes("fuck\r\nb i t c h\r\n"));
  CHECK(filter.IsClean("hello"));
  CHECK_FALSE(filter.IsClean("you FUCK"));
  CHECK(filter.ContainsBannedWord("B I T C H"));
  // After a lead byte the scan steps two bytes, so a word cannot start on its second.
  CHECK_FALSE(
      filter.IsClean("\xb0\xa1"
                     "fuck"));
  CHECK(
      filter.IsClean("\xb0"
                     "fuck"));
  CHECK(filter.IsClean(""));
}
