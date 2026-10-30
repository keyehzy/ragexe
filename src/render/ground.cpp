#include "render/ground.h"

#include <cmath>
#include <cstddef>
#include <cstdio>

#include "core/angles.h"

namespace ragexe::render {

namespace {

constexpr float kTexel = 1.0f / 256.0f;  // 005e2668

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
      mesh.corners = {x0, 0.0f, z0, x1, 0.0f, z0, x0, 0.0f, z1, x1, 0.0f, z1};
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

namespace {

constexpr float kNear = 10.0f;     // 005e88f8
constexpr float kFar = 1500.0f;    // 005e88f4
constexpr float kWaterUv = 0.25f;  // 00679f98

// far / (far − near), rounded once.
float DepthScale() {
  const float range = kFar - kNear;
  return kFar / range;
}

// The z the transforms keep: at least 1, which a NaN also becomes.
float NearClamped(float z) { return z < 1.0f || std::isnan(z) ? 1.0f : z; }

std::uint32_t ScaleColor(std::uint32_t color, const std::array<float, 3>& f) {
  const auto channel = [](std::uint32_t byte, float factor) {
    return Ftol(static_cast<float>(byte) * factor) & 0xff;
  };
  return (color & 0xff000000u) | channel(color >> 16 & 0xff, f[0]) << 16 |
         channel(color >> 8 & 0xff, f[1]) << 8 | channel(color & 0xff, f[2]);
}

// The smallest and largest of four values as the original picks them, NaN included.
float Smallest(float a, float b, float c, float d) {
  const float ab = b < a ? b : a;
  const float cd = c < d ? c : d;
  return cd <= ab ? cd : ab;
}
float Largest(float a, float b, float c, float d) {
  const float ab = b < a ? a : b;
  const float cd = d < c ? c : d;
  return ab <= cd ? cd : ab;
}

// 004bea30: a surface's four vertices through the view onto the screen, coloured, as a
// primitive for the ground list.
void DrawSurface(const GroundMesh& mesh, const GroundSurfaceMesh& surface, const ViewMatrix& m,
                 bool cull, const GroundDrawSettings& settings, std::vector<GroundPrimitive>& out) {
  const GroundViewport& vp = settings.viewport;
  const float scale = DepthScale();
  const float origin_x = static_cast<float>(vp.origin_x);
  const float origin_y = static_cast<float>(vp.origin_y);
  GroundPrimitive p;
  p.flags = 0x400;
  for (std::size_t k = 0; k < 4; ++k) {
    const GroundVertex& v = surface.vertices[k];
    const float tx = ((v.z * m[6] + m[0] * v.x) + v.y * m[3]) + m[9];
    const float ty = ((v.z * m[7] + m[1] * v.x) + v.y * m[4]) + m[10];
    const float tz = ((v.z * m[8] + m[2] * v.x) + v.y * m[5]) + m[11];
    const float rhw = 1.0f / NearClamped(tz);
    ScreenVertex& s = p.vertices[k];
    s.rhw = rhw;
    const float rx = rhw * vp.scale_x;
    s.x = rx * tx + origin_x;
    const float ry = rhw * vp.scale_y;
    s.y = ry * ty + origin_y;
    const float w = 1.0f / rhw;
    const float d = w - kNear;
    const float ds = d * scale;
    s.z = ds * rhw;
  }
  if (cull) {
    const auto& q = p.vertices;
    if (static_cast<float>(vp.width) <= Smallest(q[0].x, q[1].x, q[2].x, q[3].x)) return;
    if (Largest(q[0].x, q[1].x, q[2].x, q[3].x) < 0.0f) return;
    if (static_cast<float>(vp.height) <= Smallest(q[0].y, q[1].y, q[2].y, q[3].y)) return;
    if (Largest(q[0].y, q[1].y, q[2].y, q[3].y) < 0.0f) return;
  }
  const auto& fade = settings.alternate_fade ? settings.fade.water : settings.fade.ground;
  for (std::size_t k = 0; k < 4; ++k) {
    const GroundVertex& v = surface.vertices[k];
    p.vertices[k].diffuse = ScaleColor(v.color, fade);
    p.vertices[k].u = v.u;
    p.vertices[k].v = v.v;
    p.lightmap_uv[k * 2] = v.lightmap_u;
    p.lightmap_uv[k * 2 + 1] = v.lightmap_v;
  }
  p.texture = surface.texture;
  if (mesh.lightmaps && surface.lightmap_block >= 0) {
    p.lightmap_page = static_cast<std::int32_t>(
        mesh.atlas.blocks[static_cast<std::size_t>(surface.lightmap_block)].page);
  }
  out.push_back(p);
}

// The water over a cell whose top dips below it (in the y-down world, a top vertex above
// the level's y), when its quad faces the view.
void DrawWater(GroundMesh& mesh, GroundCellMesh& cell, std::int32_t x, std::int32_t z,
               const ViewMatrix& m, const GroundDrawSettings& settings,
               std::vector<GroundPrimitive>& out) {
  const GroundWater& water = settings.water;
  float middle = water.level;
  float before = water.level;
  float after = water.level;
  if (water.wave_height != 0.0f) {
    const auto wave = [&](std::int32_t at) {
      const float angle = static_cast<float>(at * water.step + water.phase);
      const float sine = core::SinDegrees(static_cast<std::int32_t>(Ftol(angle)));
      const float scaled = sine * water.wave_height;
      return scaled + water.level;
    };
    middle = wave(x + z);
    before = wave((x - 1) + z);
    after = wave(x + 1 + z);
  }
  const auto& top = mesh.surfaces[static_cast<std::size_t>(cell.surfaces[0])].vertices;
  const bool below =
      middle < top[0].y || middle < top[1].y || middle < top[2].y || middle < top[3].y;
  if (!below) return;
  cell.corners[1] = before;
  cell.corners[10] = after;
  cell.corners[4] = middle;
  cell.corners[7] = middle;
  const GroundViewport& vp = settings.viewport;
  const float scale = DepthScale();
  std::array<ScreenVertex, 4> q{};
  for (std::size_t k = 0; k < 4; ++k) {
    const float vx = cell.corners[k * 3];
    const float vy = cell.corners[k * 3 + 1];
    const float vz = cell.corners[k * 3 + 2];
    const float tx = ((vy * m[3] + m[6] * vz) + m[0] * vx) + m[9];
    const float ty = ((m[4] * vy + m[7] * vz) + vx * m[1]) + m[10];
    const float tz = ((m[5] * vy + m[8] * vz) + vx * m[2]) + m[11];
    const float rhw = 1.0f / NearClamped(tz);
    q[k].rhw = rhw;
    const float rx = rhw * vp.scale_x;
    q[k].x = rx * tx + static_cast<float>(vp.center_x);
    const float yr = ty * rhw;
    q[k].y = yr * vp.scale_y + static_cast<float>(vp.center_y);
    const float w = 1.0f / rhw;
    const float d = w - kNear;
    const float ds = d * scale;
    q[k].z = ds * rhw;
  }
  const float a = (q[2].y - q[0].y) * (q[1].x - q[0].x);
  const float b = (q[1].y - q[0].y) * (q[2].x - q[0].x);
  if (!(b > a)) return;
  GroundPrimitive p;
  p.flags = 1;
  p.vertices = q;
  std::uint32_t color = 0x90ffffff;
  const auto byte = [](float f) {
    const float scaled = f * 255.0f;
    return Ftol(scaled) & 0xff;
  };
  const auto& f = settings.fade.water;
  if (settings.alternate_fade) {
    color = 0x90000000u | byte(f[0]) << 16 | byte(f[1]) << 8 | byte(f[2]);
  } else if (water.type == 4) {
    color = 0xff000000u | byte(f[0]) << 16 | byte(f[1]) << 8 | byte(f[2]);
  }
  const float u0 = static_cast<float>(x % 4);
  const float v0 = static_cast<float>(z % 4);
  const float u1 = u0 + 1.0f;
  const float v1 = v0 + 1.0f;
  const std::array<float, 8> uv{u0 * kWaterUv, v0 * kWaterUv, u1 * kWaterUv, v0 * kWaterUv,
                                u0 * kWaterUv, v1 * kWaterUv, u1 * kWaterUv, v1 * kWaterUv};
  for (std::size_t k = 0; k < 4; ++k) {
    p.vertices[k].diffuse = color;
    p.vertices[k].specular = 0xff000000;
    p.vertices[k].u = uv[k * 2];
    p.vertices[k].v = uv[k * 2 + 1];
  }
  out.push_back(p);
}

}  // namespace

void DrawGroundCells(GroundMesh& mesh, const ViewMatrix& view, const CellRect& rect, bool cull,
                     const GroundDrawSettings& settings, std::vector<GroundPrimitive>& out) {
  for (std::int32_t z = rect[1]; z <= rect[3]; ++z) {
    for (std::int32_t x = rect[0]; x <= rect[2]; ++x) {
      GroundCellMesh& cell = mesh.cells[static_cast<std::size_t>(z) * mesh.width + x];
      if (cell.drawn_frame == settings.frame) continue;
      cell.drawn_frame = settings.frame;
      if (cell.surfaces[0] >= 0) {
        DrawWater(mesh, cell, x, z, view, settings, out);
        DrawSurface(mesh, mesh.surfaces[static_cast<std::size_t>(cell.surfaces[0])], view, cull,
                    settings, out);
      }
      if (cell.surfaces[1] >= 0) {
        DrawSurface(mesh, mesh.surfaces[static_cast<std::size_t>(cell.surfaces[1])], view, cull,
                    settings, out);
      }
      if (cell.surfaces[2] >= 0) {
        DrawSurface(mesh, mesh.surfaces[static_cast<std::size_t>(cell.surfaces[2])], view, cull,
                    settings, out);
      }
    }
  }
}

GroundWater WaterOfWorld(const res::World& world) {
  const auto truncate = [](float v) { return static_cast<std::int32_t>(Ftol(v)); };
  GroundWater w;
  w.level = world.water_level;
  w.type = world.water_type;
  w.interval = world.water_frame_interval;
  w.step = truncate(world.water_param_178);
  w.phase_step = truncate(world.water_phase_step) % 360;
  w.wave_height = world.water_param_170;
  return w;
}

std::string StepWater(GroundWater& water) {
  water.frame = water.frame + 1;
  if (water.frame >= water.interval * 32) water.frame = 0;
  const std::int32_t index = water.interval != 0 ? (water.frame / water.interval) % 32 : 0;
  water.phase = water.phase + water.phase_step;
  if (water.phase > 180) water.phase = water.phase - 360;
  return WaterTextureName(water.type, index);
}

std::string WaterTextureName(std::int32_t type, std::int32_t index) {
  char name[32];
  std::snprintf(name, sizeof name, "\xbf\xf6\xc5\xcd\\water%d%02d.jpg", type % 6, index);
  return name;
}

void PlaceCellCursor(CellCursor& cursor, const res::AttrMap& attr, std::int32_t x, std::int32_t z) {
  cursor.x = x;
  cursor.z = z;
  cursor.hidden = false;
  std::int32_t type = 1;
  if (x >= 0 && x < attr.width && z >= 0 && z < attr.height)
    type = attr.cells[static_cast<std::size_t>(attr.width) * static_cast<std::size_t>(z) +
                      static_cast<std::size_t>(x)]
               .attribute;
  if (type == 1 || type == 5) {
    cursor.hidden = true;
    cursor.color = type == 1 ? 0xfeff0000u : 0xfeffff00u;
  } else {
    cursor.color = 0x6600ffaau;
  }
}

bool DrawCellCursor(const GroundMesh& mesh, const res::AttrMap& attr, const ViewMatrix& view,
                    const GroundViewport& viewport, std::int32_t x, std::int32_t z,
                    std::uint32_t color, std::array<ScreenVertex, 4>& out) {
  constexpr float kNearer = 1.99999994948e-05f;  // 005e8944, added to each rhw
  if (x < 0 || x / 2 >= mesh.width || z < 0 || z / 2 >= mesh.height) return false;
  const GroundCellMesh& cell =
      mesh.cells[static_cast<std::size_t>(mesh.width) * static_cast<std::size_t>(z / 2) +
                 static_cast<std::size_t>(x / 2)];
  if (cell.surfaces[0] < 0) return false;
  // C3dGround15::GetCellCursorCorners (004c02d0): the top surface's corners, which a surface shared
  // with other cells holds as the last of them placed it; C3dGround::SetAttrHeights (004bc500): the
  // heights the attribute cell's.
  std::array<Vec3, 4> c{};
  const GroundSurfaceMesh& top = mesh.surfaces[static_cast<std::size_t>(cell.surfaces[0])];
  for (std::size_t k = 0; k < 4; ++k)
    c[k] = {top.vertices[k].x, top.vertices[k].y, top.vertices[k].z};
  if (x < attr.width && z < attr.height) {
    const res::AttrCell& a =
        attr.cells[static_cast<std::size_t>(attr.width) * static_cast<std::size_t>(z) +
                   static_cast<std::size_t>(x)];
    for (std::size_t k = 0; k < 4; ++k) c[k][1] = a.heights[k];
  }
  // Halfway to the far side for an odd x or z, to the near side for an even one; corners 1
  // and 2 hold the far x and z, corner 0 the near ones.
  const bool odd_x = x % 2 != 0;
  const bool odd_z = z % 2 != 0;
  for (Vec3& p : c) {
    if (odd_x) {
      if (p[0] < c[1][0]) p[0] = (c[1][0] - p[0]) * 0.5f + p[0];
    } else if (p[0] > c[0][0]) {
      p[0] = p[0] - (p[0] - c[0][0]) * 0.5f;
    }
    if (odd_z) {
      if (p[2] < c[2][2]) p[2] = (c[2][2] - p[2]) * 0.5f + p[2];
    } else if (p[2] > c[0][2]) {
      p[2] = p[2] - (p[2] - c[0][2]) * 0.5f;
    }
  }
  const float scale = DepthScale();
  const float origin_x = static_cast<float>(viewport.origin_x);
  const float origin_y = static_cast<float>(viewport.origin_y);
  static constexpr std::array<float, 8> kUv = {0, 0, 1, 0, 0, 1, 1, 1};  // 00679f78
  const ViewMatrix& m = view;
  for (std::size_t k = 0; k < 4; ++k) {
    const Vec3& p = c[k];
    const float tx = ((p[1] * m[3] + m[0] * p[0]) + p[2] * m[6]) + m[9];
    const float ty = ((m[4] * p[1] + p[0] * m[1]) + p[2] * m[7]) + m[10];
    const float tz = ((m[5] * p[1] + p[0] * m[2]) + p[2] * m[8]) + m[11];
    const float rhw = 1.0f / NearClamped(tz);
    ScreenVertex& s = out[k];
    s.x = (rhw * viewport.scale_x) * tx + origin_x;
    s.y = (rhw * ty) * viewport.scale_y + origin_y;
    s.rhw = rhw + kNearer;
    s.z = ((1.0f / s.rhw - kNear) * scale) * s.rhw;
    s.diffuse = color;
    s.specular = 0xff000000;
    s.u = kUv[2 * k];
    s.v = kUv[2 * k + 1];
  }
  return true;
}

}  // namespace ragexe::render
