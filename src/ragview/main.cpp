// ragview: a map's ground drawn as the client draws it (docs/render/gpu.md).
//
//   ragview [--install DIR] [--size W H] [--camera YAW PITCH DISTANCE] [--centre X Z]
//           [--frames N] [--screenshot FILE.png] MAP
//
// MAP is a .rsw name such as prontera. The ground is built and drawn by the ported
// C3dGround15, through the scene lists and DrawScene, and the calls are replayed on
// SDL_GPU. The camera is a plain orbit until CView is ported: YAW and PITCH in degrees
// (pitch 90 looks straight down), DISTANCE in world units, around (X, Z). --screenshot
// renders one frame offscreen to a PNG and exits; otherwise a window opens, and Escape or
// --frames N closes it.

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "render/fixed_function.h"
#include "render/gpu_device.h"
#include "render/ground.h"
#include "render/scene.h"
#include "render/texture.h"
#include "res/bitmap.h"
#include "res/data_files.h"
#include "res/gnd.h"
#include "res/grf.h"
#include "res/rsw.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

using namespace ragexe;

constexpr std::uint32_t kLightmapIdBase = 0x10000;

struct Options {
  std::filesystem::path install = "original/install";
  std::string map;
  int width = 800;
  int height = 600;
  float yaw = 0.0f;
  float pitch = 50.0f;
  float distance = 250.0f;
  float centre_x = 0.0f;
  float centre_z = 0.0f;
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
               "usage: ragview [--install DIR] [--size W H] [--camera YAW PITCH DISTANCE]\n"
               "               [--centre X Z] [--frames N] [--screenshot FILE.png] MAP\n");
  return 2;
}

// A row-vector view matrix looking at (x, 0, z) from distance along yaw and pitch.
render::ViewMatrix OrbitView(const Options& o) {
  const float yaw = o.yaw * 3.14159265f / 180.0f;
  const float pitch = o.pitch * 3.14159265f / 180.0f;
  const float f[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                      std::cos(yaw) * std::cos(pitch)};
  // Screen right is the mirror of the right-handed one: the ground's faces wind
  // counter-clockwise on screen, so the client's clockwise culling keeps them.
  const float right[3] = {std::cos(yaw), 0.0f, -std::sin(yaw)};
  const float d[3] = {f[1] * right[2] - f[2] * right[1], f[2] * right[0] - f[0] * right[2],
                      f[0] * right[1] - f[1] * right[0]};
  const float r[3] = {-right[0], -right[1], -right[2]};
  const float c[3] = {o.centre_x, 0.0f, o.centre_z};
  const auto dot = [&](const float* v) { return c[0] * v[0] + c[1] * v[1] + c[2] * v[2]; };
  return {r[0], d[0], f[0], r[1],    d[1],    f[1],
          r[2], d[2], f[2], -dot(r), -dot(d), -dot(f) + o.distance};
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
    } else if (a == "--camera" && next(3)) {
      o.yaw = std::strtof(argv[++i], nullptr);
      o.pitch = std::strtof(argv[++i], nullptr);
      o.distance = std::strtof(argv[++i], nullptr);
    } else if (a == "--centre" && next(2)) {
      o.centre_x = std::strtof(argv[++i], nullptr);
      o.centre_z = std::strtof(argv[++i], nullptr);
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
  std::fprintf(stderr, "ragview: %s, %s (%d x %d cells), on %s\n", o.map.c_str(),
               world.ground.c_str(), ground.width, ground.height, device->driver());

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

  render::GroundDrawSettings settings;
  settings.viewport = {0.0f,    0.0f,     o.width / 2, o.height / 2,
                       o.width, o.height, o.width / 2, o.height / 2};
  // SetSize's scales for a 90 degree field of view across the height.
  settings.viewport.scale_y = static_cast<float>(o.height) / 2.0f;
  settings.viewport.scale_x = settings.viewport.scale_y;
  settings.water.level = 1.0e30f;  // the water's textures are not loaded yet
  render::FixedFunctionState state;
  render::BoundTextures bound;
  std::vector<render::GpuVertex> vertices;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;  // first vertex, count

  const auto frame = [&](std::uint32_t number, std::vector<render::DrawRecord>& draws) {
    settings.frame = number;
    std::vector<render::GroundPrimitive> primitives;
    render::DrawGroundCells(mesh, OrbitView(o), {0, 0, ground.width - 1, ground.height - 1}, true,
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

  bool running = true;
  for (int n = 1; running; ++n) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
        running = false;
      if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.key == SDLK_LEFT) o.yaw -= 5.0f;
        if (e.key.key == SDLK_RIGHT) o.yaw += 5.0f;
        if (e.key.key == SDLK_UP) o.distance *= 0.9f;
        if (e.key.key == SDLK_DOWN) o.distance /= 0.9f;
      }
    }
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
