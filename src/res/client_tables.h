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

// Records of a name and numbers, sorted by name.
using NumberRecords = std::vector<std::pair<std::string, std::vector<std::int32_t>>>;

// The tables ClientState reads as number records, with the same code.
inline constexpr std::string_view kNumberRecordTables[] = {
    "LevelUseskillSpAmount.txt",  // 0059ca60, into +0x430: SP by level, for each skill id
    "skillAfterDelay.txt",        // 0059cdd0, into +0x440: delay by level, likewise
};

NumberRecords ParseNumberRecords(std::span<const std::uint8_t> data);
bool LoadNumberRecords(const DataFiles& files, std::string_view name, NumberRecords& out);

// Records of a name and lines of text, sorted by name: skillDescTable.txt (005a54b0, into
// +0x420).
using LineRecords = std::vector<std::pair<std::string, std::vector<std::string>>>;

inline constexpr std::string_view kSkillDescTable = "skillDescTable.txt";

LineRecords ParseLineRecords(std::span<const std::uint8_t> data);
bool LoadLineRecords(const DataFiles& files, std::string_view name, LineRecords& out);

}  // namespace ragexe::res
