#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "render/ground.h"
#include "render/matrix.h"
#include "render/model.h"
#include "res/gat.h"
#include "res/rsw.h"

namespace ragexe::render {

// A SceneGraphNode (0x80 bytes): a quarter of its parent's area down to depth 5, with the
// models that reach into it and the ground's and the attribute map's cells under it
// (docs/render/scene_graph.md).
struct SceneGraphNode {
  int parent = -1;                              // +0
  std::array<int, 4> children{-1, -1, -1, -1};  // +4, none at depth 5
  int depth = 0;
  Vec3 min{};                                // +0x14
  Vec3 max{};                                // +0x20
  Vec3 centre{};                             // +0x2c
  Vec3 half{};                               // +0x38, the half extents
  bool dirty = true;                         // +0x44
  std::vector<int> models;                   // +0x4c, at depth 5: indices of the world's models
  CellRect cells{};                          // +0x5c, the ground's
  std::array<std::int32_t, 4> attr_cells{};  // +0x70, the attribute map's
};

// The world's scene graph (CWorld+0x68), depth first from the root.
struct SceneGraph {
  std::vector<SceneGraphNode> nodes;
};

// OrientedBoxIntersectsBox (004c56d0): the separating axis test of a model's oriented box
// (+0x130) against a box given as minimum and maximum.
bool OrientedBoxIntersectsBox(const ModelInstance& model, const std::array<float, 6>& box);

// CWorld::BuildSceneGraph (004c4f10): the root over the ground, less 1 at the far edges,
// split to depth 5; each model in the leaves its oriented box meets; the ground's and the
// attribute map's cells under each node; then the bounds a version 2.1 .rsw gives (depth
// first), or else the heights under each leaf (UpdateHeightBounds, 004c5fa0).
void BuildSceneGraph(const GroundMesh& ground, const res::AttrMap& attr,
                     const std::vector<ModelInstance>& models,
                     const std::vector<res::WorldBounds>& bounds, SceneGraph& out);

// ViewFrustum's six planes (normal, distance) in world space: the sides at the slopes
// SetSize gives, near 10 and far 1500, carried by the view matrix (CullSceneGraph,
// 004ceb00). A point p is inside a plane when normal · p + distance > 0.
struct Frustum {
  std::array<std::array<float, 4>, 6> planes{};
};

Frustum MakeFrustum(float slope_x, float slope_y, const Matrix& view);

// How a box lies against the frustum: 0 outside a plane, 1 inside all six, 2 crossing.
// ClassifyNodeBox (004ced00) and ClassifyOrientedBox (004ced90, a model's +0x130).
int ClassifyNodeBox(const Frustum& frustum, const SceneGraphNode& node);
int ClassifyOrientedBox(const Frustum& frustum, const ModelInstance& model);

// CollectVisibleNodes (004cee60) from the root: the leaves of the children the frustum
// does not put outside, those under a child wholly inside listed as inside, the others as
// crossing; each list in the order found.
struct VisibleNodes {
  std::vector<int> inside;    // ViewFrustum+0xa8
  std::vector<int> crossing;  // +0xb4
};

VisibleNodes CollectVisibleNodes(const Frustum& frustum, const SceneGraph& graph);

// What CView::vf03 (004cf4d0) draws of the visible nodes, in its order.
struct VisiblePrimitive {
  int model = -1;  // the world's model, or -1 for the ground
  GroundPrimitive ground;
  ModelPrimitive face;
};

// CView::vf03's drawing: the crossing nodes, then the inside ones. In a crossing node each
// model not drawn this frame (+0x1e4 against the frame) is classified, stepped
// (AdvanceAnimation) and drawn when not outside, clipped when crossing; then the node's
// ground cells, culled. In an inside node each model not drawn this frame is drawn
// unclipped, without stepping, then the cells unculled.
void DrawVisible(const VisibleNodes& visible, const SceneGraph& graph, const Frustum& frustum,
                 const Matrix& view, GroundMesh& ground, const GroundDrawSettings& ground_settings,
                 std::vector<ModelInstance>& models,
                 const std::vector<ModelDrawSettings>& model_settings,
                 std::vector<VisiblePrimitive>& out);

}  // namespace ragexe::render
