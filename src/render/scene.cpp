#include "render/scene.h"

#include "res/vc6_sort.h"

namespace ragexe::render {

namespace {

// D3DRENDERSTATETYPE values DrawScene sets.
constexpr std::uint32_t kZEnable = 7;
constexpr std::uint32_t kZWriteEnable = 14;
constexpr std::uint32_t kAlphaTestEnable = 15;
constexpr std::uint32_t kSrcBlend = 19;
constexpr std::uint32_t kDestBlend = 20;
constexpr std::uint32_t kCullMode = 22;
constexpr std::uint32_t kAlphaRef = 24;
constexpr std::uint32_t kAlphaBlendEnable = 27;
constexpr std::uint32_t kFogEnable = 28;
constexpr std::uint32_t kAmbient = 139;
// D3DTEXTURESTAGESTATETYPE values.
constexpr std::uint32_t kColorOp = 1;
constexpr std::uint32_t kColorArg1 = 2;
constexpr std::uint32_t kColorArg2 = 3;
constexpr std::uint32_t kAlphaOp = 4;
constexpr std::uint32_t kAlphaArg1 = 5;
constexpr std::uint32_t kAlphaArg2 = 6;
constexpr std::uint32_t kTexCoordIndex = 11;
constexpr std::uint32_t kMipFilter = 18;
// D3DTEXTUREOP and D3DTA_ values.
constexpr std::uint32_t kDisable = 1;
constexpr std::uint32_t kSelectArg1 = 2;
constexpr std::uint32_t kSelectArg2 = 3;
constexpr std::uint32_t kModulate = 4;
constexpr std::uint32_t kModulateAlphaAddColor = 18;
constexpr std::uint32_t kDiffuse = 0;
constexpr std::uint32_t kCurrent = 1;
constexpr std::uint32_t kTexture = 2;

constexpr std::uint32_t kFvf = 0x1c4;           // XYZRHW | DIFFUSE | SPECULAR | TEX1
constexpr std::uint32_t kFvfTwoCoords = 0x2c4;  // the same with TEX2

// A mode's operations: stage 0's colour argument and operation and its alpha operation,
// then stage 1's colour argument and operation, or a stage 1 that is off.
struct StageMode {
  std::uint32_t arg0, op0, alpha_op0;
  bool stage1;
  std::uint32_t arg1, op1;
};

// 004112d0's switch; the arguments with D3DTA_COMPLEMENT (0x10) or D3DTA_ALPHAREPLICATE
// (0x20).
constexpr StageMode kStageModes[16] = {
    {kTexture, kModulate, kModulate, false, 0, 0},
    {kTexture, kModulate, kSelectArg1, false, 0, 0},
    {kTexture, kModulate, kSelectArg1, false, 0, 0},
    {kTexture, 7, kSelectArg2, false, 0, 0},   // ADD
    {kTexture, 13, kSelectArg2, false, 0, 0},  // BLENDTEXTUREALPHA
    {kTexture, kModulate, kSelectArg2, true, kTexture, kModulate},
    {0x12, kModulate, kSelectArg2, true, 0x12, kModulate},
    {kTexture, kSelectArg1, kSelectArg1, true, 0x22, kModulate},
    {kTexture, kModulate, kSelectArg1, true, kTexture, 7},
    {kTexture, kModulate, kSelectArg2, true, kTexture, 13},
    {kTexture, kModulate, kSelectArg2, true, kTexture, 12},  // BLENDDIFFUSEALPHA
    {kTexture, kModulate, kSelectArg2, true, kTexture, 7},
    {kTexture, kModulate, kSelectArg2, true, 0x12, 7},
    {kTexture, 7, kSelectArg1, true, kTexture, kModulate},
    {kTexture, kModulate, kSelectArg2, true, kTexture, 5},  // MODULATE2X
    {kTexture, kModulate, kSelectArg2, true, kTexture, 8},  // ADDSIGNED
};

// One DrawScene: the calls so far and the bound textures. Opaque lists sort by texture,
// then lightmap (IsPrimitiveLessByTexture, 0040f440); translucent ones by key, then
// primitive (IsFloatUintPairLess, 00415f50).
struct Flush {
  SceneLists& lists;
  BoundTextures& bound;
  std::vector<SceneCall> out;

  const ScenePrimitive& at(const SceneEntry& e) const { return lists.primitives()[e.primitive]; }

  void RenderState(std::uint32_t state, std::uint32_t value) {
    out.push_back({SceneCall::Kind::kRenderState, -1, state, value, 0});
  }
  void StageState(std::int32_t stage, std::uint32_t state, std::uint32_t value) {
    out.push_back({SceneCall::Kind::kStageState, stage, state, value, 0});
  }
  void Texture0(std::uint32_t texture) {
    if (bound.stage0 == texture) return;
    out.push_back({SceneCall::Kind::kTexture, 0, 0, texture, 0});
    bound.stage0 = texture;
  }
  void Texture1(std::uint32_t texture) {
    if (bound.stage1 == texture) return;
    out.push_back({SceneCall::Kind::kTexture, 1, 0, texture, 0});
    bound.stage1 = texture;
  }
  void Draw(const ScenePrimitive& p, std::uint32_t fvf) {
    if (p.vertex_count == 0) return;
    out.push_back({SceneCall::Kind::kDraw, -1, p.type, p.id, fvf});
  }

