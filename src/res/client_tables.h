#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ragexe::res {

class DataFiles;

// A table of name pairs, sorted by the first name as the client's std::sort leaves them.
using NamePairs = std::vector<std::pair<std::string, std::string>>;

// The name-pair tables ClientState loads, all with the same code
// (docs/formats/client_tables.md).
inline constexpr std::string_view kNamePairTables[] = {
    "mapNameTable.txt",      // 005979d0, into +0x1670: the map file and its display name
    "mapObjLightTable.txt",  // 00597e10, into +0x1718
    "mp3NameTable.txt",      // 00598290, into +0x1680: the map and its music
    "skillNameTable.txt",    // 00593c10, into +0x400: the skill id and its name
};

// The bytes of one of those tables, as its loader reads them.
NamePairs ParseNamePairs(std::span<const std::uint8_t> data);

// The loader: false, with the table emptied, when the file does not open.
bool LoadNamePairs(const DataFiles& files, std::string_view name, NamePairs& out);

}  // namespace ragexe::res
