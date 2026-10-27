#include "res/client_tables.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "res/vc6_sort.h"

using namespace ragexe::res;

namespace {

std::vector<std::uint8_t> Bytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

}  // namespace

TEST_CASE("name pairs: two names a record, sorted by the first") {
  CHECK(ParseNamePairs(Bytes("prontera.rsw#Prontera#\r\ngeffen.rsw#Geffen#\r\n")) ==
        NamePairs{{"geffen.rsw", "Geffen"}, {"prontera.rsw", "Prontera"}});
  // Bytes above 0x7f sort after ASCII.
  CHECK(ParseNamePairs(Bytes("\xb0#x#a#y#")) == NamePairs{{"a", "y"}, {"\xb0", "x"}});
}

TEST_CASE("name pairs: line breaks empty only the name being read") {
  // The break after "value" empties the second buffer, so the next '#' finds it empty and
  // only puts a NUL in it; the '#' after "lost" ends the pair, whose second name stops at
  // that NUL.
  CHECK(ParseNamePairs(Bytes("key#value\r\n#lost#")) == NamePairs{{"key", ""}});
  CHECK(ParseNamePairs(Bytes("ke\ny#v#")) == NamePairs{{"y", "v"}});
  // A '#' with the second buffer still empty puts a NUL there, which ends the name.
  CHECK(ParseNamePairs(Bytes("k##v#")) == NamePairs{{"k", ""}});
  CHECK(ParseNamePairs(Bytes("no hash\r\nonly#one")).empty());
}

TEST_CASE("the VC6 sort leaves ties as its quicksort and insertions do") {
  // Twenty equal keys tagged by position: more than 16, so the quicksort runs first.
  std::vector<std::pair<int, int>> v;
  v.reserve(20);
  for (int k = 0; k < 20; ++k) v.emplace_back(k % 2, k);
  Vc6Sort(v.data(), v.data() + v.size(),
          [](const auto& a, const auto& b) { return a.first < b.first; });
  for (std::size_t k = 1; k < v.size(); ++k) CHECK(v[k - 1].first <= v[k].first);
  // Not stable: the partition swaps equal elements across the pivot.
  std::vector<int> zeros;
  for (const auto& [key, tag] : v) {
    if (key == 0) zeros.push_back(tag);
  }
  CHECK(zeros != std::vector<int>{0, 2, 4, 6, 8, 10, 12, 14, 16, 18});
}

TEST_CASE("number records: a name, numbers, '@', sorted by name") {
  const NumberRecords records =
      ParseNumberRecords(Bytes("SM_BASH#\r\n8#\r\n15#\r\n@\r\nMG_SIGHT#\r\n10#\r\n@\r\n"));
  CHECK(records == NumberRecords{{"MG_SIGHT", {10}}, {"SM_BASH", {8, 15}}});
  // The name is the field as it is; the numbers go through atoi. '@' does not move the
  // field start, so with no line break after it, the next name begins with it.
  CHECK(ParseNumberRecords(Bytes("  -12x#  7#+3#@#5#6#@")) ==
        NumberRecords{{"  -12x", {7, 3}}, {"@", {5, 6}}});
  // An empty first field leaves the name empty for the next one.
  CHECK(ParseNumberRecords(Bytes("#5#6#@")) == NumberRecords{{"5", {6}}});
  CHECK(ParseNumberRecords(Bytes("A#1\r\n2#@")) == NumberRecords{{"A", {2}}});
  CHECK(ParseNumberRecords(Bytes("open#1#2#")).empty());
}

TEST_CASE("line records: every line break after the name ends a line") {
  const LineRecords records =
      ParseLineRecords(Bytes("NV_BASIC#\r\nBasic Skill\r\n#\r\nAL_HEAL#\r\nHeal\r\n#"));
  CHECK(records == LineRecords{{"AL_HEAL", {"", "", "Heal", "", ""}},
                               {"NV_BASIC", {"", "", "Basic Skill", "", ""}}});
  CHECK(ParseLineRecords(Bytes(std::string_view("N#a\0b\r\n#", 8))) ==
        LineRecords{{"N", {"a", "", ""}}});
  CHECK(ParseLineRecords(Bytes("open#line\r\n")).empty());
}

TEST_CASE("fields: each '#' ends one, line breaks move its start; sorted for the indoor maps") {
  using Strings = std::vector<std::string>;
  CHECK(ParseFields(Bytes("Hello#\r\nWorld#\r\n#\r\n"), false) == Strings{"Hello", "World", ""});
  CHECK(ParseFields(Bytes("a\r\nb#c\nd#e#"), false) == Strings{"b", "d", "e"});
  CHECK(ParseFields(Bytes("open\r\nfield"), false).empty());
  CHECK(ParseFields(Bytes("payon_in01.rsw#\r\nalberta_in.rsw#\r\n"), true) ==
        Strings{"alberta_in.rsw", "payon_in01.rsw"});
}

TEST_CASE("fog parameters: a name and four values a record, sorted by name") {
  CHECK(ParseFogParameters(Bytes("gef.rsw#\r\n0.2#\r\n0.45#\r\n0x2275A9FF#\r\n0.5#\r\n"
                                 "beach.rsw#1#2#3#4#")) ==
        LineRecords{{"beach.rsw", {"1", "2", "3", "4"}},
                    {"gef.rsw", {"0.2", "0.45", "0x2275A9FF", "0.5"}}});
  CHECK(ParseFogParameters(Bytes("a#1#2#3#")).empty());
}
