// A whole archive from memory: the header, the table, the lookup of every name, the
// duplicate report and the reading of up to 64 entries.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fuzz_check.h"
#include "res/grf.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::string error;
  auto grf =
      ragexe::res::GrfArchive::FromBytes(std::vector<std::uint8_t>(data, data + size), &error);
  if (!grf) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  for (const std::string& name : grf->DuplicateNames()) FUZZ_CHECK(grf->Find(name) != nullptr);
  std::size_t read = 0;
  for (const ragexe::res::GrfEntry& e : grf->entries()) {
    const ragexe::res::GrfEntry* found = grf->Find(e.name);
    FUZZ_CHECK(found != nullptr && found->key == e.key);
    if (read++ == 64) continue;
    bool damaged = false;
    auto bytes = grf->Read(e, &error, &damaged);
    if (bytes) {
      // A stream may end before the recorded size, never after it; a damaged one is
      // zero-filled to exactly that size.
      FUZZ_CHECK(bytes->size() <= e.size);
      FUZZ_CHECK(!damaged || bytes->size() == e.size);
    }
  }
  return 0;
}
