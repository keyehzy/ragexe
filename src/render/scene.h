#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ragexe::render {

// What the scene lists use of a RenderPrimitive (0x24 bytes; docs/render/scene.md).
// Textures and primitives are told apart by ids ordered as the original's pointers are,
// since the lists sort by them.
struct ScenePrimitive {
  std::uint32_t id = 0;            // the primitive's address
  std::uint32_t texture = 0;       // +0xc, 0 for none
  std::uint32_t lightmap = 0;      // +0x10, the second texture
  std::uint32_t src_blend = 0;     // +0x14, a D3DBLEND
  std::uint32_t dest_blend = 0;    // +0x18
  std::uint32_t mode = 0;          // +0x1c, the texture stage mode
  std::uint32_t type = 4;          // +0x20, a D3DPRIMITIVETYPE: TRIANGLELIST unless set
  std::uint32_t vertex_count = 0;  // +8: a primitive of none is not drawn
  float rhw0 = 0.0f;               // the rhw of the first two vertices
  float rhw1 = 0.0f;
};

// AddPrimitive's flags (00412550).
namespace scene_flags {
inline constexpr std::uint32_t kTranslucent = 0x1;
inline constexpr std::uint32_t kAdditive = 0x2;  // ONE / ONE instead of SRCALPHA / INVSRCALPHA
inline constexpr std::uint32_t kStageMode = 0x4;
inline constexpr std::uint32_t kHeld8 = 0x8;
inline constexpr std::uint32_t kDepthWrite = 0x100;
inline constexpr std::uint32_t kNoDepthTest = 0x200;
inline constexpr std::uint32_t kGround = 0x400;
inline constexpr std::uint32_t kHeld800 = 0x800;
}  // namespace scene_flags

// SceneRenderer's primitive lists, by the vector each one is.
enum class SceneList : std::uint8_t {
  kOpaque,              // +0x94
  kGround,              // +0xa4
  kHeld800,             // +0xb4, which DrawScene never draws
  kHeld8,               // +0x164, likewise
  kAlphaDepthWrite,     // +0xc4
  kAlpha,               // +0xd4
  kAlphaNoDepth,        // +0xe4
  kAdditiveDepthWrite,  // +0xf4
  kAdditive,            // +0x104
  kAdditiveNoDepth,     // +0x114
  kStageModes,          // +0x184, drawn in the order added
  kCount,
};

// The list AddPrimitive puts a primitive with these flags in.
SceneList SelectSceneList(std::uint32_t flags);

// The key a translucent list sorts by: the first vertex's rhw for kAlpha, otherwise the
// midpoint of the first two, (rhw0 - rhw1) * 0.5 + rhw1 in single precision.
float SceneDepthKey(SceneList list, float rhw0, float rhw1);

struct SceneEntry {
  float key = 0.0f;  // the depth key, in the translucent lists
  std::uint32_t primitive = 0;
};

// The lists of one frame (AddPrimitive 00412550, cleared by 0040ecf0).
class SceneLists {
 public:
  void Add(const ScenePrimitive& primitive, std::uint32_t flags);
  void Clear();

  const std::vector<ScenePrimitive>& primitives() const { return primitives_; }
  const std::vector<SceneEntry>& list(SceneList l) const {
    return lists_[static_cast<std::size_t>(l)];
  }
  std::vector<SceneEntry>& list(SceneList l) { return lists_[static_cast<std::size_t>(l)]; }

 private:
  std::vector<ScenePrimitive> primitives_;
  std::array<std::vector<SceneEntry>, static_cast<std::size_t>(SceneList::kCount)> lists_;
};

// A call DrawScene makes on the device.
struct SceneCall {
  enum class Kind : std::uint8_t { kRenderState, kStageState, kTexture, kDraw };
  Kind kind = Kind::kRenderState;
  std::int32_t stage = -1;  // the texture stage, for kStageState and kTexture
  std::uint32_t state = 0;  // the state; for kDraw, the D3DPRIMITIVETYPE
  std::uint32_t value = 0;  // its value; the texture id; for kDraw, the primitive id
  std::uint32_t fvf = 0;    // for kDraw: 0x1c4, or 0x2c4 with the lightmap's coordinates
  // For kDraw: DrawWithTexCoordArray (0040c590), the lightmap's coordinates over the first.
  bool lightmap_coords = false;

  friend bool operator==(const SceneCall&, const SceneCall&) = default;
};

// The textures last set on stages 0 and 1 (+0x50, +0x54). They carry over from one
// draw to the next, so a texture is set only when it changes.
struct BoundTextures {
  std::uint32_t stage0 = 0;
  std::uint32_t stage1 = 0;
};

struct SceneSettings {
  bool fog = false;        // the renderer's +0x44
  bool lightmaps = false;  // ISLIGHTMAP (0x61ec60)
  bool two_stage = false;  // the device's two-stage test (0x6053f0)
};

// SetStageMode (004112d0): the texture stage states of a primitive mode
// (docs/render/texture-stages.md). Modes above 15 set nothing.
void AppendStageModeCalls(std::uint32_t mode, std::vector<SceneCall>& out);

// DrawScene (0040efd0) after the projection (00411090): every pass in order, with the
// states around it, then the lists emptied. The primitive lists the port does not hold
// yet, the vertex-buffer ranges and the list at +0x174, are drawn empty.
std::vector<SceneCall> DrawScene(SceneLists& lists, const SceneSettings& settings,
                                 BoundTextures& bound);

}  // namespace ragexe::render
