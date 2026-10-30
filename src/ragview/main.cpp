// ragview: a map's ground drawn as the client draws it (docs/render/gpu.md).
//
//   ragview [--install DIR] [--size W H] [--at X Z] [--yaw DEGREES] [--frames N]
//           [--no-fog] [--actor SPRITE | --job JOB | --player JOB SEX HAIR [--hair-color N]
//           [--clothes-color N] [--headgear TOP MID BOTTOM] [--weapon KIND] [--shield ID]]
//           [--action BASE] [--effect NAME | --effect-id ID [--minimal-effects]
//           [--effect-level N]] [--effect-step N] [--fps] [--stamp] [--screenshot FILE.png] MAP
//
// MAP is a .rsw name such as prontera. The ground is built and drawn by the ported
// C3dGround15, through the scene lists and DrawScene, and the calls are replayed on
// SDL_GPU. The camera is the client's: placed as on entering the map, following a player
// standing at world (X, Z) (the map's centre by default), turned to YAW if given. The
// map's fog is on, as the client's FOG setting is by default, unless --no-fog. An actor
// stands where the player does, drawn from data\sprite\SPRITE.spr and .act (npc\4_m_01
// by default, none for no actor), idle and facing south. --job draws the NPC or monster of a
// job id instead (CNpc::vf16: its name in ClientState's job names), with the job's shadow
// scale. --player draws a player character
// there instead: the body of job JOB (0 the novice) and sex SEX (0 female, 1 male), with
// hair style HAIR, layered by the job's .imf, dressed in the palettes, headgear, weapon and
// shield given. --action sets the base action (0 idle; 80 attacking, which shows the
// weapon and shield). --effect plays data\texture\effect\NAME.str where the actor
// stands, a step each 16 ms world step and again when it ends; a screenshot shows it after
// --effect-step steps (15 by default). --effect-id plays what the client plays for that
// effect id: the .str its table names (the lighter one with --minimal-effects) and the
// primitives the id adds, ported for ids 0 to 2 and the ring effects. --effect-level gives
// the effect the level its message would (+0xb8, 0 by default), which the arrows and blitz
// beat count by.
// --fps draws the frame rate as SceneRenderer 00410cf0 would, which the client never calls;
// --stamp draws the date and time on the screenshot as the client stamps its own. Text is
// drawn with FreeType in place of GDI (docs/render/text.md#the-glyphs).
// --screenshot renders one frame offscreen to a PNG and exits; otherwise a window opens,
// and Escape or --frames N closes it.

#include <SDL3/SDL.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/frame_timer.h"
#include "render/actor.h"
#include "render/camera.h"
#include "render/cursor.h"
#include "render/effect.h"
#include "render/effect_prim.h"
#include "render/effect_table.h"
#include "render/fixed_function.h"
#include "render/fog.h"
#include "render/glyphs.h"
#include "render/gpu_device.h"
#include "render/ground.h"
#include "render/model.h"
#include "render/player.h"
#include "render/player_names.h"
#include "render/scene.h"
#include "render/scene_graph.h"
#include "render/text.h"
#include "render/texture.h"
#include "render/world_load.h"
#include "res/act.h"
#include "res/bitmap.h"
#include "res/client_tables.h"
#include "res/data_files.h"
#include "res/gat.h"
#include "res/gnd.h"
#include "res/grf.h"
#include "res/imf.h"
#include "res/name_tables.h"
#include "res/palette.h"
#include "res/rsm.h"
#include "res/rsw.h"
#include "res/sprite.h"
#include "res/str.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

using namespace ragexe;

constexpr std::uint32_t kLightmapIdBase = 0x10000;
constexpr std::uint32_t kModelTextureIdBase = 0x20000;
constexpr std::uint32_t kWaterTextureIdBase = 0x30000;
constexpr std::uint32_t kActorTextureIdBase = 0x40000;
constexpr std::uint32_t kShadowTextureIdBase = 0x48000;
constexpr std::uint32_t kEffectTextureIdBase = 0x50000;
constexpr std::uint32_t kPrimTextureIdBase = 0x58000;
constexpr std::uint32_t kParticleTextureIdBase = 0x5c000;  // then 0x400 for each effect sprite
constexpr std::uint32_t kTextTextureId = 0x60000;
constexpr std::uint32_t kCursorTextureIdBase = 0x61000;
constexpr std::uint32_t kSpritePage = 256;  // the sprite texture cache's pages (0040e130)

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
  bool fog = true;
  std::string actor = "npc\\4_m_01";
  bool player = false;
  int job = 0;
  int npc_job = -1;
  int sex = 1;
  int hair = 1;
  int hair_color = 0;
  int clothes_color = 0;
  std::array<int, 3> headgear{};
  int weapon = 0;
  int shield = 0;
  int action = 0;
  std::string effect;
  int effect_id = -1;
  bool minimal_effects = false;
  int effect_level = 0;
  int effect_step = 15;
  bool fps = false;
  bool stamp = false;
  std::string screenshot;
};

// A whole decimal number, or fallback when the text is not one.
int ParseInt(const char* text, int fallback) {
  char* end = nullptr;
  const long v = std::strtol(text, &end, 10);
  return end != text && *end == '\0' ? static_cast<int>(v) : fallback;
}

// GetLocalTime's fields from the C library's clock.
render::LocalTime LocalNow() {
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  const auto field = [](int v) { return static_cast<std::uint16_t>(v); };
  return {field(tm.tm_year + 1900), field(tm.tm_mon + 1), field(tm.tm_wday), field(tm.tm_mday),
          field(tm.tm_hour),        field(tm.tm_min),     field(tm.tm_sec)};
}

