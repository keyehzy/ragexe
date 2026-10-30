#include "render/scene_graph.h"

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

struct SceneNodeVector {
  std::uint32_t box[12];  // min, max, centre, half extents
  std::uint32_t dirty;
  std::int32_t cells[4];
  std::int32_t attr_cells[4];
};

struct FrustumVector {
  std::uint32_t view[12];
  std::uint32_t planes[42];  // six (normal, distance), then six normals
  const std::int32_t* inside;
  std::size_t inside_count;
  const std::int32_t* crossing;
  std::size_t crossing_count;
  int classes[30];
};

struct IntersectVector {
  std::uint32_t obb[15];
  std::uint32_t box[6];
  int result;
};

struct PickVector {
  int view, x, y;
  std::uint32_t ray[6];
  const std::int32_t* leaves;
  std::size_t leaf_count;
  int picked, cell_x, cell_z;
};

struct BoxRayVector {
  std::uint32_t values[13];  // origin, direction, range, minimum, maximum
  int hit;
};

struct TriangleRayVector {
  std::uint32_t values[15];  // origin, direction, a, b, c
  std::uint32_t hit;
  std::uint32_t uvt[3];
};

struct CellRayVector {
  std::uint32_t ray[6];
  std::int32_t x, z;
  std::uint32_t distance;
};

#include "scene_graph_vectors.inc"

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

// The test map's height at a cell's corner, and the bounds tree a version 2.1 .rsw gives
// (tools/scene_graph_vectors.py).
float AttrTestHeight(std::int32_t cell, std::int32_t corner) {
  return static_cast<float>(10 - (cell * 7 + corner * 13) * 29 % 61);
}

float SourceBound(std::int32_t node, std::int32_t value) {
  return static_cast<float>((node * 37 + value * 101) % 601 - 300);
}

ModelInstance BoxModel(const std::uint32_t* values) {
  ModelInstance m;
  for (std::size_t i = 0; i < 3; ++i) {
    m.half_extents[i] = F(values[i]);
    m.box_centre[i] = F(values[3 + i]);
    for (std::size_t k = 0; k < 3; ++k) m.axes[i][k] = F(values[6 + i * 3 + k]);
  }
  for (std::size_t i = 0; i < 6; ++i) m.bounds[i] = F(values[15 + i]);
  return m;
}

struct Scene {
  GroundMesh ground;
  ragexe::res::AttrMap attr;
  std::vector<ModelInstance> models;
};

Scene MakeScene() {
  Scene s;
  ragexe::res::Ground file;
  std::string error;
  REQUIRE(ragexe::res::DecodeGround(FromHex(kSceneGround), file, &error));
  const GroundLight light{{F(kSceneLight[0]), F(kSceneLight[1]), F(kSceneLight[2])},
                          {F(kSceneLight[3]), F(kSceneLight[4]), F(kSceneLight[5])},
                          {F(kSceneLight[6]), F(kSceneLight[7]), F(kSceneLight[8])}};
  REQUIRE(BuildGround(file, light, true, s.ground, &error));
  s.attr.width = kSceneGroundWidth * 2;
  s.attr.height = kSceneGroundHeight * 2;
  for (std::int32_t c = 0; c < s.attr.width * s.attr.height; ++c) {
    ragexe::res::AttrCell cell;
    for (std::int32_t k = 0; k < 4; ++k)
      cell.heights[static_cast<std::size_t>(k)] = AttrTestHeight(c, k);
    s.attr.cells.push_back(cell);
  }
  for (const auto& values : kSceneModels) s.models.push_back(BoxModel(values));
  return s;
}

void CheckGraph(const SceneGraph& graph, const SceneNodeVector* want, const std::int32_t* members) {
  REQUIRE(graph.nodes.size() == 1365);
  std::vector<std::vector<int>> expected(graph.nodes.size());
  for (const std::int32_t* p = members; *p >= 0;) {
    const auto node = static_cast<std::size_t>(*p++);
    const std::int32_t count = *p++;
    for (std::int32_t i = 0; i < count; ++i) expected[node].push_back(*p++);
  }
  for (std::size_t k = 0; k < graph.nodes.size(); ++k) {
    INFO("node " << k);
    const SceneGraphNode& n = graph.nodes[k];
    const float got[12] = {n.min[0],    n.min[1],    n.min[2],    n.max[0],  n.max[1],  n.max[2],
                           n.centre[0], n.centre[1], n.centre[2], n.half[0], n.half[1], n.half[2]};
    for (std::size_t i = 0; i < 12; ++i) {
      INFO("box " << i);
      CHECK(B(got[i]) == want[k].box[i]);
    }
    CHECK(static_cast<std::uint32_t>(n.dirty) == want[k].dirty);
    for (std::size_t i = 0; i < 4; ++i) {
      CHECK(n.cells[i] == want[k].cells[i]);
      CHECK(n.attr_cells[i] == want[k].attr_cells[i]);
    }
    CHECK(n.models == expected[k]);
  }
}

