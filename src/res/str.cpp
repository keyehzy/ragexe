#include "res/str.h"

#include <utility>

#include "res/data_file.h"
#include "res/record_reader.h"

namespace ragexe::res {

namespace {

constexpr std::int32_t kVersion = 0x94;
constexpr std::size_t kNameSize = 0x80;
constexpr std::size_t kKeySize = 0x7c;

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

}  // namespace

bool DecodeStr(std::span<const std::uint8_t> data, Str& out, std::string* error) {
  out = Str{};
  RecordReader in(data);
  const char* truncated = "the file is truncated";
  std::int32_t version = 0;
  std::int32_t layer_count = 0;
  // The first 4 bytes, "STRM" in data.grf, are read and never checked.
  if (!in.Skip(4) || !in.Value(version) || !in.Value(out.field_114) || !in.Value(out.field_110) ||
      !in.Value(layer_count)) {
    return Fail(error, truncated);
  }
  if (version != kVersion) return Fail(error, "LoadAniClip: Version is incorrect");  // 0060604c
  if (!in.Skip(16)) return Fail(error, truncated);
  if (std::cmp_greater(layer_count, kStrMaxLayers))
    return Fail(error, "more layers than the client's array holds");
  out.layers.resize(layer_count < 0 ? 0 : static_cast<std::size_t>(layer_count));
  for (StrLayer& layer : out.layers) {
    if (!in.Value(layer.texture_count)) return Fail(error, truncated);
    if (std::cmp_greater(layer.texture_count, kStrMaxTextures))
      return Fail(error, "more textures than a layer's slots");
    for (std::int32_t k = 0; k < layer.texture_count; ++k) {
      std::string name;
      if (!in.Name(name, kNameSize))
        return Fail(error, "a texture name without a NUL, or a truncated file");
      layer.textures.push_back(std::move(name));
    }
    std::int32_t key_count = 0;
    if (!in.Value(key_count)) return Fail(error, truncated);
    // A count of 0 or less reads no keys, as in the original.
    if (key_count > 0) {
      if (static_cast<std::size_t>(key_count) > in.remaining() / kKeySize)
        return Fail(error, truncated);
      layer.keys.resize(static_cast<std::size_t>(key_count));
      for (StrKey& key : layer.keys) {
        in.Value(key.frame);
        in.Value(key.type);
        in.Bytes(key.values.data(), sizeof key.values);
      }
    }
  }
  return true;
}

std::unique_ptr<Resource> StrRes::Clone() const { return std::make_unique<StrRes>(); }

bool StrRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "Str::Cannot find File : " + file.path();  // 00606070
    return false;
  }
  error_.clear();
  return DecodeStr(file.data(), str_, &error_);
}

void StrRes::Reset() {
  // Slot 4 (00428b30) zeroes everything from +0x110 to the DataFile, then closes it.
  str_ = Str{};
}

void RegisterStrTypes(ResourceManager& manager) {
  manager.RegisterType("str", "texture\\effect\\", std::make_unique<StrRes>());
}

}  // namespace ragexe::res
