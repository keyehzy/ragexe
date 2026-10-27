#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/resource.h"

namespace ragexe::res {

// A model placed in the world (object type 1), as C3dWorldRes::Load (00435dd0) keeps it
// in its 0x104-byte record (docs/formats/rsw.md).
struct WorldModel {
  std::string name;  // +0, copied to ModelInstance +0x2c
  std::string file;  // +0x28, the .rsm
  std::string node;  // +0x78, a node of that model
  std::array<float, 3> position{};
  std::array<float, 3> rotation{};  // degrees about x, y and z
  std::array<float, 3> scale{};
  std::int32_t field_ec = 0;     // +0xec; no consumer found yet
  std::uint32_t field_f0 = 0;    // +0xf0, copied byte for byte to ModelInstance +0x1f0
  float animation_speed = 1.0f;  // +0xf4; CWorld::LoadWorld uses a third of it
};

// One node of the scene graph's bounds (SceneGraphNode::LoadBounds 004c67e0).
struct WorldBounds {
  std::array<float, 3> max{};
  std::array<float, 3> min{};
  std::array<float, 3> half_extent{};
  std::array<float, 3> centre{};
};

struct World {
  std::uint8_t major = 0;
  std::uint8_t minor = 0;
  std::string ground;  // the .gnd
  std::string attr;    // the .gat
  std::string extra;   // the header's last name; no consumer found yet
  // The water, as CWorld::LoadWorld passes it to C3dGround::SetWaterParams (004bc410).
  float water_level = 0.0f;
  std::int32_t water_type = 0;            // the texture set, water<type % 6><frame>.jpg
  float water_param_170 = 1.0f;           // to C3dGround +0x54
  float water_phase_step = 2.0f;          // truncated: degrees added to a phase each frame
  float water_param_178 = 50.0f;          // truncated, to C3dGround +0x48
  std::int32_t water_frame_interval = 3;  // frames per texture of the animation
  // The directional light, as SceneRenderer::method_0040ef30 sets it with SetLight.
  std::int32_t light_angle_y = 45;      // degrees
  std::int32_t light_angle_x = 45;      // degrees
  std::array<float, 3> light_vector{};  // the light shines along its negation
  std::array<float, 3> diffuse{1.0f, 1.0f, 1.0f};
  std::array<float, 3> ambient{0.3f, 0.3f, 0.3f};
  // +0x1b0..+0x1bc, copied to the game mode's +0x48, +0x4c, +0x40 and +0x44.
  std::array<std::int32_t, 4> field_1b0{-500, 500, -500, 500};
  std::vector<WorldModel> models;
  std::vector<std::array<std::uint8_t, 0xc0>> sounds;   // object type 3
  std::vector<std::array<std::uint8_t, 0x74>> effects;  // object type 4
  std::vector<WorldBounds> bounds;                      // version 2.1: the scene graph, depth first
};

// C3dWorldRes::Load on a whole .rsw file. lightmaps is the ISLIGHTMAP option (0061ec60):
// without it the ambient colour is brightened.
bool DecodeWorld(std::span<const std::uint8_t> data, bool lightmaps, World& out,
                 std::string* error);

// The direction CWorld lights the world from: Matrix::MakeRotationX by angle_x, then
// RotateY by angle_y, both in degrees, and the sum of the matrix's second and fourth rows.
std::array<float, 3> WorldLightVector(std::int32_t angle_y, std::int32_t angle_x);

// C3dWorldRes.
class RswRes : public Resource {
 public:
  explicit RswRes(bool lightmaps = true) : lightmaps_(lightmaps) {}

  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const World& world() const { return world_; }
  const std::string& error() const { return error_; }

 private:
  bool lightmaps_;
  World world_;
  std::string error_;
};

// The original's registration (static initializer 00435810), with the ISLIGHTMAP option.
void RegisterWorldTypes(ResourceManager& manager, bool lightmaps = true);

}  // namespace ragexe::res
