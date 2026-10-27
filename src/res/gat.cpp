#include "res/gat.h"

#include <cstring>

#include "res/data_file.h"

namespace ragexe::res {

namespace {

constexpr std::size_t kHeaderSize = 14;
constexpr std::size_t kCellSize = 20;

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

std::int32_t I32(const std::uint8_t* p) {
  std::int32_t v = 0;
  std::memcpy(&v, p, 4);
  return v;
}

}  // namespace

bool DecodeAttrMap(std::span<const std::uint8_t> data, AttrMap& out, std::string* error,
                   std::string* warning) {
  out = AttrMap{};
  if (data.size() < 4 || std::memcmp(data.data(), "GRAT", 4) != 0) {
    // 00605e88, in CP949: "an old GAT version without block attributes".
    return Fail(error, "Block Attr: an old GAT version that is not supported");
  }
  if (data.size() < 6) return Fail(error, "the file is truncated");
  out.major = data[4];
  out.minor = data[5];
  if (out.major != 1 || out.minor >= 3) {
    if (warning != nullptr) *warning = "C3dAttr::Unsupported Version";
    return true;
  }
  if (data.size() < kHeaderSize) return Fail(error, "the file is truncated");
  const std::int32_t width = I32(&data[6]);
  const std::int32_t height = I32(&data[10]);
  if (width < 0 || height < 0) return Fail(error, "the map has a negative size");
  const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (count > (data.size() - kHeaderSize) / kCellSize)
    return Fail(error, "the cells run past the file");
  out.width = width;
  out.height = height;
  out.cells.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::uint8_t* p = &data[kHeaderSize + i * kCellSize];
    std::memcpy(out.cells[i].heights.data(), p, 16);
    out.cells[i].attribute = I32(p + 16);
  }
  return true;
}

std::unique_ptr<Resource> AttrRes::Clone() const { return std::make_unique<AttrRes>(); }

bool AttrRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "cannot open " + file.path();
    return false;
  }
  error_.clear();
  return DecodeAttrMap(file.data(), map_, &error_);
}

void AttrRes::Reset() {
  // Slot 4 (00426eb0) zeroes the size only.
  map_.width = 0;
  map_.height = 0;
}

void RegisterAttrTypes(ResourceManager& manager) {
  manager.RegisterType("gat", "", std::make_unique<AttrRes>());
}

}  // namespace ragexe::res
