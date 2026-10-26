// ParseResNameTable on arbitrary text: keys and values never hold the bytes that end
// tokens.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "fuzz_check.h"
#include "res/resource.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  for (const auto& [key, value] : ragexe::res::ParseResNameTable(text)) {
    FUZZ_CHECK(key.find_first_of("#\r\n") == std::string_view::npos);
    FUZZ_CHECK(value.find_first_of("#\r\n") == std::string_view::npos);
  }
  return 0;
}
