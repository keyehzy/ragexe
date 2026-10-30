#include "render/model.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "render/camera.h"
#include "res/rsm.h"

using namespace ragexe::render;

namespace {

struct NodeVector {
  std::uint32_t alpha;
  std::uint32_t matrix[12];
  std::uint32_t box[6];
  const std::uint32_t* shades;  // per face: 3 colours, then (u, v) at each corner
  const std::int32_t* tint;     // per face: 9
  std::size_t faces;
};

struct DrawnFace {
  std::uint32_t flags;
  std::uint32_t count;
  std::int32_t texture;
  const std::uint32_t* vertices;  // 8 dwords each
};

struct RenderVector {
  std::uint32_t view[12];
  int clip, alternate, tint, translucent;
  const DrawnFace* faces;
  std::size_t count;
};

struct ModelVector {
  const char* const* file;
  std::uint32_t placement[9];
  std::uint32_t speed;
  const std::uint32_t* instance;
  const NodeVector* nodes;
  std::size_t node_count;
  RenderVector renders[4];
  const std::uint32_t* steps;
};

struct SlerpVector {
  std::uint32_t t;
  std::uint32_t a[4], b[4];
  std::uint32_t result[4];
};

// The stand-in for C3dGround15::GetCellColor (tools/model_vectors.py).
std::array<std::int32_t, 3> CellColor(float x, float y, float z) {
  return {static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(x) >> 7 & 0x7f),
          static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(y) >> 9 & 0x7f),
          static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(z) >> 5 & 0x7f)};
}

// The stand-in for the placements of kDoublePlacements: the points' last bits.
std::array<std::int32_t, 3> LowCellColor(float x, float y, float z) {
  return {static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(x) & 0x3f),
          static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(y) & 0x3f),
          static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(z) & 0x3f)};
}

struct SingleNodeVector {
  const std::uint32_t* vertices;
  std::size_t vertex_count;
  const std::uint32_t* face_normals;
  const std::uint32_t* corner_normals;
  std::size_t face_count;
};

struct SingleLoadVector {
  const char* const* file;
  const SingleNodeVector* nodes;
  std::size_t node_count;
};

struct PlacementVector {
  const char* const* file;
  std::uint32_t placement[9];
  int twice;  // at double precision
  const std::uint32_t* instance;
  const NodeVector* nodes;
  std::size_t node_count;
};

struct CellCoordVector {
  std::uint32_t x, z, cell;
  std::int32_t width, height;
  std::int32_t single[2];
  std::int32_t twice[2];  // at double precision
};

#include "model_vectors.inc"

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }

std::vector<std::uint8_t> FromHex(const char* const* pieces) {
  std::vector<std::uint8_t> out;
  const auto nibble = [](char c) {
    return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
  };
  for (; *pieces != nullptr; ++pieces) {
    const std::string piece = *pieces;
    for (std::size_t i = 0; i + 1 < piece.size(); i += 2)
      out.push_back(static_cast<std::uint8_t>(nibble(piece[i]) << 4 | nibble(piece[i + 1])));
  }
  return out;
}

// The instance's fields +0xc0 to +0x1e4.
std::vector<std::uint32_t> InstanceFields(const ModelInstance& m) {
  std::vector<std::uint32_t> out;
  const auto put = [&](float f) { out.push_back(B(f)); };
  for (const float f : m.world) put(f);
  for (const float f : m.inverse) put(f);
  for (const float f : m.centre) put(f);
  out.push_back(static_cast<std::uint32_t>(m.field_12c));
  for (const float f : m.half_extents) put(f);
  for (const float f : m.box_centre) put(f);
  for (const Vec3& a : m.axes)
    for (const float f : a) put(f);
  for (const Vec3& c : m.corners)
    for (const float f : c) put(f);
  for (const float f : m.bounds) put(f);
  return out;
}

const char* const kInstanceFieldNames[] = {"world",  "inverse",      "centre",
                                           "+0x12c", "half extents", "box centre",
                                           "axes",   "corners",      "bounds"};
const std::size_t kInstanceFieldStarts[] = {0, 12, 24, 27, 28, 31, 34, 43, 67, 73};

std::string FieldName(std::size_t i) {
  for (std::size_t k = 0; k + 1 < std::size(kInstanceFieldStarts); ++k)
    if (i < kInstanceFieldStarts[k + 1]) return kInstanceFieldNames[k];
  return "?";
}

