#include "render/effect.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "render/camera.h"
#include "res/str.h"

using namespace ragexe::render;

namespace {

struct KeyVector {
  int frame, type;
  std::uint32_t values[29];
};

struct LayerVector {
  int textures;
  const KeyVector* keys;
  std::size_t count;
};

struct TriangleVector {
  std::uint32_t vertices[24];  // three of x, y, z, rhw, diffuse, specular, u, v
  std::uint32_t texture, src_blend, dest_blend, mode, flags;
};

struct FrameVector {
  std::uint32_t offset[3];
  int more;
  std::uint32_t screen[3];
  int finished;
  int current[8];
  int moved[8];
  const std::uint32_t* state;
  const TriangleVector* triangles;
  std::size_t count;
};

struct ViewVector {
  int frame;
  std::uint32_t view[12];
  std::uint32_t position[3];
};

struct EffectVector {
  const LayerVector* layers;
  std::size_t layer_count;
  const FrameVector* frames;
  std::size_t frame_count;
  const ViewVector* views;
  std::size_t view_count;
};

#include "effect_vectors.inc"

constexpr std::uint32_t kTextureBase = 0x5000;  // GetEffectEntryTexture's stand-in
constexpr std::uint32_t kStageModeFlags = 5;

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }

ragexe::res::Str EffectOf(const EffectVector& e) {
  ragexe::res::Str str;
  for (std::size_t l = 0; l < e.layer_count; ++l) {
    ragexe::res::StrLayer layer;
    layer.texture_count = e.layers[l].textures;
    for (std::size_t k = 0; k < e.layers[l].count; ++k) {
      const KeyVector& kv = e.layers[l].keys[k];
      ragexe::res::StrKey key;
      key.frame = kv.frame;
      key.type = kv.type;
      for (std::size_t i = 0; i < 29; ++i) key.values[i] = kv.values[i];
      layer.keys.push_back(key);
    }
    str.layers.push_back(layer);
  }
  return str;
}

}  // namespace

TEST_CASE("Effects play and draw as the original's", "[effect]") {
  const Projection projection = SetSize(800, 600);
  std::size_t n = 0;
  for (const EffectVector& e : kEffects) {
    INFO("effect " << n++);
    const ragexe::res::Str str = EffectOf(e);
    StrPlayer player(str);
    Matrix view{};
    Vec3 position{};
    std::size_t next_view = 0;
    for (std::size_t f = 0; f < e.frame_count; ++f) {
      INFO("frame " << f);
      if (next_view < e.view_count && std::cmp_equal(e.views[next_view].frame, f)) {
        for (std::size_t i = 0; i < 12; ++i) view[i] = F(e.views[next_view].view[i]);
        for (std::size_t i = 0; i < 3; ++i) position[i] = F(e.views[next_view].position[i]);
        ++next_view;
      }
      const FrameVector& v = e.frames[f];
      CHECK(player.Advance(view, position, projection, F(v.offset[0]), F(v.offset[1]),
                           F(v.offset[2])) == (v.more != 0));
      CHECK(B(player.screen_x()) == v.screen[0]);
      CHECK(B(player.screen_y()) == v.screen[1]);
      CHECK(B(player.screen_z()) == v.screen[2]);
      CHECK(player.finished() == v.finished);
      for (std::size_t l = 0; l < e.layer_count; ++l) {
        INFO("layer " << l);
        CHECK(player.keys()[l] == v.current[l]);
        CHECK(player.moved()[l] == (v.moved[l] != 0));
        for (std::size_t i = 0; i < 29; ++i) {
          INFO("value " << i);
          CHECK(player.states()[l].values[i] == v.state[l * 29 + i]);
        }
      }
      const std::vector<EffectTriangle> triangles = player.Draw();
      REQUIRE(triangles.size() == v.count);
      for (std::size_t t = 0; t < v.count; ++t) {
        INFO("triangle " << t);
        const EffectTriangle& got = triangles[t];
        const TriangleVector& want = v.triangles[t];
        CHECK(kTextureBase + static_cast<std::uint32_t>(got.texture) == want.texture);
        CHECK(got.src_blend == want.src_blend);
        CHECK(got.dest_blend == want.dest_blend);
        CHECK(got.mode == want.mode);
        CHECK(want.flags == kStageModeFlags);
        for (std::size_t k = 0; k < 3; ++k) {
          INFO("vertex " << k);
          const ScreenVertex& sv = got.vertices[k];
          const std::uint32_t* w = want.vertices + k * 8;
          CHECK(B(sv.x) == w[0]);
          CHECK(B(sv.y) == w[1]);
          CHECK(B(sv.z) == w[2]);
          CHECK(B(sv.rhw) == w[3]);
          CHECK(sv.diffuse == w[4]);
          CHECK(sv.specular == w[5]);
          CHECK(B(sv.u) == w[6]);
          CHECK(B(sv.v) == w[7]);
        }
      }
      player.Step();
    }
  }
}
