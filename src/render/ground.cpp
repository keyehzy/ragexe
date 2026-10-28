#include "render/ground.h"

#include <cmath>
#include <cstddef>

namespace ragexe::render {

namespace {

constexpr float kTexel = 1.0f / 256.0f;  // 005e2668

using Vec3 = std::array<float, 3>;

// The two triangle normals of a cell's top, then its two wall normals, as Build keeps
// them while it works (0x30 bytes a cell). Cells without a top keep zeros.
struct CellNormals {
  Vec3 first{}, second{}, north{}, east{};
};

bool Fail(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
  return false;
}

// 1 / sqrt((x·x + y·y) + z·z), each operation rounded to single precision.
float InverseLength(float x, float y, float z) {
  const float xx = x * x;
  const float yy = y * y;
  const float zz = z * z;
  const float sum = (xx + yy) + zz;
  return 1.0f / std::sqrt(sum);
}

void Scale(Vec3& v, float r) {
  v[0] = r * v[0];
  v[1] = r * v[1];
  v[2] = r * v[2];
}

Vec3 Add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }

// __ftol (005c2084): a truncation to 64 bits; values it cannot hold, NaN among them,
// give the low dword of 0x8000000000000000.
std::uint32_t Ftol(float v) {
  if (std::isnan(v) || v <= -9.2233720368547758e18f || v >= 9.2233720368547758e18f) return 0;
  return static_cast<std::uint32_t>(static_cast<std::int64_t>(v));
}

// A vertex colour: each of the colour's red, green and blue times the light on that
// channel, intensity × diffuse + ambient, at most 1. Alpha is 0xff.
std::uint32_t LitColor(std::uint32_t color, float intensity, const GroundLight& light) {
  std::array<float, 3> f{};
  for (std::size_t c = 0; c < 3; ++c) {
    const float diffuse = intensity * light.diffuse[c];
    f[c] = diffuse + light.ambient[c];
    if (!(f[c] < 1.0f)) f[c] = 1.0f;
  }
  const auto channel = [&](std::uint32_t byte, float factor) {
    return Ftol(static_cast<float>(byte) * factor) & 0xff;
  };
  const std::uint32_t r = channel(color >> 16 & 0xff, f[0]);
  const std::uint32_t g = channel(color >> 8 & 0xff, f[1]);
  const std::uint32_t b = channel(color & 0xff, f[2]);
  return 0xff000000u | r << 16 | g << 8 | b;
}

float ClampUnit(float v) {
  if (!(0.0f <= v)) return 0.0f;
  return v <= 1.0f ? v : 1.0f;
}

}  // namespace

LightmapAtlas BuildLightmapAtlas(const res::Ground& ground) {
  LightmapAtlas atlas;
  const auto count = static_cast<std::uint32_t>(ground.lightmaps.size());
  atlas.blocks.resize(count);
  std::vector<std::uint32_t> page(static_cast<std::size_t>(LightmapAtlas::kPageSize) *
                                  LightmapAtlas::kPageSize);
  for (std::uint32_t first = 0; first < count; first += LightmapAtlas::kBlocksPerPage) {
    const auto page_index = static_cast<std::uint32_t>(atlas.pages.size());
    for (std::uint32_t i = 0; i < LightmapAtlas::kBlocksPerPage && first + i < count; ++i) {
      const res::GroundLightmap& lm = ground.lightmaps[first + i];
      LightmapBlock& block = atlas.blocks[first + i];
      const std::uint32_t column = i % 32;
      const std::uint32_t row = i / 32;
      for (std::uint32_t y = 0; y < 8; ++y) {
        for (std::uint32_t x = 0; x < 8; ++x) {
          const std::size_t k = std::size_t{y} * 8 + x;
          const std::size_t texel =
              (std::size_t{row} * 8 + y) * LightmapAtlas::kPageSize + std::size_t{column} * 8 + x;
          page[texel] = static_cast<std::uint32_t>(lm.alpha[k]) << 24 |
                        static_cast<std::uint32_t>(lm.color[k * 3]) << 16 |
                        static_cast<std::uint32_t>(lm.color[k * 3 + 1]) << 8 | lm.color[k * 3 + 2];
        }
      }
      const float u0 = ClampUnit(static_cast<float>(column * 8 + 1) * kTexel);
      const float v0 = ClampUnit(static_cast<float>(row * 8 + 1) * kTexel);
      const float u1 = ClampUnit(static_cast<float>(column * 8 + 7) * kTexel);
      const float v1 = ClampUnit(static_cast<float>(row * 8 + 7) * kTexel);
      block.uv = {u0, v0, u1, v0, u0, v1, u1, v1};
      // Texel (4, 4): byte 0x6c of the colours, 0xac of the record.
      block.color = {lm.color[0x6c], lm.color[0x6d], lm.color[0x6e]};
      block.page = page_index;
    }
    atlas.pages.push_back(page);
  }
  return atlas;
}

