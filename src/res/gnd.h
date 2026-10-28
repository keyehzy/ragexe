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

// An 8 x 8 lightmap, as LightmapAtlas::Build (004bbcc0) reads it (docs/formats/gnd.md).
struct GroundLightmap {
  std::array<std::uint8_t, 64> alpha{};   // row by row
  std::array<std::uint8_t, 192> color{};  // R, G, B per pixel, row by row
};

// A textured quad that cells refer to.
struct GroundSurface {
  std::array<float, 4> u{};  // texture coordinates of the four corners, in the cell's
  std::array<float, 4> v{};  // corner order
  std::int16_t texture = 0;  // index into Ground::textures
  std::uint16_t lightmap = 0;
  std::uint32_t color = 0;
};

// One cell, as CGndRes::GetCell (004295f0) gives it.
struct GroundCell {
  std::array<float, 4> heights{};  // corners: (x, z), (x+1, z), (x, z+1), (x+1, z+1)
  // Surface indices, -1 for none: the top, the wall toward the next row (z + 1) and the
  // wall toward the next column (x + 1).
  std::array<std::int32_t, 3> surfaces{};
};

struct Ground {
  std::uint8_t major = 0;
  std::uint8_t minor = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  float cell_size = 10.0f;  // world units per cell (00429170)
  std::vector<std::string> textures;
  std::vector<GroundLightmap> lightmaps;
  std::vector<GroundSurface> surfaces;
  std::vector<GroundCell> cells;  // row by row
};

// CGndRes::Load (00429200) on a whole .gnd file, with each cell converted by GetCell.
bool DecodeGround(std::span<const std::uint8_t> data, Ground& out, std::string* error);

// CGndRes.
class GndRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const Ground& ground() const { return ground_; }
  const std::string& error() const override { return error_; }

 private:
  Ground ground_;
  std::string error_;
};

// The original's registration (static initializer 00428ea0).
void RegisterGroundTypes(ResourceManager& manager);

}  // namespace ragexe::res