  template <typename Less>
  void Sort(SceneList l, Less less) {
    std::vector<SceneEntry>& v = lists.list(l);
    res::Vc6Sort(v.data(), v.data() + v.size(), less);
  }
  void SortByTexture(SceneList l) {
    Sort(l, [this](const SceneEntry& a, const SceneEntry& b) {
      const ScenePrimitive& pa = at(a);
      const ScenePrimitive& pb = at(b);
      if (pa.texture != pb.texture) return pa.texture < pb.texture;
      return pa.lightmap < pb.lightmap;
    });
  }
  void SortByDepth(SceneList l) {
    Sort(l, [this](const SceneEntry& a, const SceneEntry& b) {
      if (a.key != b.key) return a.key < b.key;
      return at(a).id < at(b).id;
    });
  }

  // 0040f300 and 0040fa40, then the translucent lists' passes (0040fec0 and others).
  void DrawSorted(SceneList l, bool by_depth) {
    if (by_depth) {
      SortByDepth(l);
    } else {
      SortByTexture(l);
    }
    for (const SceneEntry& e : lists.list(l)) {
      Texture0(at(e).texture);
      Draw(at(e), kFvf);
    }
  }
  void UnbindStage1() {
    if (bound.stage1 == 0) return;
    out.push_back({SceneCall::Kind::kTexture, 1, 0, 0, 0});
    bound.stage1 = 0;
  }
  // 0040fb60: the ground again, by lightmap alone, with the lightmap's coordinates.
  void DrawGroundLightmaps() {
    Sort(SceneList::kGround, [this](const SceneEntry& a, const SceneEntry& b) {
      return at(a).lightmap < at(b).lightmap;
    });
    for (const SceneEntry& e : lists.list(SceneList::kGround)) {
      Texture0(at(e).lightmap);
      Draw(at(e), kFvf);
    }
  }
  // 0040f900: the ground with both textures at once.
  void DrawGroundTwoStages() {
    SortByTexture(SceneList::kGround);
    for (const SceneEntry& e : lists.list(SceneList::kGround)) {
      Texture0(at(e).texture);
      Texture1(at(e).lightmap);
      Draw(at(e), kFvfTwoCoords);
    }
    UnbindStage1();
  }
  // 00410560: in the order added, each with its mode and blend.
  void DrawStageModes() {
    for (const SceneEntry& e : lists.list(SceneList::kStageModes)) {
      const ScenePrimitive& p = at(e);
      Texture0(p.texture);
      AppendStageModeCalls(p.mode, out);
      RenderState(kSrcBlend, p.src_blend);
      RenderState(kDestBlend, p.dest_blend);
      Draw(p, kFvf);
    }
  }
};

}  // namespace

SceneList SelectSceneList(std::uint32_t flags) {
  using namespace scene_flags;
  if ((flags & kTranslucent) == 0) {
    if ((flags & kHeld8) != 0) return SceneList::kHeld8;
    if ((flags & kGround) != 0) return SceneList::kGround;
    if ((flags & kHeld800) != 0) return SceneList::kHeld800;
    return SceneList::kOpaque;
  }
  if ((flags & kStageMode) != 0) return SceneList::kStageModes;
  if ((flags & kAdditive) == 0) {
    if ((flags & kDepthWrite) != 0) return SceneList::kAlphaDepthWrite;
    if ((flags & kNoDepthTest) != 0) return SceneList::kAlphaNoDepth;
    return SceneList::kAlpha;
  }
  if ((flags & kDepthWrite) != 0) return SceneList::kAdditiveDepthWrite;
  if ((flags & kNoDepthTest) != 0) return SceneList::kAdditiveNoDepth;
  return SceneList::kAdditive;
}

float SceneDepthKey(SceneList list, float rhw0, float rhw1) {
  if (list == SceneList::kAlpha) return rhw0;
  // One operation a statement, so that no compiler fuses them: the client's x87 rounds
  // each to single precision.
  const float difference = rhw0 - rhw1;
  const float half = difference * 0.5f;
  return half + rhw1;
}

void SceneLists::Add(const ScenePrimitive& primitive, std::uint32_t flags) {
  const SceneList l = SelectSceneList(flags);
  const auto index = static_cast<std::uint32_t>(primitives_.size());
  primitives_.push_back(primitive);
  SceneEntry entry{0.0f, index};
  switch (l) {
    case SceneList::kAlphaDepthWrite:
    case SceneList::kAlpha:
    case SceneList::kAlphaNoDepth:
    case SceneList::kAdditiveDepthWrite:
    case SceneList::kAdditive:
    case SceneList::kAdditiveNoDepth:
      entry.key = SceneDepthKey(l, primitive.rhw0, primitive.rhw1);
      break;
    default: break;
  }
  list(l).push_back(entry);
}

void SceneLists::Clear() {
  primitives_.clear();
  for (auto& l : lists_) l.clear();
}

void AppendStageModeCalls(std::uint32_t mode, std::vector<SceneCall>& out) {
  if (mode >= 16) return;
  const StageMode& m = kStageModes[mode];
  const auto stage = [&](std::int32_t s, std::uint32_t state, std::uint32_t value) {
    out.push_back({SceneCall::Kind::kStageState, s, state, value, 0});
  };
  stage(0, kTexCoordIndex, 0);
  stage(0, kColorArg1, m.arg0);
  stage(0, kColorOp, m.op0);
  if (m.op0 != kSelectArg1) stage(0, kColorArg2, kDiffuse);
  if (m.alpha_op0 == kSelectArg2) {
    stage(0, kAlphaOp, kSelectArg2);
    stage(0, kAlphaArg2, kDiffuse);
  } else {
    stage(0, kAlphaArg1, kTexture);
    stage(0, kAlphaOp, m.alpha_op0);
    if (m.alpha_op0 != kSelectArg1) stage(0, kAlphaArg2, kDiffuse);
  }
  if (!m.stage1) {
    stage(1, kColorOp, kDisable);
    stage(1, kAlphaOp, kDisable);
    return;
  }
  stage(1, kTexCoordIndex, 1);
  stage(1, kColorArg1, m.arg1);
  stage(1, kColorOp, m.op1);
  stage(1, kColorArg2, kCurrent);
  stage(1, kAlphaArg1, kTexture);
  stage(1, kAlphaOp, kSelectArg1);
}

std::vector<SceneCall> DrawScene(SceneLists& lists, const SceneSettings& settings,
                                 BoundTextures& bound) {
  Flush f{lists, bound, {}};
  f.StageState(0, kMipFilter, 3);
  f.StageState(0, kColorOp, kModulate);
  f.StageState(0, kAlphaOp, kModulate);
  f.StageState(1, kAlphaOp, kDisable);
  f.RenderState(kFogEnable, settings.fog ? 1 : 0);
  f.RenderState(kAlphaRef, 0xcf);
  f.RenderState(kAlphaTestEnable, 1);
  f.RenderState(kAlphaBlendEnable, 0);
  f.RenderState(kZEnable, 1);
  f.RenderState(kZWriteEnable, 1);
  f.DrawSorted(SceneList::kOpaque, false);
  f.UnbindStage1();
  // 0040f480: the opaque vertex-buffer ranges.
  f.RenderState(kAmbient, 0xffffffff);
  f.RenderState(kCullMode, 2);
  if (!settings.lightmaps) {
    f.DrawSorted(SceneList::kGround, false);
  } else if (!settings.two_stage) {
    f.DrawSorted(SceneList::kGround, false);
    f.StageState(0, kColorOp, kSelectArg1);
    f.StageState(0, kAlphaOp, kSelectArg1);
    f.RenderState(kAlphaBlendEnable, 1);
    f.RenderState(kAlphaRef, 1);
    f.RenderState(kSrcBlend, 2);
    f.RenderState(kDestBlend, 5);
    f.DrawGroundLightmaps();
    f.StageState(0, kColorOp, kModulate);
    f.StageState(0, kAlphaOp, kModulate);
  } else {
    f.StageState(1, kColorOp, kModulateAlphaAddColor);
    f.DrawGroundTwoStages();
    // 0040f580: the ground's two-texture vertex-buffer ranges.
    f.StageState(1, kColorOp, kDisable);
  }
  f.RenderState(kAmbient, 0);
  f.RenderState(kCullMode, 1);
  // 004101f0: the list at +0x174.
  f.RenderState(kAlphaRef, 1);
  f.RenderState(kAlphaBlendEnable, 1);
  f.RenderState(kSrcBlend, 5);
  f.RenderState(kDestBlend, 6);
  f.DrawSorted(SceneList::kAlphaDepthWrite, true);
  f.RenderState(kSrcBlend, 2);
  f.RenderState(kDestBlend, 2);
  f.DrawSorted(SceneList::kAdditiveDepthWrite, true);
  f.RenderState(kZWriteEnable, 0);
  f.RenderState(kSrcBlend, 5);
  f.RenderState(kDestBlend, 6);
  f.DrawSorted(SceneList::kAlpha, true);
  // 0040f700: the translucent vertex-buffer ranges.
  f.RenderState(kFogEnable, 0);
  f.RenderState(kSrcBlend, 2);
  f.RenderState(kDestBlend, 2);
  // 0040f800: the additive vertex-buffer ranges.
  f.DrawSorted(SceneList::kAdditive, true);
  f.DrawStageModes();
  f.RenderState(kZEnable, 0);
  f.RenderState(kSrcBlend, 5);
  f.RenderState(kDestBlend, 6);
  f.DrawSorted(SceneList::kAlphaNoDepth, true);
  f.RenderState(kSrcBlend, 2);
  f.RenderState(kDestBlend, 2);
  f.DrawSorted(SceneList::kAdditiveNoDepth, true);
  lists.Clear();
  f.RenderState(kSrcBlend, 5);
  f.RenderState(kDestBlend, 6);
  f.StageState(0, kMipFilter, 1);
  return std::move(f.out);
}

}  // namespace ragexe::render
