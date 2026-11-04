#include "render/world_frame.h"

#include <bit>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

#include "core/x87.h"
#include "render/scene_graph.h"
#include "res/bitmap.h"
#include "res/gat.h"

namespace ragexe::render {

namespace {

constexpr std::uint32_t kFvfTwoCoords = 0x2c4;
constexpr std::uint32_t kTriangleStrip = 5;
constexpr std::uint32_t kTriangleList = 4;
constexpr std::uint32_t kSrcAlpha = 5;
constexpr std::uint32_t kInvSrcAlpha = 6;
constexpr const char* kCursorTexture = "grid.tga";

}  // namespace

std::uint32_t FrameTextures::Get(const std::string& name) {
  if (const std::uint32_t id = Find(name); id != 0) return id;
  res::Bitmap image;
  std::string ignored;
  const auto bytes = files_.Load("data\\texture\\" + name);
  if (bytes && res::DecodeBitmap(name, *bytes, image, &ignored)) {
    return Make(name, static_cast<std::uint32_t>(image.width),
                static_cast<std::uint32_t>(image.height), image.has_alpha);
  }
  return Make(name, 1, 1, false);
}

std::uint32_t FrameTextures::Find(const std::string& name) const {
  const auto it = by_name_.find(name);
  return it == by_name_.end() ? 0 : it->second;
}

std::uint32_t FrameTextures::Create(std::uint32_t width, std::uint32_t height, bool alpha) {
  return Make({}, width, height, alpha);
}

std::uint32_t FrameTextures::Construct() {
  textures_.emplace_back();
  return static_cast<std::uint32_t>(textures_.size());
}

WorldFrameState::WorldFrameState(const res::DataFiles& files) : textures(files) {
  SetSpriteScalesForSize(projection, sprite_scales);
  sprites.MakeSpareTextures([this] { return textures.Construct(); });
}

// CreateTexture: the size divided by g_textureDivisor, the texture fitted to the device
// (CTexture::CreateSurface), then the whole image through UpdatePixels, which sets the uv scale.
std::uint32_t FrameTextures::Make(std::string name, std::uint32_t width, std::uint32_t height,
                                  bool alpha) {
  FrameTexture t;
  t.format = alpha ? TextureFormat::kArgb4444 : TextureFormat::kArgb1555;
  const std::optional<TextureSize> fitted =
      FitTextureSize(width / divisor_, height / divisor_, limits_);
  if (fitted) t.uv = ImageUvScale(fitted->width, fitted->height, width, height);
  t.name = std::move(name);
  textures_.push_back(std::move(t));
  const auto id = static_cast<std::uint32_t>(textures_.size());
  if (!textures_.back().name.empty()) by_name_.emplace(textures_.back().name, id);
  return id;
}

std::vector<std::uint32_t> MakeWorldTextures(const WorldScene& scene, FrameTextures& textures) {
  // LightmapAtlas::Build: each page by C3dDevice::CreateTexture(256, 256, ..., 1, 0).
  std::vector<std::uint32_t> pages;
  pages.reserve(scene.mesh.atlas.pages.size());
  for (std::size_t p = 0; p < scene.mesh.atlas.pages.size(); ++p)
    pages.push_back(textures.Create(LightmapAtlas::kPageSize, LightmapAtlas::kPageSize, true));
  for (const GroundCellMesh& cell : scene.mesh.cells) {
    for (const std::int32_t surface : cell.surfaces) {
      if (surface < 0) continue;
      const GroundSurfaceMesh& s = scene.mesh.surfaces[static_cast<std::size_t>(surface)];
      textures.Get(scene.ground.textures[static_cast<std::size_t>(s.texture)]);
    }
  }
  std::set<const res::Model*> loaded;
  for (const ModelInstance& instance : scene.models) {
    if (!loaded.insert(instance.model).second) continue;
    for (const res::ModelNode& node : instance.model->nodes) {
      for (const std::int32_t t : node.textures)
        textures.Get(instance.model->textures[static_cast<std::size_t>(t)]);
    }
  }
  return pages;
}

std::vector<std::uint32_t> PrimitivePools::Ids(std::uint32_t first, std::uint32_t count) {
  std::vector<std::uint32_t> ids(count);
  for (std::uint32_t k = 0; k < count; ++k) ids[k] = first + k;
  return ids;
}

std::uint32_t PrimitivePools::Take(std::vector<std::uint32_t>& pool, std::size_t& next) {
  if (next == pool.size()) pool.push_back(grown_++);
  return pool[next++];
}

std::vector<FrameDraw> FrameDraws(const std::vector<SceneCall>& calls,
                                  std::map<std::uint32_t, FramePrimitive>& primitives) {
  using F = core::X87<float>;
  std::vector<FrameDraw> out;
  for (const SceneCall& call : calls) {
    if (call.kind != SceneCall::Kind::kDraw) continue;
    FramePrimitive& p = primitives.at(call.value);
    if (p.uv) {
      for (ScreenVertex& v : p.vertices) {
        v.u = static_cast<float>(F(p.uv->u) * F(v.u));
        v.v = static_cast<float>(F(p.uv->v) * F(v.v));
      }
    }
    if (call.lightmap_coords) {
      for (std::size_t k = 0; k < p.vertices.size(); ++k) {
        p.vertices[k].u = p.second[k][0];
        p.vertices[k].v = p.second[k][1];
      }
    }
    FrameDraw d{call.state, call.fvf, static_cast<std::uint32_t>(p.vertices.size()), {}};
    for (std::size_t k = 0; k < p.vertices.size(); ++k) {
      const ScreenVertex& v = p.vertices[k];
      for (const float f : {v.x, v.y, v.z, v.rhw})
        d.words.push_back(std::bit_cast<std::uint32_t>(f));
      d.words.push_back(v.diffuse);
      d.words.push_back(v.specular);
      d.words.push_back(std::bit_cast<std::uint32_t>(v.u));
      d.words.push_back(std::bit_cast<std::uint32_t>(v.v));
      if (call.fvf == kFvfTwoCoords) {
        d.words.push_back(std::bit_cast<std::uint32_t>(p.second[k][0]));
        d.words.push_back(std::bit_cast<std::uint32_t>(p.second[k][1]));
      }
    }
    out.push_back(std::move(d));
  }
  return out;
}

void SetFogEnabled(WorldFrameState& state, bool enabled) {
  state.vertex_fog = true;
  state.scene.fog = enabled;
  state.setup_projection = true;
  state.fog.mode = 3;  // D3DFOG_LINEAR
  state.fog.range = 1;
}

void FrameLists::Add(ScenePrimitive s, std::vector<ScreenVertex> vertices,
                     std::vector<std::array<float, 2>> second, std::uint32_t flags) {
  s.vertex_count = static_cast<std::uint32_t>(vertices.size());
  s.rhw0 = vertices.empty() ? 0.0f : vertices[0].rhw;
  s.rhw1 = vertices.size() > 1 ? vertices[1].rhw : 0.0f;
  if (state_.vertex_fog) {
    for (ScreenVertex& v : vertices) v.specular = VertexFog(v.rhw, state_.fog);
  }
  std::optional<UvScale> uv;
  if (s.texture != 0) uv = state_.textures.at(s.texture).uv;
  primitives_[s.id] = {uv, std::move(vertices), std::move(second)};
  lists_.Add(s, flags);
}

void FrameLists::AddQuad(ScenePrimitive s, std::span<const ScreenVertex> vertices,
                         std::uint32_t flags) {
  s.id = state_.pools.NewQuad();
  s.type = kTriangleStrip;
  Add(s, {vertices.begin(), vertices.end()}, {}, flags);
}

void FrameLists::AddTriangleList(ScenePrimitive s, std::vector<ScreenVertex> vertices,
                                 std::uint32_t flags) {
  s.id = state_.pools.NewTriangleList();
  s.type = kTriangleList;
  Add(s, std::move(vertices), {}, flags);
}

std::optional<SpriteSlot> AddSpriteQuad(FrameLists& lists, const SpriteQuad& quad,
                                        const SpriteImageRef& image, std::uint32_t now) {
  WorldFrameState& state = lists.state();
  ScenePrimitive s;
  SpriteTextureRect rect = image.rect;
  std::optional<SpriteSlot> drawn;
  if (image.texture != 0) {
    s.texture = image.texture;
  } else if (const std::optional<CachedSprite> found =
                 state.sprites.Find(image.image, image.palette, image.width, image.height, now)) {
    s.texture = found->texture;
    rect = found->rect;
  } else {
    drawn = state.sprites.Cache(image.image, image.palette, image.width, image.height, now,
                                [&] { return state.textures.Construct(); });
    if (drawn) {
      s.texture = drawn->sprite.texture;
      rect = drawn->sprite.rect;
    }
  }
  lists.AddQuad(s, SpriteQuadVertices(quad, rect), quad.flags);
  return drawn;
}

void EnterWorld(const WorldScene& scene, float x, float z, bool indoor, WorldFrameState& state) {
  state.lightmap_pages = MakeWorldTextures(scene, state.textures);
  state.player = {x, res::AttrHeight(scene.attr, x, z), z};
  ResetCamera(state.camera);
  PlaceCamera(state.camera, state.control, state.player, indoor);
  state.ground.viewport = GroundViewportOf(state.projection);
  state.ground.water = WaterOfWorld(scene.world);
  state.ground.fade.water = scene.world.ambient;
  state.models.clear();
  for (const ModelInstance& instance : scene.models) {
    ModelDrawSettings m;
    m.viewport = state.ground.viewport;
    m.fade = state.ground.fade;
    m.ground_tint = true;
    for (const std::string& name : instance.model->textures) {
      const std::uint32_t id = state.textures.Find(name);
      m.texture_alpha.push_back(id != 0 &&
                                state.textures.at(id).format == TextureFormat::kArgb4444);
    }
    state.models.push_back(std::move(m));
  }
}

std::vector<SceneCall> DrawWorldFrame(WorldScene& scene, WorldFrameState& state, std::uint32_t now,
                                      std::vector<FrameDraw>& draws,
                                      const WorldObjectsDraw& objects) {
  const std::uint32_t water = state.textures.Get(StepWater(state.ground.water));
  state.camera.target_at = state.player;
  StepCamera(state.camera, false);
  StepSpriteScales(state.camera.pitch, state.camera.distance, state.projection,
                   state.sprite_scales);
  ApplyShake(state.camera, state.shake, now, *state.rand);
  BuildCameraView(state.camera, &scene.attr);
  state.shake.step_pending = true;
  state.ground.frame = state.frame;

  const Frustum frustum =
      MakeFrustum(state.projection.slope_x, state.projection.slope_y, state.camera.view);
  const VisibleNodes visible = CollectVisibleNodes(frustum, scene.graph);
  std::vector<VisiblePrimitive> visible_primitives;
  DrawVisible(visible, scene.graph, frustum, state.camera.view, scene.mesh, state.ground,
              scene.models, state.models, visible_primitives);

  FrameLists lists(state);
  for (const VisiblePrimitive& v : visible_primitives) {
    ScenePrimitive s;
    if (v.model < 0) {
      const GroundPrimitive& p = v.ground;
      s.id = state.pools.NewQuad();
      s.type = kTriangleStrip;
      s.texture =
          p.texture >= 0
              ? state.textures.Get(scene.ground.textures[static_cast<std::size_t>(p.texture)])
              : water;
      if (p.lightmap_page >= 0)
        s.lightmap = state.lightmap_pages[static_cast<std::size_t>(p.lightmap_page)];
      std::vector<std::array<float, 2>> second;
      if (p.texture >= 0) {
        for (std::size_t k = 0; k < 4; ++k)
          second.push_back({p.lightmap_uv[k * 2], p.lightmap_uv[k * 2 + 1]});
      }
      lists.Add(s, {p.vertices.begin(), p.vertices.end()}, std::move(second), p.flags);
      continue;
    }
    const ModelPrimitive& p = v.face;
    const ModelInstance& instance = scene.models[static_cast<std::size_t>(v.model)];
    s.texture = state.textures.Get(instance.model->textures[static_cast<std::size_t>(p.texture)]);
    lists.AddTriangleList(s, p.vertices, p.flags);
  }
  std::array<ScreenVertex, 4> quad{};
  if (!state.cursor.hidden &&
      DrawCellCursor(scene.mesh, scene.attr, state.camera.view, state.ground.viewport,
                     state.cursor.x, state.cursor.z, state.cursor.color, quad)) {
    ScenePrimitive s;
    s.texture = state.textures.Get(kCursorTexture);
    s.src_blend = kSrcAlpha;
    s.dest_blend = kInvSrcAlpha;
    lists.AddQuad(s, quad, scene_flags::kTranslucent);
  }
  if (objects) objects(lists);

  std::vector<SceneCall> projection;
  if (state.setup_projection) {
    projection = SetupFog(state.fog, state.scene.fog, true).calls;
    state.setup_projection = false;
  }
  std::vector<SceneCall> calls = DrawScene(lists.lists(), state.scene, state.bound, projection);
  draws = FrameDraws(calls, lists.primitives());
  ++state.frame;
  state.pools.Rewind();
  return calls;
}

}  // namespace ragexe::render
