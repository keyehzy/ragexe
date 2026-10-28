#include "render/fixed_function.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "render/device.h"
#include "render/scene.h"

using namespace ragexe::render;

namespace {

SceneCall Render(std::uint32_t state, std::uint32_t value) {
  return {SceneCall::Kind::kRenderState, -1, state, value, 0};
}
SceneCall Stage(int stage, std::uint32_t state, std::uint32_t value) {
  return {SceneCall::Kind::kStageState, stage, state, value, 0};
}
SceneCall Texture(int stage, std::uint32_t id) {
  return {SceneCall::Kind::kTexture, stage, 0, id, 0};
}
SceneCall Draw(std::uint32_t id, std::uint32_t fvf = 0x1c4) {
  return {SceneCall::Kind::kDraw, -1, 5, id, fvf};
}

}  // namespace

TEST_CASE("a draw takes Init's states and Direct3D's defaults") {
  FixedFunctionState state;
  const auto draws = RecordDraws(state, {Draw(7)});
  REQUIRE(draws.size() == 1);
  const DrawRecord& d = draws[0];
  CHECK(d.primitive == 7);
  CHECK_FALSE(d.pipeline.blend);  // ALPHABLENDENABLE defaults to off
  CHECK(d.pipeline.src_blend == d3d7::kBlendSrcAlpha);
  CHECK(d.pipeline.dest_blend == d3d7::kBlendInvSrcAlpha);
  CHECK(d.pipeline.z_test);
  CHECK(d.pipeline.z_write);
  CHECK(d.pipeline.z_func == d3d7::kCmpLessEqual);
  CHECK(d.pipeline.cull == d3d7::kCullNone);
  CHECK(d.pipeline.primitive == 5);
  CHECK_FALSE(d.pipeline.two_coords);
  CHECK(d.uniforms.stage0_color[0] == static_cast<std::int32_t>(d3d7::kOpModulate));
  CHECK(d.uniforms.stage1_color[0] == static_cast<std::int32_t>(d3d7::kOpModulateAlphaAddColor));
  CHECK(d.uniforms.alpha_test[0] == 0);  // ALPHATESTENABLE defaults to off
  CHECK(d.uniforms.alpha_test[2] == 0xf0);
  CHECK(d.textures[0] == 0);
  CHECK(d.samplers[0].mag_filter == d3d7::kFilterLinear);
  CHECK(d.samplers[0].address_u == d3d7::kAddressClamp);
  CHECK(d.samplers[1].address_v == d3d7::kAddressBorder);
  CHECK(d.samplers[1].min_filter == d3d7::kFilterLinear);
}

TEST_CASE("states and textures set before a draw are its own") {
  FixedFunctionState state;
  const auto draws =
      RecordDraws(state, {Render(d3d7::kAlphaBlendEnable, 1), Render(d3d7::kSrcBlend, 2),
                          Render(d3d7::kDestBlend, 2), Render(d3d7::kZWriteEnable, 0),
                          Render(d3d7::kCullMode, 2), Render(d3d7::kAlphaTestEnable, 1),
                          Render(d3d7::kAlphaRef, 0xcf), Render(28, 1), Render(34, 0x00336699),
                          Stage(1, d3d7::kColorOp, d3d7::kOpDisable), Texture(0, 3), Texture(1, 9),
                          Draw(1, 0x2c4), Texture(1, 0), Render(d3d7::kZEnable, 0), Draw(2)});
  REQUIRE(draws.size() == 2);
  const DrawRecord& a = draws[0];
  CHECK(a.pipeline.blend);
  CHECK(a.pipeline.src_blend == 2);
  CHECK(a.pipeline.dest_blend == 2);
  CHECK_FALSE(a.pipeline.z_write);
  CHECK(a.pipeline.cull == 2);
  CHECK(a.pipeline.two_coords);
  CHECK(a.uniforms.alpha_test[0] == 1);
  CHECK(a.uniforms.alpha_test[1] == static_cast<std::int32_t>(d3d7::kCmpGreaterEqual));
  CHECK(a.uniforms.alpha_test[2] == 0xcf);
  CHECK(a.uniforms.alpha_test[3] == 1);
  CHECK(a.uniforms.fog_color[0] == 0x33 / 255.0f);
  CHECK(a.uniforms.stage1_color[0] == static_cast<std::int32_t>(d3d7::kOpDisable));
  CHECK(a.textures[0] == 3);
  CHECK(a.textures[1] == 9);
  const DrawRecord& b = draws[1];
  CHECK_FALSE(b.pipeline.z_test);
  CHECK(b.textures[1] == 0);
  CHECK_FALSE(b.pipeline.two_coords);
}
