#include "res/item_tables.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace ragexe::res;

namespace {

using Fields = std::vector<std::string>;

std::vector<std::uint8_t> Bytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

TableReader Reader(const std::map<std::string, std::string, std::less<>>& tables) {
  return [tables](std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
    const auto found = tables.find(name);
    if (found == tables.end()) return std::nullopt;
    return Bytes(found->second);
  };
}

}  // namespace

TEST_CASE("fields end at '#', and in line mode at '\\r' too") {
  CHECK(SplitHashSeparated(Bytes("1#One#\r\n2#Two#\r\n"), false) == Fields{"1", "One", "2", "Two"});
  // In line mode the '\r' right after a '#' ends an empty field.
  CHECK(SplitHashSeparated(Bytes("501#\r\nA potion.\r\n#\r\n"), true) ==
        Fields{"501", "", "A potion.", "", ""});
  // A newline starts the next field over: what followed the last '#' is lost.
  CHECK(SplitHashSeparated(Bytes("1#a#lost\n2#b#"), false) == Fields{"1", "a", "2", "b"});
  // In line mode a line that ends with '\n' alone is lost the same way.
  CHECK(SplitHashSeparated(Bytes("1\nlost\nkept\r"), true) == Fields{"kept"});
  // A field left open at the end is dropped, and a NUL ends a field early.
  CHECK(SplitHashSeparated(Bytes("1#tail"), false) == Fields{"1"});
  CHECK(SplitHashSeparated(Bytes(std::string_view("ab\0cd#", 6)), false) == Fields{"ab"});
}

TEST_CASE("atoi, digit strings, the reversed alphabet and underscores") {
  CHECK(ParseLeadingInt("  12") == 12);
  CHECK(ParseLeadingInt("-3x") == -3);
  CHECK(ParseLeadingInt("+7") == 7);
  CHECK(ParseLeadingInt("4294967297") == 1);
  CHECK(ParseLeadingInt("") == 0);
  CHECK(IsDigitString("0123"));
  CHECK(IsDigitString(""));
  CHECK_FALSE(IsDigitString("-1"));
  CHECK(ReverseAlphabet("Abc xyz 09\xb0\xbf\xc0") == "Zyx cba 09\xbf\xb0\xc0");
  CHECK(UnderscoresToSpaces("Mirror_Shield", 0) == "Mirror Shield");
  // In code page 949 the second byte of a double-byte character may be '_' (0x5f).
  CHECK(UnderscoresToSpaces("\xb0\x5f_", 949) == "\xb0\x5f ");
  CHECK(UnderscoresToSpaces("\xb0\x5f_", 0) == "\xb0  ");
}

TEST_CASE("the item tables fill the item map, then item 0 or the fallback") {
  ItemTables tables;
  std::string error;
  REQUIRE(LoadItemTables(Reader({
                             {"Num2ItemDisplayNameTable.txt", "501#Red_Potion#\r\n"},
                             {"Num2ItemResNameTable.txt", "501#red_potion#\r\n"},
                             {"Num2itemDescTable.txt", "stray\r\n501#\r\nHeals.\r\n\r\n#\r\n"},
                             {"IdNum2ItemDisplayNameTable.txt", "501#Red_Potion#\r\n"},
                             {"itemSlotTable.txt", "501#8#\r\n"},
                             {"itemParamTable.txt", "501#1#2#\r\n501#3#4#\r\n"},
                             {"itemSlotCountTable.txt", "501#2#\r\n"},
                         }),
                         {}, tables, &error));
  const ItemInfo& potion = tables.items.at(501);
  CHECK(potion.name == "Red Potion");
  CHECK(potion.resource == "red_potion");
  CHECK(potion.description == Fields{"Heals."});
  CHECK(potion.identified_name == "Red Potion");
  CHECK(potion.slot == 8);
  CHECK(potion.slot_count == 2);
  CHECK(potion.params == std::vector<std::int32_t>{1, 2, 3, 4});
  // A line before the first id belongs to item 0, which is then in the map and gets the
  // defaults; the fallback keeps none.
  const ItemInfo& zero = tables.items.at(0);
  CHECK(zero.description == Fields{"stray", "Unknown Item"});
  CHECK(zero.name == "Unknown Item");
  CHECK(zero.resource == "\xbb\xe7\xb0\xfa");
  CHECK(tables.unknown_item.name.empty());
  CHECK(tables.unknown_card.illustration == "sorry");
}

TEST_CASE("cards: prefixes, and the ones moved to postfixes") {
  ItemTables tables;
  std::string error;
  REQUIRE(LoadItemTables(Reader({
                             {"CardItemNameTable.txt", "4001#4002#"},
                             {"Num2CardIllustNameTable.txt", "4001#poring#"},
                             {"CardPrefixNameTable.txt", "4001#Lucky#4002#Hard#"},
                             {"CardPostfixNameTable.txt", "4002#"},
                             {"MetalProcessItemList.txt", "1000\r\nOre\r\n\r\nSteel\r\n"},
                         }),
                         {}, tables, &error));
  CHECK(tables.cards.at(4001).illustration == "poring");
  CHECK(tables.cards.at(4001).prefix == "Lucky");
  CHECK(tables.cards.at(4002).prefix.empty());
  CHECK(tables.cards.at(4002).postfix == "Hard");
  CHECK(tables.metal_process.at(1000) == Fields{"Ore", "Steel"});
}

TEST_CASE("descriptions for the Korean service are stored reversed") {
  ItemTables tables;
  std::string error;
  REQUIRE(LoadItemTables(Reader({{"Num2itemDescTable.txt", "7\r\nZyx\r\n"}}),
                         {.korean_service = true}, tables, &error));
  CHECK(tables.items.at(7).description == Fields{"Abc"});
}

TEST_CASE("what runs the client past its buffers or round a table fails") {
  ItemTables tables;
  std::string error;
  CHECK_FALSE(LoadItemTables(Reader({{"Num2ItemResNameTable.txt", "1#a#2#"}}), {}, tables, &error));
  CHECK_FALSE(LoadItemTables(Reader({{"itemParamTable.txt", "1#2#3#4#"}}), {}, tables, &error));
  CHECK(
      LoadItemTables(Reader({{"Num2ItemDisplayNameTable.txt", "1#" + std::string(255, 'n') + "#"}}),
                     {}, tables, &error));
  CHECK_FALSE(
      LoadItemTables(Reader({{"Num2ItemDisplayNameTable.txt", "1#" + std::string(256, 'n') + "#"}}),
                     {}, tables, &error));
  CHECK(
      LoadItemTables(Reader({{"Num2itemDescTable.txt", "1\r\n" + std::string(1023, 'd') + "\r\n"}}),
                     {}, tables, &error));
  CHECK_FALSE(
      LoadItemTables(Reader({{"Num2itemDescTable.txt", "1\r\n" + std::string(1024, 'd') + "\r\n"}}),
                     {}, tables, &error));
}
