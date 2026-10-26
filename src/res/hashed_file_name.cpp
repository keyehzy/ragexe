#include "res/hashed_file_name.h"

namespace ragexe::res {

HashedFileName::HashedFileName(std::string_view raw) : name(raw) {
  for (char& c : name) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  hash = HashFileName(name);
}

std::uint32_t HashFileName(std::string_view lowered) {
  std::uint32_t h = 0x1505;
  for (char c : lowered) {
    h = h * 33 + static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(c)));
  }
  return h;
}

}  // namespace ragexe::res