// The bounds tree tools/scene_graph_vectors.py gives BuildSceneGraph to copy.
std::vector<ragexe::res::WorldBounds> SourceBounds() {
  std::vector<ragexe::res::WorldBounds> bounds(1365);
  for (std::size_t k = 0; k < bounds.size(); ++k) {
    const auto node = static_cast<std::int32_t>(k);
    for (std::size_t i = 0; i < 3; ++i) {
      const auto at = static_cast<std::int32_t>(i);
      bounds[k].min[i] = SourceBound(node, at);
      bounds[k].max[i] = SourceBound(node, at + 3);
      bounds[k].centre[i] = SourceBound(node, at + 6);
      bounds[k].half_extent[i] = SourceBound(node, at + 9);
    }
  }
  return bounds;
}

}  // namespace

TEST_CASE("OrientedBoxIntersectsBox separates boxes as the original does", "[scene_graph]") {
  std::size_t n = 0;
  for (const IntersectVector& v : kIntersectVectors) {
    INFO("case " << n++);
    std::uint32_t values[21] = {};
    for (std::size_t i = 0; i < 15; ++i) values[i] = v.obb[i];
    const ModelInstance m = BoxModel(values);
    const std::array<float, 6> box = {F(v.box[0]), F(v.box[1]), F(v.box[2]),
                                      F(v.box[3]), F(v.box[4]), F(v.box[5])};
    CHECK(OrientedBoxIntersectsBox(m, box) == (v.result != 0));
  }
}

TEST_CASE("BuildSceneGraph fills the tree as the original does", "[scene_graph]") {
  const Scene s = MakeScene();
  SECTION("from the heights") {
    SceneGraph graph;
    BuildSceneGraph(s.ground, s.attr, s.models, {}, graph);
    CheckGraph(graph, kSceneNodes0, kSceneMembers0);
  }
  SECTION("copying a version 2.1 world's bounds") {
    SceneGraph graph;
    BuildSceneGraph(s.ground, s.attr, s.models, SourceBounds(), graph);
    CheckGraph(graph, kSceneNodes1, kSceneMembers1);
  }
}

TEST_CASE("Models inserted after BuildSceneGraph go where the original puts them",
          "[scene_graph]") {
  const Scene s = MakeScene();
  const std::span<const ModelInstance> first = std::span(s.models).first(kLateFrom);
  const auto insert_rest = [&](SceneGraph& graph) {
    for (std::size_t m = kLateFrom; m < s.models.size(); ++m)
      InsertLateModel(graph, s.models[m], static_cast<int>(m));
  };
  SECTION("from the heights") {
    SceneGraph graph;
    BuildSceneGraph(s.ground, s.attr, first, {}, graph);
    insert_rest(graph);
    CheckGraph(graph, kLateNodes0, kLateMembers0);
  }
  SECTION("copying a version 2.1 world's bounds") {
    SceneGraph graph;
    BuildSceneGraph(s.ground, s.attr, first, SourceBounds(), graph);
    insert_rest(graph);
    CheckGraph(graph, kLateNodes1, kLateMembers1);
  }
}

