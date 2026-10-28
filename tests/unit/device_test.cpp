#include "render/device.h"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>

using namespace ragexe::render;

namespace {

struct StateCall {
  int stage;  // -1 for a render state
  std::uint32_t state;
  std::uint32_t value;
};

struct InitVector {
  const char* name;
  const StateCall* calls;
  std::size_t call_count;
  std::uint32_t bit_count;
  std::uint32_t masks[4];
  std::int32_t shifts[4];
  std::int32_t losses[4];
};

struct ExpandVector {
  std::size_t init;
  std::uint32_t pixel;
  std::int32_t red, green, blue;
};

struct FitVector {
  std::uint32_t min_width, min_height, max_width, max_height, max_aspect;
  std::uint32_t width, height, fitted_width, fitted_height;
};

struct MipmapVector {
  std::uint32_t setting, caps, description_flags, surface_caps;
};

#include "device_vectors.inc"

PixelChannels ChannelsOf(const InitVector& v) {
  return ChannelsFromFormat(v.bit_count, v.masks[0], v.masks[1], v.masks[2], v.masks[3]);
}

}  // namespace

TEST_CASE("InitialDeviceState is what the original's Init sets") {
  for (const InitVector& v : kInitVectors) {
    INFO(v.name);
    DeviceState replayed;
    for (std::size_t i = 0; i < v.call_count; ++i) {
      const StateCall& c = v.calls[i];
      if (c.stage < 0) {
        replayed.SetRenderState(c.state, c.value);
      } else {
        replayed.SetTextureStageState(c.stage, c.state, c.value);
      }
    }
    CHECK(replayed == InitialDeviceState());
  }
}

TEST_CASE("ADDRESS sets both address modes, and the filter follows +0x1e4") {
  DeviceState s;
  s.SetTextureStageState(0, d3d7::kAddressV, d3d7::kAddressWrap);
  s.SetTextureStageState(0, d3d7::kAddress, d3d7::kAddressClamp);
  CHECK(s.stage_state(0, d3d7::kAddressU) == d3d7::kAddressClamp);
  CHECK(s.stage_state(0, d3d7::kAddressV) == d3d7::kAddressClamp);
  CHECK_FALSE(s.stage_state(0, d3d7::kAddress).has_value());
  CHECK_FALSE(s.render_state(d3d7::kAlphaTestEnable).has_value());

  const DeviceState point = InitialDeviceState(true);
  for (const int stage : {0, 1}) {
    CHECK(point.stage_state(stage, d3d7::kMinFilter) == d3d7::kFilterPoint);
    CHECK(point.stage_state(stage, d3d7::kMagFilter) == d3d7::kFilterPoint);
    CHECK(InitialDeviceState().stage_state(stage, d3d7::kMagFilter) == d3d7::kFilterLinear);
  }
}

TEST_CASE("channel shifts and losses match the original's") {
  for (const InitVector& v : kInitVectors) {
    INFO(v.name);
    const PixelChannels c = ChannelsOf(v);
    const PixelChannel* channels[] = {&c.red, &c.green, &c.blue, &c.alpha};
    for (std::size_t k = 0; k < 4; ++k) {
      INFO(k);
      CHECK(channels[k]->mask == v.masks[k]);
      CHECK(channels[k]->shift == v.shifts[k]);
      CHECK(channels[k]->loss == v.losses[k]);
    }
  }
}

TEST_CASE("channel expansion matches the original's") {
  for (const ExpandVector& v : kExpandVectors) {
    const InitVector& init = kInitVectors[v.init];
    INFO(init.name << ", pixel " << v.pixel);
    const PixelChannels c = ChannelsOf(init);
    CHECK(ExpandChannel(c.red, v.pixel) == v.red);
    CHECK(ExpandChannel(c.green, v.pixel) == v.green);
    CHECK(ExpandChannel(c.blue, v.pixel) == v.blue);
  }
}

TEST_CASE("FitTextureSize matches the original's") {
  for (const FitVector& v : kFitVectors) {
    INFO(v.width << "x" << v.height << " within " << v.min_width << "x" << v.min_height << " to "
                 << v.max_width << "x" << v.max_height << ", aspect " << v.max_aspect);
    const auto fitted = FitTextureSize(
        v.width, v.height, {v.min_width, v.min_height, v.max_width, v.max_height, v.max_aspect});
    REQUIRE(fitted.has_value());
    CHECK(fitted->width == v.fitted_width);
    CHECK(fitted->height == v.fitted_height);
  }
}

TEST_CASE("FitTextureSize fails where the original hangs or divides by zero") {
  CHECK_FALSE(FitTextureSize(0x80000001u, 4, {}).has_value());
  CHECK(FitTextureSize(0x80000000u, 4, {1, 1, 0xffffffffu, 0xffffffffu, 0}).has_value());
  CHECK_FALSE(FitTextureSize(64, 64, {1, 1, 64, 0, 2}).has_value());
  CHECK_FALSE(FitTextureSize(64, 64, {1, 1, 0, 64, 2}).has_value());
  CHECK(FitTextureSize(64, 64, {1, 1, 64, 0, 0})->height == 0);
}

TEST_CASE("SelectMipmapFlags matches the original's") {
  for (const MipmapVector& v : kMipmapVectors) {
    INFO(v.setting << ", caps " << v.caps);
    CHECK(SelectMipmapFlags(v.setting, v.caps) == MipmapFlags{v.description_flags, v.surface_caps});
  }
}

TEST_CASE("the pixel mode, the two-stage test and the texture divisor") {
  CHECK(SelectPixelMode(ChannelsFromFormat(16, 0xf800, 0x7e0, 0x1f, 0)).mode == PixelMode::kRgb565);
  CHECK(SelectPixelMode(ChannelsFromFormat(16, 0x7c00, 0x3e0, 0x1f, 0)).mode == PixelMode::kRgb555);
  const PixelModeChoice bgr = SelectPixelMode(ChannelsFromFormat(16, 0x1f, 0x7e0, 0xf800, 0));
  CHECK(bgr.mode == PixelMode::kRgb565);
  CHECK(bgr.message == "BGR 565 MODE. NOT SUPPORTED");
  CHECK(SelectPixelMode(ChannelsFromFormat(32, 0xff0000, 0xff00, 0xff, 0)).mode ==
        PixelMode::kRgb32);
  const PixelModeChoice other = SelectPixelMode(ChannelsFromFormat(24, 0xff0000, 0xff00, 0xff, 0));
  CHECK(other.mode == PixelMode::kUnsupported);
  CHECK(other.message == "NOT SUPPORTED MODE");

  CHECK(CanBlendTwoStages(2, 0x20000));
  CHECK_FALSE(CanBlendTwoStages(1, 0x20000));
  CHECK_FALSE(CanBlendTwoStages(8, 0x1ffff));

  CHECK(TextureDivisor(0) == 4);
  CHECK(TextureDivisor(1) == 2);
  CHECK(TextureDivisor(2) == 1);
  CHECK(TextureDivisor(7) == 1);
}
