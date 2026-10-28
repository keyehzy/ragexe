#include "render/scene_graph.h"

#include <cmath>

namespace ragexe::render {

namespace {

constexpr int kLeafDepth = 5;
constexpr float kUnbounded = 999999.0f;  // 005e28cc
constexpr float kNear = 10.0f;           // 005e8a98
constexpr float kFar = 1500.0f;          // 005e8a94

// __ftol (005c2084) of a whole number from floor.
std::int32_t Ftol(double v) {
  if (std::isnan(v) || v <= -9.2233720368547758e18 || v >= 9.2233720368547758e18) return 0;
  return static_cast<std::int32_t>(static_cast<std::int64_t>(v));
}

// C3dGround::GetCellCoords (004bc3b0) and SceneGraphNode::SetAttr's same sum (004c5de0):
// floor(v / size) + count / 2.
std::int32_t CellOf(float v, float size, std::int32_t count) {
  return Ftol(std::floor(static_cast<double>(v / size))) + count / 2;
}

// SceneGraphNode::Subdivide (004c5340), depth first.
void Subdivide(SceneGraph& graph, int index, int depth) {
  SceneGraphNode& n = graph.nodes[static_cast<std::size_t>(index)];
  n.depth = depth;
  n.centre[1] = 0.0f;
  n.min[1] = -kUnbounded;
  n.max[1] = kUnbounded;
  n.centre[0] = (n.min[0] + n.max[0]) * 0.5f;
  n.centre[2] = (n.max[2] + n.min[2]) * 0.5f;
  if (depth == kLeafDepth) return;
  for (int k = 0; k < 4; ++k) {
    const SceneGraphNode& p = graph.nodes[static_cast<std::size_t>(index)];
    SceneGraphNode child;
    child.parent = index;
    child.min[0] = (k & 1) != 0 ? p.centre[0] : p.min[0];
    child.max[0] = (k & 1) != 0 ? p.max[0] : p.centre[0];
    child.min[2] = (k & 2) != 0 ? p.centre[2] : p.min[2];
    child.max[2] = (k & 2) != 0 ? p.max[2] : p.centre[2];
    const int added = static_cast<int>(graph.nodes.size());
    graph.nodes.push_back(child);
    graph.nodes[static_cast<std::size_t>(index)].children[static_cast<std::size_t>(k)] = added;
    Subdivide(graph, added, depth + 1);
  }
}

// SceneGraphNode::InsertModel (004c54c0).
void InsertModel(SceneGraph& graph, int index, const ModelInstance& model, int model_index) {
  SceneGraphNode& n = graph.nodes[static_cast<std::size_t>(index)];
  if (n.depth == kLeafDepth) {
    n.models.push_back(model_index);
    for (int at = index; at >= 0; at = graph.nodes[static_cast<std::size_t>(at)].parent)
      graph.nodes[static_cast<std::size_t>(at)].dirty = true;
    return;
  }
  for (const int child : n.children) {
    const SceneGraphNode& c = graph.nodes[static_cast<std::size_t>(child)];
    if (OrientedBoxIntersectsBox(model,
                                 {c.min[0], c.min[1], c.min[2], c.max[0], c.max[1], c.max[2]}))
      InsertModel(graph, child, model, model_index);
  }
}

// C3dGround15::vf08 (004bf7a0): the lowest and highest corner of the tops in a
// rectangle, or 0 and 0 when it is not wholly on the ground or is a line.
void GroundHeightRange(const GroundMesh& ground, const CellRect& r, float& low, float& high) {
  const auto outside = [](std::int32_t v, std::int32_t size) { return v < 0 || size <= v; };
  if (outside(r[0], ground.width) || outside(r[2], ground.width) || outside(r[1], ground.height) ||
      outside(r[3], ground.height) || !(r[0] < r[2]) || !(r[1] < r[3])) {
    low = 0.0f;
    high = 0.0f;
    return;
  }
  low = kUnbounded;
  high = -kUnbounded;
  for (std::int32_t x = r[0]; x <= r[2]; ++x) {
    for (std::int32_t z = r[1]; z <= r[3]; ++z) {
      const std::size_t cell =
          (static_cast<std::size_t>(ground.width) * static_cast<std::size_t>(z)) +
          static_cast<std::size_t>(x);
      const std::int32_t top = ground.cells[cell].surfaces[0];
      if (top < 0) continue;
      const auto& v = ground.surfaces[static_cast<std::size_t>(top)].vertices;
      for (const GroundVertex& corner : v)
        if (high < corner.y) high = corner.y;
      for (const GroundVertex& corner : v)
        if (corner.y < low) low = corner.y;
    }
  }
}

// C3dAttr::GetHeightRange (00427390).
void AttrHeightRange(const res::AttrMap& attr, const std::array<std::int32_t, 4>& r, float& low,
                     float& high) {
  low = kUnbounded;
  high = -kUnbounded;
  for (std::int32_t x = r[0]; x <= r[2]; ++x) {
    for (std::int32_t z = r[1]; z <= r[3]; ++z) {
      if (x < 0 || x >= attr.width || z < 0 || z >= attr.height) continue;
      const std::size_t cell =
          (static_cast<std::size_t>(attr.width) * static_cast<std::size_t>(z)) +
          static_cast<std::size_t>(x);
      const auto& h = attr.cells[cell].heights;
      for (const float v : h)
        if (high < v) high = v;
      for (const float v : h)
        if (v < low) low = v;
    }
  }
}

// SceneGraphNode::UpdateHeightBounds (004c5fa0).
void UpdateHeightBounds(SceneGraph& graph, int index, const GroundMesh& ground,
                        const res::AttrMap& attr, const std::vector<ModelInstance>& models) {
  SceneGraphNode& n = graph.nodes[static_cast<std::size_t>(index)];
  n.dirty = false;
  if (n.depth != kLeafDepth) {
    for (const int child : n.children)
      if (graph.nodes[static_cast<std::size_t>(child)].dirty)
        UpdateHeightBounds(graph, child, ground, attr, models);
    return;
  }
  float ground_low = 0.0f, ground_high = 0.0f, attr_low = 0.0f, attr_high = 0.0f;
  GroundHeightRange(ground, n.cells, ground_low, ground_high);
  AttrHeightRange(attr, n.attr_cells, attr_low, attr_high);
  n.max[1] = ground_high > attr_high ? ground_high : attr_high;
  n.min[1] = ground_low < attr_low ? ground_low : attr_low;
  for (const int m : n.models) {
    const ModelInstance& model = models[static_cast<std::size_t>(m)];
    if (n.min[1] > model.bounds[1]) n.min[1] = model.bounds[1];
    if (n.max[1] < model.bounds[4]) n.max[1] = model.bounds[4];
  }
  const SceneGraphNode leaf = n;
  for (int at = index; at >= 0; at = graph.nodes[static_cast<std::size_t>(at)].parent) {
    SceneGraphNode& a = graph.nodes[static_cast<std::size_t>(at)];
    if (a.min[1] > leaf.min[1]) a.min[1] = leaf.min[1];
    if (a.max[1] < leaf.max[1]) a.max[1] = leaf.max[1];
    const float centre = (a.max[1] + a.min[1]) * 0.5f;
    a.centre[1] = centre;
    a.half[2] = a.max[2] - a.centre[2];
    a.half[1] = a.max[1] - centre;
    a.half[0] = a.max[0] - a.centre[0];
  }
}

float Abs(float v) { return std::fabs(v); }

// Inside (2), crossing (1) or outside (0) one plane: distance from the plane against the
// box's reach toward it.
int Side(float distance, float reach) {
  if (distance > reach) return 2;
  if (!(-reach < distance)) return 0;
  return 1;
}

int Classify(const Frustum& f, const auto& distance_and_reach) {
  bool crossing = false;
  for (const auto& p : f.planes) {
    float distance = 0.0f, reach = 0.0f;
    distance_and_reach(p, distance, reach);
    const int side = Side(distance, reach);
    if (side == 0) return 0;
    if (side == 1) crossing = true;
  }
  return crossing ? 2 : 1;
}

void Collect(const Frustum& f, const SceneGraph& graph, int index, bool inside, VisibleNodes& out) {
  const SceneGraphNode& n = graph.nodes[static_cast<std::size_t>(index)];
  if (n.depth == kLeafDepth) {
    (inside ? out.inside : out.crossing).push_back(index);
    return;
  }
  for (const int child : n.children) {
    if (inside) {
      Collect(f, graph, child, true, out);
      continue;
    }
    const int c = ClassifyNodeBox(f, graph.nodes[static_cast<std::size_t>(child)]);
    if (c != 0) Collect(f, graph, child, c == 1, out);
  }
}

}  // namespace

bool OrientedBoxIntersectsBox(const ModelInstance& model, const std::array<float, 6>& box) {
  const Vec3& h = model.half_extents;
  const Vec3& c = model.box_centre;
  const std::array<Vec3, 3>& a = model.axes;
  const float ex = (box[3] - box[0]) * 0.5f;
  const float ey = (box[4] - box[1]) * 0.5f;
  const float ez = (box[5] - box[2]) * 0.5f;
  const float cy = (box[1] + box[4]) * 0.5f;
  const float cz = (box[2] + box[5]) * 0.5f;
  const float dx = c[0] - ((box[0] + box[3]) * 0.5f);
  const float dy = c[1] - cy;
  const float dz = c[2] - cz;
  // |a_i| componentwise, and the distance along each of the box's axes.
  std::array<Vec3, 3> abs{};
  for (std::size_t i = 0; i < 3; ++i) abs[i] = {Abs(a[i][0]), Abs(a[i][1]), Abs(a[i][2])};
  const auto separated = [](float reach, float distance) { return !(reach >= distance); };
  const float t0 = ((dz * a[0][2]) + (dy * a[0][1])) + (dx * a[0][0]);
  if (separated((((abs[0][2] * ez) + (abs[0][1] * ey)) + (abs[0][0] * ex)) + h[0], Abs(t0)))
    return false;
  const float t1 = ((dz * a[1][2]) + (dy * a[1][1])) + (dx * a[1][0]);
  if (separated((((abs[1][2] * ez) + (abs[1][1] * ey)) + (abs[1][0] * ex)) + h[1], Abs(t1)))
    return false;
  const float t2 = ((dz * a[2][2]) + (dy * a[2][1])) + (dx * a[2][0]);
  if (separated((((abs[2][2] * ez) + (abs[2][1] * ey)) + (abs[2][0] * ex)) + h[2], Abs(t2)))
    return false;
  if (separated((((abs[2][0] * h[2]) + (abs[1][0] * h[1])) + (abs[0][0] * h[0])) + ex, Abs(dx)))
    return false;
  if (separated((((abs[2][1] * h[2]) + (abs[1][1] * h[1])) + (abs[0][1] * h[0])) + ey, Abs(dy)))
    return false;
  if (separated((((abs[2][2] * h[2]) + (abs[1][2] * h[1])) + (abs[0][2] * h[0])) + ez, Abs(dz)))
    return false;
  // The nine cross products of an axis of each box.
  const auto cross = [&](float r0, float r1, float r2, float r3, float s) {
    return separated((r0 + r1) + (r2 + r3), Abs(s));
  };
  if (cross(abs[2][0] * h[1], abs[1][0] * h[2], abs[0][1] * ez, abs[0][2] * ey,
            (t2 * a[1][0]) - (a[2][0] * t1)))
    return false;
  if (cross(abs[2][1] * h[1], abs[1][1] * h[2], abs[0][0] * ez, abs[0][2] * ex,
            (t2 * a[1][1]) - (a[2][1] * t1)))
    return false;
  if (cross(abs[2][2] * h[1], abs[1][2] * h[2], abs[0][0] * ey, abs[0][1] * ex,
            (t2 * a[1][2]) - (a[2][2] * t1)))
    return false;
  if (cross(abs[1][1] * ez, abs[1][2] * ey, abs[2][0] * h[0], abs[0][0] * h[2],
            (a[2][0] * t0) - (t2 * a[0][0])))
    return false;
  if (cross(abs[1][0] * ez, abs[1][2] * ex, abs[2][1] * h[0], abs[0][1] * h[2],
            (a[2][1] * t0) - (t2 * a[0][1])))
    return false;
  if (cross(abs[1][0] * ey, abs[1][1] * ex, abs[2][2] * h[0], abs[0][2] * h[2],
            (a[2][2] * t0) - (t2 * a[0][2])))
    return false;
  if (cross(abs[2][1] * ez, abs[2][2] * ey, abs[1][0] * h[0], abs[0][0] * h[1],
            (t1 * a[0][0]) - (a[1][0] * t0)))
    return false;
  if (cross(abs[2][0] * ez, abs[2][2] * ex, abs[1][1] * h[0], abs[0][1] * h[1],
            (t1 * a[0][1]) - (a[1][1] * t0)))
    return false;
  if (cross(abs[2][0] * ey, abs[2][1] * ex, abs[1][2] * h[0], abs[0][2] * h[1],
            (t1 * a[0][2]) - (a[1][2] * t0)))
    return false;
  return true;
}

void BuildSceneGraph(const GroundMesh& ground, const res::AttrMap& attr,
                     const std::vector<ModelInstance>& models,
                     const std::vector<res::WorldBounds>& bounds, SceneGraph& out) {
  out.nodes.assign(1, SceneGraphNode{});
  SceneGraphNode& root = out.nodes[0];
  const float width = static_cast<float>(ground.width) * ground.cell_size;
  const float height = static_cast<float>(ground.height) * ground.cell_size;
  root.min[0] = width * -0.5f;
  root.max[0] = (width * 0.5f) - 1.0f;
  root.min[2] = height * -0.5f;
  root.max[2] = (height * 0.5f) - 1.0f;
  Subdivide(out, 0, 0);
  for (std::size_t m = 0; m < models.size(); ++m)
    InsertModel(out, 0, models[m], static_cast<int>(m));
  const auto attr_size = static_cast<float>(attr.cell_size);
  for (SceneGraphNode& n : out.nodes) {
    n.dirty = true;
    n.cells = {CellOf(n.min[0], ground.cell_size, ground.width),
               CellOf(n.min[2], ground.cell_size, ground.height),
               CellOf(n.max[0], ground.cell_size, ground.width),
               CellOf(n.max[2], ground.cell_size, ground.height)};
    n.attr_cells = {
        CellOf(n.min[0], attr_size, attr.width), CellOf(n.min[2], attr_size, attr.height),
        CellOf(n.max[0], attr_size, attr.width), CellOf(n.max[2], attr_size, attr.height)};
  }
  if (!bounds.empty()) {
    // SceneGraphNode::CopyBounds (004c68c0), both trees depth first.
    for (std::size_t k = 0; k < out.nodes.size() && k < bounds.size(); ++k) {
      SceneGraphNode& n = out.nodes[k];
      n.dirty = false;
      n.min = bounds[k].min;
      n.max = bounds[k].max;
      n.centre = bounds[k].centre;
      n.half = bounds[k].half_extent;
    }
    return;
  }
  UpdateHeightBounds(out, 0, ground, attr, models);
}

Frustum MakeFrustum(float slope_x, float slope_y, const Matrix& view) {
  const std::array<std::array<float, 4>, 6> local = {{{-slope_x, 0.0f, 1.0f, 0.0f},
                                                      {slope_x, 0.0f, 1.0f, 0.0f},
                                                      {0.0f, -slope_y, 1.0f, 0.0f},
                                                      {0.0f, slope_y, 1.0f, 0.0f},
                                                      {0.0f, 0.0f, 1.0f, -kNear},
                                                      {0.0f, 0.0f, -1.0f, kFar}}};
  const Matrix& m = view;
  Frustum f;
  for (std::size_t i = 0; i < 6; ++i) {
    const auto& p = local[i];
    f.planes[i] = {((m[0] * p[0]) + (m[2] * p[2])) + (p[1] * m[1]),
                   ((m[4] * p[1]) + (m[5] * p[2])) + (p[0] * m[3]),
                   ((m[7] * p[1]) + (m[8] * p[2])) + (p[0] * m[6]),
                   (((m[10] * p[1]) + (m[11] * p[2])) + (p[0] * m[9])) + p[3]};
  }
  return f;
}

int ClassifyNodeBox(const Frustum& frustum, const SceneGraphNode& node) {
  const Vec3& c = node.centre;
  const Vec3& h = node.half;
  return Classify(frustum, [&](const std::array<float, 4>& p, float& distance, float& reach) {
    distance = (((c[0] * p[0]) + (c[2] * p[2])) + (p[1] * c[1])) + p[3];
    reach = (Abs(h[2] * p[2]) + Abs(h[1] * p[1])) + Abs(p[0] * h[0]);
  });
}

int ClassifyOrientedBox(const Frustum& frustum, const ModelInstance& model) {
  const Vec3& h = model.half_extents;
  const Vec3& c = model.box_centre;
  const std::array<Vec3, 3>& a = model.axes;
  return Classify(frustum, [&](const std::array<float, 4>& p, float& distance, float& reach) {
    distance = (((p[1] * c[1]) + (c[2] * p[2])) + (p[0] * c[0])) + p[3];
    const float along1 = (((a[1][2] * p[2]) + (a[1][0] * p[0])) + (a[1][1] * p[1])) * h[1];
    const float along2 = (((a[2][2] * p[2]) + (a[2][0] * p[0])) + (a[2][1] * p[1])) * h[2];
    const float along0 = (((a[0][1] * p[1]) + (a[0][2] * p[2])) + (p[0] * a[0][0])) * h[0];
    reach = (Abs(along1) + Abs(along2)) + Abs(along0);
  });
}

VisibleNodes CollectVisibleNodes(const Frustum& frustum, const SceneGraph& graph) {
  VisibleNodes out;
  if (!graph.nodes.empty()) Collect(frustum, graph, 0, false, out);
  return out;
}

void DrawVisible(const VisibleNodes& visible, const SceneGraph& graph, const Frustum& frustum,
                 const Matrix& view, GroundMesh& ground, const GroundDrawSettings& ground_settings,
                 std::vector<ModelInstance>& models,
                 const std::vector<ModelDrawSettings>& model_settings,
                 std::vector<VisiblePrimitive>& out) {
  const std::uint32_t frame = ground_settings.frame;
  std::vector<ModelPrimitive> faces;
  std::vector<GroundPrimitive> cells;
  const auto draw_model = [&](int m, bool clip) {
    faces.clear();
    DrawModel(models[static_cast<std::size_t>(m)], view, clip,
              model_settings[static_cast<std::size_t>(m)], faces);
    for (ModelPrimitive& face : faces) {
      VisiblePrimitive v;
      v.model = m;
      v.face = std::move(face);
      out.push_back(std::move(v));
    }
  };
  const auto draw_cells = [&](const SceneGraphNode& n, bool cull) {
    cells.clear();
    DrawGroundCells(ground, view, n.cells, cull, ground_settings, cells);
    for (GroundPrimitive& cell : cells) {
      VisiblePrimitive v;
      v.ground = cell;
      out.push_back(std::move(v));
    }
  };
  for (const int index : visible.crossing) {
    const SceneGraphNode& n = graph.nodes[static_cast<std::size_t>(index)];
    for (const int m : n.models) {
      ModelInstance& model = models[static_cast<std::size_t>(m)];
      if (model.drawn_frame == frame) continue;
      const int c = ClassifyOrientedBox(frustum, model);
      AdvanceAnimation(model);
      if (c != 0) draw_model(m, c == 2);
      model.drawn_frame = frame;
    }
    draw_cells(n, true);
  }
  for (const int index : visible.inside) {
    const SceneGraphNode& n = graph.nodes[static_cast<std::size_t>(index)];
    for (const int m : n.models) {
      ModelInstance& model = models[static_cast<std::size_t>(m)];
      if (model.drawn_frame == frame) continue;
      draw_model(m, false);
      model.drawn_frame = frame;
    }
    draw_cells(n, false);
  }
}

}  // namespace ragexe::render
