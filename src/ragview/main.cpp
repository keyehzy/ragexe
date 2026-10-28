// ragview: a map's ground drawn as the client draws it (docs/render/gpu.md).
//
//   ragview [--install DIR] [--size W H] [--at X Z] [--yaw DEGREES] [--frames N]
//           [--screenshot FILE.png] MAP
//
// MAP is a .rsw name such as prontera. The ground is built and drawn by the ported
// C3dGround15, through the scene lists and DrawScene, and the calls are replayed on
// SDL_GPU. The camera is the client's: placed as on entering the map, following a player
// standing at world (X, Z) (the map's centre by default), turned to YAW if given.
// --screenshot renders one frame offscreen to a PNG and exits; otherwise a window opens,
// and Escape or --frames N closes it.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "render/camera.h"
#include "render/fixed_function.h"
#include "render/gpu_device.h"
#include "render/ground.h"
#include "render/model.h"
#include "render/scene.h"
#include "render/texture.h"
#include "res/bitmap.h"
#include "res/client_tables.h"
#include "res/data_files.h"
#include "res/gat.h"
#include "res/gnd.h"
#include "res/grf.h"
#include "res/rsm.h"
#include "res/rsw.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

using namespace ragexe;

constexpr std::uint32_t kLightmapIdBase = 0x10000;
constexpr std::uint32_t kModelTextureIdBase = 0x20000;

struct Options {
  std::filesystem::path install = "original/install";
  std::string map;
  int width = 800;
  int height = 600;
  float at_x = 0.0f;
  float at_z = 0.0f;
  bool turned = false;
  float yaw = 0.0f;
  int frames = 0;
  std::string screenshot;
};

// A whole decimal number, or fallback when the text is not one.
int ParseInt(const char* text, int fallback) {
  char* end = nullptr;
  const long v = std::strtol(text, &end, 10);
  return end != text && *end == '\0' ? static_cast<int>(v) : fallback;
}

int Usage() {
  std::fprintf(stderr,
               "usage: ragview [--install DIR] [--size W H] [--at X Z] [--yaw DEGREES]\n"
               "               [--frames N] [--screenshot FILE.png] MAP\n");
  return 2;
}

// A texture as TextureCache makes it: CreateTexture at the image's size in 1555, or 4444
// with alpha, then UpdatePixels with black opaque, widened back to 32 bits.
std::vector<std::uint32_t> ClientTexels(const res::Bitmap& image, std::uint32_t& width,
                                        std::uint32_t& height) {
  const auto fitted = render::FitTextureSize(static_cast<std::uint32_t>(image.width),
                                             static_cast<std::uint32_t>(image.height), {});
  width = fitted ? fitted->width : 1;
  height = fitted ? fitted->height : 1;
  const auto format =
      image.has_alpha ? render::TextureFormat::kArgb4444 : render::TextureFormat::kArgb1555;
  render::TexelImage texture(width, height);
  render::UpdateTexturePixels(texture, format, 0, 0, image.width, image.height, image.pixels, false,
                              0);
  std::vector<std::uint32_t> out(texture.texels.size());
  for (std::size_t i = 0; i < out.size(); ++i)
    out[i] = render::ExpandTexel(format, texture.texels[i]);
  return out;
}

