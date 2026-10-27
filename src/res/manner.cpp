#include "res/manner.h"

#include <cstring>

#include "res/data_file.h"

namespace ragexe::res {

namespace {

// The lower-casing of AssignLowercase (004d2070) and IsClean: A..Z, by isupper in the C
// locale, gain 0x20; a byte above 0x7f is the first of two, and both are left alone.
void LowerCase(std::string& text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto c = static_cast<std::uint8_t>(text[i]);
    if (c >= 0x80) {
      ++i;
    } else if (c >= 'A' && c <= 'Z') {
      text[i] = static_cast<char>(c + 0x20);
    }
  }
}

}  // namespace

bool MannerFilter::Load(const DataFiles& files, std::string_view name, std::string* error) {
  DataFile file;
  if (!file.Open(files, name)) {
    if (error != nullptr) *error = "Cannot find File : " + std::string(name);  // 00605c3c
    return false;
  }
  if (file.data().empty()) return false;
  Parse(file.data());
  return true;
}

void MannerFilter::Parse(std::span<const std::uint8_t> data) {
  words_.clear();
  // '\r' and '\n' become NULs, so each line is the C string from its start; '\n' ends it.
  std::size_t start = 0;
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] != '\n') continue;
    std::size_t end = start;
    while (end < i && data[end] != 0 && data[end] != '\r') ++end;
    const std::string_view line(reinterpret_cast<const char*>(data.data()) + start, end - start);
    start = i + 1;
    // method_004d2450: a line of only spaces and tabs is skipped; otherwise the whole line
    // is kept, leading blanks and all (the trim of trailing spaces starts at the NUL and
    // never moves), lower-cased.
    if (line.find_first_not_of(" \t") == std::string_view::npos) continue;
    std::string word(line);
    LowerCase(word);
    words_.push_back(std::move(word));
  }
}

bool MannerFilter::IsClean(std::string_view text) const {
  // A copy up to its first NUL, lower-cased.
  std::string copy(text.substr(0, text.find('\0')));
  LowerCase(copy);
  // Each word at each character: two bytes for a lead byte (0x80..0xfe), else one.
  for (std::size_t pos = 0; pos < copy.size();) {
    for (const std::string& word : words_) {
      if (std::strncmp(word.c_str(), copy.c_str() + pos, word.size()) == 0) return false;
    }
    const auto c = static_cast<std::uint8_t>(copy[pos]);
    pos += (c & 0x80) == 0 || c == 0xff ? 1 : 2;
  }
  return true;
}

}  // namespace ragexe::res
