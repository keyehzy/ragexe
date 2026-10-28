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

// One cell of an attribute map (docs/formats/gat.md).
struct AttrCell {
  std::array<float, 4> heights{};  // corners: (x, z), (x+1, z), (x, z+1), (x+1, z+1)
  std::int32_t attribute = 0;
};

struct AttrMap {
  std::uint8_t major = 0;
  std::uint8_t minor = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t cell_size = 5;   // world units per cell (00426ba0)
  std::vector<AttrCell> cells;  // row by row
};

// C3dAttr::Load (00426ec0) on a whole .gat file. An unsupported version succeeds with an
// empty map, as in the original; *warning then holds its message.
bool DecodeAttrMap(std::span<const std::uint8_t> data, AttrMap& out, std::string* error,
                   std::string* warning = nullptr);

// C3dAttr::GetHeight (00427150): the height at world (x, z), whose origin is the map's
// centre, interpolated between the corners of the cell under it along z and then x; 0
// outside the map.
float AttrHeight(const AttrMap& map, float x, float z);

// C3dAttr.
class AttrRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const AttrMap& map() const { return map_; }
  const std::string& error() const override { return error_; }

 private:
  AttrMap map_;
  std::string error_;
};

// The original's registration (static initializer 00426a00).
void RegisterAttrTypes(ResourceManager& manager);

}  // namespace ragexe::res
