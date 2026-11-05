#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/crt_rand.h"
#include "render/actor.h"
#include "render/camera.h"
#include "render/device.h"
#include "render/fog.h"
#include "render/ground.h"
#include "render/model.h"
#include "render/scene.h"
#include "render/sprite_cache.h"
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
  // CTexture(256, 256, 0) itself (004165e0), not through CreateTexture, as a sprite page's
  // texture is made: ARGB 1555, its uv scale 1.
  std::uint32_t Construct();

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
  // A primitive an object makes and keeps, such as an effect's RenderQuads: above every node
  // made so far.
  std::uint32_t NewOwned() { return grown_++; }
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
  // The renderer made: its sprite scales for the projection's size (SetSize), and the spare
  // sprite page textures its constructor makes (0040deb0), the first textures of all.
  explicit WorldFrameState(const res::DataFiles& files);
  WorldFrameState(const WorldFrameState&) = delete;
  WorldFrameState& operator=(const WorldFrameState&) = delete;

  Projection projection = SetSize(800, 600);
  Camera camera;
  CameraControl control;
  ShakeSwing shake;
  core::CrtRand own_rand;
  core::CrtRand* rand = &own_rand;  // the client's one CRT rand, which the effects share
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
  std::uint32_t water = 0;                    // the texture the water's last step asked for
  SpriteScales sprite_scales;                 // CView::Step's, each frame
  SpriteTextureCache sprites;                 // +0x194
  FogState fog;                               // 00625ea0 to 00625eb4
  bool vertex_fog = false;                    // +0x4c: AddPrimitive writes each vertex's fog
  bool setup_projection = false;              // +0x48: the next DrawScene sets up the projection
};

// SceneRenderer::SetFogEnabled (00411010) on a hardware device with vertex fog, as
// CGameMode::OnInit's 00516570 calls it (docs/render/fog.md#the-maps-fog): the fog linear
// with range fog, the device's vertex fog (+0x4c), the fog on or off (+0x44), and the
// projection to set up at the next DrawScene (+0x48).
void SetFogEnabled(WorldFrameState& state, bool enabled);

// A frame's scene lists and the primitives in them, as SceneRenderer::AddPrimitive (00412550)
// fills them: each primitive's vertices kept for the draws, with its texture's uv scale, and
// with the device's vertex fog each vertex's fog (ComputeVertexFog 0040c2e0).
class FrameLists {
 public:
  explicit FrameLists(WorldFrameState& state) : state_(state) {}

  // A primitive of s's id, textures, blends, stage mode and type.
  void Add(ScenePrimitive s, std::vector<ScreenVertex> vertices,
           std::vector<std::array<float, 2>> second, std::uint32_t flags);
  // A RenderQuad from the pool (NewQuad), a triangle strip, as SceneRenderer::AddSpriteQuad
  // (0040e980) takes one.
  void AddQuad(ScenePrimitive s, std::span<const ScreenVertex> vertices, std::uint32_t flags);
  // A RenderPrimitive from the pool (NewTriangleList), a triangle list.
  void AddTriangleList(ScenePrimitive s, std::vector<ScreenVertex> vertices, std::uint32_t flags);

  WorldFrameState& state() { return state_; }
  SceneLists& lists() { return lists_; }
  std::map<std::uint32_t, FramePrimitive>& primitives() { return primitives_; }

 private:
  WorldFrameState& state_;
  SceneLists lists_;
  std::map<std::uint32_t, FramePrimitive> primitives_;
};

// The image a sprite quad draws: an 8-bit one by its image and palette, as the sprite texture
// cache keeps them, or one with a texture of its own (a 32-bit image's, +8 of its record) and
// the corners it spans there, 0 to its uv scale.
struct SpriteImageRef {
  SpriteTextureCache::Key image = 0;
  SpriteTextureCache::Key palette = 0;
  std::int16_t width = 0;
  std::int16_t height = 0;
  std::uint32_t texture = 0;
  SpriteTextureRect rect;
};

// SceneRenderer::AddSpriteQuad (0040e980) at time now: the image's own texture and corners, or
// an 8-bit image's from the sprite texture cache (FindCachedSprite, else CacheSpriteImage with
// a page texture Construct makes once the spares run out); then the quad from the pool into
// the lists (SpriteQuadVertices). The slot CacheSpriteImage drew the image into, if it did.
std::optional<SpriteSlot> AddSpriteQuad(FrameLists& lists, const SpriteQuad& quad,
                                        const SpriteImageRef& image, std::uint32_t now);

// What the world's objects add to a frame (the list at CWorld+0xc, each through its slot 3).
using WorldObjectsDraw = std::function<void(FrameLists&)>;

// A world entered with the player standing at (x, z) on the attribute map's ground: the
// world's textures made, the camera reset and placed (PlaceCamera, indoors when indoor), the
// ground's and the models' draw settings at the projection's viewport, with the ground tint that
// CGameMode::OnInit sets and g_WorldColorFade at its constructor's values but the world's
// ambient colour, which C3dWorldRes::Load copies to the water's multipliers (00436918).
void EnterWorld(const WorldScene& scene, float x, float z, bool indoor, WorldFrameState& state);

// What a world step of CGameMode::OnFrame does after CWorld::UpdateGameObjects, at time now: the
// water's step (C3dGround15::StepWater, its texture asked for) and the view's (CView::Update, with
// CView::Step's sprite scales).
void StepWorld(const WorldScene& scene, WorldFrameState& state, std::uint32_t now);

// The rest of a frame of OnFrame: CView::DrawWorld (004cf4d0), the visible scene graph's models
// and cells and the cell cursor into the scene lists, then the world's objects (objects), and
// DrawScene, which first sets up the projection when SetFogEnabled asked. Then the frame counted,
// as Present counts it, and the pools rewound. The calls DrawScene makes, and its draws.
std::vector<SceneCall> DrawWorld(WorldScene& scene, WorldFrameState& state,
                                 std::vector<FrameDraw>& draws,
                                 const WorldObjectsDraw& objects = {});

// One frame of one world step, with no objects to update: StepWorld, then DrawWorld.
std::vector<SceneCall> DrawWorldFrame(WorldScene& scene, WorldFrameState& state, std::uint32_t now,
                                      std::vector<FrameDraw>& draws,
                                      const WorldObjectsDraw& objects = {});

}  // namespace ragexe::render