TEST_CASE("The frustum finds the nodes and models the original does", "[scene_graph]") {
  const Scene s = MakeScene();
  SceneGraph graph;
  BuildSceneGraph(s.ground, s.attr, s.models, {}, graph);
  std::size_t index = 0;
  for (const FrustumVector& v : kFrustumVectors) {
    INFO("view " << index++);
    Matrix view{};
    for (std::size_t i = 0; i < 12; ++i) view[i] = F(v.view[i]);
    const Frustum f = MakeFrustum(kSceneSlopes[0], kSceneSlopes[1], view);
    for (std::size_t p = 0; p < 6; ++p) {
      INFO("plane " << p);
      for (std::size_t i = 0; i < 4; ++i) CHECK(B(f.planes[p][i]) == v.planes[p * 4 + i]);
      for (std::size_t i = 0; i < 3; ++i) CHECK(B(f.planes[p][i]) == v.planes[24 + p * 3 + i]);
    }
    const VisibleNodes visible = CollectVisibleNodes(f, graph);
    CHECK(visible.inside == std::vector<int>(v.inside, v.inside + v.inside_count));
    CHECK(visible.crossing == std::vector<int>(v.crossing, v.crossing + v.crossing_count));
    for (std::size_t m = 0; m < s.models.size(); ++m) {
      INFO("model " << m);
      CHECK(ClassifyOrientedBox(f, s.models[m]) == v.classes[m]);
    }
  }
}

namespace {

ViewRay RayOf(const std::uint32_t* v) {
  return {{F(v[0]), F(v[1]), F(v[2])}, {F(v[3]), F(v[4]), F(v[5])}};
}

}  // namespace

TEST_CASE("The mouse's ray picks the cell the original does", "[scene_graph]") {
  const Scene s = MakeScene();
  SceneGraph graph;
  BuildSceneGraph(s.ground, s.attr, s.models, {}, graph);
  const Projection projection = SetSize(800, 600);
  std::size_t n = 0;
  for (const PickVector& v : kPickVectors) {
    INFO("pick " << n++ << " at " << v.x << ", " << v.y);
    Camera camera;
    const auto view = static_cast<std::size_t>(v.view);
    for (std::size_t i = 0; i < 3; ++i) camera.eye[i] = F(kPickEyes[view][i]);
    for (std::size_t i = 0; i < 12; ++i) camera.inverse[i] = F(kPickInverses[view][i]);
    const ViewRay ray = ScreenRay(projection, v.x, v.y, camera.eye, camera.inverse);
    const float got[6] = {ray.origin[0],    ray.origin[1],    ray.origin[2],
                          ray.direction[0], ray.direction[1], ray.direction[2]};
    for (std::size_t i = 0; i < 6; ++i) {
      INFO("ray " << i);
      CHECK(B(got[i]) == v.ray[i]);
    }
    CHECK(CollectRayLeaves(graph, ray) == std::vector<int>(v.leaves, v.leaves + v.leaf_count));
    std::int32_t x = -1, z = -1;
    CHECK(PickCell(projection, camera, v.x, v.y, graph, s.attr, x, z) == (v.picked != 0));
    CHECK(x == v.cell_x);
    CHECK(z == v.cell_z);
  }
}

TEST_CASE("Rays meet boxes, triangles and cells as the original's", "[scene_graph]") {
  std::size_t n = 0;
  for (const BoxRayVector& v : kBoxRayVectors) {
    INFO("box " << n++);
    const Vec3 min{F(v.values[7]), F(v.values[8]), F(v.values[9])};
    const Vec3 max{F(v.values[10]), F(v.values[11]), F(v.values[12])};
    CHECK(RayHitsBox(RayOf(v.values), F(v.values[6]), min, max) == (v.hit != 0));
  }
  n = 0;
  for (const TriangleRayVector& v : kTriangleRayVectors) {
    INFO("triangle " << n++);
    const auto corner = [&v](std::size_t k) {
      return Vec3{F(v.values[6 + 3 * k]), F(v.values[7 + 3 * k]), F(v.values[8 + 3 * k])};
    };
    float u = F(0xdeadbeef), w = F(0xdeadbeef), t = F(0xdeadbeef);
    CHECK(RayHitsTriangle(RayOf(v.values), corner(0), corner(1), corner(2), u, w, t) ==
          (v.hit != 0));
    CHECK(B(u) == v.uvt[0]);
    CHECK(B(w) == v.uvt[1]);
    CHECK(B(t) == v.uvt[2]);
  }
  const Scene s = MakeScene();
  n = 0;
  for (const CellRayVector& v : kCellRayVectors) {
    INFO("cell " << n++ << ": " << v.x << ", " << v.z);
    CHECK(B(IntersectCellRay(s.attr, RayOf(v.ray), v.x, v.z)) == v.distance);
  }
}
