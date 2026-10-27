#include "res/client_tables.h"

#include <cstring>

#include "res/data_file.h"
#include "res/vc6_sort.h"

namespace ragexe::res {

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
  // StringLess (00599400): the first names compared byte by byte, unsigned.
  Vc6Sort(out.data(), out.data() + out.size(),
          [](const NamePairs::value_type& a, const NamePairs::value_type& b) {
            return std::strcmp(a.first.c_str(), b.first.c_str()) < 0;
          });
  return out;
}

bool LoadNamePairs(const DataFiles& files, std::string_view name, NamePairs& out) {
  out.clear();
  DataFile file;
  if (!file.Open(files, name)) return false;
  out = ParseNamePairs(file.data());
  return true;
}

}  // namespace ragexe::res