// The instance's fields and nodes after Setup and Light against the original's.
void CheckPlaced(const ModelInstance& instance, const std::uint32_t* want_fields,
                 const NodeVector* want_nodes, std::size_t node_count) {
  const std::vector<std::uint32_t> fields = InstanceFields(instance);
  for (std::size_t i = 0; i < 73; ++i) {
    INFO("instance value " << i << " (" << FieldName(i) << ")");
    CHECK(fields[i] == want_fields[i]);
  }
  REQUIRE(instance.nodes.size() == node_count);
  for (std::size_t n = 0; n < node_count; ++n) {
    INFO("node " << n);
    const ModelNodeInstance& node = instance.nodes[n];
    const NodeVector& want = want_nodes[n];
    CHECK(B(node.alpha) == want.alpha);
    for (std::size_t i = 0; i < 12; ++i) {
      INFO("matrix " << i);
      CHECK(B(node.matrix[i]) == want.matrix[i]);
    }
    for (std::size_t i = 0; i < 6; ++i) {
      INFO("box " << i);
      CHECK(B(node.box[i]) == want.box[i]);
    }
    REQUIRE(node.faces.size() == want.faces);
    for (std::size_t f = 0; f < want.faces; ++f) {
      INFO("face " << f);
      const std::uint32_t* shade = want.shades + f * 9;
      for (std::size_t c = 0; c < 3; ++c) {
        INFO("corner " << c);
        CHECK(node.faces[f].colors[c] == shade[c]);
        CHECK(B(node.faces[f].uv[c * 2]) == shade[3 + c * 2]);
        CHECK(B(node.faces[f].uv[c * 2 + 1]) == shade[4 + c * 2]);
      }
      for (std::size_t k = 0; k < 9; ++k) CHECK(node.tint[f * 9 + k] == want.tint[f * 9 + k]);
    }
  }
}

GroundLight ModelLight() {
  return {{F(kModelLight[0]), F(kModelLight[1]), F(kModelLight[2])},
          {F(kModelLight[3]), F(kModelLight[4]), F(kModelLight[5])},
          {F(kModelLight[6]), F(kModelLight[7]), F(kModelLight[8])}};
}

}  // namespace

TEST_CASE("SlerpRotation interpolates rotation keys as the original does", "[model]") {
  for (const SlerpVector& v : kSlerpVectors) {
    const std::array<float, 4> a = {F(v.a[0]), F(v.a[1]), F(v.a[2]), F(v.a[3])};
    const std::array<float, 4> b = {F(v.b[0]), F(v.b[1]), F(v.b[2]), F(v.b[3])};
    INFO("t " << F(v.t));
    const std::array<float, 4> got = SlerpRotation(F(v.t), a, b);
    for (std::size_t i = 0; i < 4; ++i) CHECK(B(got[i]) == v.result[i]);
  }
}

TEST_CASE("SlerpRotation at double precision interpolates as the lazy loader does", "[model]") {
  for (const SlerpVector& v : kDoubleSlerpVectors) {
    const std::array<float, 4> a = {F(v.a[0]), F(v.a[1]), F(v.a[2]), F(v.a[3])};
    const std::array<float, 4> b = {F(v.b[0]), F(v.b[1]), F(v.b[2]), F(v.b[3])};
    INFO("t " << F(v.t));
    const std::array<float, 4> got =
        SlerpRotation(F(v.t), a, b, ragexe::core::FpuPrecision::kDouble);
    for (std::size_t i = 0; i < 4; ++i) CHECK(B(got[i]) == v.result[i]);
  }
}

TEST_CASE("GroundCellColor finds the cell under a point as the original does", "[model]") {
  for (const CellCoordVector& v : kCellCoordVectors) {
    INFO("x " << F(v.x) << " z " << F(v.z) << " cell " << F(v.cell));
    GroundMesh ground;
    ground.width = v.width;
    ground.height = v.height;
    ground.cell_size = F(v.cell);
    const auto width = static_cast<std::size_t>(v.width);
    ground.cells.resize(width * static_cast<std::size_t>(v.height));
    for (std::size_t i = 0; i < ground.cells.size(); ++i)
      ground.cells[i].lightmap_color = {static_cast<std::uint8_t>(i % width),
                                        static_cast<std::uint8_t>(i / width), 7};
    const auto expect = [&](const std::int32_t* cell) -> std::array<std::int32_t, 3> {
      if (cell[0] < 0 || cell[1] < 0 || cell[0] >= v.width || cell[1] >= v.height)
        return {0xff, 0xff, 0xff};
      return {cell[0], cell[1], 7};
    };
    CHECK(GroundCellColor(ground, F(v.x), F(v.z), ragexe::core::FpuPrecision::kSingle) ==
          expect(v.single));
    CHECK(GroundCellColor(ground, F(v.x), F(v.z), ragexe::core::FpuPrecision::kDouble) ==
          expect(v.twice));
  }
}

TEST_CASE(
    "Model instances at double precision are placed and lit as the lazy loader does, "
    "degenerate ones at either precision",
    "[model]") {
  const GroundLight light = ModelLight();
  std::size_t index = 0;
  for (const PlacementVector& v : kDoublePlacements) {
    INFO("placement " << index++);
    const auto precision =
        v.twice != 0 ? ragexe::core::FpuPrecision::kDouble : ragexe::core::FpuPrecision::kSingle;
    ragexe::res::Model model;
    std::string error;
    const std::vector<std::uint8_t> file = FromHex(v.file);
    REQUIRE(ragexe::res::DecodeModel(file, model, &error));
    ModelInstance instance;
    REQUIRE(CreateModelInstance(model, {F(v.placement[0]), F(v.placement[1]), F(v.placement[2])},
                                {F(v.placement[3]), F(v.placement[4]), F(v.placement[5])},
                                {F(v.placement[6]), F(v.placement[7]), F(v.placement[8])}, 0.0f,
                                instance, precision));
    LightModel(instance, light, LowCellColor, precision);
    CheckPlaced(instance, v.instance, v.nodes, v.node_count);
  }
}

