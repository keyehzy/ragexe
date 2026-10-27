// The tip of the day and the manner filter on arbitrary files: the input is a tip file;
// the same bytes, split at the first 0x01, are a word list and a text to test.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "fuzz_check.h"
#include "res/manner.h"
#include "res/tips.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  const auto text = ragexe::res::ReadAsText(input);
  FUZZ_CHECK(text.size() <= size);
  for (const auto& tip : ragexe::res::ParseTips(text)) {
    FUZZ_CHECK(!tip.empty());
    for (const std::string& line : tip) {
      FUZZ_CHECK(line.find('\n') == std::string::npos);
      FUZZ_CHECK(line.find('#') == std::string::npos);
    }
  }
  std::size_t split = 0;
  while (split < size && data[split] != 0x01) ++split;
  ragexe::res::MannerFilter filter;
  filter.Parse(input.first(split));
  for (const std::string& word : filter.words()) FUZZ_CHECK(!word.empty());
  if (split < size) {
    const std::string_view probe(reinterpret_cast<const char*>(data) + split + 1, size - split - 1);
    FUZZ_CHECK(filter.IsClean(probe) != filter.ContainsBannedWord(probe));
  }
  return 0;
}
