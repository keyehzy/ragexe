#include "render/fixed_function.h"

#include "render/device.h"

namespace ragexe::render {

namespace {

// Direct3D 7's defaults for the states the draws read and the original leaves alone.
constexpr std::uint32_t kAlphaBlendDefault = 0;
constexpr std::uint32_t kAlphaTestDefault = 0;
constexpr std::uint32_t kFogEnableDefault = 0;
constexpr std::uint32_t kFogEnable = 28;  // D3DRENDERSTATE_FOGENABLE
constexpr std::uint32_t kFogColor = 34;   // D3DRENDERSTATE_FOGCOLOR

std::uint32_t Get(const DeviceState& s, std::uint32_t state, std::uint32_t fallback) {
  return s.render_state(state).value_or(fallback);
}

std::int32_t Stage(const DeviceState& s, int stage, std::uint32_t state, std::uint32_t fallback) {
  return static_cast<std::int32_t>(s.stage_state(stage, state).value_or(fallback));
}

// D3DTSS defaults: stage 0 MODULATE(TEXTURE, CURRENT) and SELECTARG1(TEXTURE), later
// stages DISABLE.
std::array<std::int32_t, 4> Colors(const DeviceState& s, int stage) {
  return {Stage(s, stage, d3d7::kColorOp, stage == 0 ? d3d7::kOpModulate : d3d7::kOpDisable),
          Stage(s, stage, d3d7::kColorArg1, d3d7::kArgTexture),
          Stage(s, stage, d3d7::kColorArg2, d3d7::kArgCurrent), 0};
}

std::array<std::int32_t, 4> Alphas(const DeviceState& s, int stage) {
  return {Stage(s, stage, d3d7::kAlphaOp, stage == 0 ? 2 : d3d7::kOpDisable),
          Stage(s, stage, d3d7::kAlphaArg1, d3d7::kArgTexture),
          Stage(s, stage, d3d7::kAlphaArg2, d3d7::kArgCurrent), 0};
}

SamplerKey Sampler(const DeviceState& s, int stage) {
  return {s.stage_state(stage, d3d7::kMagFilter).value_or(d3d7::kFilterPoint),
          s.stage_state(stage, d3d7::kMinFilter).value_or(d3d7::kFilterPoint),
          s.stage_state(stage, d3d7::kAddressU).value_or(d3d7::kAddressWrap),
          s.stage_state(stage, d3d7::kAddressV).value_or(d3d7::kAddressWrap)};
}

}  // namespace

FixedFunctionState::FixedFunctionState() : state_(InitialDeviceState()) {}

void FixedFunctionState::Apply(const SceneCall& call, std::vector<DrawRecord>& draws) {
  switch (call.kind) {
    case SceneCall::Kind::kRenderState:
      state_.SetRenderState(call.state, call.value);
      if (call.state == kFogColor) fog_color_ = call.value;
      return;
    case SceneCall::Kind::kStageState:
      state_.SetTextureStageState(call.stage, call.state, call.value);
      return;
    case SceneCall::Kind::kTexture:
      if (call.stage == 0 || call.stage == 1)
        textures_[static_cast<std::size_t>(call.stage)] = call.value;
      return;
    case SceneCall::Kind::kDraw: break;
  }
  DrawRecord r;
  r.primitive = call.value;
  PipelineKey& p = r.pipeline;
  p.blend = Get(state_, d3d7::kAlphaBlendEnable, kAlphaBlendDefault) != 0;
  p.src_blend = Get(state_, d3d7::kSrcBlend, 2);
  p.dest_blend = Get(state_, d3d7::kDestBlend, 1);
  p.z_test = Get(state_, d3d7::kZEnable, 1) != 0;
  p.z_write = Get(state_, d3d7::kZWriteEnable, 1) != 0;
  p.z_func = Get(state_, d3d7::kZFunc, d3d7::kCmpLessEqual);
  p.cull = Get(state_, d3d7::kCullMode, 3);
  p.primitive = call.state;
  p.two_coords = call.fvf == 0x2c4;
  StageUniforms& u = r.uniforms;
  u.stage0_color = Colors(state_, 0);
  u.stage0_alpha = Alphas(state_, 0);
  u.stage1_color = Colors(state_, 1);
  u.stage1_alpha = Alphas(state_, 1);
  u.alpha_test = {static_cast<std::int32_t>(Get(state_, d3d7::kAlphaTestEnable, kAlphaTestDefault)),
                  static_cast<std::int32_t>(Get(state_, d3d7::kAlphaFunc, 8)),
                  static_cast<std::int32_t>(Get(state_, d3d7::kAlphaRef, 0)),
                  static_cast<std::int32_t>(Get(state_, kFogEnable, kFogEnableDefault))};
  u.fog_color = {static_cast<float>(fog_color_ >> 16 & 0xff) / 255.0f,
                 static_cast<float>(fog_color_ >> 8 & 0xff) / 255.0f,
                 static_cast<float>(fog_color_ & 0xff) / 255.0f, 1.0f};
  r.textures = textures_;
  r.samplers = {Sampler(state_, 0), Sampler(state_, 1)};
  draws.push_back(r);
}

std::vector<DrawRecord> RecordDraws(FixedFunctionState& state,
                                    const std::vector<SceneCall>& calls) {
  std::vector<DrawRecord> draws;
  for (const SceneCall& c : calls) state.Apply(c, draws);
  return draws;
}

}  // namespace ragexe::render
