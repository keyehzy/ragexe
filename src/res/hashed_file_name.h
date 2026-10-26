#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ragexe::res {

// A resource name as the client keys it (HashedFileName, 004df750): lower-cased A-Z
// only, with a hash of h * 33 + c from 0x1505 over its bytes taken as signed chars.
// The archive table and the resource cache are ordered by hash, then by the bytes.
struct HashedFileName {
  std::uint32_t hash = 0x1505;
  std::string name;

  HashedFileName() = default;
  explicit HashedFileName(std::string_view raw);

  // IsLess (004df7a0): the hash as an unsigned number, then the names as unsigned bytes.
  friend bool operator<(const HashedFileName& a, const HashedFileName& b) {
    if (a.hash != b.hash) return a.hash < b.hash;
    return a.name < b.name;
  }
  friend bool operator==(const HashedFileName& a, const HashedFileName& b) {
    return a.hash == b.hash && a.name == b.name;
  }
};

std::uint32_t HashFileName(std::string_view lowered);

}  // namespace ragexe::res
