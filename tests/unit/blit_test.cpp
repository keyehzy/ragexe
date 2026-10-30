#include "render/blit.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

using namespace ragexe::render;

namespace {

struct CallVector {
  SceneCall call;
  std::uint32_t vertices[32];  // a draw's four, x to v each
};

struct RectVector {
  int image;
  int x, y, width, height;
  std::uint32_t color;
  std::uint32_t texture, texture_width, texture_height;
  std::uint32_t scale[2], offset[2];
  std::uint32_t bound;
  const CallVector* calls;
  std::size_t call_count;
  std::uint32_t bound_after;
};

struct SurfaceVector {
  int slot;
  int x, y, width, height;
  std::uint32_t screen_width, screen_height;
  int kind;  // 0 none, 1 BltFast, 2 Blt
  int blit_x, blit_y;
  int rect[4];
  std::uint32_t flags;
};

#include "blit_vectors.inc"

float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }

}  // namespace

TEST_CASE("Texture rectangles make the original's calls", "[blit]") {
  std::size_t n = 0;
  for (const RectVector& v : kRects) {
    INFO("case " << n++ << (v.image != 0 ? ", DrawImage" : ", DrawRegion"));
    const TextureExtent extent{v.texture_width, v.texture_height, Float(v.scale[0]),
                               Float(v.scale[1])};
    const TexelOffset offset{Float(v.offset[0]), Float(v.offset[1])};
    BoundTextures bound{v.bound, 0};
    ScreenSprites out;
    if (v.image != 0) {
      DrawTextureImage(v.x, v.y, v.width, v.height, v.texture, extent, offset, bound, out);
    } else {
      DrawTextureRegion(v.x, v.y, v.width, v.height, v.color, v.texture, extent, offset, bound,
                        out);
    }
    REQUIRE(out.calls.size() == v.call_count);
    for (std::size_t i = 0; i < out.calls.size(); ++i) {
      INFO("call " << i);
      const SceneCall& got = out.calls[i];
      const SceneCall& want = v.calls[i].call;
      CHECK(got.kind == want.kind);
      CHECK(got.stage == want.stage);
      CHECK(got.state == want.state);
      CHECK(got.fvf == want.fvf);
      if (got.kind != SceneCall::Kind::kDraw) {
        CHECK(got.value == want.value);
        continue;
      }
      REQUIRE(got.value < out.quads.size());
      const auto& quad = out.quads[got.value];
      const std::uint32_t* vertices = v.calls[i].vertices;
      for (std::size_t k = 0; k < 4; ++k) {
        INFO("vertex " << k);
        const ScreenVertex& sv = quad[k];
        CHECK(std::bit_cast<std::uint32_t>(sv.x) == vertices[k * 8]);
        CHECK(std::bit_cast<std::uint32_t>(sv.y) == vertices[k * 8 + 1]);
        CHECK(std::bit_cast<std::uint32_t>(sv.z) == vertices[k * 8 + 2]);
        CHECK(std::bit_cast<std::uint32_t>(sv.rhw) == vertices[k * 8 + 3]);
        CHECK(sv.diffuse == vertices[k * 8 + 4]);
        CHECK(sv.specular == vertices[k * 8 + 5]);
        CHECK(std::bit_cast<std::uint32_t>(sv.u) == vertices[k * 8 + 6]);
        CHECK(std::bit_cast<std::uint32_t>(sv.v) == vertices[k * 8 + 7]);
      }
    }
    CHECK(bound.stage0 == v.bound_after);
  }
}

TEST_CASE("Surfaces are blitted as the original blits them", "[blit]") {
  std::size_t n = 0;
  for (const SurfaceVector& v : kSurfaces) {
    INFO("case " << n++ << ", slot " << v.slot);
    if (v.slot == 4) {
      const StretchBlit blit = StretchSurface(v.width, v.height);
      CHECK(v.kind == 2);
      CHECK(blit.destination[0] == v.rect[0]);
      CHECK(blit.destination[1] == v.rect[1]);
      CHECK(blit.destination[2] == v.rect[2]);
      CHECK(blit.destination[3] == v.rect[3]);
      CHECK(blit.flags == v.flags);
      continue;
    }
    const auto blit = BlitSurface(v.x, v.y, v.width, v.height, v.screen_width, v.screen_height);
    REQUIRE(blit.has_value() == (v.kind == 1));
    if (!blit) continue;
    CHECK(blit->x == v.blit_x);
    CHECK(blit->y == v.blit_y);
    CHECK(blit->source[0] == v.rect[0]);
    CHECK(blit->source[1] == v.rect[1]);
    CHECK(blit->source[2] == v.rect[2]);
    CHECK(blit->source[3] == v.rect[3]);
    CHECK(blit->flags == v.flags);
  }
}
