#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/angles.h"
#include "render/ground.h"
#include "res/gnd.h"

using namespace ragexe::render;

namespace {

struct DrawnPrimitive {
  std::uint32_t flags;
  std::int32_t texture, page;
  std::uint32_t count, type;
  std::uint32_t vertices[32];
  std::uint32_t second[8];
};

struct DrawnCell {
  std::uint32_t frame;
  std::uint32_t heights[4];
};

struct WaterVector {
  std::uint32_t level;
  std::int32_t type, step, phase;
  std::uint32_t wave_height;
};

struct ViewportVector {
  std::uint32_t scale_x, scale_y;
  std::int32_t center_x, center_y, width, height, origin_x, origin_y;
};

struct DrawVector {
  const char* const* file;
  int lightmaps, cull, alternate;
  WaterVector water;
  std::uint32_t matrix[12];
  std::int32_t rect[4];
  ViewportVector viewport;
  const DrawnPrimitive* primitives;
  std::size_t primitive_count;
  const DrawnCell* cells;
  std::size_t cell_count;
};

struct CursorAttrCell {
  std::uint32_t heights[4];
  std::int32_t attribute;
};

struct CursorVector {
  int view, x, z;
  std::uint32_t color;
  int drawn;
  std::uint32_t vertices[32];
  const char* texture;
  int src_blend, dest_blend;
};

struct CellAttributeVector {
  int x, z, attribute;
};

#include "ground_draw_vectors.inc"

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

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

}  // namespace

TEST_CASE("the angle tables match InitAngleTables") {
  const auto& t = ragexe::core::GetAngleTables();
  for (std::size_t k = 0; k < 361; ++k) {
    INFO(k << " degrees");
    CHECK(Bits(t.sin[k]) == kSinTable[k]);
    CHECK(Bits(t.cos[k]) == kCosTable[k]);
  }
  CHECK(ragexe::core::SinDegrees(-1) == t.sin[360]);
  CHECK(ragexe::core::SinDegrees(-90) == t.sin[271]);
  CHECK(ragexe::core::SinDegrees(450) == t.sin[90]);
  CHECK(ragexe::core::SinDegrees(-450) == t.sin[271]);
  CHECK(ragexe::core::CosDegrees(-360) == t.cos[1]);
}

TEST_CASE("DrawGroundCells adds the original's primitives") {
  for (std::size_t n = 0; n < std::size(kDrawVectors); ++n) {
    const DrawVector& v = kDrawVectors[n];
    INFO("case " << n);
    ragexe::res::Ground ground;
    std::string error;
    REQUIRE(ragexe::res::DecodeGround(FromHex(v.file), ground, &error));
    GroundLight light;
    for (std::size_t k = 0; k < 3; ++k) {
      light.direction[k] = F(kDrawLight[k]);
      light.diffuse[k] = F(kDrawLight[3 + k]);
      light.ambient[k] = F(kDrawLight[6 + k]);
    }
    GroundMesh mesh;
    REQUIRE(BuildGround(ground, light, v.lightmaps != 0, mesh, &error));
    GroundDrawSettings settings;
    settings.viewport = {F(v.viewport.scale_x), F(v.viewport.scale_y), v.viewport.center_x,
                         v.viewport.center_y,   v.viewport.width,      v.viewport.height,
                         v.viewport.origin_x,   v.viewport.origin_y};
    settings.water.level = F(v.water.level);
    settings.water.type = v.water.type;
    settings.water.step = v.water.step;
    settings.water.phase = v.water.phase;
    settings.water.wave_height = F(v.water.wave_height);
    settings.fade.water = {F(kDrawFade[0]), F(kDrawFade[1]), F(kDrawFade[2])};
    settings.fade.ground = {F(kDrawFade[3]), F(kDrawFade[4]), F(kDrawFade[5])};
    settings.alternate_fade = v.alternate != 0;
    settings.frame = kDrawFrame;
    ViewMatrix view{};
    for (std::size_t k = 0; k < 12; ++k) view[k] = F(v.matrix[k]);
    std::vector<GroundPrimitive> drawn;
    DrawGroundCells(mesh, view, {v.rect[0], v.rect[1], v.rect[2], v.rect[3]}, v.cull != 0, settings,
                    drawn);

    // The original's lists: the ground's in the order added, then the translucent ones.
    std::vector<const GroundPrimitive*> ordered;
    for (const auto& p : drawn) {
      if (p.flags == 0x400) ordered.push_back(&p);
    }
    for (const auto& p : drawn) {
      if (p.flags == 1) ordered.push_back(&p);
    }
    REQUIRE(ordered.size() == v.primitive_count);
    for (std::size_t i = 0; i < v.primitive_count; ++i) {
      INFO("primitive " << i);
      const DrawnPrimitive& want = v.primitives[i];
      const GroundPrimitive& got = *ordered[i];
      CHECK(got.flags == want.flags);
      CHECK(got.texture == want.texture);
      CHECK(got.lightmap_page == want.page);
      CHECK(want.count == 4);
      CHECK(want.type == 5);
      for (std::size_t k = 0; k < 4; ++k) {
        INFO("vertex " << k);
        const ScreenVertex& s = got.vertices[k];
        const std::uint32_t* w = want.vertices + k * 8;
        CHECK(Bits(s.x) == w[0]);
        CHECK(Bits(s.y) == w[1]);
        CHECK(Bits(s.z) == w[2]);
        CHECK(Bits(s.rhw) == w[3]);
        CHECK(s.diffuse == w[4]);
        CHECK(s.specular == w[5]);
        CHECK(Bits(s.u) == w[6]);
        CHECK(Bits(s.v) == w[7]);
      }
      if (want.flags == 0x400 && v.lightmaps != 0) {
        for (std::size_t k = 0; k < 8; ++k) CHECK(Bits(got.lightmap_uv[k]) == want.second[k]);
      }
    }
    REQUIRE(mesh.cells.size() == v.cell_count);
    for (std::int32_t z = v.rect[1]; z <= v.rect[3]; ++z) {
      for (std::int32_t x = v.rect[0]; x <= v.rect[2]; ++x) {
        const std::size_t c = static_cast<std::size_t>(z) * static_cast<std::size_t>(mesh.width) +
                              static_cast<std::size_t>(x);
        INFO("cell " << x << "," << z);
        CHECK(mesh.cells[c].drawn_frame == v.cells[c].frame);
        if (mesh.cells[c].surfaces[0] < 0) continue;
        for (std::size_t k = 0; k < 4; ++k) {
          CHECK(Bits(mesh.cells[c].corners[k * 3 + 1]) == v.cells[c].heights[k]);
        }
      }
    }
  }
}