TEST_CASE("Model instances are placed, lit and drawn as the original does", "[model]") {
  const GroundLight light = ModelLight();
  GroundColorFade fade;
  fade.water = {F(kModelFade[0]), F(kModelFade[1]), F(kModelFade[2])};
  fade.ground = {F(kModelFade[3]), F(kModelFade[4]), F(kModelFade[5])};
  std::size_t index = 0;
  for (const ModelVector& v : kModelVectors) {
    INFO("model " << index++);
    ragexe::res::Model model;
    std::string error;
    const std::vector<std::uint8_t> file = FromHex(v.file);
    REQUIRE(ragexe::res::DecodeModel(file, model, &error));
    ModelInstance instance;
    REQUIRE(CreateModelInstance(model, {F(v.placement[0]), F(v.placement[1]), F(v.placement[2])},
                                {F(v.placement[3]), F(v.placement[4]), F(v.placement[5])},
                                {F(v.placement[6]), F(v.placement[7]), F(v.placement[8])},
                                F(v.speed), instance));
    LightModel(instance, light, CellColor);
    CheckPlaced(instance, v.instance, v.nodes, v.node_count);

    ModelDrawSettings settings;
    settings.viewport = GroundViewportOf(SetSize(800, 600));
    settings.fade = fade;
    for (const std::string& name : model.textures)
      settings.texture_alpha.push_back(name.size() % 2 == 1);
    for (std::size_t r = 0; r < 4; ++r) {
      INFO("render " << r);
      const RenderVector& want = v.renders[r];
      Matrix view{};
      for (std::size_t i = 0; i < 12; ++i) view[i] = F(want.view[i]);
      settings.alternate_fade = want.alternate != 0;
      settings.ground_tint = want.tint != 0;
      if (want.translucent != 0)
        for (ModelNodeInstance& node : instance.nodes) node.translucent = true;
      std::vector<ModelPrimitive> drawn;
      DrawModel(instance, view, want.clip != 0, settings, drawn);
      REQUIRE(drawn.size() == want.count);
      for (std::size_t p = 0; p < want.count; ++p) {
        INFO("primitive " << p);
        const DrawnFace& face = want.faces[p];
        CHECK(drawn[p].flags == face.flags);
        CHECK(model.textures[static_cast<std::size_t>(drawn[p].texture)] ==
              model.textures[static_cast<std::size_t>(face.texture)]);
        REQUIRE(drawn[p].vertices.size() == face.count);
        for (std::size_t k = 0; k < face.count; ++k) {
          INFO("vertex " << k);
          const ScreenVertex& got = drawn[p].vertices[k];
          const std::uint32_t* w = face.vertices + k * 8;
          CHECK(B(got.x) == w[0]);
          CHECK(B(got.y) == w[1]);
          CHECK(B(got.z) == w[2]);
          CHECK(B(got.rhw) == w[3]);
          CHECK(got.diffuse == w[4]);
          CHECK(got.specular == w[5]);
          CHECK(B(got.u) == w[6]);
          CHECK(B(got.v) == w[7]);
        }
      }
    }

    const std::uint32_t* step = v.steps;
    for (int s = 0; s < 3; ++s) {
      INFO("step " << s);
      AdvanceAnimation(instance);
      CHECK(static_cast<std::uint32_t>(instance.time) == *step++);
      for (const ModelNodeInstance& node : instance.nodes)
        for (std::size_t i = 0; i < 12; ++i) CHECK(B(node.matrix[i]) == *step++);
    }
  }
}

TEST_CASE("Models load as the original's at single precision", "[model]") {
  std::size_t n = 0;
  for (const SingleLoadVector& v : kSingleLoads) {
    INFO("model " << n++);
    ragexe::res::Model model;
    std::string error;
    REQUIRE(ragexe::res::DecodeModel(FromHex(v.file), model, &error,
                                     ragexe::res::FpuPrecision::kSingle));
    REQUIRE(model.nodes.size() == v.node_count);
    for (std::size_t k = 0; k < v.node_count; ++k) {
      INFO("node " << k);
      const ragexe::res::ModelMesh& mesh = model.nodes[k].mesh;
      const SingleNodeVector& want = v.nodes[k];
      REQUIRE(mesh.vertices.size() == want.vertex_count);
      REQUIRE(mesh.faces.size() == want.face_count);
      for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
        for (std::size_t c = 0; c < 3; ++c)
          CHECK(B(mesh.vertices[i][c]) == want.vertices[i * 3 + c]);
      for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
        for (std::size_t c = 0; c < 3; ++c) {
          CHECK(B(mesh.face_normals[f][c]) == want.face_normals[f * 3 + c]);
          for (std::size_t corner = 0; corner < 3; ++corner)
            CHECK(B(mesh.corner_normals[f][corner][c]) ==
                  want.corner_normals[f * 9 + corner * 3 + c]);
        }
      }
    }
  }
}
