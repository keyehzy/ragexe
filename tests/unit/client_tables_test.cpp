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
