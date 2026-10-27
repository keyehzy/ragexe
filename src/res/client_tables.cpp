#include "res/client_tables.h"

#include <cstring>

#include "res/data_file.h"
#include "res/item_tables.h"
#include "res/vc6_sort.h"

namespace ragexe::res {

namespace {

// StringLess (00599400 and its copies 0059fea0, 005a0fc0): the first strings compared as
// C strings, byte by byte, unsigned.
template <typename Record>
void SortByName(std::vector<Record>& records) {
  Vc6Sort(records.data(), records.data() + records.size(), [](const Record& a, const Record& b) {
    return std::strcmp(a.first.c_str(), b.first.c_str()) < 0;
  });
}

}  // namespace

NamePairs ParseNamePairs(std::span<const std::uint8_t> data) {
  NamePairs out;
  // Two character buffers, the first name's and the second's, and the one being filled.
  std::string first;
  std::string second;
  std::string* current = &first;
  // A buffer read as a C string: up to its first NUL.
  const auto c_string = [](const std::string& buffer) {
    return buffer.substr(0, buffer.find('\0'));
  };
  for (const std::uint8_t c : data) {
    if (c == '\n' || c == '\r') {
      // Only the buffer being filled is emptied.
      current->clear();
    } else if (c == '#') {
      if (second.empty()) {
        // Ends the first name, or, while the second buffer is filled and still empty,
        // puts a NUL in it.
        current->push_back('\0');
        current = &second;
      } else {
        current->push_back('\0');
        out.emplace_back(c_string(first), c_string(second));
        first.clear();
        second.clear();
        current = &first;
      }
    } else {
      current->push_back(static_cast<char>(c));
    }
  }
  SortByName(out);
  return out;
}

bool LoadNamePairs(const DataFiles& files, std::string_view name, NamePairs& out) {
  out.clear();
  DataFile file;
  if (!file.Open(files, name)) return false;
  out = ParseNamePairs(file.data());
  return true;
}

NumberRecords ParseNumberRecords(std::span<const std::uint8_t> data) {
  NumberRecords out;
  std::string name;
  std::vector<std::int32_t> numbers;
  // Where the field being read starts: after the last '#' or line break, not moved by '@'.
  std::size_t start = 0;
  for (std::size_t i = 0; i < data.size(); ++i) {
    switch (data[i]) {
      case '\n':
      case '\r': start = i + 1; break;
      case '#': {
        // The field as a byte range, NULs and all; a name only while there is none.
        const std::string_view field(reinterpret_cast<const char*>(data.data()) + start, i - start);
        if (name.empty()) {
          name.assign(field);
        } else {
          numbers.push_back(ParseLeadingInt(field));
        }
        start = i + 1;
        break;
      }
      case '@':
        out.emplace_back(name, numbers);
        name.clear();
        numbers.clear();
        break;
      default: break;
    }
  }
  SortByName(out);
  return out;
}

bool LoadNumberRecords(const DataFiles& files, std::string_view name, NumberRecords& out) {
  out.clear();
  DataFile file;
  if (!file.Open(files, name)) return false;
  out = ParseNumberRecords(file.data());
  return true;
}

LineRecords ParseLineRecords(std::span<const std::uint8_t> data) {
  LineRecords out;
  std::string name;
  std::vector<std::string> lines;
  std::size_t start = 0;
  // The loader ends each field with a NUL in its copy of the file and reads it as a C
  // string, so a NUL the file holds ends it early.
  const auto field = [&](std::size_t end) {
    std::size_t stop = start;
    while (stop < end && data[stop] != 0) ++stop;
    return std::string(reinterpret_cast<const char*>(data.data()) + start, stop - start);
  };
  for (std::size_t i = 0; i < data.size(); ++i) {
    switch (data[i]) {
      case '\n':
      case '\r':
        // Once there is a name, every line break ends a line, so "\r\n" adds an empty one.
        if (!name.empty()) lines.push_back(field(i));
        start = i + 1;
        break;
      case '#':
        if (name.empty()) {
          name = field(i);
        } else {
          lines.push_back(field(i));
          out.emplace_back(name, lines);
          name.clear();
          lines.clear();
        }
        start = i + 1;
        break;
      default: break;
    }
  }
  SortByName(out);
  return out;
}

bool LoadLineRecords(const DataFiles& files, std::string_view name, LineRecords& out) {
  out.clear();
  DataFile file;
  if (!file.Open(files, name)) return false;
  out = ParseLineRecords(file.data());
  return true;
}

}  // namespace ragexe::res
