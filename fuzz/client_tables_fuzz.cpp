// ClientState's tables on arbitrary files, through each of its three parsers.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/client_tables.h"

namespace {

template <typename Records>
void CheckSorted(const Records& records) {
  for (std::size_t k = 1; k < records.size(); ++k) {
    FUZZ_CHECK(std::strcmp(records[k - 1].first.c_str(), records[k].first.c_str()) <= 0);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  const ragexe::res::NamePairs pairs = ragexe::res::ParseNamePairs(input);
  CheckSorted(pairs);
  for (const auto& [first, second] : pairs) {
    FUZZ_CHECK(first.find('\0') == std::string::npos);
    FUZZ_CHECK(second.find('\0') == std::string::npos);
  }
  CheckSorted(ragexe::res::ParseNumberRecords(input));
  const ragexe::res::LineRecords lines = ragexe::res::ParseLineRecords(input);
  CheckSorted(lines);
  for (const auto& [name, text] : lines) {
    FUZZ_CHECK(!name.empty());
    FUZZ_CHECK(!text.empty());
  }
  return 0;
}