bool Load(const res::DataFiles& files, const std::string& path, std::vector<std::uint8_t>& out) {
  auto data = files.Load(path);
  if (!data) return false;
  out = std::move(*data);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto next = [&](int n) { return i + n < argc; };
    if (a == "--install" && next(1)) {
      o.install = argv[++i];
    } else if (a == "--size" && next(2)) {
      o.width = ParseInt(argv[++i], 0);
      o.height = ParseInt(argv[++i], 0);
    } else if (a == "--at" && next(2)) {
      o.at_x = std::strtof(argv[++i], nullptr);
      o.at_z = std::strtof(argv[++i], nullptr);
    } else if (a == "--yaw" && next(1)) {
      o.turned = true;
      o.yaw = std::strtof(argv[++i], nullptr);
    } else if (a == "--frames" && next(1)) {
      o.frames = ParseInt(argv[++i], -1);
    } else if (a == "--screenshot" && next(1)) {
      o.screenshot = argv[++i];
    } else if (!a.starts_with("--") && o.map.empty()) {
      o.map = a;
    } else {
      return Usage();
    }
  }
  if (o.map.empty() || o.width <= 0 || o.height <= 0 || o.frames < 0) return Usage();
  if (o.map.ends_with(".rsw")) o.map.resize(o.map.size() - 4);

  std::string error;
  auto archive = res::GrfArchive::Open(o.install / "data.grf", &error);
  if (!archive) {
    std::fprintf(stderr, "ragview: %s\n", error.c_str());
    return 1;
  }
  const res::DataFiles files(o.install, std::move(archive));
  std::vector<std::uint8_t> bytes;
  res::World world;
  if (!Load(files, "data\\" + o.map + ".rsw", bytes) ||
      !res::DecodeWorld(bytes, true, world, &error)) {
    std::fprintf(stderr, "ragview: %s.rsw: %s\n", o.map.c_str(),
                 error.empty() ? "not found" : error.c_str());
    return 1;
  }
  res::Ground ground;
  if (!Load(files, "data\\" + world.ground, bytes) || !res::DecodeGround(bytes, ground, &error)) {
    std::fprintf(stderr, "ragview: %s: %s\n", world.ground.c_str(),
                 error.empty() ? "not found" : error.c_str());
    return 1;
  }
  res::AttrMap attr;
  if (!Load(files, "data\\" + world.attr, bytes) || !res::DecodeAttrMap(bytes, attr, &error)) {
    std::fprintf(stderr, "ragview: %s: %s\n", world.attr.c_str(),
                 error.empty() ? "not found" : error.c_str());
    return 1;
  }
  // ClientState's indoor check (00598ba0): the map's name in the sorted indoorRswTable.txt.
  std::vector<std::string> indoor_maps;
  const bool indoor = res::LoadFields(files, res::kIndoorRswTable, true, indoor_maps) &&
                      std::binary_search(indoor_maps.begin(), indoor_maps.end(), o.map + ".rsw");
  render::GroundMesh mesh;
  const render::GroundLight light{world.light_vector, world.diffuse, world.ambient};
  if (!render::BuildGround(ground, light, true, mesh, &error)) {
    std::fprintf(stderr, "ragview: %s: %s\n", world.ground.c_str(), error.c_str());
    return 1;
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "ragview: SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = nullptr;
  if (o.screenshot.empty()) {
    window = SDL_CreateWindow(("ragview " + o.map).c_str(), o.width, o.height, 0);
    if (window == nullptr) {
      std::fprintf(stderr, "ragview: SDL_CreateWindow: %s\n", SDL_GetError());
      return 1;
    }
  }
  auto device = render::RenderDevice::Create(window, &error);
  if (!device) {
    std::fprintf(stderr, "ragview: %s\n", error.c_str());
    return 1;
  }
  std::fprintf(stderr, "ragview: %s, %s (%d x %d cells, %s), on %s\n", o.map.c_str(),
               world.ground.c_str(), ground.width, ground.height, indoor ? "indoor" : "outdoor",
               device->driver());

  // The ground's textures (ids from 1, in the file's order) and the lightmap pages.
  for (std::size_t t = 0; t < ground.textures.size(); ++t) {
    res::Bitmap image;
    std::string ignored;
    std::uint32_t w = 1, h = 1;
    std::vector<std::uint32_t> texels{0};
    if (Load(files, "data\\texture\\" + ground.textures[t], bytes) &&
        res::DecodeBitmap(ground.textures[t], bytes, image, &ignored)) {
      texels = ClientTexels(image, w, h);
    }
    device->SetTexture(static_cast<std::uint32_t>(t + 1), w, h, texels);
  }
  for (std::size_t p = 0; p < mesh.atlas.pages.size(); ++p) {
    std::vector<std::uint32_t> page(mesh.atlas.pages[p].size());
    for (std::size_t i = 0; i < page.size(); ++i) {
      const auto t =
          render::ConvertTexel(render::TextureFormat::kArgb4444, mesh.atlas.pages[p][i], false);
      page[i] = render::ExpandTexel(render::TextureFormat::kArgb4444, t.value_or(0));
    }
    device->SetTexture(kLightmapIdBase + static_cast<std::uint32_t>(p), 256, 256, page);
  }

  // The player stands on the ground; the camera is placed as CGameMode places it.
  render::Vec3 player = {o.at_x, res::AttrHeight(attr, o.at_x, o.at_z), o.at_z};
  render::Camera camera;
  render::ResetCamera(camera);
  render::CameraControl control;
  render::PlaceCamera(camera, control, player, indoor);
  if (o.turned) {
    camera.yaw = o.yaw;
    camera.target_yaw = o.yaw;
  }

  // The world's models, as CWorld::LoadWorld places them, lit by the map's light and the
  // ground's colours. Each texture is uploaded once, by name.
  std::map<std::string, std::unique_ptr<res::Model>> model_files;
  std::map<std::string, std::pair<std::uint32_t, bool>> model_textures;  // id, ARGB 4444
  std::vector<render::ModelInstance> models;
  std::vector<std::vector<std::uint32_t>> model_texture_ids;  // per instance, by model texture
  std::vector<render::ModelDrawSettings> model_settings;
  const render::CellColorLookup cell_color = [&](float x, float, float z) {
    return render::GroundCellColor(mesh, x, z);
  };
  for (const res::WorldModel& placed : world.models) {
    auto& file = model_files[placed.file];
    if (!file) {
      auto model = std::make_unique<res::Model>();
      std::string ignored;
      if (Load(files, "data\\model\\" + placed.file, bytes) &&
          res::DecodeModel(bytes, *model, &ignored))
        file = std::move(model);
    }
    if (!file) continue;
    render::ModelInstance instance;
    if (!render::CreateModelInstance(*file, placed.position, placed.rotation, placed.scale,
                                     placed.animation_speed, instance))
      continue;
    render::LightModel(instance, light, cell_color);
    std::vector<std::uint32_t> ids;
    render::ModelDrawSettings draw;
    for (const std::string& name : file->textures) {
      auto [it, added] = model_textures.try_emplace(
          name, kModelTextureIdBase + static_cast<std::uint32_t>(model_textures.size()), false);
      if (added) {
        res::Bitmap image;
        std::string ignored;
        std::uint32_t w = 1, h = 1;
        std::vector<std::uint32_t> texels{0};
        if (Load(files, "data\\texture\\" + name, bytes) &&
            res::DecodeBitmap(name, bytes, image, &ignored)) {
          texels = ClientTexels(image, w, h);
          it->second.second = image.has_alpha;
        }
        device->SetTexture(it->second.first, w, h, texels);
      }
      ids.push_back(it->second.first);
      draw.texture_alpha.push_back(it->second.second);
    }
    models.push_back(std::move(instance));
    model_texture_ids.push_back(std::move(ids));
    model_settings.push_back(std::move(draw));
  }
  std::fprintf(stderr, "ragview: %zu of %zu models, %zu files, %zu textures\n", models.size(),
               world.models.size(), model_files.size(), model_textures.size());

  render::GroundDrawSettings settings;
  settings.viewport = render::GroundViewportOf(render::SetSize(o.width, o.height));
  for (render::ModelDrawSettings& m : model_settings) {
    m.viewport = settings.viewport;
    m.fade = settings.fade;
  }
  settings.water.level = 1.0e30f;  // the water's textures are not loaded yet
  render::FixedFunctionState state;
  render::BoundTextures bound;
  std::vector<render::GpuVertex> vertices;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;  // first vertex, count

  const auto frame = [&](std::uint32_t number, std::vector<render::DrawRecord>& draws) {
    settings.frame = number;
    render::UpdateCamera(camera, player, false, &attr);
    std::vector<render::GroundPrimitive> primitives;
    render::DrawGroundCells(mesh, camera.view, {0, 0, ground.width - 1, ground.height - 1}, true,
                            settings, primitives);
    render::SceneLists lists;
    vertices.clear();
    spans.clear();
    for (std::size_t i = 0; i < primitives.size(); ++i) {
      const render::GroundPrimitive& p = primitives[i];
      render::ScenePrimitive s;
      s.id = static_cast<std::uint32_t>(i);
      s.texture = p.texture >= 0 ? static_cast<std::uint32_t>(p.texture + 1) : 0;
      s.lightmap =
          p.lightmap_page >= 0 ? kLightmapIdBase + static_cast<std::uint32_t>(p.lightmap_page) : 0;
      s.type = 5;
      s.vertex_count = 4;
      s.rhw0 = p.vertices[0].rhw;
      s.rhw1 = p.vertices[1].rhw;
      lists.Add(s, p.flags);
      spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), 4);
      for (std::size_t k = 0; k < 4; ++k) {
        const render::ScreenVertex& v = p.vertices[k];
        vertices.push_back({v.x, v.y, v.z, v.rhw, v.diffuse, v.specular, v.u, v.v,
                            p.lightmap_uv[k * 2], p.lightmap_uv[k * 2 + 1]});
      }
    }
    // CView::vf03 steps and draws each visible model; every model here, clipped to the
    // screen.
    for (std::size_t m = 0; m < models.size(); ++m) {
      render::AdvanceAnimation(models[m]);
      std::vector<render::ModelPrimitive> faces;
      render::DrawModel(models[m], camera.view, true, model_settings[m], faces);
      for (const render::ModelPrimitive& p : faces) {
        render::ScenePrimitive s;
        s.id = static_cast<std::uint32_t>(spans.size());
        s.texture = model_texture_ids[m][static_cast<std::size_t>(p.texture)];
        s.type = 4;
        s.vertex_count = static_cast<std::uint32_t>(p.vertices.size());
        s.rhw0 = p.vertices[0].rhw;
        s.rhw1 = p.vertices.size() > 1 ? p.vertices[1].rhw : 0.0f;
        lists.Add(s, p.flags);
        spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), s.vertex_count);
        for (const render::ScreenVertex& v : p.vertices)
          vertices.push_back({v.x, v.y, v.z, v.rhw, v.diffuse, v.specular, v.u, v.v, 0.0f, 0.0f});
      }
    }
    const auto calls = render::DrawScene(lists, {false, true, true}, bound);
    draws = render::RecordDraws(state, calls);
  };
  const render::VertexLookup lookup = [&](std::uint32_t id) {
    return std::span<const render::GpuVertex>(vertices.data() + spans[id].first, spans[id].second);
  };
  const auto width = static_cast<std::uint32_t>(o.width);
  const auto height = static_cast<std::uint32_t>(o.height);
  const std::uint32_t clear = 0xff404060;

  if (!o.screenshot.empty()) {
    std::vector<render::DrawRecord> draws;
    frame(1, draws);
    std::vector<std::uint32_t> argb;
    if (!device->RenderToImage(draws, lookup, width, height, clear, argb)) {
      std::fprintf(stderr, "ragview: %s\n", device->error().c_str());
      return 1;
    }
    std::vector<std::uint8_t> rgba(argb.size() * 4);
    for (std::size_t i = 0; i < argb.size(); ++i) {
      rgba[i * 4] = static_cast<std::uint8_t>(argb[i] >> 16);
      rgba[i * 4 + 1] = static_cast<std::uint8_t>(argb[i] >> 8);
      rgba[i * 4 + 2] = static_cast<std::uint8_t>(argb[i]);
      rgba[i * 4 + 3] = 0xff;
    }
    if (stbi_write_png(o.screenshot.c_str(), o.width, o.height, 4, rgba.data(), o.width * 4) == 0) {
      std::fprintf(stderr, "ragview: cannot write %s\n", o.screenshot.c_str());
      return 1;
    }
    std::fprintf(stderr, "ragview: %zu draws to %s\n", draws.size(), o.screenshot.c_str());
    return 0;
  }

  // The client's camera input, once a frame: holding the right button is its drag state,
  // which turns, with Shift pitches and with Ctrl zooms; a right double-click is its reset
  // state. The arrow keys walk the player a cell.
  bool running = true;
  float mouse_x = 0.0f, mouse_y = 0.0f;
  SDL_GetMouseState(&mouse_x, &mouse_y);
  render::CameraDrag drag;
  drag.last_x = static_cast<std::int32_t>(mouse_x);
  drag.last_y = static_cast<std::int32_t>(mouse_y);
  for (int n = 1; running; ++n) {
    SDL_Event e;
    bool reset = false;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
        running = false;
      if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT &&
          e.button.clicks == 2)
        reset = true;
      if (e.type == SDL_EVENT_KEY_DOWN) {
        const float step = static_cast<float>(attr.cell_size);
        if (e.key.key == SDLK_LEFT) player[0] -= step;
        if (e.key.key == SDLK_RIGHT) player[0] += step;
        if (e.key.key == SDLK_UP) player[2] += step;
        if (e.key.key == SDLK_DOWN) player[2] -= step;
        player[1] = res::AttrHeight(attr, player[0], player[2]);
      }
    }
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    const SDL_Keymod mods = SDL_GetModState();
    drag.mouse_x = static_cast<std::int32_t>(mouse_x);
    drag.mouse_y = static_cast<std::int32_t>(mouse_y);
    drag.shift = (mods & SDL_KMOD_SHIFT) != 0;
    drag.ctrl = (mods & SDL_KMOD_CTRL) != 0;
    if (reset) {
      drag.kind = render::CameraDrag::Kind::kReset;
      render::DragCamera(camera, control, drag);
    } else if ((buttons & SDL_BUTTON_RMASK) != 0) {
      drag.kind = render::CameraDrag::Kind::kDrag;
      render::DragCamera(camera, control, drag);
    }
    drag.last_x = drag.mouse_x;
    drag.last_y = drag.mouse_y;
    std::vector<render::DrawRecord> draws;
    frame(static_cast<std::uint32_t>(n), draws);
    if (!device->DrawFrame(draws, lookup, width, height, clear)) {
      std::fprintf(stderr, "ragview: %s\n", device->error().c_str());
      return 1;
    }
    if (o.frames > 0 && n >= o.frames) running = false;
  }
  device.reset();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
