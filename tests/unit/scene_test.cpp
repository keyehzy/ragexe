#include "render/scene.h"

#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <vector>

#include "render/world_frame.h"

using namespace ragexe::render;

namespace {

struct CallList {
  const SceneCall* calls;
  std::size_t count;
};

struct ScenePrimitiveVector {
  std::uint32_t address, texture, lightmap, src_blend, dest_blend, mode, type, vertex_count;
  std::uint32_t rhw0_bits, rhw1_bits;
};

struct SceneAdd {
  std::size_t primitive;
  std::uint32_t flags;
};

struct SceneVector {
  int fog, lightmaps, two_stage;
  const ScenePrimitiveVector* primitives;
  std::size_t primitive_count;
  const SceneAdd* adds;
  std::size_t add_count;
  const SceneCall* calls;
  std::size_t call_count;
};

struct ScaledPrimitive {
  std::uint32_t address, texture, lightmap;
  std::uint32_t vertices[32];  // four 0x1c4 vertices
  std::uint32_t second[8];     // the second coordinates
};

struct ScaledDraw {
  std::uint32_t type, fvf, count;
  std::uint32_t words[40];
};

struct ScaledScene {
  int lightmaps, two_stage;
  const ScaledPrimitive* primitives;
  std::size_t primitive_count;
  const SceneAdd* adds;
  std::size_t add_count;
  const ScaledDraw* draws;
  std::size_t draw_count;
};

#include "scene_vectors.inc"

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }

void CheckCalls(const std::vector<SceneCall>& got, const SceneCall* want, std::size_t count) {
  const std::size_t n = std::min(got.size(), count);
  for (std::size_t i = 0; i < n; ++i) {
    INFO("call " << i);
    CHECK(static_cast<int>(got[i].kind) == static_cast<int>(want[i].kind));
    CHECK(got[i].stage == want[i].stage);
    CHECK(got[i].state == want[i].state);
    CHECK(got[i].value == want[i].value);
    CHECK(got[i].fvf == want[i].fvf);
    if (got[i].kind != want[i].kind || got[i].stage != want[i].stage ||
        got[i].state != want[i].state || got[i].value != want[i].value || got[i].fvf != want[i].fvf)
      return;
  }
  CHECK(got.size() == count);
}

}  // namespace

TEST_CASE("each stage mode sets what the original's SetStageMode does") {
  for (std::uint32_t mode = 0; mode < std::size(kModeVectors); ++mode) {
    INFO("mode " << mode);
    std::vector<SceneCall> calls;
    AppendStageModeCalls(mode, calls);
    CheckCalls(calls, kModeVectors[mode].calls, kModeVectors[mode].count);
  }
}

TEST_CASE("DrawScene makes the original's calls, in its order") {
  for (const SceneVector& v : kSceneVectors) {
    INFO("fog " << v.fog << ", lightmaps " << v.lightmaps << ", two stages " << v.two_stage);
    SceneLists lists;
    for (std::size_t k = 0; k < v.add_count; ++k) {
      const ScenePrimitiveVector& p = v.primitives[v.adds[k].primitive];
      lists.Add(
          {p.address, p.texture, p.lightmap, p.src_blend, p.dest_blend, p.mode, p.type,
           p.vertex_count, std::bit_cast<float>(p.rhw0_bits), std::bit_cast<float>(p.rhw1_bits)},
          v.adds[k].flags);
    }
    BoundTextures bound;
    const std::vector<SceneCall> calls =
        DrawScene(lists, {v.fog != 0, v.lightmaps != 0, v.two_stage != 0}, bound);
    CheckCalls(calls, v.calls, v.call_count);
    for (std::size_t l = 0; l < static_cast<std::size_t>(SceneList::kCount); ++l) {
      CHECK(lists.list(static_cast<SceneList>(l)).empty());
    }
  }
}

