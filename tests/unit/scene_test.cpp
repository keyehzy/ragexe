#include "render/scene.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

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

#include "scene_vectors.inc"

void CheckCalls(const std::vector<SceneCall>& got, const SceneCall* want, std::size_t count) {
  const std::size_t n = std::min(got.size(), count);
  for (std::size_t i = 0; i < n; ++i) {
    INFO("call " << i);
    CHECK(static_cast<int>(got[i].kind) == static_cast<int>(want[i].kind));
    CHECK(got[i].stage == want[i].stage);
    CHECK(got[i].state == want[i].state);
    CHECK(got[i].value == want[i].value);
    CHECK(got[i].fvf == want[i].fvf);
    if (!(got[i] == want[i])) return;
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
