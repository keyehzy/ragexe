#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/crt_rand.h"
#include "render/camera.h"
#include "render/device.h"
#include "render/ground.h"
#include "render/model.h"
#include "render/scene.h"
#include "render/texture.h"
#include "render/world_scene.h"
#include "res/data_files.h"

namespace ragexe::render {

// A texture as C3dDevice::CreateTexture (00405350) makes it for an image
// (docs/render/frame.md#textures).
struct FrameTexture {
  std::string name;  // as TextureCache was asked for it, under data\texture\; empty for none
  TextureFormat format = TextureFormat::kArgb1555;  // ARGB 4444 for an image with alpha
  UvScale uv;                                       // +0x18, +0x1c
};

// TextureCache (00625e88) and C3dDevice::CreateTexture: the textures in the order they are made,
// ids from 1 in that order. The scene lists sort by the CTextures' addresses, which follow that
// order on a heap that only grows.
class FrameTextures {
 public:
  explicit FrameTextures(const res::DataFiles& files, TextureLimits limits = {},
                         std::uint32_t divisor = 1)
      : files_(files), limits_(limits), divisor_(divisor) {}

  // TextureCache::GetTexture (0040cc00): the texture made for a name, made on the first ask
  // from data\texture\ and the name (CreateTextureFromResource 0040cff0), or 1 × 1 when the
  // image does not load.
  std::uint32_t Get(const std::string& name);
  // The texture made for a name, or 0 when none was asked for.
  std::uint32_t Find(const std::string& name) const;
  // C3dDevice::CreateTexture of an image of this size that no name holds.
  std::uint32_t Create(std::uint32_t width, std::uint32_t height, bool alpha);

  const FrameTexture& at(std::uint32_t id) const { return textures_[id - 1]; }
  std::size_t size() const { return textures_.size(); }

 private:
  std::uint32_t Make(std::string name, std::uint32_t width, std::uint32_t height, bool alpha);

  const res::DataFiles& files_;
  TextureLimits limits_;
  std::uint32_t divisor_;  // g_textureDivisor (0061ec50), from TEXTUREMODE
  std::vector<FrameTexture> textures_;
  std::map<std::string, std::uint32_t> by_name_;
};

// The textures LoadWorld makes, in order: the lightmap pages (LightmapAtlas::Build), each
// ground texture as C3dGround15::Build first places a surface of it, then each model file as
// its first instance loads it, its nodes' textures in file order (C3dModelRes). The pages' ids.
std::vector<std::uint32_t> MakeWorldTextures(const WorldScene& scene, FrameTextures& textures);

// SceneRenderer's primitive pools (docs/render/frame.md#the-pools): the triangle lists'
// (+0x260, 1024 to start, 00412aa0) and the quads' (+0x270, 5120 to start, NewQuad 00412b40),
// each taken in turn through a frame and rewound after it (00412e70); a pool taken past its
// end grows by one. The ids order as the nodes' addresses: the triangle lists' first, the
// quads' after, and the nodes the pools grew by after both, in the order they were made.
class PrimitivePools {
 public:
  std::uint32_t NewTriangleList() { return Take(lists_, lists_next_); }
  std::uint32_t NewQuad() { return Take(quads_, quads_next_); }
  void Rewind() { lists_next_ = quads_next_ = 0; }

 private:
  static constexpr std::uint32_t kInitialLists = 0x400;
  static constexpr std::uint32_t kInitialQuads = 0x1400;
  // After the second quad pool (+0x280) that the world does not take from.
  static constexpr std::uint32_t kGrown = kInitialLists + 2 * kInitialQuads;

  std::uint32_t Take(std::vector<std::uint32_t>& pool, std::size_t& next);
  static std::vector<std::uint32_t> Ids(std::uint32_t first, std::uint32_t count);

  std::vector<std::uint32_t> lists_ = Ids(0, kInitialLists);
  std::vector<std::uint32_t> quads_ = Ids(kInitialLists, kInitialQuads);
  std::size_t lists_next_ = 0;
  std::size_t quads_next_ = 0;
  std::uint32_t grown_ = kGrown;
};

// A primitive's vertices as the renderer keeps them through the frame: the 0x1c4 vertices and,
// for the ground's, the lightmap's coordinates (+4).
struct FramePrimitive {
  std::optional<UvScale> uv;  // its texture's (+0xc), which the draws multiply by; none without
  std::vector<ScreenVertex> vertices;
  std::vector<std::array<float, 2>> second;
};

// A DrawPrimitive call: its type, FVF and the vertices' dwords as sent.
struct FrameDraw {
  std::uint32_t type = 0;
  std::uint32_t fvf = 0;
  std::uint32_t count = 0;
  std::vector<std::uint32_t> words;
};

// The vertices DrawScene's draws send (docs/render/scene.md#renderprimitive). Each draw first
// multiplies the primitive's texture coordinates by its texture's uv scale, in place
// (ScaleTexCoordsByTexture 0040c280). DrawWithTexCoordArray (0040c590) then writes the second
// coordinates over the first, in place; DrawTwoTexCoords (0040c3e0) sends both.
std::vector<FrameDraw> FrameDraws(const std::vector<SceneCall>& calls,
                                  std::map<std::uint32_t, FramePrimitive>& primitives);

// What CGameMode keeps of a world between frames, and the renderer's state that lasts.
struct WorldFrameState {
  explicit WorldFrameState(const res::DataFiles& files) : textures(files) {}

  Projection projection = SetSize(800, 600);
  Camera camera;
  CameraControl control;
  ShakeSwing shake;
  core::CrtRand rand;
  Vec3 player{};
  GroundDrawSettings ground;
  std::vector<ModelDrawSettings> models;  // per instance
  CellCursor cursor;
  SceneSettings scene{false, true, true};
  BoundTextures bound;
  PrimitivePools pools;
  FrameTextures textures;
  std::vector<std::uint32_t> lightmap_pages;  // the atlas pages' textures
  std::uint32_t frame = 1;                    // the renderer's frame count (+0x34)
};

// A world entered with the player standing at (x, z) on the attribute map's ground: the
// world's textures made, the camera reset and placed (PlaceCamera, indoors when indoor), the
// ground's and the models' draw settings at an 800 × 600 viewport, with the ground tint that
// CGameMode::OnInit sets and g_WorldColorFade at its constructor's values.
void EnterWorld(const WorldScene& scene, float x, float z, bool indoor, WorldFrameState& state);

// One frame as CGameMode::OnFrame steps and draws it at time now: the water's step and the
// view's (CView::Update), then CView::DrawWorld (004cf4d0): the visible scene graph's models
// and cells and the cell cursor into the scene lists, and DrawScene. Then the frame counted,
// as Present counts it, and the pools rewound. The calls DrawScene makes, and its draws.
std::vector<SceneCall> DrawWorldFrame(WorldScene& scene, WorldFrameState& state, std::uint32_t now,
                                      std::vector<FrameDraw>& draws);

}  // namespace ragexe::render