bool BuildGround(const res::Ground& ground, const GroundLight& light, bool lightmaps,
                 GroundMesh& out, std::string* error) {
  out = GroundMesh{};
  out.width = ground.width;
  out.height = ground.height;
  out.cell_size = ground.cell_size;
  out.lightmaps = lightmaps;
  const std::int32_t w = ground.width;
  const std::int32_t h = ground.height;
  const float s = ground.cell_size;
  const std::int32_t half_w = w / 2;  // C's integer division, as the original's
  const std::int32_t half_h = h / 2;
  if (lightmaps) out.atlas = BuildLightmapAtlas(ground);
  out.surfaces.resize(ground.surfaces.size());
  out.cells.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
  std::vector<CellNormals> normals(out.cells.size());
  const auto count = static_cast<std::int32_t>(ground.surfaces.size());
  const auto cell = [&](std::int32_t x, std::int32_t z) -> const res::GroundCell& {
    return ground.cells[static_cast<std::size_t>(z) * w + x];
  };
  const auto at = [&](std::int32_t x, std::int32_t z) {
    return static_cast<std::size_t>(z) * w + x;
  };

  // Places a surface: its colour, texture, the four corners and their coordinates.
  const auto place = [&](std::int32_t index, const std::array<Vec3, 4>& corners) -> bool {
    const res::GroundSurface& src = ground.surfaces[static_cast<std::size_t>(index)];
    GroundSurfaceMesh& dst = out.surfaces[static_cast<std::size_t>(index)];
    dst.used = true;
    dst.color = src.color;
    if (src.texture < 0 || static_cast<std::size_t>(src.texture) >= ground.textures.size()) {
      return Fail(error, "a surface's texture index is outside the texture names");
    }
    dst.texture = src.texture;
    for (std::size_t k = 0; k < 4; ++k) {
      dst.vertices[k].x = corners[k][0];
      dst.vertices[k].y = corners[k][1];
      dst.vertices[k].z = corners[k][2];
      dst.vertices[k].u = src.u[k];
      dst.vertices[k].v = src.v[k];
    }
    if (lightmaps) {
      if (src.lightmap >= out.atlas.blocks.size()) {
        return Fail(error, "a surface's lightmap index is past the lightmaps");
      }
      dst.lightmap_block = src.lightmap;
      const LightmapBlock& block = out.atlas.blocks[src.lightmap];
      for (std::size_t k = 0; k < 4; ++k) {
        dst.vertices[k].lightmap_u = block.uv[k * 2];
        dst.vertices[k].lightmap_v = block.uv[k * 2 + 1];
      }
    }
    return true;
  };

  for (std::int32_t z = 0; z < h; ++z) {
    for (std::int32_t x = 0; x < w; ++x) {
      const res::GroundCell& c = cell(x, z);
      GroundCellMesh& mesh = out.cells[at(x, z)];
      CellNormals& n = normals[at(x, z)];
      const float x0 = static_cast<float>(x - half_w) * s;
      const float x1 = static_cast<float>((x - half_w) + 1) * s;
      const float z0 = static_cast<float>(z - half_h) * s;
      const float z1 = static_cast<float>((z - half_h) + 1) * s;
      const auto& hh = c.heights;
      if (const std::int32_t top = c.surfaces[0]; top > -1 && top < count) {
        mesh.surfaces[0] = top;
        if (!place(top, {Vec3{x0, hh[0], z0}, Vec3{x1, hh[1], z0}, Vec3{x0, hh[2], z1},
                         Vec3{x1, hh[3], z1}})) {
          return false;
        }
        if (lightmaps) {
          const auto& color = out.atlas.blocks[ground.surfaces[top].lightmap].color;
          for (std::size_t k = 0; k < 3; ++k) {
            const std::uint32_t doubled = static_cast<std::uint32_t>(color[k]) * 2;
            mesh.lightmap_color[k] = static_cast<std::uint8_t>(doubled > 0xfe ? 0xff : doubled);
          }
        }
        // The first triangle's normal, from (0, h0, 0), (s, h1, 0) and (0, h2, s).
        const float a = hh[2] - hh[0];
        const float b = hh[2] - hh[1];
        const float neg_s = -s;
        const float bs = b * s;
        const float sa = s * a;
        n.first[0] = bs - sa;
        const float s_neg_s = s * neg_s;
        n.first[1] = -s_neg_s;
        n.first[2] = a * neg_s;
        Scale(n.first, 1.0f / std::sqrt(((n.first[0] * n.first[0]) + (n.first[2] * n.first[2])) +
                                        (n.first[1] * n.first[1])));
        // The second's, from (s, h1, 0), (0, h2, s) and (s, h3, s).
        const float cc = hh[3] - hh[2];
        // The original subtracts the cell size from itself: 0, but NaN for an infinite or
        // NaN size.
        const float d = s - s;  // NOLINT(misc-redundant-expression)
        const float e = s - s;  // NOLINT(misc-redundant-expression)
        const float f = hh[3] - hh[1];
        const float fd = f * d;
        const float sc = s * cc;
        n.second[0] = fd - sc;
        const float ss = s * s;
        const float de = d * e;
        n.second[1] = ss - de;
        const float ce = cc * e;
        const float fs = f * s;
        n.second[2] = ce - fs;
        Scale(n.second, InverseLength(n.second[0], n.second[1], n.second[2]));
      }
      if (z < h - 1) {
        const res::GroundCell& north = cell(x, z + 1);
        if (const std::int32_t wall = c.surfaces[1]; wall > -1 && wall < count) {
          mesh.surfaces[1] = wall;
          if (!place(wall, {Vec3{x0, hh[2], z1}, Vec3{x1, hh[3], z1},
                            Vec3{x0, north.heights[0], z1}, Vec3{x1, north.heights[1], z1}})) {
            return false;
          }
          n.north = {0.0f, 0.0f, north.heights[0] <= hh[2] ? 1.0f : -1.0f};
        }
      }
      if (x < w - 1) {
        const res::GroundCell& east = cell(x + 1, z);
        if (const std::int32_t wall = c.surfaces[2]; wall > -1 && wall < count) {
          mesh.surfaces[2] = wall;
          if (!place(wall, {Vec3{x1, hh[3], z1}, Vec3{x1, hh[1], z0}, Vec3{x1, east.heights[2], z1},
                            Vec3{x1, east.heights[0], z0}})) {
            return false;
          }
          n.east = {east.heights[0] <= hh[2] ? 1.0f : -1.0f, 0.0f, 0.0f};
        }
      }
    }
  }

  // The colour of a cell's top, 0 where there is no cell or top.
  const auto top_color = [&](std::int32_t x, std::int32_t z) -> std::uint32_t {
    if (x < 0 || z < 0 || x >= w || z >= h) return 0;
    const std::int32_t top = out.cells[at(x, z)].surfaces[0];
    return top < 0 ? 0 : out.surfaces[static_cast<std::size_t>(top)].color;
  };
  const auto intensity = [&](float dot) { return dot > 0.0f ? dot : 0.0f; };

  for (std::int32_t z = 0; z < h; ++z) {
    for (std::int32_t x = 0; x < w; ++x) {
      const GroundCellMesh& mesh = out.cells[at(x, z)];
      const CellNormals& n = normals[at(x, z)];
      const auto& hh = cell(x, z).heights;
      // A neighbour's heights and normals, when it exists.
      const auto near = [&](std::int32_t dx, std::int32_t dz) -> const res::GroundCell* {
        const std::int32_t nx = x + dx;
        const std::int32_t nz = z + dz;
        if (nx < 0 || nz < 0 || nx >= w || nz >= h) return nullptr;
        return &cell(nx, nz);
      };
      const auto nn = [&](std::int32_t dx, std::int32_t dz) -> const CellNormals& {
        return normals[at(x + dx, z + dz)];
      };
      std::array<Vec3, 4> corner{n.first, Add(n.first, n.second), Add(n.first, n.second), n.first};
      if (const auto* c = near(0, -1); c != nullptr && c->heights[2] == hh[0]) {
        corner[0] = Add(Add(corner[0], nn(0, -1).first), nn(0, -1).second);
      }
      if (const auto* c = near(-1, 0); c != nullptr && c->heights[1] == hh[0]) {
        corner[0] = Add(Add(corner[0], nn(-1, 0).first), nn(-1, 0).second);
      }
      if (const auto* c = near(-1, -1); c != nullptr && c->heights[3] == hh[0]) {
        corner[0] = Add(corner[0], nn(-1, -1).second);
      }
      if (const auto* c = near(1, 0); c != nullptr && c->heights[0] == hh[1]) {
        corner[1] = Add(corner[1], nn(1, 0).first);
      }
      if (const auto* c = near(0, -1); c != nullptr && c->heights[3] == hh[1]) {
        corner[1] = Add(corner[1], nn(0, -1).second);
      }
      if (const auto* c = near(1, -1); c != nullptr && c->heights[2] == hh[1]) {
        corner[1] = Add(Add(corner[1], nn(1, -1).first), nn(1, -1).second);
      }
      if (const auto* c = near(-1, 0); c != nullptr && c->heights[3] == hh[2]) {
        corner[2] = Add(corner[2], nn(-1, 0).second);
      }
      if (const auto* c = near(0, 1); c != nullptr && c->heights[0] == hh[2]) {
        corner[2] = Add(corner[2], nn(0, 1).first);
      }
      if (const auto* c = near(-1, 1); c != nullptr && c->heights[1] == hh[2]) {
        corner[2] = Add(Add(corner[2], nn(-1, 1).first), nn(-1, 1).second);
      }
      // The last corner starts from the first triangle's normal, as the original's does.
      if (const auto* c = near(1, 0); c != nullptr && c->heights[2] == hh[3]) {
        corner[3] = Add(Add(corner[3], nn(1, 0).first), nn(1, 0).second);
      }
      if (const auto* c = near(0, 1); c != nullptr && c->heights[1] == hh[3]) {
        corner[3] = Add(Add(corner[3], nn(0, 1).first), nn(0, 1).second);
      }
      if (const auto* c = near(1, 1); c != nullptr && c->heights[0] == hh[3]) {
        corner[3] = Add(corner[3], nn(1, 1).first);
      }

      const auto& l = light.direction;
      if (const std::int32_t top = mesh.surfaces[0]; top >= 0) {
        const std::array<std::uint32_t, 4> colors{top_color(x, z), top_color(x + 1, z),
                                                  top_color(x, z + 1), top_color(x + 1, z + 1)};
        for (std::size_t k = 0; k < 4; ++k) {
          Vec3& v = corner[k];
          Scale(v, InverseLength(v[0], v[1], v[2]));
          const float yl = v[1] * l[1];
          const float zl = v[2] * l[2];
          const float xl = v[0] * l[0];
          const float dot = (yl + zl) + xl;
          out.surfaces[static_cast<std::size_t>(top)].vertices[k].color =
              LitColor(colors[k], intensity(dot), light);
        }
      }
      const auto light_wall = [&](std::int32_t wall, const Vec3& normal,
                                  const std::array<std::uint32_t, 4>& colors) {
        const float yl = normal[1] * l[1];
        const float xl = l[0] * normal[0];
        const float zl = l[2] * normal[2];
        const float dot = (yl + xl) + zl;
        for (std::size_t k = 0; k < 4; ++k) {
          out.surfaces[static_cast<std::size_t>(wall)].vertices[k].color =
              LitColor(colors[k], intensity(dot), light);
        }
      };
      if (mesh.surfaces[1] >= 0) {
        const std::uint32_t n0 = top_color(x, z + 1);
        const std::uint32_t n1 = top_color(x + 1, z + 1);
        light_wall(mesh.surfaces[1], n.north, {n0, n1, n0, n1});
      }
      if (mesh.surfaces[2] >= 0) {
        const std::uint32_t ne = top_color(x + 1, z + 1);
        const std::uint32_t e = top_color(x + 1, z);
        light_wall(mesh.surfaces[2], n.east, {ne, e, ne, e});
      }
    }
  }
  return true;
}

}  // namespace ragexe::render
