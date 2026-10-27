// ClientState's name-pair tables on arbitrary files.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/client_tables.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const ragexe::res::NamePairs pairs =
      ragexe::res::ParseNamePairs(std::span<const std::uint8_t>(data, size));
  for (std::size_t k = 0; k < pairs.size(); ++k) {
    FUZZ_CHECK(pairs[k].first.find('\0') == std::string::npos);
    FUZZ_CHECK(pairs[k].second.find('\0') == std::string::npos);
    if (k > 0) FUZZ_CHECK(std::strcmp(pairs[k - 1].first.c_str(), pairs[k].first.c_str()) <= 0);
  }
  return 0;
}
