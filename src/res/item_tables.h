#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ragexe::res {

class DataFiles;

// LoadHashSeparatedStrings (004d3850): the fields of a text table, each ended by '#' or,
// in line mode, also by '\r' (docs/formats/item_tables.md).
std::vector<std::string> SplitHashSeparated(std::span<const std::uint8_t> data, bool lines);

// The client's atoi (MSVC's atol): leading spaces, a sign, then digits, wrapping in 32
// bits.
std::int32_t ParseLeadingInt(std::string_view text);

// IsDigitString (004d3af0): every byte '0'..'9'; true for an empty string.
bool IsDigitString(std::string_view text);

// ReverseAlphabetInPlace (004d93a0) for the Korean service: a..z and A..Z reversed, and
// 0xb0..0xbf too.
std::string ReverseAlphabet(std::string_view text);

// ClientState::method_00599560: '_' to ' ', stepping with CharNextExA on the code page,
// so that the trail byte of a double-byte character is never taken for one.
std::string UnderscoresToSpaces(std::string_view text, unsigned code_page);

// What the client knows of an item id (the value of g_itemInfoById, 0x78 bytes): from the
// IdNum2 tables, the identified item; from the Num2 tables, the unidentified one.
struct ItemInfo {
  std::string identified_name;                      // +0x00, IdNum2ItemDisplayNameTable
  std::string identified_resource;                  // +0x10, IdNum2ItemResNameTable
  std::vector<std::string> identified_description;  // +0x20, IdNum2itemDescTable
  std::string name;                                 // +0x30, Num2ItemDisplayNameTable
  std::string resource;                             // +0x40, Num2ItemResNameTable
  std::vector<std::string> description;             // +0x50, Num2itemDescTable
  std::int32_t slot = 0;                            // +0x60, itemSlotTable
  std::int32_t slot_count = 0;                      // +0x64, itemSlotCountTable
  std::vector<std::int32_t> params;                 // +0x68, itemParamTable: two a line
};

// The value of g_cardInfoById.
struct CardInfo {
  std::string illustration;  // +0x00, Num2CardIllustNameTable
  std::string prefix;        // +0x10, CardPrefixNameTable, unless moved to the postfix
  std::string postfix;       // +0x20, the prefix of a card in CardPostfixNameTable
};

struct ItemTables {
  std::map<std::int32_t, ItemInfo> items;
  // The client's fallback for ids not in the map (00689140); "Unknown Item" lands here
  // unless id 0 is in the map.
  ItemInfo unknown_item;
  std::map<std::int32_t, CardInfo> cards;
  CardInfo unknown_card;  // 006891c0, likewise for card 0 ("sorry")
  // MetalProcessItemList: the lines under each id.
  std::map<std::int32_t, std::vector<std::string>> metal_process;
};

struct ItemTableSettings {
  // The service type (0067c9d4) is 0, Korea: descriptions are stored with their alphabet
  // reversed.
  bool korean_service = false;
  // The code page (0067c998) that SelectLanguage sets for the service: 0 for the system's.
  unsigned code_page = 0;
};

// The table files LoadItemTables reads, in its order.
inline constexpr std::string_view kItemTableFiles[] = {"Num2ItemDisplayNameTable.txt",
                                                       "Num2ItemResNameTable.txt",
                                                       "Num2itemDescTable.txt",
                                                       "IdNum2ItemDisplayNameTable.txt",
                                                       "IdNum2ItemResNameTable.txt",
                                                       "IdNum2itemDescTable.txt",
                                                       "itemSlotTable.txt",
                                                       "itemParamTable.txt",
                                                       "itemSlotCountTable.txt",
                                                       "CardItemNameTable.txt",
                                                       "Num2CardIllustNameTable.txt",
                                                       "CardPrefixNameTable.txt",
                                                       "CardPostfixNameTable.txt",
                                                       "MetalProcessItemList.txt"};

// The bytes of a table by its name; nullopt when it is missing, which reads as empty.
using TableReader = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view name)>;

// LoadItemTables (004d48e0).
bool LoadItemTables(const TableReader& read, const ItemTableSettings& settings, ItemTables& out,
                    std::string* error);
bool LoadItemTables(const DataFiles& files, const ItemTableSettings& settings, ItemTables& out,
                    std::string* error);

}  // namespace ragexe::res
