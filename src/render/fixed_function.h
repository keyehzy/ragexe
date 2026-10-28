#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "render/device.h"
#include "render/scene.h"

namespace ragexe::render {

// What a draw needs of the Direct3D 7 state the original drives, for a renderer without
// one (docs/render/gpu.md). The pipeline key holds what a GPU fixes in a pipeline; the
// stage uniforms what the fragment shader reads per draw.
struct PipelineKey {
  bool blend = false;
  std::uint32_t src_blend = 2;  // D3DBLEND
  std::uint32_t dest_blend = 1;
  bool z_test = true;
  bool z_write = true;
  std::uint32_t z_func = 4;     // D3DCMP
  std::uint32_t cull = 1;       // D3DCULL: NONE, CW, CCW
  std::uint32_t primitive = 5;  // D3DPRIMITIVETYPE
  bool two_coords = false;      // the 0x2c4 vertex format

  friend bool operator==(const PipelineKey&, const PipelineKey&) = default;
};

// The fragment shader's uniforms: the two texture stages' operations and arguments
// (D3DTOP, D3DTA with its modifiers), the alpha test and the fog.
struct StageUniforms {
  std::array<std::int32_t, 4> stage0_color{};  // op, arg1, arg2, unused
  std::array<std::int32_t, 4> stage0_alpha{};
  std::array<std::int32_t, 4> stage1_color{};
  std::array<std::int32_t, 4> stage1_alpha{};
  std::array<std::int32_t, 4> alpha_test{};  // enabled, D3DCMP function, reference, fog
  std::array<float, 4> fog_color{};

  friend bool operator==(const StageUniforms&, const StageUniforms&) = default;
};

// A stage's sampler: D3DTEXTUREFILTERTYPE and D3DTEXTUREADDRESS values.
struct SamplerKey {
  std::uint32_t mag_filter = 2;
  std::uint32_t min_filter = 2;
  std::uint32_t address_u = 1;
  std::uint32_t address_v = 1;

  friend bool operator==(const SamplerKey&, const SamplerKey&) = default;
  friend bool operator<(const SamplerKey& a, const SamplerKey& b) {
    return std::array{a.mag_filter, a.min_filter, a.address_u, a.address_v} <
           std::array{b.mag_filter, b.min_filter, b.address_u, b.address_v};
  }
};

// One draw: its primitive and everything set when it was made.
struct DrawRecord {
  std::uint32_t primitive = 0;  // the ScenePrimitive id
  PipelineKey pipeline;
  StageUniforms uniforms;
  std::array<std::uint32_t, 2> textures{};  // texture ids on stages 0 and 1, 0 for none
  std::array<SamplerKey, 2> samplers{};
};

// The device state a stream of calls builds up, from InitialDeviceState on: Direct3D 7's
// defaults where the original sets nothing, and its states as they change.
class FixedFunctionState {
 public:
  FixedFunctionState();

  // Applies one call; a draw appends its record.
  void Apply(const SceneCall& call, std::vector<DrawRecord>& draws);

  const DeviceState& state() const { return state_; }
  std::uint32_t fog_color() const { return fog_color_; }
  void set_fog_color(std::uint32_t argb) { fog_color_ = argb; }

 private:
  DeviceState state_;
  std::array<std::uint32_t, 2> textures_{};
  std::uint32_t fog_color_ = 0;
};

// The records of a whole stream.
std::vector<DrawRecord> RecordDraws(FixedFunctionState& state, const std::vector<SceneCall>& calls);

}  // namespace ragexe::render
