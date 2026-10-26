// ParseGrfTable on arbitrary tables. Input: one byte choosing the version (0x100 + the low
// two bits), four bytes of entry count (little-endian), then the table.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fuzz_check.h"
#include "res/grf.h"
#include "res/hashed_file_name.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size < 5) return 0;
  const std::uint32_t version = 0x100u + (data[0] & 3u);
  const std::uint32_t count =
      static_cast<std::uint32_t>(data[1]) | static_cast<std::uint32_t>(data[2]) << 8 |
      static_cast<std::uint32_t>(data[3]) << 16 | static_cast<std::uint32_t>(data[4]) << 24;
  std::vector<std::uint8_t> table(data + 5, data + size);
  std::string error;
  auto entries = ragexe::res::ParseGrfTable(version, count, std::move(table), &error);
  if (!entries) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(entries->size() == count);
  for (const ragexe::res::GrfEntry& e : *entries) {
    FUZZ_CHECK(e.key == ragexe::res::HashedFileName(e.name));
  }
  return 0;
}
