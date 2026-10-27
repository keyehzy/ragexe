#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/resource.h"

namespace ragexe::res {

// A 0x7c-byte key of an effect layer (docs/formats/str.md).
struct StrKey {
  std::int32_t frame = 0;
  // 0: CRagEffect's player (0054fba0) takes the values as they are; otherwise it adds
  // them to the current ones.
  std::int32_t type = 0;
  std::array<std::uint32_t, 29> values{};  // as stored; their meanings come with the player
};

struct StrLayer {
  std::int32_t texture_count = 0;  // as read; may be 0 or negative
  // The first texture_count names. The client points its other slots, up to 110, at
  // "1.bmp".
  std::vector<std::string> textures;
  std::vector<StrKey> keys;
};

struct Str {
  std::int32_t field_110 = 0;  // the file's fourth dword
  std::int32_t field_114 = 0;  // its third: 60 in every file of data.grf
  std::vector<StrLayer> layers;
};

// The client's array holds this many layers, each with this many texture slots.
inline constexpr std::size_t kStrMaxLayers = 60;
inline constexpr std::size_t kStrMaxTextures = 110;

// CEZeffectRes::Load (00428b50) on a whole .str file.
bool DecodeStr(std::span<const std::uint8_t> data, Str& out, std::string* error);

// CEZeffectRes.
class StrRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const Str& str() const { return str_; }
  const std::string& error() const { return error_; }

 private:
  Str str_;
  std::string error_;
};

// The original's registration (static initializer 00428770).
void RegisterStrTypes(ResourceManager& manager);

}  // namespace ragexe::res
