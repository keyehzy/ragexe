#include "res/item_tables.h"

#include <cstddef>

#include "res/data_file.h"

namespace ragexe::res {

namespace {

// The stack buffers LoadItemTables copies into: a display name (local_2c4) and a
// description line (local_6c4), each with its NUL.
constexpr std::size_t kNameBuffer = 256;
constexpr std::size_t kLineBuffer = 1024;
// The resource name of an unknown item, "사과" in code page 949 (0060c364).
constexpr std::string_view kUnknownResource = "\xbb\xe7\xb0\xfa";
constexpr std::string_view kUnknownItem = "Unknown Item";  // 0060c36c
constexpr std::string_view kUnknownCard = "sorry";         // 0060c35c

bool Fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

// The lead bytes of the double-byte code pages SelectLanguage can choose.
bool IsLeadByte(unsigned code_page, std::uint8_t c) {
  switch (code_page) {
    case 932: return (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
    case 936:
    case 949:
    case 950: return c >= 0x81 && c <= 0xfe;
    default: return false;
  }
}

// Walks the fields of one table as LoadItemTables does: by pairs, triples or one at a
// time. A record cut short by the end of the list reads the list's head node, which has
// no string, and goes round the list again; the port fails there.
class Fields {
 public:
  explicit Fields(std::vector<std::string> fields) : fields_(std::move(fields)) {}

  bool Done() const { return next_ >= fields_.size(); }
  bool Take(std::size_t n, std::span<const std::string>& out) {
    if (fields_.size() - next_ < n) return false;
    out = std::span<const std::string>(fields_).subspan(next_, n);
    next_ += n;
    return true;
  }

 private:
  std::vector<std::string> fields_;
  std::size_t next_ = 0;
};

}  // namespace

std::vector<std::string> SplitHashSeparated(std::span<const std::uint8_t> data, bool lines) {
  std::vector<std::string> out;
  std::size_t start = 0;
  // Each field runs from its start to the first NUL: the byte that ended it, or one the
  // file holds.
  const auto field = [&](std::size_t end) {
    std::size_t stop = start;
    while (stop < end && data[stop] != 0) ++stop;
    out.emplace_back(reinterpret_cast<const char*>(data.data()) + start, stop - start);
  };
  for (std::size_t i = 0; i < data.size(); ++i) {
    switch (data[i]) {
      case '\n': start = i + 1; break;
      case '\r':
        if (lines) field(i);
        start = i + 1;
        break;
      case '#':
        field(i);
        start = i + 1;
        break;
      default: break;
    }
  }
  return out;
}

std::int32_t ParseLeadingInt(std::string_view text) {
  std::size_t i = 0;
  while (i < text.size() && (text[i] == ' ' || (text[i] >= '\t' && text[i] <= '\r'))) ++i;
  bool negative = false;
  if (i < text.size() && (text[i] == '-' || text[i] == '+')) negative = text[i++] == '-';
  std::uint32_t total = 0;
  for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
    total = total * 10 + static_cast<std::uint32_t>(text[i] - '0');
  }
  return static_cast<std::int32_t>(negative ? 0u - total : total);
}

bool IsDigitString(std::string_view text) {
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

std::string ReverseAlphabet(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    auto b = static_cast<std::uint8_t>(c);
    if (b >= 'a' && b <= 'z') {
      b = static_cast<std::uint8_t>(0xdb - b);
    } else if (b >= 'A' && b <= 'Z') {
      b = static_cast<std::uint8_t>(0x9b - b);
    } else if (b >= 0xb0 && b <= 0xbf) {
      b = static_cast<std::uint8_t>(0x16f - b);
    }
    c = static_cast<char>(b);
  }
  return out;
}

std::string UnderscoresToSpaces(std::string_view text, unsigned code_page) {
  std::string out(text);
  std::size_t i = 0;
  while (i < out.size() && out[i] != '\0') {
    if (out[i] == '_') out[i] = ' ';
    // CharNextExA: past a lead byte and its trail, unless the trail is the end.
    const bool pair = IsLeadByte(code_page, static_cast<std::uint8_t>(out[i])) &&
                      i + 1 < out.size() && out[i + 1] != '\0';
    i += pair ? 2 : 1;
  }
  return out;
}

bool LoadItemTables(const TableReader& read, const ItemTableSettings& settings, ItemTables& out,
                    std::string* error) {
  out = ItemTables{};
  const auto fields = [&](std::string_view name, bool lines) {
    const auto data = read(name);
    return Fields(data ? SplitHashSeparated(*data, lines) : std::vector<std::string>{});
  };
  const auto cut_short = [&](std::string_view name) {
    return Fail(error, std::string(name) + ": a record runs past the last field");
  };
  const auto display_name = [&](std::string_view name, const std::string& text, std::string& to) {
    if (text.size() >= kNameBuffer) {
      return Fail(error, std::string(name) + ": a name overruns the client's 256-byte buffer");
    }
    to = UnderscoresToSpaces(text, settings.code_page);
    return true;
  };
  // A description table, line by line: a line of digits starts the next item, any other
  // non-empty line is added to the current one, from id 0.
  const auto descriptions = [&](std::string_view name,
                                std::vector<std::string> ItemInfo::* lines_of) {
    Fields table = fields(name, true);
    std::int32_t current = 0;
    std::span<const std::string> f;
    while (table.Take(1, f)) {
      if (f[0].empty()) continue;
      if (f[0].size() >= kLineBuffer) {
        return Fail(error, std::string(name) + ": a line overruns the client's 1024-byte buffer");
      }
      const std::string line = settings.korean_service ? ReverseAlphabet(f[0]) : f[0];
      if (IsDigitString(line)) {
        current = ParseLeadingInt(line);
      } else {
        (out.items[current].*lines_of).push_back(line);
      }
    }
    return true;
  };
  // A table of id and name pairs.
  const auto pairs = [&](std::string_view name, const auto& apply) {
    Fields table = fields(name, false);
    std::span<const std::string> f;
    while (!table.Done()) {
      if (!table.Take(2, f)) return cut_short(name);
      if (!apply(ParseLeadingInt(f[0]), f[1])) return false;
    }
    return true;
  };

  if (!pairs(kItemTableFiles[0],
             [&](std::int32_t id, const std::string& v) {
               return display_name(kItemTableFiles[0], v, out.items[id].name);
             }) ||
      !pairs(kItemTableFiles[1],
             [&](std::int32_t id, const std::string& v) {
               out.items[id].resource = v;
               return true;
             }) ||
      !descriptions(kItemTableFiles[2], &ItemInfo::description) ||
      !pairs(kItemTableFiles[3],
             [&](std::int32_t id, const std::string& v) {
               return display_name(kItemTableFiles[3], v, out.items[id].identified_name);
             }) ||
      !pairs(kItemTableFiles[4],
             [&](std::int32_t id, const std::string& v) {
               out.items[id].identified_resource = v;
               return true;
             }) ||
      !descriptions(kItemTableFiles[5], &ItemInfo::identified_description) ||
      !pairs(kItemTableFiles[6], [&](std::int32_t id, const std::string& v) {
        out.items[id].slot = ParseLeadingInt(v);
        return true;
      })) {
    return false;
  }
  {
    // Three fields a record: the id and two values.
    Fields table = fields(kItemTableFiles[7], false);
    std::span<const std::string> f;
    while (!table.Done()) {
      if (!table.Take(3, f)) return cut_short(kItemTableFiles[7]);
      ItemInfo& item = out.items[ParseLeadingInt(f[0])];
      item.params.push_back(ParseLeadingInt(f[1]));
      item.params.push_back(ParseLeadingInt(f[2]));
    }
  }
  if (!pairs(kItemTableFiles[8], [&](std::int32_t id, const std::string& v) {
        out.items[id].slot_count = ParseLeadingInt(v);
        return true;
      })) {
    return false;
  }
  {
    // One field a record: the card ids.
    Fields table = fields(kItemTableFiles[9], false);
    std::span<const std::string> f;
    while (table.Take(1, f)) out.cards[ParseLeadingInt(f[0])];
  }
  if (!pairs(kItemTableFiles[10],
             [&](std::int32_t id, const std::string& v) {
               out.cards[id].illustration = v;
               return true;
             }) ||
      !pairs(kItemTableFiles[11], [&](std::int32_t id, const std::string& v) {
        out.cards[id].prefix = v;
        return true;
      })) {
    return false;
  }
  {
    // One field a record: the cards whose prefix is a postfix.
    Fields table = fields(kItemTableFiles[12], false);
    std::span<const std::string> f;
    while (table.Take(1, f)) {
      CardInfo& card = out.cards[ParseLeadingInt(f[0])];
      card.postfix = card.prefix;
      card.prefix.clear();
    }
  }
  {
    // Like the description tables, without the alphabet.
    Fields table = fields(kItemTableFiles[13], true);
    std::int32_t current = 0;
    std::span<const std::string> f;
    while (table.Take(1, f)) {
      if (f[0].empty()) continue;
      if (IsDigitString(f[0])) {
        current = ParseLeadingInt(f[0]);
      } else {
        out.metal_process[current].push_back(f[0]);
      }
    }
  }

  // Id 0, or the fallback when it is not in the map.
  const auto zero = out.items.find(0);
  ItemInfo& unknown = zero != out.items.end() ? zero->second : out.unknown_item;
  unknown.identified_name = kUnknownItem;
  unknown.identified_resource = kUnknownResource;
  unknown.identified_description.emplace_back(kUnknownItem);
  unknown.name = kUnknownItem;
  unknown.resource = kUnknownResource;
  unknown.description.emplace_back(kUnknownItem);
  const auto card_zero = out.cards.find(0);
  CardInfo& unknown_card = card_zero != out.cards.end() ? card_zero->second : out.unknown_card;
  unknown_card.illustration = kUnknownCard;
  unknown_card.prefix.clear();
  unknown_card.postfix.clear();
  return true;
}

bool LoadItemTables(const DataFiles& files, const ItemTableSettings& settings, ItemTables& out,
                    std::string* error) {
  return LoadItemTables(
      [&files](std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
        DataFile file;
        if (!file.Open(files, name)) return std::nullopt;
        return std::vector<std::uint8_t>(file.data().begin(), file.data().end());
      },
      settings, out, error);
}

}  // namespace ragexe::res
