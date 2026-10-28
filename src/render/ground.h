#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "render/matrix.h"
#include "res/gnd.h"
#include "res/rsw.h"

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
  // The four corners (x, y, z) at height 0; drawing the water writes its heights into y.
  std::array<float, 12> corners{};
  // The frame the cell was last drawn in (+0x4c), so that it is drawn once a frame.
  std::uint32_t drawn_frame = 0xffffffff;
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

// The view's transform (render/matrix.h).
using ViewMatrix = Matrix;

// What the drawing reads of the SceneRenderer (SetSize 0040db70 and the projection).
struct GroundViewport {
  float scale_x = 0.0f;       // +0
  float scale_y = 0.0f;       // +4
  std::int32_t center_x = 0;  // +0x1c, the water's
  std::int32_t center_y = 0;  // +0x20
  std::int32_t width = 0;     // +0x24
  std::int32_t height = 0;    // +0x28
  std::int32_t origin_x = 0;  // +0x2c, the surfaces'
  std::int32_t origin_y = 0;  // +0x30
};

// The map's water (SetWaterParams 004bc410, and the steps 004bc450 takes).
struct GroundWater {
  float level = 0.0f;           // +0x40
  std::int32_t type = 0;        // +0x50
  std::int32_t step = 0;        // +0x48, degrees per cell
  std::int32_t phase = 0;       // +0x60, degrees
  float wave_height = 0.0f;     // +0x54
  std::int32_t interval = 3;    // +0x44, steps a texture of the animation lasts
  std::int32_t phase_step = 2;  // +0x4c, degrees the phase moves a step
  std::int32_t frame = 0;       // +0x5c, steps into the animation
};

// SetWaterParams as CWorld::LoadWorld calls it with the .rsw's water: the level, the type,
// the interval, the step and the phase step truncated (the phase step then modulo 360,
// C's remainder) and the wave height; the frame and the phase start at 0.
GroundWater WaterOfWorld(const res::World& world);

// C3dGround15 004bc450, once a frame step (CGameMode::OnFrame): the frame on by one,
// wrapping at interval · 32; the phase on by the phase step, brought back by 360 when it
// passes 180. Returns the texture it then asks TextureCache for (WaterTextureName).
std::string StepWater(GroundWater& water);

// "워터\water%d%02d.jpg" (0060b0c0) in CP949, under data\texture\: the type modulo 6 and
// the texture of the animation, 0 to 31.
std::string WaterTextureName(std::int32_t type, std::int32_t index);

// g_WorldColorFade's multipliers (00686598 + 0x2c and + 0x38).
struct GroundColorFade {
  std::array<float, 3> water{1.0f, 1.0f, 1.0f};
  std::array<float, 3> ground{1.0f, 1.0f, 1.0f};
};

struct GroundDrawSettings {
  GroundViewport viewport;
  GroundWater water;
  GroundColorFade fade;
  // 00679f9c, set by CPlayer and CGameMode: the ground takes the water's multipliers and
  // the water an alpha of 0x90 with them.
  bool alternate_fade = false;
  std::uint32_t frame = 0;  // the renderer's frame count (+0x34)
};

// A transformed vertex, as Direct3D's 0x1c4 format holds it.
struct ScreenVertex {
  float x = 0.0f, y = 0.0f, z = 0.0f, rhw = 0.0f;
  std::uint32_t diffuse = 0;
  std::uint32_t specular = 0xff000000;
  float u = 0.0f, v = 0.0f;
};

// A quad the drawing hands AddPrimitive: a surface (flags 0x400, the ground list), or a
// cell's water (flags 1, translucent).
struct GroundPrimitive {
  std::uint32_t flags = 0;
  std::int32_t texture = -1;        // the surface's texture index; -1 for the water's
  std::int32_t lightmap_page = -1;  // the atlas page, with ISLIGHTMAP
  std::array<ScreenVertex, 4> vertices{};
  std::array<float, 8> lightmap_uv{};  // the second coordinates, the surface's
};

// The cells of a rectangle, inclusive: x0, z0, x1, z1.
using CellRect = std::array<std::int32_t, 4>;

// DrawCells (C3dGround15 slot 4, 004be500): each cell of the rectangle not yet drawn this
// frame, its water when the top dips below the level and faces the view, then its top
// and walls. With cull, a surface wholly off the viewport is left out.
void DrawGroundCells(GroundMesh& mesh, const ViewMatrix& view, const CellRect& rect, bool cull,
                     const GroundDrawSettings& settings, std::vector<GroundPrimitive>& out);

}  // namespace ragexe::render
