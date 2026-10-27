#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ragexe::res {

class DataFiles;

// A tip of the day: the lines it shows (docs/formats/tips.md).
using Tip = std::vector<std::string>;

// TipOfTheDayList::Load (004db290) picks the file by the service type: tipOfTheday_e.txt
// (1), tipOfTheday_j.txt (2), otherwise tipOfTheday.txt. It opens it with fopen, beside
// the client rather than in data.
std::string_view TipFileName(std::int32_t service_type);

// What the C library's text mode reads from a file: CR LF becomes LF, and the first
// Ctrl-Z ends it.
std::vector<std::uint8_t> ReadAsText(std::span<const std::uint8_t> file);

// The tips in the bytes as read: each '\n' ends a line, each '#' ends a line and the tip.
std::vector<Tip> ParseTips(std::span<const std::uint8_t> text);

// The loader: false when the file does not open.
bool LoadTips(const DataFiles& files, std::int32_t service_type, std::vector<Tip>& out);

// TipOfTheDayList::GetTip (004db4f0): the tip at index, or the first when it is out of
// range; null when there are none.
const Tip* GetTip(const std::vector<Tip>& tips, int index);

}  // namespace ragexe::res