namespace {

ragexe::res::AttrMap CursorAttr() {
  ragexe::res::AttrMap attr;
  attr.width = kCursorAttrWidth;
  attr.height = kCursorAttrHeight;
  for (const CursorAttrCell& c : kCursorAttr) {
    ragexe::res::AttrCell cell;
    for (std::size_t k = 0; k < 4; ++k) cell.heights[k] = F(c.heights[k]);
    cell.attribute = c.attribute;
    attr.cells.push_back(cell);
  }
  return attr;
}

}  // namespace

TEST_CASE("The cell cursor draws the original's quad") {
  ragexe::res::Ground ground;
  std::string error;
  REQUIRE(ragexe::res::DecodeGround(FromHex(kCursorGround), ground, &error));
  GroundLight light;
  for (std::size_t k = 0; k < 3; ++k) {
    light.direction[k] = F(kDrawLight[k]);
    light.diffuse[k] = F(kDrawLight[3 + k]);
    light.ambient[k] = F(kDrawLight[6 + k]);
  }
  GroundMesh mesh;
  REQUIRE(BuildGround(ground, light, true, mesh, &error));
  const ragexe::res::AttrMap attr = CursorAttr();
  const ViewportVector& vp = kDrawVectors[0].viewport;
  const GroundViewport viewport{F(vp.scale_x), F(vp.scale_y), vp.center_x, vp.center_y,
                                vp.width,      vp.height,     vp.origin_x, vp.origin_y};
  std::size_t n = 0;
  for (const CursorVector& v : kCursorVectors) {
    INFO("cursor " << n++ << " at " << v.x << ", " << v.z);
    ViewMatrix view{};
    for (std::size_t k = 0; k < 12; ++k)
      view[k] = F(kCursorViews[static_cast<std::size_t>(v.view)][k]);
    std::array<ScreenVertex, 4> quad{};
    REQUIRE(DrawCellCursor(mesh, attr, view, viewport, v.x, v.z, v.color, quad) == (v.drawn != 0));
    if (v.drawn == 0) continue;
    CHECK(std::string(v.texture) == "grid.tga");
    CHECK(v.src_blend == 5);
    CHECK(v.dest_blend == 6);
    for (std::size_t k = 0; k < 4; ++k) {
      INFO("vertex " << k);
      const ScreenVertex& q = quad[k];
      const std::uint32_t got[8] = {Bits(q.x), Bits(q.y),  Bits(q.z), Bits(q.rhw),
                                    q.diffuse, q.specular, Bits(q.u), Bits(q.v)};
      for (std::size_t i = 0; i < 8; ++i) {
        INFO("field " << i);
        CHECK(got[i] == v.vertices[k * 8 + i]);
      }
    }
  }
}

TEST_CASE("The cell cursor reads the attribute the original does") {
  const ragexe::res::AttrMap attr = CursorAttr();
  for (const CellAttributeVector& v : kCellAttributes) {
    INFO("cell " << v.x << ", " << v.z);
    CellCursor cursor;
    PlaceCellCursor(cursor, attr, v.x, v.z);
    CHECK(cursor.x == v.x);
    CHECK(cursor.z == v.z);
    CHECK(cursor.hidden == (v.attribute == 1 || v.attribute == 5));
    const std::uint32_t color = v.attribute == 1   ? 0xfeff0000u
                                : v.attribute == 5 ? 0xfeffff00u
                                                   : 0x6600ffaau;
    CHECK(cursor.color == color);
  }
}
