#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ragexe::res {

class DataFiles;

// The chat filter, MannerFilter (0067cbe8): the banned words of manner.txt and the test
// against them (docs/formats/manner.md).
class MannerFilter {
 public:
  // Load (004d25e0) through data\: false, with the words kept, when the file does not
  // open ("Cannot find File : " and the name) or is empty.
  bool Load(const DataFiles& files, std::string_view name, std::string* error);
  // The words of a file's bytes, replacing the list.
  void Parse(std::span<const std::uint8_t> data);

  // IsClean (004d2220): no word matches the text, lower-cased, at any character.
  bool IsClean(std::string_view text) const;
  // ContainsBannedWord (004d2200).
  bool ContainsBannedWord(std::string_view text) const { return !IsClean(text); }

  const std::vector<std::string>& words() const { return words_; }

 private:
  std::vector<std::string> words_;
};

}  // namespace ragexe::res
