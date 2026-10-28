#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "render/ground.h"
#include "render/matrix.h"
#include "res/rsm.h"

namespace ragexe::render {

// A node's keys as C3dNode keeps them (+0xe4, +0xf4, +0x104), with the node's own
// placement standing in for the keys a file leaves out.
struct NodeKeys {
  std::vector<res::TranslationKey> translation;
  std::vector<res::RotationKey> rotation;  // unit quaternions (x, y, z, w)
  Vec3 scale{};                            // the one scale key's
};

// The rotation key AddFromAxisAngle (00401a70) makes: sin(angle / 2) · axis, cos(angle / 2).
std::array<float, 4> RotationFromAxisAngle(const Vec3& axis, float angle);

// SlerpRotationKey (00401050): from a to b by t along the shorter arc, linearly when they
// are within 1e-6 of each other.
std::array<float, 4> SlerpRotation(float t, const std::array<float, 4>& a,
                                   const std::array<float, 4>& b);

// C3dNode::ComputeMatrixAtTime (004b6db0) for one node: scale, then rotation, then
// translation, each from its keys at time (TranslationKeys 00401520, RotationKeys 00401d10,
// ScaleKeys 00402070). Between two keys the translation is interpolated linearly and the
// rotation spherically; past the last key the original reads one key beyond its keys, and
// the port holds the last one.
Matrix NodeMatrixAt(const NodeKeys& keys, std::int32_t time);

// A face's colours and texture coordinates as the lighting leaves them (C3dNode+0x118,
// 0x24 bytes each): ARGB at each corner, then (u, v) at each corner.
struct FaceShade {
  std::array<std::uint32_t, 3> colors{};
  std::array<float, 6> uv{};
};

// A C3dNode (0x150 bytes) of an instance.
struct ModelNodeInstance {
  int source = -1;     // its node in the model
  int parent = -1;     // in ModelInstance::nodes; +0xa4
  float alpha = 0.0f;  // +0xa0, the node's alpha / 255
  NodeKeys keys;
  Matrix matrix{};                 // +0xb4, the node's own transform at the instance's time
  std::array<float, 6> box{};      // +0x128, its vertices' world box: minimum, maximum
  std::vector<FaceShade> faces;    // +0x118
  std::vector<std::int32_t> tint;  // +0x144, 9 per face: each corner's colour plus the ground's
  bool translucent = false;        // +0x124, for the next draw only
  std::vector<int> children;       // in ModelInstance::nodes
};

// A ModelInstance (0x200 bytes): a model placed in the world.
struct ModelInstance {
  const res::Model* model = nullptr;
  Vec3 position{};                       // +0
  Vec3 rotation{};                       // +0xc, degrees about x, y and z
  Vec3 scale{1.0f, 1.0f, 1.0f};          // +0x18
  std::int32_t shade_type = 0;           // +0x24, the model's
  std::int32_t time = 0;                 // +0xac
  std::int32_t loop = 2;                 // +0xb0: 1 stops at the end, 2 wraps
  float speed = 0.0f;                    // +0xb4
  std::int32_t length = 0;               // +0xb8, the model's animation length
  bool dirty = true;                     // +0xbc, the world matrix wants rebuilding
  Matrix world{};                        // +0xc0
  Matrix inverse{};                      // +0xf0
  Vec3 centre{};                         // +0x120, the offset that stands the model on its box
  std::int32_t field_12c = 1;            // +0x12c
  Vec3 half_extents{};                   // +0x130, the oriented box's
  Vec3 box_centre{};                     // +0x13c
  std::array<Vec3, 3> axes{};            // +0x148, normalised
  std::array<Vec3, 8> corners{};         // +0x16c, the local box's corners in the world
  std::array<float, 6> bounds{};         // +0x1cc, their minimum and maximum
  std::uint32_t drawn_frame = 0;         // +0x1e4, the frame CView::vf03 last drew it in
  bool fading = false;                   // +0x1ec, on the node's instance: fade out this frame
  std::uint8_t alpha = 0xff;             // +0x1ed
  std::vector<ModelNodeInstance> nodes;  // depth first from the model's first node
};

// The instance CWorld::LoadWorld (004c3ab0) makes for a world model: the constructor
// (004b9340), the speed as the .rsw's times 1/3 (005e8968), then Setup (004b9820): the
// nodes from the model's first node down (C3dNode::vf01, 004b7580), their matrices at time
// 0, and the box (004b98c0), which centres the model on its box's x and z and stands it on
// its top. False for a model without nodes.
bool CreateModelInstance(const res::Model& model, const Vec3& position, const Vec3& rotation,
                         const Vec3& scale, float rsw_speed, ModelInstance& out);

// ModelInstance 004b95f0: the world matrix (the centring, scale, rotations about y, x and
// z, then the position) and its inverse.
void UpdateWorldMatrix(ModelInstance& instance);

// The ground colour C3dNode 004b8510 adds to each corner (C3dGround15::GetCellColor,
// 004c03b0): red, green and blue at world (x, y, z).
using CellColorLookup = std::function<std::array<std::int32_t, 3>(float x, float y, float z)>;

// GetCellColor on a built ground: the lightmap colour of the cell under (x, z), white
// outside the ground.
std::array<std::int32_t, 3> GroundCellColor(const GroundMesh& ground, float x, float z);

// ModelInstance 004ba270 and C3dNode::vf02 (004b7e30): each face's colours by the
// instance's shade type (0 white, 1 lit by the face normal, 2 by the corner normals) with
// the node's alpha, its texture coordinates, and the colours plus the ground's.
void LightModel(ModelInstance& instance, const GroundLight& light, const CellColorLookup& cell);

// What the draw reads besides the instance.
struct ModelDrawSettings {
  GroundViewport viewport;
  GroundColorFade fade;
  bool alternate_fade = false;  // g_groundWaterFade, 00679f9c
  bool ground_tint = false;     // g_WorldColorFade+0xd: the colours plus the ground's
  // The textures the faces' indices name (the model's), and whether each is ARGB 4444,
  // which makes its faces translucent.
  std::vector<bool> texture_alpha;
};

// The faces of a run of one texture, as C3dNode::vf00 hands them to AddPrimitive: a
// triangle list.
struct ModelPrimitive {
  std::uint32_t flags = 0;    // 0, or 1 for translucent
  std::int32_t texture = -1;  // in the model's textures
  std::vector<ScreenVertex> vertices;
};

// ModelInstance::Render (004b9740) and C3dNode::vf00 (004b6e50): each node's vertices
// projected through world · node · view; each face kept when it faces the view (either way
// for a two-sided face, field_10), and with clip only when its screen box meets the
// viewport; faces in runs of one texture. The node's translucency is cleared, and the
// instance's alpha steps by 5 toward 0x5a or 0xff.
void DrawModel(ModelInstance& instance, const Matrix& view, bool clip,
               const ModelDrawSettings& settings, std::vector<ModelPrimitive>& out);

// ModelInstance::AdvanceAnimation (004b96a0): the nodes' matrices at the current time,
// then the time on by speed · 100, wrapped by the length or held at its end.
void AdvanceAnimation(ModelInstance& instance);

}  // namespace ragexe::render
