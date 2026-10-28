#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

#include "render/ground.h"
#include "res/rsw.h"

using namespace ragexe::render;

namespace {

struct WaterStep {
  std::int32_t frame;
  std::int32_t phase;
  const char* texture;
};

struct WaterVector {
  std::uint32_t rsw[6];     // level, type, wave height, phase step, step, interval
  std::uint32_t fields[6];  // +0x40 level, +0x44 interval, +0x48 step, +0x4c phase step,
                            // +0x50 type, +0x54 wave height
  const WaterStep* steps;
};

#include "water_vectors.inc"

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }

}  // namespace

TEST_CASE("The water steps its frame, phase and texture as the original does", "[water]") {
  std::size_t index = 0;
  for (const WaterVector& v : kWaterVectors) {
    INFO("case " << index++);
    ragexe::res::World world;
    world.water_level = F(v.rsw[0]);
    world.water_type = static_cast<std::int32_t>(v.rsw[1]);
    world.water_param_170 = F(v.rsw[2]);
    world.water_phase_step = F(v.rsw[3]);
    world.water_param_178 = F(v.rsw[4]);
    world.water_frame_interval = static_cast<std::int32_t>(v.rsw[5]);
    GroundWater water = WaterOfWorld(world);
    CHECK(B(water.level) == v.fields[0]);
    CHECK(water.interval == static_cast<std::int32_t>(v.fields[1]));
    CHECK(water.step == static_cast<std::int32_t>(v.fields[2]));
    CHECK(water.phase_step == static_cast<std::int32_t>(v.fields[3]));
    CHECK(water.type == static_cast<std::int32_t>(v.fields[4]));
    CHECK(B(water.wave_height) == v.fields[5]);
    for (std::size_t s = 0; s < 200; ++s) {
      INFO("step " << s);
      const std::string texture = StepWater(water);
      CHECK(water.frame == v.steps[s].frame);
      CHECK(water.phase == v.steps[s].phase);
      CHECK(texture == v.steps[s].texture);
    }
  }
}
