#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/data_files.h"

namespace ragexe::res {

// A resource file read whole into memory (DataFile, 004dd2f0, in its reading mode):
// Open builds the path with MakeDataPath and loads it from the archive or the disk; Read
// and Seek work on the buffer as the original does.
class DataFile {
 public:
  enum class Origin { kBegin = 0, kCurrent = 1, kEnd = 2 };

  // Open (004dd310), mode 0.
  bool Open(const DataFiles& files, std::string_view name);
  // Close (004dd3f0).
  void Close();

  // Read (004dd430): all n bytes from the position, or nothing and false.
  bool Read(void* out, std::size_t n);
  // Seek (004dd4e0): offsets wrap as 32-bit values; a position past the end is clamped to
  // the end and the call fails.
  bool Seek(std::uint32_t offset, Origin origin);

  bool is_open() const { return open_; }
  std::uint32_t size() const { return static_cast<std::uint32_t>(data_.size()); }
  std::uint32_t position() const { return position_; }
  const std::string& path() const { return path_; }
  std::span<const std::uint8_t> data() const { return data_; }

 private:
  std::string path_;
  std::vector<std::uint8_t> data_;
  std::uint32_t position_ = 0;
  bool open_ = false;
};

}  // namespace ragexe::res