int Usage() {
  std::fprintf(stderr,
               "usage: ragview [--install DIR] [--size W H] [--at X Z] [--yaw DEGREES]\n"
               "               [--frames N] [--no-fog] [--actor SPRITE | --job JOB |\n"
               "               --player JOB SEX HAIR\n"
               "               [--hair-color N] [--clothes-color N] [--headgear TOP MID BOTTOM]\n"
               "               [--weapon KIND] [--shield ID]] [--action BASE]\n"
               "               [--effect NAME | --effect-id ID [--minimal-effects]\n"
               "               [--effect-level N]] [--effect-step N] [--fps] [--stamp]\n"
               "               [--screenshot FILE.png] MAP\n");
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

// A sprite and its actions, with each image uploaded as the texture cache would hold it:
// an 8-bit image decoded through the palette into a slot at the corner of a 256 x 256
// page (CTexture::DrawRleImage, index 0 clear), a 32-bit image in a texture of its own.
// The launched effect's .str (EffectStr) through the StrPlayer, at the effect's frame, under
// the view and projection of the frame being drawn; it leaves the actor's point on screen
// in the effect, as AdvanceStr does.
class PlayerStr : public render::EffectStr {
 public:
  PlayerStr(render::StrPlayer& player, const render::Matrix& view,
            const render::Projection& projection)
      : player_(player), view_(view), projection_(projection) {}
  bool Advance(render::RagEffect& effect, const render::Vec3& actor, float x, float y,
               float z) override {
    player_.SetFrame(effect.frame);
    if (!player_.Advance(view_, actor, projection_, x, y, z)) return false;
    effect.screen_x = player_.screen_x();
    effect.screen_y = player_.screen_y();
    return true;
  }
  void Reset() override { player_.Reset(); }

 private:
  render::StrPlayer& player_;
  const render::Matrix& view_;
  const render::Projection& projection_;
};

struct ActorSprite {
  res::Sprite sprite;
  res::Act act;
  std::vector<std::uint32_t> ids8, ids32;
  std::vector<render::SpriteTextureRect> rects8, rects32;
};

bool LoadActorSprite(const res::DataFiles& files, const std::string& name,
                     render::RenderDevice& device, std::uint32_t id_base, ActorSprite& out,
                     const res::Palette* palette = nullptr) {
  std::vector<std::uint8_t> bytes;
  std::string ignored;
  if (!Load(files, "data\\sprite\\" + name + ".spr", bytes) ||
      !res::DecodeSprite(bytes, {}, out.sprite, &ignored))
    return false;
  const res::ActImageLookup lookup = [&](std::int32_t type,
                                         std::int32_t index) -> std::optional<res::ActImageSize> {
    const auto i = static_cast<std::size_t>(index);
    if (index >= 0 && type == 0 && i < out.sprite.images.size()) {
      const auto& image = out.sprite.images[i];
      return res::ActImageSize{image.width, image.height, image.halved_x, image.halved_y};
    }
    if (index >= 0 && type == 1 && i < out.sprite.images32.size()) {
      const auto& image = out.sprite.images32[i];
      return res::ActImageSize{image.width, image.height, image.halved_x, image.halved_y};
    }
    return std::nullopt;
  };
  if (!Load(files, "data\\sprite\\" + name + ".act", bytes) ||
      !res::DecodeAct(bytes, lookup, out.act, &ignored))
    return false;
  std::uint32_t id = id_base;
  for (const res::SpriteImage& image : out.sprite.images) {
    const std::vector<std::uint8_t> indices = image.Decode();
    std::vector<std::uint32_t> page(static_cast<std::size_t>(kSpritePage) * kSpritePage, 0);
    const auto w = std::min<std::uint32_t>(static_cast<std::uint32_t>(image.width), kSpritePage);
    const auto h = std::min<std::uint32_t>(static_cast<std::uint32_t>(image.height), kSpritePage);
    for (std::uint32_t y = 0; y < h; ++y) {
      for (std::uint32_t x = 0; x < w; ++x) {
        const std::uint8_t index = indices[y * static_cast<std::uint32_t>(image.width) + x];
        const std::uint32_t texel =
            index == 0 ? 0 : (palette != nullptr ? *palette : out.sprite.palette)[index] & 0xffff;
        page[y * kSpritePage + x] = render::ExpandTexel(render::TextureFormat::kArgb1555, texel);
      }
    }
    device.SetTexture(id, kSpritePage, kSpritePage, page);
    out.ids8.push_back(id++);
    const auto page_size = static_cast<float>(kSpritePage);
    out.rects8.push_back({0.0f, 0.0f, static_cast<float>(image.width) / page_size,
                          static_cast<float>(image.height) / page_size});
  }
  for (const res::SpriteImage32& image : out.sprite.images32) {
    const auto w = static_cast<std::uint32_t>(std::max<std::int16_t>(image.pixel_width, 1));
    const auto h = static_cast<std::uint32_t>(std::max<std::int16_t>(image.pixel_height, 1));
    std::vector<std::uint32_t> pixels = image.pixels;
    pixels.resize(static_cast<std::size_t>(w) * h, 0);
    device.SetTexture(id, w, h, pixels);
    out.ids32.push_back(id++);
    out.rects32.push_back({0.0f, 0.0f, 1.0f, 1.0f});
  }
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
    } else if (a == "--actor" && next(1)) {
      o.actor = argv[++i];
    } else if (a == "--job" && next(1)) {
      o.npc_job = ParseInt(argv[++i], -1);
    } else if (a == "--player" && next(3)) {
      o.player = true;
      o.job = ParseInt(argv[++i], -1);
      o.sex = ParseInt(argv[++i], -1);
      o.hair = ParseInt(argv[++i], 0);
    } else if (a == "--hair-color" && next(1)) {
      o.hair_color = ParseInt(argv[++i], 0);
    } else if (a == "--clothes-color" && next(1)) {
      o.clothes_color = ParseInt(argv[++i], 0);
    } else if (a == "--headgear" && next(3)) {
      for (int& h : o.headgear) h = ParseInt(argv[++i], 0);
    } else if (a == "--weapon" && next(1)) {
      o.weapon = ParseInt(argv[++i], 0);
    } else if (a == "--shield" && next(1)) {
      o.shield = ParseInt(argv[++i], 0);
    } else if (a == "--action" && next(1)) {
      o.action = ParseInt(argv[++i], 0);
    } else if (a == "--effect" && next(1)) {
      o.effect = argv[++i];
    } else if (a == "--effect-id" && next(1)) {
      o.effect_id = ParseInt(argv[++i], -1);
    } else if (a == "--minimal-effects") {
      o.minimal_effects = true;
    } else if (a == "--effect-level" && next(1)) {
      o.effect_level = ParseInt(argv[++i], 0);
    } else if (a == "--effect-step" && next(1)) {
      o.effect_step = ParseInt(argv[++i], 15);
    } else if (a == "--no-fog") {
      o.fog = false;
    } else if (a == "--fps") {
      o.fps = true;
    } else if (a == "--stamp") {
      o.stamp = true;
    } else if (a == "--screenshot" && next(1)) {
      o.screenshot = argv[++i];
    } else if (!a.starts_with("--") && o.map.empty()) {
      o.map = a;
    } else {
      return Usage();
    }
  }
  if (o.map.empty() || o.width <= 0 || o.height <= 0 || o.frames < 0) return Usage();
  if (o.player && (o.job < 0 || o.job > 44 || o.sex < 0 || o.sex > 1)) return Usage();
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
  // CGameMode::OnInit's fog (00516570): the map's fogParameterTable.txt record, when the
  // fog flag is on. The device has vertex fog, so every primitive gets its fog factor.
  res::LineRecords fog_table;
  render::FogState fog;
  const bool fog_on = o.fog && res::LoadFogParameters(files, fog_table) &&
                      render::FindMapFog(fog_table, o.map + ".rsw", fog);
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
  std::fprintf(stderr, "ragview: %s, %s (%d x %d cells, %s, %s), on %s\n", o.map.c_str(),
               world.ground.c_str(), ground.width, ground.height, indoor ? "indoor" : "outdoor",
               fog_on ? "fog" : "no fog", device->driver());

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
  render::ShakeSwing shake_swing;
  render::ResetCamera(camera);
  render::CameraControl control;
  render::PlaceCamera(camera, control, player, indoor);
  if (o.turned) {
    camera.yaw = o.yaw;
    camera.target_yaw = o.yaw;
  }

  // The world's models, as CWorld::LoadWorld places them, lit by the map's light and the
  // ground's colours, in the order it and its lazy loader add them from where the player
  // stands (docs/render/model.md#loading); a model that fails to load drops out of it. Each
  // texture is uploaded once, by name.
  std::map<std::string, std::unique_ptr<res::Model>> model_files;
  std::map<std::string, std::pair<std::uint32_t, bool>> model_textures;  // id, ARGB 4444
  std::vector<render::ModelInstance> models;
  std::vector<std::vector<std::uint32_t>> model_texture_ids;  // per instance, by model texture
  std::vector<render::ModelDrawSettings> model_settings;
  const render::CellColorLookup cell_color = [&](float x, float, float z) {
    return render::GroundCellColor(mesh, x, z);
  };
  const auto load_order =
      render::ModelLoadOrder(world.models, player[0], player[2], [](std::size_t) { return true; });
  for (const std::size_t index : load_order) {
    const res::WorldModel& placed = world.models[index];
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

  // The actor, its shadow (shadow.spr and shadow.act), and the scales its sprites read. An
  // actor has one layer; a player has a body and a head, ordered by the job's .imf.
  std::array<ActorSprite, render::kPlayerLayerCount> layers;
  ActorSprite& actor = layers[render::kPlayerBody];
  ActorSprite shadow;
  res::Imf imf;
  bool has_actor = false;
  if (o.player) {
    std::string ignored;
    // CPc::vf27 and vf28: the hair's and the clothes' palettes, when chosen.
    const auto palette = [&](const std::string& name, res::PaletteRes& out) {
      return !name.empty() && Load(files, "data\\palette\\" + name, bytes) &&
             out.LoadFromBytes(bytes);
    };
    res::PaletteRes hair_palette;
    res::PaletteRes clothes_palette;
    const bool hair_colored =
        palette(render::PlayerHeadPaletteName(o.hair, o.job, o.sex, o.hair_color), hair_palette);
    const bool clothes_colored =
        palette(render::PlayerBodyPaletteName(o.job, o.sex, o.clothes_color), clothes_palette);
    const auto layer_id = [](int layer) {
      return kActorTextureIdBase + static_cast<std::uint32_t>(layer) * 0x1000;
    };
    has_actor = LoadActorSprite(files, render::PlayerBodyName(o.job, o.sex), *device, layer_id(0),
                                actor, clothes_colored ? &clothes_palette.palette() : nullptr) &&
                LoadActorSprite(files, render::PlayerHeadName(o.hair, o.sex), *device, layer_id(1),
                                layers[render::kPlayerHead],
                                hair_colored ? &hair_palette.palette() : nullptr) &&
                Load(files, "data\\imf\\" + render::PlayerImfName(o.job, o.sex), bytes) &&
                res::DecodeImf(bytes, imf, &ignored);
    if (!has_actor) std::fprintf(stderr, "ragview: no player sprites for job %d\n", o.job);
    // The headgear, the weapon and its glow, and the shield; a layer whose names are missing
    // stays empty and is not drawn.
    const auto dress = [&](int layer, const render::PlayerSpriteNames& names) {
      if (names.act.empty() || names.sprite.empty()) return;
      if (!LoadActorSprite(files, names.sprite, *device, layer_id(layer),
                           layers[static_cast<std::size_t>(layer)]))
        layers[static_cast<std::size_t>(layer)] = {};
    };
    for (int i = 0; i < 3; ++i)
      dress(render::kPlayerHeadgearTop + i,
            render::PlayerHeadgearNames(o.sex, o.headgear[static_cast<std::size_t>(i)]));
    const auto weapon = render::PlayerWeaponNames(o.job, o.sex, o.weapon);
    dress(render::kPlayerWeapon, weapon[0]);
    dress(render::kPlayerWeaponGlow, weapon[1]);
    dress(render::kPlayerShield, render::PlayerShieldNames(o.job, o.sex, o.shield));
  } else if (o.npc_job >= 0) {
    // iRO's service type, 1; the actor's own job is the one asked for.
    const render::PlayerSpriteNames names = render::NpcSpriteNames(o.npc_job, o.npc_job, 1);
    has_actor = !names.sprite.empty() &&
                LoadActorSprite(files, names.sprite, *device, kActorTextureIdBase, actor);
    if (!has_actor) std::fprintf(stderr, "ragview: no sprite for job %d\n", o.npc_job);
  } else if (o.actor != "none") {
    has_actor = LoadActorSprite(files, o.actor, *device, kActorTextureIdBase, actor);
    if (!has_actor) std::fprintf(stderr, "ragview: no sprite %s\n", o.actor.c_str());
  }
  has_actor = has_actor && LoadActorSprite(files, "shadow", *device, kShadowTextureIdBase, shadow);
  render::PlayerHeadOffset head_offset;
  render::RenderObject actor_object;
  actor_object.shadow = true;
  // CPc::UpdateBody and CNpc::vf16: the job's shadow scale.
  if (o.player) actor_object.shadow_scale = res::JobShadowScale(o.job);
  if (o.npc_job >= 0) actor_object.shadow_scale = res::JobShadowScale(o.npc_job);
  // vf07 with the base action: the action's delay (4 beyond the .act's), at least 1.
  actor_object.base_action = o.action;
  const auto base = static_cast<std::size_t>(std::max(o.action, 0));
  actor_object.delay = base < actor.act.delays.size() ? actor.act.delays[base] : 4.0f;
  if (actor_object.delay < 1.0f) actor_object.delay = 1.0f;
  actor_object.start_time = static_cast<std::uint32_t>(SDL_GetTicks());

  render::GroundDrawSettings settings;
  const render::Projection projection = render::SetSize(o.width, o.height);
  render::SpriteScales sprite_scales;
  render::SetSpriteScalesForSize(projection, sprite_scales);
  settings.viewport = render::GroundViewportOf(projection);
  // CWorld::BuildSceneGraph, with the .rsw's bounds when it has them (version 2.1).
  render::SceneGraph graph;
  render::BuildSceneGraph(mesh, attr, models, world.bounds, graph);
  for (render::ModelDrawSettings& m : model_settings) {
    m.viewport = settings.viewport;
    m.fade = settings.fade;
    // CGameMode::OnInit sets g_WorldColorFade+0xd on every map: the models' colours plus
    // the ground's.
    m.ground_tint = true;
  }
  settings.water = render::WaterOfWorld(world);
  // The water's textures, uploaded as the steps first ask for them.
  std::map<std::string, std::uint32_t> water_textures;
  std::uint32_t water_texture = 0;
  render::FixedFunctionState state;
  render::BoundTextures bound;
  std::vector<render::GpuVertex> vertices;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;  // first vertex, count
  // SetFogEnabled leaves SceneRenderer+0x48 set, so the first DrawScene sets up the fog.
  bool setup_projection = true;

  // The effect: CRagEffect playing the .str, from the world steps the frame timer counts
  // (ProcessGenericType's offset, 80 pixels up). Each texture is uploaded the first time a
  // triangle asks for it: effect\ and its name, .bmp added when it has none, and "1.bmp"
  // past a layer's names (GetEffectEntryTexture, 004289a0).
  // --effect-id: the world's effect objects for the primitives, and the .str the id names,
  // which the effect plays as its case of vf01 does, if at all.
  render::EffectWorld prim_world;
  const bool has_prims = o.effect_id >= 0;
  // The sprites effects load, the particle first; each drawn quad names the one it is of.
  std::list<ActorSprite> effect_sprites;
  std::map<const res::Sprite*, const ActorSprite*> sprite_of;
  if (has_prims) {
    std::vector<const char*> names = {render::kParticleSprite};
    names.insert(names.end(), render::kEffectSprites.begin(), render::kEffectSprites.end());
    for (std::size_t k = 0; k < names.size(); ++k) {
      ActorSprite& loaded = effect_sprites.emplace_back();
      const auto first_id = kParticleTextureIdBase + static_cast<std::uint32_t>(k) * 0x400;
      if (!LoadActorSprite(files, names[k], *device, first_id, loaded)) {
        effect_sprites.pop_back();
        continue;
      }
      prim_world.SetSprite(names[k], {&loaded.act, &loaded.sprite});
      sprite_of[&loaded.sprite] = &loaded;
    }
  }
  prim_world.SetMinimal(o.minimal_effects);
  if (has_prims && o.effect.empty()) {
    const std::string name =
        render::EffectStrName(o.effect_id, o.minimal_effects, prim_world.rand());
    if (name.size() > 4) o.effect = name.substr(0, name.size() - 4);
  }
  res::Str effect_str;
  bool has_effect = false;
  if (!o.effect.empty()) {
    std::string ignored;
    has_effect = Load(files, "data\\texture\\effect\\" + o.effect + ".str", bytes) &&
                 res::DecodeStr(bytes, effect_str, &ignored);
    if (!has_effect) std::fprintf(stderr, "ragview: no effect %s\n", o.effect.c_str());
  }
  render::StrPlayer effect_player(effect_str);
  PlayerStr launched_str(effect_player, camera.view, projection);
  core::FrameTimer world_steps;
  world_steps.SetFrameRate(60, static_cast<std::uint32_t>(SDL_GetTicks()));
  std::map<std::pair<std::size_t, std::int32_t>, std::uint32_t> effect_textures;
  const auto effect_texture = [&](std::size_t layer, std::int32_t index) {
    auto [it, added] = effect_textures.try_emplace(
        {layer, index}, kEffectTextureIdBase + static_cast<std::uint32_t>(effect_textures.size()));
    if (added) {
      const res::StrLayer& l = effect_str.layers[layer];
      std::string name = static_cast<std::size_t>(index) < l.textures.size()
                             ? l.textures[static_cast<std::size_t>(index)]
                             : std::string("1.bmp");
      if (name.find('.') == std::string::npos) name += ".bmp";
      res::Bitmap image;
      std::string ignored;
      std::uint32_t w = 1, h = 1;
      std::vector<std::uint32_t> texels{0};
      if (Load(files, "data\\texture\\effect\\" + name, bytes) &&
          res::DecodeBitmap(name, bytes, image, &ignored))
        texels = ClientTexels(image, w, h);
      device->SetTexture(it->second, w, h, texels);
    }
    return it->second;
  };
  bool effect_started = false;
  std::map<std::string, std::uint32_t> prim_textures;
  const auto prim_texture = [&](const std::string& name) {
    auto [it, added] = prim_textures.try_emplace(
        name, kPrimTextureIdBase + static_cast<std::uint32_t>(prim_textures.size()));
    if (added) {
      res::Bitmap image;
      std::string ignored;
      std::uint32_t w = 1, h = 1;
      std::vector<std::uint32_t> texels{0};
      if (Load(files, "data\\texture\\" + name, bytes) &&
          res::DecodeBitmap(name, bytes, image, &ignored))
        texels = ClientTexels(image, w, h);
      device->SetTexture(it->second, w, h, texels);
    }
    return it->second;
  };

  // The mouse's point for the cell cursor, none in a screenshot.
  bool has_mouse = false;
  std::int32_t mouse_cell_x = 0, mouse_cell_y = 0;
  render::CellCursor cell_cursor;

  const auto frame = [&](std::uint32_t number, std::uint32_t now,
                         std::vector<render::DrawRecord>& draws) {
    settings.frame = number;
    // CGameMode::OnFrame's step: the water's frame and phase, and its texture.
    const std::string water_name = render::StepWater(settings.water);
    auto [water_it, water_added] = water_textures.try_emplace(
        water_name, kWaterTextureIdBase + static_cast<std::uint32_t>(water_textures.size()));
    if (water_added) {
      res::Bitmap image;
      std::string ignored;
      std::uint32_t w = 1, h = 1;
      std::vector<std::uint32_t> texels{0};
      if (Load(files, "data\\texture\\" + water_name, bytes) &&
          res::DecodeBitmap(water_name, bytes, image, &ignored))
        texels = ClientTexels(image, w, h);
      device->SetTexture(water_it->second, w, h, texels);
    }
    water_texture = water_it->second;
    // The client's one CRT rand serves the shake and the effects alike.
    render::UpdateCamera(camera, player, false, &attr, shake_swing, now, prim_world.rand());
    render::StepSpriteScales(camera.pitch, camera.distance, projection, sprite_scales);
    // CView::DrawWorld leaves the next frame a shake step.
    shake_swing.step_pending = true;
    // CView::vf03: the scene graph's nodes the frustum keeps, their models and cells.
    const render::Frustum frustum =
        render::MakeFrustum(projection.slope_x, projection.slope_y, camera.view);
    const render::VisibleNodes visible = render::CollectVisibleNodes(frustum, graph);
    std::vector<render::VisiblePrimitive> primitives;
    render::DrawVisible(visible, graph, frustum, camera.view, mesh, settings, models,
                        model_settings, primitives);
    render::SceneLists lists;
    vertices.clear();
    spans.clear();
    for (const render::VisiblePrimitive& v : primitives) {
      render::ScenePrimitive s;
      s.id = static_cast<std::uint32_t>(spans.size());
      if (v.model < 0) {
        const render::GroundPrimitive& p = v.ground;
        s.texture = p.texture >= 0 ? static_cast<std::uint32_t>(p.texture + 1) : water_texture;
        s.lightmap = p.lightmap_page >= 0
                         ? kLightmapIdBase + static_cast<std::uint32_t>(p.lightmap_page)
                         : 0;
        s.type = 5;
        s.vertex_count = 4;
        s.rhw0 = p.vertices[0].rhw;
        s.rhw1 = p.vertices[1].rhw;
        lists.Add(s, p.flags);
        spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), 4);
        for (std::size_t k = 0; k < 4; ++k) {
          const render::ScreenVertex& sv = p.vertices[k];
          vertices.push_back({sv.x, sv.y, sv.z, sv.rhw, sv.diffuse, render::VertexFog(sv.rhw, fog),
                              sv.u, sv.v, p.lightmap_uv[k * 2], p.lightmap_uv[k * 2 + 1]});
        }
        continue;
      }
      const render::ModelPrimitive& p = v.face;
      const auto m = static_cast<std::size_t>(v.model);
      s.texture = model_texture_ids[m][static_cast<std::size_t>(p.texture)];
      s.type = 4;
      s.vertex_count = static_cast<std::uint32_t>(p.vertices.size());
      s.rhw0 = p.vertices[0].rhw;
      s.rhw1 = p.vertices.size() > 1 ? p.vertices[1].rhw : 0.0f;
      lists.Add(s, p.flags);
      spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), s.vertex_count);
      for (const render::ScreenVertex& sv : p.vertices)
        vertices.push_back({sv.x, sv.y, sv.z, sv.rhw, sv.diffuse, render::VertexFog(sv.rhw, fog),
                            sv.u, sv.v, 0.0f, 0.0f});
    }
    // CGameMode::method_005159d0 keeps the attribute cell under the mouse, and
    // CView::DrawWorld draws the cursor over it after the visible nodes.
    std::int32_t picked_x = 0, picked_z = 0;
    if (has_mouse && render::PickCell(projection, camera, mouse_cell_x, mouse_cell_y, graph, attr,
                                      picked_x, picked_z))
      render::PlaceCellCursor(cell_cursor, attr, picked_x, picked_z);
    std::array<render::ScreenVertex, 4> cursor_quad{};
    if (!cell_cursor.hidden &&
        render::DrawCellCursor(mesh, attr, camera.view, settings.viewport, cell_cursor.x,
                               cell_cursor.z, cell_cursor.color, cursor_quad)) {
      render::ScenePrimitive s;
      s.id = static_cast<std::uint32_t>(spans.size());
      s.texture = prim_texture("grid.tga");
      s.src_blend = 5;
      s.dest_blend = 6;
      s.type = 5;
      s.vertex_count = 4;
      s.rhw0 = cursor_quad[0].rhw;
      s.rhw1 = cursor_quad[1].rhw;
      lists.Add(s, render::scene_flags::kTranslucent);
      spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), 4);
      for (const render::ScreenVertex& sv : cursor_quad)
        vertices.push_back({sv.x, sv.y, sv.z, sv.rhw, sv.diffuse, render::VertexFog(sv.rhw, fog),
                            sv.u, sv.v, 0.0f, 0.0f});
    }
    // A sprite quad through SceneRenderer 0040e980.
    const auto add = [&](const render::SpriteQuad& q, const ActorSprite& from) {
      const bool is32 = q.image_type != 0;
      const auto i = static_cast<std::size_t>(q.image);
      if (i >= (is32 ? from.ids32.size() : from.ids8.size())) return;
      const auto v = render::SpriteQuadVertices(q, is32 ? from.rects32[i] : from.rects8[i]);
      render::ScenePrimitive s;
      s.id = static_cast<std::uint32_t>(spans.size());
      s.texture = is32 ? from.ids32[i] : from.ids8[i];
      s.type = 5;
      s.vertex_count = 4;
      s.rhw0 = v[0].rhw;
      s.rhw1 = v[1].rhw;
      lists.Add(s, q.flags);
      spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), 4);
      for (const render::ScreenVertex& sv : v)
        vertices.push_back({sv.x, sv.y, sv.z, sv.rhw, sv.diffuse, render::VertexFog(sv.rhw, fog),
                            sv.u, sv.v, 0.0f, 0.0f});
    };
    // The actor's vf03: its shadow, then its body.
    if (has_actor) {
      actor_object.position = player;
      render::UpdateActorFrame(actor_object, actor.act, camera.yaw, now);
      const float ground_height = res::AttrHeight(attr, player[0], player[2]);
      if (const auto q =
              render::DrawActorShadow(actor_object, ground_height, actor_object.scale, shadow.act,
                                      shadow.sprite, camera.view, projection, sprite_scales))
        add(*q, shadow);
      // C3dGround15::vf11's colour table is never filled in this client: always white.
      const std::array<float, 3> white = {1.0f, 1.0f, 1.0f};
      render::ActorBody body;
      if (o.player) {
        std::array<render::PlayerLayer, render::kPlayerLayerCount> player_layers;
        for (std::size_t l = 0; l < layers.size(); ++l)
          if (!layers[l].ids8.empty() || !layers[l].ids32.empty())
            player_layers[l] = {&layers[l].act, &layers[l].sprite};
        body = render::DrawPlayer(actor_object, player_layers, imf, camera.view, projection,
                                  sprite_scales, white, head_offset);
      } else {
        body = render::DrawActorBody(actor_object, actor.act, actor.sprite, camera.view, projection,
                                     sprite_scales, white);
      }
      for (const render::SpriteQuad& q : body.quads)
        add(q, layers[static_cast<std::size_t>(q.layer)]);
    }
    // CRagEffect::vf01 for each world step due, at most 8 a frame but all of --effect-step at
    // the first, then vf03's triangles, each with its own blends and stage mode.
    std::uint32_t steps = std::min<std::uint32_t>(world_steps.StepsDue(now), 8);
    if (!effect_started) {
      steps = static_cast<std::uint32_t>(std::max(o.effect_step, 1));
      effect_started = true;
    }
    if (has_prims) {
      render::PrimScene scene;
      scene.view_yaw = camera.yaw;
      scene.view_pitch = camera.pitch;
      scene.attr = &attr;
      scene.view = camera.view;
      scene.projection = projection;
      scene.slope_down = sprite_scales.slope_down;
      for (std::uint32_t i = 0; i < steps; ++i) {
        if (prim_world.empty()) {
          // The effects thrown at a target aim at a point 50 out either way along the ground.
          render::RagEffect& e =
              prim_world.Launch(o.effect_id, {player, actor_object.facing, actor_object.color});
          e.target = {player[0] + 50.0f, player[1], player[2] + 50.0f};
          e.level = o.effect_level;
          // InitEffect's LoadStrFile: the .str from its start.
          if (has_effect) {
            effect_player.Reset();
            e.str = &launched_str;
          }
        }
        prim_world.SetPlayer(player);
        prim_world.Step(scene);
        // Cloaking and the after-images colour the actor, drawn from the next frame.
        for (const render::EffectWorld::Object& object : prim_world.objects())
          if (object.effect) actor_object.color = object.actor.color;
      }
      // CGameMode::SendMsg's shake for the view; the jokes are not ported, and the
      // after-images not drawn.
      for (const render::ModeMessage& m : prim_world.TakeMessages()) {
        if (m.message == render::mode_message::kShake) {
          // SendMsg (0051da28 and 0051da4c): 200 ms for 1, 650 for anything else.
          render::StartShake(camera, 1, m.values[0] == 1 ? 1 : 0, 1.0f, 0.24f, 0.2f, now);
        } else if (m.message == render::mode_message::kShakeAmplitudes) {
          render::SetShakeAmplitudes(camera, std::bit_cast<float>(m.values[0]),
                                     std::bit_cast<float>(m.values[1]),
                                     std::bit_cast<float>(m.values[2]));
        }
      }
      const render::PrimDrawing drawing = prim_world.Draw(scene);
      for (std::size_t i = 0; i < drawing.quads.size(); ++i) {
        const auto it = sprite_of.find(drawing.quad_sprites[i]);
        if (it != sprite_of.end()) add(drawing.quads[i], *it->second);
      }
      for (const render::PrimTriangle& t : drawing.triangles) {
        render::ScenePrimitive s;
        s.id = static_cast<std::uint32_t>(spans.size());
        s.texture = prim_texture(t.texture);
        s.src_blend = t.src_blend;
        s.dest_blend = t.dest_blend;
        s.mode = t.mode;
        s.type = 4;
        s.vertex_count = 3;
        s.rhw0 = t.vertices[0].rhw;
        s.rhw1 = t.vertices[1].rhw;
        lists.Add(s, t.flags);
        spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), 3);
        for (const render::ScreenVertex& sv : t.vertices)
          vertices.push_back({sv.x, sv.y, sv.z, sv.rhw, sv.diffuse, render::VertexFog(sv.rhw, fog),
                              sv.u, sv.v, 0.0f, 0.0f});
      }
    }
    if (has_effect) {
      for (std::uint32_t i = 0; i < steps && !has_prims; ++i) {
        if (!effect_player.Advance(camera.view, player, projection, 0.0f, -80.0f, 0.0f)) {
          effect_player.Reset();
          effect_player.Advance(camera.view, player, projection, 0.0f, -80.0f, 0.0f);
        }
        effect_player.Step();
      }
      for (const render::EffectTriangle& t : effect_player.Draw()) {
        render::ScenePrimitive s;
        s.id = static_cast<std::uint32_t>(spans.size());
        s.texture = effect_texture(static_cast<std::size_t>(t.layer), t.texture);
        s.src_blend = t.src_blend;
        s.dest_blend = t.dest_blend;
        s.mode = t.mode;
        s.type = 4;
        s.vertex_count = 3;
        s.rhw0 = t.vertices[0].rhw;
        s.rhw1 = t.vertices[1].rhw;
        lists.Add(s, render::scene_flags::kTranslucent | render::scene_flags::kStageMode);
        spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), 3);
        for (const render::ScreenVertex& sv : t.vertices)
          vertices.push_back({sv.x, sv.y, sv.z, sv.rhw, sv.diffuse, render::VertexFog(sv.rhw, fog),
                              sv.u, sv.v, 0.0f, 0.0f});
      }
    }
    std::vector<render::SceneCall> calls;
    if (setup_projection) {
      calls = render::SetupFog(fog, fog_on, true).calls;
      setup_projection = false;
    }
    const auto scene = render::DrawScene(lists, {fog_on, true, true}, bound);
    calls.insert(calls.end(), scene.begin(), scene.end());
    draws = render::RecordDraws(state, calls);
  };
  // Text over the finished scene, as GDI drew it on the back buffer before Present: the runs
  // drawn into a layer the screen's size, then the layer as one last draw of only the texels
  // the glyphs cover (an alpha test), outside the scene's device state.
  const auto glyphs = render::GlyphRenderer::Create(1);  // iRO's service language
  std::vector<std::uint32_t> text_layer;
  const auto draw_text = [&](const std::vector<render::TextRun>& runs,
                             std::vector<render::DrawRecord>& draws) {
    if (runs.empty() || !glyphs) return;
    text_layer.assign(static_cast<std::size_t>(o.width) * static_cast<std::size_t>(o.height), 0);
    for (const render::TextRun& run : runs) glyphs->Draw(run, o.width, o.height, text_layer);
    device->SetTexture(kTextTextureId, static_cast<std::uint32_t>(o.width),
                       static_cast<std::uint32_t>(o.height), text_layer);
    render::DrawRecord d;
    d.primitive = static_cast<std::uint32_t>(spans.size());
    spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), 4);
    const auto w = static_cast<float>(o.width);
    const auto h = static_cast<float>(o.height);
    for (const auto& [x, y] : {std::pair{0.0f, 0.0f}, {w, 0.0f}, {0.0f, h}, {w, h}})
      vertices.push_back({x, y, 0.0f, 1.0f, 0xffffffff, 0, x / w, y / h, 0.0f, 0.0f});
    d.pipeline.z_test = false;
    d.pipeline.z_write = false;
    d.pipeline.cull = 1;                     // NONE
    d.pipeline.primitive = 5;                // TRIANGLESTRIP
    d.uniforms.stage0_color = {2, 2, 0, 0};  // SELECTARG1(TEXTURE)
    d.uniforms.stage0_alpha = {2, 2, 0, 0};
    d.uniforms.stage1_color = {1, 0, 0, 0};  // DISABLE
    d.uniforms.stage1_alpha = {1, 0, 0, 0};
    d.uniforms.alpha_test = {1, 5, 0, 0};  // GREATER than 0
    d.textures = {kTextTextureId, 0};
    d.samplers[0] = {1, 1, 3, 3};  // POINT, CLAMP
    draws.push_back(d);
  };
  render::FrameCount frame_count;
  // CMode::DrawCursor: cursors.act's arrow (action 0) at the mouse, drawn straight onto the
  // screen after the scene through the same device state. InitAudioAndDevice leaves the
  // device filtering by points (C3dDevice+0x1e4), so the draw sets no filters.
  ActorSprite cursor_sprite;
  const bool has_cursor =
      o.screenshot.empty() &&
      LoadActorSprite(files, "cursors", *device, kCursorTextureIdBase, cursor_sprite);
  render::Cursor cursor;
  const auto draw_cursor = [&](std::uint32_t now, bool button, std::int32_t mouse_x,
                               std::int32_t mouse_y, std::vector<render::DrawRecord>& draws) {
    if (!has_cursor) return;
    const render::ActFrameDraw at =
        render::StepCursor(cursor, cursor_sprite.act, now, button, mouse_x, mouse_y);
    const render::ActImageTextures textures = [&](std::int32_t type, std::int32_t image) {
      const auto i = static_cast<std::size_t>(image);
      if (type == 0) return render::ActImageTexture{cursor_sprite.ids8[i], cursor_sprite.rects8[i]};
      return render::ActImageTexture{cursor_sprite.ids32[i], {}};
    };
    render::ScreenSprites sprites;
    render::DrawActFrame(at, cursor_sprite.act, cursor_sprite.sprite, true, textures, bound,
                         sprites);
    for (render::SceneCall& call : sprites.calls) {
      if (call.kind != render::SceneCall::Kind::kDraw) continue;
      const auto& quad = sprites.quads[call.value];
      call.value = static_cast<std::uint32_t>(spans.size());
      spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), 4);
      for (const render::ScreenVertex& sv : quad)
        vertices.push_back(
            {sv.x, sv.y, sv.z, sv.rhw, sv.diffuse, sv.specular, sv.u, sv.v, 0.0f, 0.0f});
    }
    const std::vector<render::DrawRecord> more = render::RecordDraws(state, sprites.calls);
    draws.insert(draws.end(), more.begin(), more.end());
  };
  const render::VertexLookup lookup = [&](std::uint32_t id) {
    return std::span<const render::GpuVertex>(vertices.data() + spans[id].first, spans[id].second);
  };
  const auto width = static_cast<std::uint32_t>(o.width);
  const auto height = static_cast<std::uint32_t>(o.height);
  // CGameMode::OnInit's clear colour (00623c0c): two sky blues for three maps, black
  // otherwise.
  std::uint32_t clear = 0;
  if (o.map == "yuno") clear = 0xff99ccff;
  if (o.map == "gonryun" || o.map == "gon_dun02") clear = 0xff6699cc;

  if (!o.screenshot.empty()) {
    std::vector<render::DrawRecord> draws;
    frame(1, actor_object.start_time, draws);
    std::vector<render::TextRun> runs;
    if (o.fps) {
      render::StartFrameCount(frame_count, actor_object.start_time);
      render::DrawFrameRate(frame_count, actor_object.start_time, 0, o.width, o.height, runs);
    }
    if (o.stamp) render::DrawScreenshotStamp(LocalNow(), o.height, runs);
    draw_text(runs, draws);
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
  render::StartFrameCount(frame_count, static_cast<std::uint32_t>(SDL_GetTicks()));
  cursor.start = static_cast<std::uint32_t>(SDL_GetTicks());  // CMode::CMode
  if (has_cursor) SDL_HideCursor();
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
    has_mouse = true;
    mouse_cell_x = drag.mouse_x;
    mouse_cell_y = drag.mouse_y;
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
    const auto now = static_cast<std::uint32_t>(SDL_GetTicks());
    frame(static_cast<std::uint32_t>(n), now, draws);
    draw_cursor(now, (buttons & SDL_BUTTON_LMASK) != 0, drag.mouse_x, drag.mouse_y, draws);
    std::vector<render::TextRun> runs;
    // No textures wait to be deleted: ragview keeps them all.
    if (o.fps) render::DrawFrameRate(frame_count, now, 0, o.width, o.height, runs);
    draw_text(runs, draws);
    if (!device->DrawFrame(draws, lookup, width, height, clear)) {
      std::fprintf(stderr, "ragview: %s\n", device->error().c_str());
      return 1;
    }
    ++frame_count.frames;  // SceneRenderer::Present
    if (o.frames > 0 && n >= o.frames) running = false;
  }
  device.reset();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
