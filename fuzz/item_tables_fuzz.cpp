// The item and card tables on arbitrary files: the first byte picks the service and the
// code page, and the rest splits at each 0x01 into the tables, in LoadItemTables' order.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fuzz_check.h"
#include "res/item_tables.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) return 0;
  ragexe::res::ItemTableSettings settings;
  settings.korean_service = (data[0] & 1) != 0;
  settings.code_page = (data[0] & 2) != 0 ? 949 : 0;
  std::vector<std::vector<std::uint8_t>> parts(1);
  for (std::size_t i = 1; i < size; ++i) {
    if (data[i] == 0x01) {
      parts.emplace_back();
    } else {
      parts.back().push_back(data[i]);
    }
  }
  for (const auto& part : parts) {
    for (bool lines : {false, true}) {
      for (const std::string& field : ragexe::res::SplitHashSeparated(part, lines)) {
        FUZZ_CHECK(field.find('#') == std::string::npos);
        FUZZ_CHECK(field.find('\0') == std::string::npos);
      }
    }
  }
  const ragexe::res::TableReader read =
      [&parts](std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
    for (std::size_t k = 0; k < std::size(ragexe::res::kItemTableFiles); ++k) {
      if (ragexe::res::kItemTableFiles[k] == name && k < parts.size()) return parts[k];
    }
    return std::nullopt;
  };
  ragexe::res::ItemTables tables;
  std::string error;
  if (!ragexe::res::LoadItemTables(read, settings, tables, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  const auto zero = tables.items.find(0);
  const ragexe::res::ItemInfo& unknown =
      zero != tables.items.end() ? zero->second : tables.unknown_item;
  FUZZ_CHECK(unknown.name == "Unknown Item");
  for (const auto& [id, item] : tables.items) {
    FUZZ_CHECK(item.params.size() % 2 == 0);
    for (const std::string& line : item.description) FUZZ_CHECK(line.size() < 1024);
  }
  return 0;
}