TEST_CASE("AddPrimitive's flags pick the list") {
  using namespace scene_flags;
  CHECK(SelectSceneList(0) == SceneList::kOpaque);
  CHECK(SelectSceneList(kGround) == SceneList::kGround);
  CHECK(SelectSceneList(kHeld8 | kGround) == SceneList::kHeld8);
  CHECK(SelectSceneList(kHeld800) == SceneList::kHeld800);
  CHECK(SelectSceneList(kTranslucent) == SceneList::kAlpha);
  CHECK(SelectSceneList(kTranslucent | kGround) == SceneList::kAlpha);
  CHECK(SelectSceneList(kTranslucent | kDepthWrite | kNoDepthTest) == SceneList::kAlphaDepthWrite);
  CHECK(SelectSceneList(kTranslucent | kNoDepthTest) == SceneList::kAlphaNoDepth);
  CHECK(SelectSceneList(kTranslucent | kAdditive) == SceneList::kAdditive);
  CHECK(SelectSceneList(kTranslucent | kAdditive | kDepthWrite) == SceneList::kAdditiveDepthWrite);
  CHECK(SelectSceneList(kTranslucent | kAdditive | kNoDepthTest) == SceneList::kAdditiveNoDepth);
  CHECK(SelectSceneList(kTranslucent | kStageMode | kAdditive) == SceneList::kStageModes);
  CHECK(SceneDepthKey(SceneList::kAlpha, 0.25f, 0.75f) == 0.25f);
  CHECK(SceneDepthKey(SceneList::kAdditive, 0.25f, 0.75f) == 0.5f);
}

TEST_CASE("the draws scale texture coordinates in place, as the original's do") {
  for (const ScaledScene& v : kScaledScenes) {
    INFO("lightmaps " << v.lightmaps << ", two stages " << v.two_stage);
    SceneLists lists;
    std::map<std::uint32_t, FramePrimitive> primitives;
    for (std::size_t k = 0; k < v.primitive_count; ++k) {
      const ScaledPrimitive& p = v.primitives[k];
      FramePrimitive& f = primitives[p.address];
      for (const auto& t : kScaledTextures) {
        if (t[0] == p.texture) f.uv = UvScale{F(t[1]), F(t[2])};
      }
      for (std::size_t n = 0; n < 4; ++n) {
        const std::uint32_t* w = p.vertices + n * 8;
        f.vertices.push_back({F(w[0]), F(w[1]), F(w[2]), F(w[3]), w[4], w[5], F(w[6]), F(w[7])});
        f.second.push_back({F(p.second[n * 2]), F(p.second[n * 2 + 1])});
      }
    }
    for (std::size_t k = 0; k < v.add_count; ++k) {
      const ScaledPrimitive& p = v.primitives[v.adds[k].primitive];
      lists.Add(
          {p.address, p.texture, p.lightmap, 5, 6, 0, 5, 4, F(p.vertices[3]), F(p.vertices[11])},
          v.adds[k].flags);
    }
    BoundTextures bound;
    const std::vector<FrameDraw> draws = FrameDraws(
        DrawScene(lists, {false, v.lightmaps != 0, v.two_stage != 0}, bound), primitives);
    REQUIRE(draws.size() == v.draw_count);
    for (std::size_t d = 0; d < draws.size(); ++d) {
      INFO("draw " << d);
      CHECK(draws[d].type == v.draws[d].type);
      CHECK(draws[d].fvf == v.draws[d].fvf);
      CHECK(draws[d].count == v.draws[d].count);
      REQUIRE(draws[d].words.size() == v.draws[d].count * (v.draws[d].fvf == 0x2c4 ? 10 : 8));
      for (std::size_t w = 0; w < draws[d].words.size(); ++w) {
        INFO("word " << w);
        CHECK(draws[d].words[w] == v.draws[d].words[w]);
      }
    }
  }
}

TEST_CASE("the primitive pools hand out nodes in the order of the original's addresses") {
  PrimitivePools pools;
  std::vector<std::uint32_t> ids;
  for (const auto& op : kPoolOps) {
    for (std::uint32_t n = 0; n < op[1]; ++n) {
      if (op[0] == 0) ids.push_back(pools.NewTriangleList());
      if (op[0] == 1) ids.push_back(pools.NewQuad());
      if (op[0] == 2) pools.Rewind();
    }
  }
  std::vector<std::uint32_t> sorted = ids;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
  std::vector<std::uint32_t> want;
  for (const auto& run : kPoolRanks) {
    for (std::uint32_t n = 0; n < run[1]; ++n) want.push_back(run[0] + n);
  }
  REQUIRE(ids.size() == want.size());
  for (std::size_t k = 0; k < ids.size(); ++k) {
    INFO("node " << k);
    const auto rank = static_cast<std::uint32_t>(
        std::lower_bound(sorted.begin(), sorted.end(), ids[k]) - sorted.begin());
    CHECK(rank == want[k]);
  }
}
