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

void EnterWorld(const WorldScene& scene, float x, float z, bool indoor, WorldFrameState& state) {
  state.lightmap_pages = MakeWorldTextures(scene, state.textures);
  state.player = {x, res::AttrHeight(scene.attr, x, z), z};
  ResetCamera(state.camera);
  PlaceCamera(state.camera, state.control, state.player, indoor);
  state.ground.viewport = GroundViewportOf(state.projection);
  state.ground.water = WaterOfWorld(scene.world);
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
                                      std::vector<FrameDraw>& draws) {
  const std::uint32_t water = state.textures.Get(StepWater(state.ground.water));
  UpdateCamera(state.camera, state.player, false, &scene.attr, state.shake, now, state.rand);
  state.shake.step_pending = true;
  state.ground.frame = state.frame;

  const Frustum frustum =
      MakeFrustum(state.projection.slope_x, state.projection.slope_y, state.camera.view);
  const VisibleNodes visible = CollectVisibleNodes(frustum, scene.graph);
  std::vector<VisiblePrimitive> visible_primitives;
  DrawVisible(visible, scene.graph, frustum, state.camera.view, scene.mesh, state.ground,
              scene.models, state.models, visible_primitives);

  SceneLists lists;
  std::map<std::uint32_t, FramePrimitive> primitives;
  const auto add = [&](std::uint32_t id, std::uint32_t texture, std::uint32_t lightmap,
                       std::uint32_t type, std::vector<ScreenVertex> vertices,
                       std::vector<std::array<float, 2>> second) {
    ScenePrimitive s;
    s.id = id;
    s.texture = texture;
    s.lightmap = lightmap;
    s.type = type;
    s.vertex_count = static_cast<std::uint32_t>(vertices.size());
    s.rhw0 = vertices[0].rhw;
    s.rhw1 = vertices.size() > 1 ? vertices[1].rhw : 0.0f;
    std::optional<UvScale> uv;
    if (texture != 0) uv = state.textures.at(texture).uv;
    primitives[id] = {uv, std::move(vertices), std::move(second)};
    return s;
  };
  for (const VisiblePrimitive& v : visible_primitives) {
    if (v.model < 0) {
      const GroundPrimitive& p = v.ground;
      const std::uint32_t texture =
          p.texture >= 0
              ? state.textures.Get(scene.ground.textures[static_cast<std::size_t>(p.texture)])
              : water;
      const std::uint32_t lightmap =
          p.lightmap_page >= 0 ? state.lightmap_pages[static_cast<std::size_t>(p.lightmap_page)]
                               : 0;
      std::vector<std::array<float, 2>> second;
      if (p.texture >= 0) {
        for (std::size_t k = 0; k < 4; ++k)
          second.push_back({p.lightmap_uv[k * 2], p.lightmap_uv[k * 2 + 1]});
      }
      lists.Add(add(state.pools.NewQuad(), texture, lightmap, kTriangleStrip,
                    {p.vertices.begin(), p.vertices.end()}, std::move(second)),
                p.flags);
      continue;
    }
    const ModelPrimitive& p = v.face;
    const ModelInstance& instance = scene.models[static_cast<std::size_t>(v.model)];
    const std::uint32_t texture =
        state.textures.Get(instance.model->textures[static_cast<std::size_t>(p.texture)]);
    lists.Add(add(state.pools.NewTriangleList(), texture, 0, kTriangleList, p.vertices, {}),
              p.flags);
  }
  std::array<ScreenVertex, 4> quad{};
  if (!state.cursor.hidden &&
      DrawCellCursor(scene.mesh, scene.attr, state.camera.view, state.ground.viewport,
                     state.cursor.x, state.cursor.z, state.cursor.color, quad)) {
    const std::uint32_t texture = state.textures.Get(kCursorTexture);
    ScenePrimitive s =
        add(state.pools.NewQuad(), texture, 0, kTriangleStrip, {quad.begin(), quad.end()}, {});
    s.src_blend = kSrcAlpha;
    s.dest_blend = kInvSrcAlpha;
    lists.Add(s, scene_flags::kTranslucent);
  }

  const std::vector<SceneCall> calls = DrawScene(lists, state.scene, state.bound);
  draws = FrameDraws(calls, primitives);
  ++state.frame;
  state.pools.Rewind();
  return calls;
}

}  // namespace ragexe::render
