#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

namespace ragexe::res {

// A cursor over a file's bytes for the loaders that read field by field. Where the
// original's DataFile::Read would fail and leave its buffer as it was, every read here
// fails, so a truncated file is refused instead.
class RecordReader {
 public:
  explicit RecordReader(std::span<const std::uint8_t> data) : data_(data) {}

  bool Bytes(void* out, std::size_t n) {
    if (remaining() < n) return false;
    std::memcpy(out, data_.data() + pos_, n);
    pos_ += n;
    return true;
  }
  template <typename T>
  bool Value(T& out) {
    return Bytes(&out, sizeof out);
  }
  bool Skip(std::size_t n) {
    if (remaining() < n) return false;
    pos_ += n;
    return true;
  }
  // A count of records of at least `size` bytes each that the rest of the file can hold.
  bool Count(std::int32_t& n, std::size_t size) {
    return Value(n) && n >= 0 && static_cast<std::size_t>(n) <= remaining() / size;
  }
  // The C string at the start of a record of `size` bytes; false when the record has no
  // NUL, where the original would copy on past it.
  bool Name(std::string& out, std::size_t size = 40) {
    if (remaining() < size) return false;
    const std::uint8_t* begin = data_.data() + pos_;
    const std::uint8_t* end = begin + size;
    const std::uint8_t* nul = std::find(begin, end, std::uint8_t{0});
    if (nul == end) return false;
    out.assign(begin, nul);
    pos_ += size;
    return true;
  }
  std::size_t remaining() const { return data_.size() - pos_; }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

}  // namespace ragexe::res
