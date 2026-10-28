#include "render/ground.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "res/gnd.h"

using namespace ragexe::render;

namespace {

struct CellVector {
  std::int32_t top, north, east;
  std::uint32_t east_marker;
  std::uint8_t r, g, b;
};

struct SurfaceVector {
  std::int32_t index, used, texture, page, block;
  std::uint32_t color;
  std::uint32_t vertices[32];
};

struct BlockVector {
  std::uint32_t page;
  std::uint8_t r, g, b;
  std::uint32_t uv[8];
};

struct PageVector {
  std::uint32_t width, height, format, whole;
  std::uint64_t hash;
};

struct GroundVector {
  const char* const* file;
  int lightmaps;
  std::uint32_t light[9];
  const CellVector* cells;
  std::size_t cell_count;
  const SurfaceVector* surfaces;
  std::size_t surface_count;
  const BlockVector* blocks;
  std::size_t block_count;
  const PageVector* pages;
  std::size_t page_count;
};

#include "ground_vectors.inc"

std::vector<std::uint8_t> FromHex(const char* const* pieces) {
  std::vector<std::uint8_t> out;
  const auto nibble = [](char c) {
    return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
  };
  for (; *pieces != nullptr; ++pieces) {
    for (const char* p = *pieces; p[0] != '\0' && p[1] != '\0'; p += 2) {
      out.push_back(static_cast<std::uint8_t>(nibble(p[0]) << 4 | nibble(p[1])));
    }
  }
  return out;
}

// FNV-1a over the texels' bytes: all of the page, or only the first blocks' 8 x 8 texels,
// block by block and row by row.
std::uint64_t PageHash(const std::vector<std::uint32_t>& page, std::size_t blocks, bool whole) {
  std::uint64_t h = 0xcbf29ce484222325ull;
  const auto add = [&](std::uint32_t v) {
    for (int k = 0; k < 4; ++k) h = (h ^ ((v >> (8 * k)) & 0xff)) * 0x100000001b3ull;
  };
  if (whole) {
    for (std::uint32_t v : page) add(v);
    return h;
  }
  for (std::size_t i = 0; i < blocks; ++i) {
    for (std::size_t y = 0; y < 8; ++y) {
      for (std::size_t x = 0; x < 8; ++x) add(page[(i / 32 * 8 + y) * 256 + i % 32 * 8 + x]);
    }
  }
  return h;
}

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

}  // namespace

TEST_CASE("BuildGround matches the original's C3dGround15::Build") {
  for (std::size_t n = 0; n < std::size(kGroundVectors); ++n) {
    const GroundVector& v = kGroundVectors[n];
    INFO("case " << n << ", lightmaps " << v.lightmaps);
    ragexe::res::Ground ground;
    std::string error;
    REQUIRE(ragexe::res::DecodeGround(FromHex(v.file), ground, &error));
    GroundLight light;
    for (std::size_t k = 0; k < 3; ++k) {
      light.direction[k] = std::bit_cast<float>(v.light[k]);
      light.diffuse[k] = std::bit_cast<float>(v.light[3 + k]);
      light.ambient[k] = std::bit_cast<float>(v.light[6 + k]);
    }
    GroundMesh mesh;
    REQUIRE(BuildGround(ground, light, v.lightmaps != 0, mesh, &error));
    REQUIRE(mesh.cells.size() == v.cell_count);
    for (std::size_t c = 0; c < v.cell_count; ++c) {
      INFO("cell " << c);
      const CellVector& want = v.cells[c];
      CHECK(mesh.cells[c].surfaces[0] == want.top);
      CHECK(mesh.cells[c].surfaces[1] == want.north);
      CHECK(mesh.cells[c].surfaces[2] == want.east);
      if (want.east >= 0) CHECK(want.east_marker == 0xffffffffu);
      CHECK(mesh.cells[c].lightmap_color[0] == want.r);
      CHECK(mesh.cells[c].lightmap_color[1] == want.g);
      CHECK(mesh.cells[c].lightmap_color[2] == want.b);
    }
    REQUIRE(mesh.surfaces.size() == v.surface_count);
    for (std::size_t s = 0; s < v.surface_count; ++s) {
      INFO("surface " << s);
      const SurfaceVector& want = v.surfaces[s];
      const GroundSurfaceMesh& got = mesh.surfaces[s];
      CHECK(got.used == (want.used != 0));
      CHECK(got.texture == want.texture);
      if (want.used == 0) continue;
      CHECK(got.color == want.color);
      CHECK(got.lightmap_block == want.block);
      if (want.block >= 0) {
        CHECK(static_cast<std::int32_t>(
                  mesh.atlas.blocks[static_cast<std::size_t>(want.block)].page) == want.page);
      }
      for (std::size_t k = 0; k < 4; ++k) {
        INFO("vertex " << k);
        const GroundVertex& vx = got.vertices[k];
        const std::uint32_t* w = want.vertices + k * 8;
        CHECK(Bits(vx.x) == w[0]);
        CHECK(Bits(vx.y) == w[1]);
        CHECK(Bits(vx.z) == w[2]);
        CHECK(vx.color == w[3]);
        CHECK(Bits(vx.u) == w[4]);
        CHECK(Bits(vx.v) == w[5]);
        CHECK(Bits(vx.lightmap_u) == w[6]);
        CHECK(Bits(vx.lightmap_v) == w[7]);
      }
    }
    REQUIRE(mesh.atlas.blocks.size() == v.block_count);
    for (std::size_t b = 0; b < v.block_count; ++b) {
      INFO("block " << b);
      const BlockVector& want = v.blocks[b];
      const LightmapBlock& got = mesh.atlas.blocks[b];
      CHECK(got.page == want.page);
      CHECK(got.color[0] == want.r);
      CHECK(got.color[1] == want.g);
      CHECK(got.color[2] == want.b);
      for (std::size_t k = 0; k < 8; ++k) CHECK(Bits(got.uv[k]) == want.uv[k]);
    }
    REQUIRE(mesh.atlas.pages.size() == v.page_count);
    for (std::size_t p = 0; p < v.page_count; ++p) {
      INFO("page " << p);
      CHECK(v.pages[p].width == LightmapAtlas::kPageSize);
      CHECK(v.pages[p].format == 1);
      CHECK(PageHash(mesh.atlas.pages[p], v.block_count, v.pages[p].whole != 0) == v.pages[p].hash);
    }
  }
}

TEST_CASE("BuildGround fails where the original reads outside its arrays") {
  ragexe::res::Ground ground;
  ground.width = 1;
  ground.height = 1;
  ground.textures = {"a.bmp"};
  ground.lightmaps.resize(1);
  ground.surfaces.resize(1);
  ground.cells.resize(1);
  ground.cells[0].surfaces = {0, -1, -1};
  GroundMesh mesh;
  std::string error;
  CHECK(BuildGround(ground, {}, true, mesh, &error));
  ground.surfaces[0].texture = 1;
  CHECK_FALSE(BuildGround(ground, {}, true, mesh, &error));
  ground.surfaces[0].texture = 0;
  ground.surfaces[0].lightmap = 1;
  CHECK_FALSE(BuildGround(ground, {}, true, mesh, &error));
  CHECK(BuildGround(ground, {}, false, mesh, &error));
}
