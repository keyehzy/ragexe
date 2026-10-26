#include "res/data_file.h"

#include <cstring>
#include <utility>

#include "res/data_path.h"

namespace ragexe::res {

bool DataFile::Open(const DataFiles& files, std::string_view name) {
  Close();
  path_ = MakeDataPath(name);
  auto data = files.Load(path_);
  if (!data) return false;
  data_ = std::move(*data);
  open_ = true;
  return true;
}

void DataFile::Close() {
  data_.clear();
  data_.shrink_to_fit();
  position_ = 0;
  open_ = false;
}

bool DataFile::Read(void* out, std::size_t n) {
  if (!open_ || n > data_.size() - position_) return false;
  if (n != 0) std::memcpy(out, data_.data() + position_, n);
  position_ += static_cast<std::uint32_t>(n);
  return true;
}

bool DataFile::Seek(std::uint32_t offset, Origin origin) {
  if (!open_) return false;
  switch (origin) {
    case Origin::kBegin: position_ = offset; break;
    case Origin::kCurrent: position_ += offset; break;
    case Origin::kEnd: position_ = size() + offset; break;
  }
  if (position_ <= size()) return true;
  position_ = size();
  return false;
}

}  // namespace ragexe::res
