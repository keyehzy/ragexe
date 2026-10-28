#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "res/gnd.h"

namespace ragexe::render {

// Where an 8 x 8 lightmap sits in the atlas (docs/render/ground.md).
struct LightmapBlock {
  std::uint32_t page = 0;
  // (u, v) of the four corners in the cell's corner order, inset by one texel.
  std::array<float, 8> uv{};
  // The lightmap's texel (4, 4), red, green and blue.
  std::array<std::uint8_t, 3> color{};
};

// LightmapAtlas::Build (004bbcc0): 1024 lightmaps to a 256 x 256 page of ARGB texels, 32
// across. A page's texels past its last lightmap keep what the page before left there,
// zeros on the first.
struct LightmapAtlas {
  static constexpr std::uint32_t kPageSize = 256;
  static constexpr std::uint32_t kBlocksPerPage = 1024;
  std::vector<LightmapBlock> blocks;
  std::vector<std::vector<std::uint32_t>> pages;
};

LightmapAtlas BuildLightmapAtlas(const res::Ground& ground);

// A vertex of a ground surface, as C3dGround15 keeps it: 32 bytes.
struct GroundVertex {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  std::uint32_t color = 0;  // ARGB, lit
  float u = 0.0f, v = 0.0f;
  float lightmap_u = 0.0f, lightmap_v = 0.0f;
};

// A surface of the ground file, with the four vertices of the last cell part that used it.
struct GroundSurfaceMesh {
  bool used = false;
  std::int32_t texture = 0;          // the texture name's index
  std::int32_t lightmap_block = -1;  // with ISLIGHTMAP
  std::uint32_t color = 0;           // the file's surface colour
  std::array<GroundVertex, 4> vertices{};
};

struct GroundCellMesh {
  // Surface indices, -1 for none: the top, the wall toward z + 1 and the wall toward x + 1.
  std::array<std::int32_t, 3> surfaces{-1, -1, -1};
  // The top's lightmap colour doubled and clamped to 255, white without ISLIGHTMAP or a top.
  std::array<std::uint8_t, 3> lightmap_color{0xff, 0xff, 0xff};
};

// The map's light (0x67bf80, 0x67bf70, 0x67bf60, from the .rsw).
struct GroundLight {
  std::array<float, 3> direction{};
  std::array<float, 3> diffuse{};
  std::array<float, 3> ambient{};
};

struct GroundMesh {
  std::int32_t width = 0;
  std::int32_t height = 0;
  float cell_size = 0.0f;
  bool lightmaps = false;
  std::vector<GroundCellMesh> cells;  // row by row
  std::vector<GroundSurfaceMesh> surfaces;
  LightmapAtlas atlas;
};

// C3dGround15::Build (vf03, 004bcc30), with ISLIGHTMAP as lightmaps: each cell's top and
// walls placed and textured, then lit, with vertex normals that average the faces meeting
// at a corner. False where the original reads outside its arrays: a texture index outside
// the names, or a lightmap index past the lightmaps.
bool BuildGround(const res::Ground& ground, const GroundLight& light, bool lightmaps,
                 GroundMesh& out, std::string* error);

}  // namespace ragexe::render
