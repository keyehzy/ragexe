// ragview: a map's ground drawn as the client draws it (docs/render/gpu.md).
//
//   ragview [--install DIR] [--size W H] [--at X Z | --cell X Y] [--yaw DEGREES] [--frames N]
//           [--no-fog] [--actor SPRITE | --job JOB | --player JOB SEX HAIR [--hair-color N]
//           [--clothes-color N] [--headgear TOP MID BOTTOM] [--weapon KIND] [--shield ID]]
//           [--action BASE] [--effect NAME | --effect-id ID [--minimal-effects]
//           [--effect-level N]] [--effect-step N] [--fps] [--stamp] [--screenshot FILE.png
//           [--digest]] MAP
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
// --cell places the player in the centre of the attribute map's cell (X, Y) instead of at --at,
// as CGameMode::GetCellCentre (00511de0) does.
// --screenshot renders one frame offscreen to a PNG and exits; otherwise a window opens,
// and Escape or --frames N closes it. --digest also prints the FNV-1a hashes of what the
// frame gives the GPU: the textures' texels, and the draws' states and vertices.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "core/frame_timer.h"
#include "core/x87.h"
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
#include "render/world_frame.h"
#include "render/world_load.h"
#include "render/world_scene.h"
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
#include "world/actor_state.h"
#include "world/actor_walk.h"
#include "world/path_finder.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

using namespace ragexe;

// The scene's textures take FrameTextures' ids, from 1; the screen's draws take these.
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
  std::optional<std::array<std::int32_t, 2>> cell;
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
  bool digest = false;
};

// FNV-1a over 64 bits.
struct Digest {
  std::uint64_t h = 0xcbf29ce484222325;
  void Add(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      h ^= (v >> (i * 8)) & 0xff;
      h *= 0x100000001b3;
    }
  }
  void Add(float f) { Add(std::bit_cast<std::uint32_t>(f)); }
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
               "usage: ragview [--install DIR] [--size W H] [--at X Z | --cell X Y]\n"
               "               [--yaw DEGREES]\n"
               "               [--frames N] [--no-fog] [--actor SPRITE | --job JOB |\n"
               "               --player JOB SEX HAIR\n"
               "               [--hair-color N] [--clothes-color N] [--headgear TOP MID BOTTOM]\n"
               "               [--weapon KIND] [--shield ID]] [--action BASE]\n"
               "               [--effect NAME | --effect-id ID [--minimal-effects]\n"
               "               [--effect-level N]] [--effect-step N] [--fps] [--stamp]\n"
               "               [--screenshot FILE.png [--digest]] MAP\n");
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

// A texture made for a sprite's image of width × height texels: its id, and the share of
// the texture the image covers.
struct SpriteTexture {
  std::uint32_t id = 0;
  float u = 1.0f;
  float v = 1.0f;
};
using SpriteTextureMaker = std::function<SpriteTexture(std::uint32_t width, std::uint32_t height,
                                                       std::vector<std::uint32_t> argb)>;

// A sprite and its actions, with each image uploaded as the texture cache would hold it:
// an 8-bit image decoded through the palette into a slot at the corner of a 256 x 256
// page (CTexture::DrawRleImage, index 0 clear), a 32-bit image in a texture of its own,
// spanning 0 to its uv scale (docs/render/actor.md#the-quad).
struct ActorSprite {
  res::Sprite sprite;
  res::Act act;
  std::vector<std::uint32_t> ids8, ids32;
  std::vector<render::SpriteTextureRect> rects8, rects32;
};

bool LoadActorSprite(const res::DataFiles& files, const std::string& name,
                     const SpriteTextureMaker& make, ActorSprite& out,
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
    out.ids8.push_back(make(kSpritePage, kSpritePage, std::move(page)).id);
    const auto page_size = static_cast<float>(kSpritePage);
    out.rects8.push_back({0.0f, 0.0f, static_cast<float>(image.width) / page_size,
                          static_cast<float>(image.height) / page_size});
  }
  for (const res::SpriteImage32& image : out.sprite.images32) {
    const auto w = static_cast<std::uint32_t>(std::max<std::int16_t>(image.pixel_width, 1));
    const auto h = static_cast<std::uint32_t>(std::max<std::int16_t>(image.pixel_height, 1));
    std::vector<std::uint32_t> pixels = image.pixels;
    pixels.resize(static_cast<std::size_t>(w) * h, 0);
    const SpriteTexture t = make(w, h, std::move(pixels));
    out.ids32.push_back(t.id);
    out.rects32.push_back({0.0f, 0.0f, t.u, t.v});
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
    } else if (a == "--cell" && next(2)) {
      const std::int32_t x = ParseInt(argv[++i], -1);
      o.cell = {{x, ParseInt(argv[++i], -1)}};
    } else if (a == "--digest") {
      o.digest = true;
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
  std::vector<std::unique_ptr<res::GrfArchive>> archives;
  res::DataFiles files(o.install);
  if (!res::OpenInstallArchives(o.install, archives, files, &error)) {
    std::fprintf(stderr, "ragview: %s\n", error.c_str());
    return 1;
  }
  std::vector<std::uint8_t> bytes;
  if (o.cell) {
    res::World rsw;
    res::AttrMap gat;
    if (!Load(files, "data\\" + o.map + ".rsw", bytes) ||
        !res::DecodeWorld(bytes, true, rsw, &error) || !Load(files, "data\\" + rsw.attr, bytes) ||
        !res::DecodeAttrMap(bytes, gat, &error)) {
      std::fprintf(stderr, "ragview: %s: %s\n", o.map.c_str(), error.c_str());
      return 1;
    }
    const std::array<float, 2> centre = render::CellCentre(gat, (*o.cell)[0], (*o.cell)[1]);
    o.at_x = centre[0];
    o.at_z = centre[1];
  }
  // The world as CWorld::LoadWorld leaves it, its models loaded from where the player stands
  // (docs/render/model.md#loading).
  render::WorldScene scene;
  if (!render::LoadWorldScene(files, o.map, o.at_x, o.at_z, scene, &error)) {
    std::fprintf(stderr, "ragview: %s\n", error.c_str());
    return 1;
  }
  const res::World& world = scene.world;
  const res::Ground& ground = scene.ground;
  const res::AttrMap& attr = scene.attr;
  // ClientState's indoor check (00598ba0): the map's name in the sorted indoorRswTable.txt.
  std::vector<std::string> indoor_maps;
  const bool indoor = res::LoadFields(files, res::kIndoorRswTable, true, indoor_maps) &&
                      std::binary_search(indoor_maps.begin(), indoor_maps.end(), o.map + ".rsw");
  // The world entered with the player standing on the ground at (X, Z) and the camera placed
  // as CGameMode places it (docs/render/frame.md), at the window's size.
  render::WorldFrameState frame_state(files);
  frame_state.projection = render::SetSize(o.width, o.height);
  render::EnterWorld(scene, o.at_x, o.at_z, indoor, frame_state);
  if (o.turned) {
    frame_state.camera.yaw = o.yaw;
    frame_state.camera.target_yaw = o.yaw;
  }
  // CGameMode::OnInit's fog (00516570): the map's fogParameterTable.txt record, when the
  // fog flag is on, then SetFogEnabled. The device has vertex fog, so every primitive gets its
  // fog factor.
  res::LineRecords fog_table;
  const bool fog_on = o.fog && res::LoadFogParameters(files, fog_table) &&
                      render::FindMapFog(fog_table, o.map + ".rsw", frame_state.fog);
  render::SetFogEnabled(frame_state, fog_on);
  const render::GroundMesh& mesh = scene.mesh;
  render::Camera& camera = frame_state.camera;
  render::Vec3& player = frame_state.player;
  const render::Projection& projection = frame_state.projection;

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
  // Every texture the GPU is given, through the digest.
  Digest texture_digest;
  const auto set_texture = [&](std::uint32_t id, std::uint32_t w, std::uint32_t h,
                               std::span<const std::uint32_t> argb) {
    for (const std::uint32_t v : {id, w, h}) texture_digest.Add(v);
    for (const std::uint32_t v : argb) texture_digest.Add(v);
    device->SetTexture(id, w, h, argb);
  };
  std::fprintf(stderr, "ragview: %s, %s (%d x %d cells, %s, %s), on %s\n", o.map.c_str(),
               world.ground.c_str(), ground.width, ground.height, indoor ? "indoor" : "outdoor",
               fog_on ? "fog" : "no fog", device->driver());

  // The textures the world made as it was entered, in their order: each named one as
  // TextureCache makes it, from data\texture\, and the lightmap pages. Those the frames make
  // later, the water's, the cell cursor's and the effects', are uploaded after the frame
  // that made them; the sprites' as they are made.
  std::size_t uploaded = 0;
  const auto upload_textures = [&] {
    for (; uploaded < frame_state.textures.size(); ++uploaded) {
      const auto id = static_cast<std::uint32_t>(uploaded + 1);
      const std::string& name = frame_state.textures.at(id).name;
      if (name.empty()) continue;
      res::Bitmap image;
      std::string ignored;
      std::uint32_t w = 1, h = 1;
      std::vector<std::uint32_t> texels{0};
      if (Load(files, "data\\texture\\" + name, bytes) &&
          res::DecodeBitmap(name, bytes, image, &ignored))
        texels = ClientTexels(image, w, h);
      set_texture(id, w, h, texels);
    }
  };
  for (std::size_t p = 0; p < mesh.atlas.pages.size(); ++p) {
    std::vector<std::uint32_t> page(mesh.atlas.pages[p].size());
    for (std::size_t i = 0; i < page.size(); ++i) {
      const auto t =
          render::ConvertTexel(render::TextureFormat::kArgb4444, mesh.atlas.pages[p][i], false);
      page[i] = render::ExpandTexel(render::TextureFormat::kArgb4444, t.value_or(0));
    }
    set_texture(frame_state.lightmap_pages[p], 256, 256, page);
  }
  upload_textures();
  std::fprintf(stderr, "ragview: %zu of %zu models, %zu files, %zu textures\n", scene.models.size(),
               world.models.size(), scene.model_files.size(), frame_state.textures.size());
  // A sprite's texture, made as C3dDevice::CreateTexture makes one and filled at its corner.
  const SpriteTextureMaker sprite_texture = [&](std::uint32_t w, std::uint32_t h,
                                                std::vector<std::uint32_t> argb) {
    const std::uint32_t id = frame_state.textures.Create(w, h, false);
    const auto fitted = render::FitTextureSize(w, h, {});
    const std::uint32_t fw = fitted ? fitted->width : 1;
    const std::uint32_t fh = fitted ? fitted->height : 1;
    std::vector<std::uint32_t> texels(static_cast<std::size_t>(fw) * fh, 0);
    for (std::uint32_t y = 0; y < std::min(h, fh); ++y)
      for (std::uint32_t x = 0; x < std::min(w, fw); ++x)
        texels[static_cast<std::size_t>(y) * fw + x] = argb[static_cast<std::size_t>(y) * w + x];
    set_texture(id, fw, fh, texels);
    const render::UvScale uv = frame_state.textures.at(id).uv;
    return SpriteTexture{id, uv.u, uv.v};
  };

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
    has_actor = LoadActorSprite(files, render::PlayerBodyName(o.job, o.sex), sprite_texture, actor,
                                clothes_colored ? &clothes_palette.palette() : nullptr) &&
                LoadActorSprite(files, render::PlayerHeadName(o.hair, o.sex), sprite_texture,
                                layers[render::kPlayerHead],
                                hair_colored ? &hair_palette.palette() : nullptr) &&
                Load(files, "data\\imf\\" + render::PlayerImfName(o.job, o.sex), bytes) &&
                res::DecodeImf(bytes, imf, &ignored);
    if (!has_actor) std::fprintf(stderr, "ragview: no player sprites for job %d\n", o.job);
    // The headgear, the weapon and its glow, and the shield; a layer whose names are missing
    // stays empty and is not drawn.
    const auto dress = [&](int layer, const render::PlayerSpriteNames& names) {
      if (names.act.empty() || names.sprite.empty()) return;
      if (!LoadActorSprite(files, names.sprite, sprite_texture,
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
    has_actor =
        !names.sprite.empty() && LoadActorSprite(files, names.sprite, sprite_texture, actor);
    if (!has_actor) std::fprintf(stderr, "ragview: no sprite for job %d\n", o.npc_job);
  } else if (o.actor != "none") {
    has_actor = LoadActorSprite(files, o.actor, sprite_texture, actor);
    if (!has_actor) std::fprintf(stderr, "ragview: no sprite %s\n", o.actor.c_str());
  }
  has_actor = has_actor && LoadActorSprite(files, "shadow", sprite_texture, shadow);
  render::PlayerHeadOffset head_offset;
  // The actor, a CGameActor: its job picks how its vf04 rounds the direction.
  world::GameActor actor_state;
  actor_state.job = o.player ? o.job : o.npc_job;
  render::RenderObject& actor_object = actor_state.object;
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

  render::SpriteScales sprite_scales;
  render::SetSpriteScalesForSize(projection, sprite_scales);
  const render::SceneGraph& graph = scene.graph;
  render::FixedFunctionState state;
  std::vector<render::FrameDraw> frame_draws;
  std::vector<render::GpuVertex> vertices;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;  // first vertex, count

  // The effect: CRagEffect playing the .str, from the world steps the frame timer counts
  // (ProcessGenericType's offset, 80 pixels up). Each texture is made the first time a
  // triangle asks for it: effect\ and its name, .bmp added when it has none, and "1.bmp"
  // past a layer's names (GetEffectEntryTexture, 004289a0).
  // --effect-id: the world's effect objects for the primitives, and the .str the id names,
  // which the effect plays as its case of vf01 does, if at all.
  render::EffectWorld prim_world;
  // The client's one CRT rand serves the shake and the effects alike.
  frame_state.rand = &prim_world.rand();
  const bool has_prims = o.effect_id >= 0;
  // The sprites effects load, the particle first; each drawn quad names the one it is of.
  std::list<ActorSprite> effect_sprites;
  std::map<const res::Sprite*, const ActorSprite*> sprite_of;
  if (has_prims) {
    std::vector<const char*> names = {render::kParticleSprite};
    names.insert(names.end(), render::kEffectSprites.begin(), render::kEffectSprites.end());
    for (const char* name : names) {
      ActorSprite& loaded = effect_sprites.emplace_back();
      if (!LoadActorSprite(files, name, sprite_texture, loaded)) {
        effect_sprites.pop_back();
        continue;
      }
      prim_world.SetSprite(name, {&loaded.act, &loaded.sprite});
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
  const auto effect_texture = [&](std::size_t layer, std::int32_t index) {
    const res::StrLayer& l = effect_str.layers[layer];
    std::string name = static_cast<std::size_t>(index) < l.textures.size()
                           ? l.textures[static_cast<std::size_t>(index)]
                           : std::string("1.bmp");
    if (name.find('.') == std::string::npos) name += ".bmp";
    return frame_state.textures.Get("effect\\" + name);
  };
  bool effect_started = false;

  // The mouse's point for the cell cursor, none in a screenshot.
  bool has_mouse = false;
  std::int32_t mouse_cell_x = 0, mouse_cell_y = 0;
  // The cell the mouse picked last frame, where a left click sends the actor.
  bool has_pick = false;
  std::int32_t pick_x = 0, pick_z = 0;

  // The world's objects, as CView::DrawWorld has each draw itself after the cell cursor: the
  // actor and the effect.
  const auto draw_objects = [&](render::FrameLists& lists, std::uint32_t now) {
    render::StepSpriteScales(camera.pitch, camera.distance, projection, sprite_scales);
    // A sprite quad through SceneRenderer 0040e980.
    const auto add = [&](const render::SpriteQuad& q, const ActorSprite& from) {
      const bool is32 = q.image_type != 0;
      const auto i = static_cast<std::size_t>(q.image);
      if (i >= (is32 ? from.ids32.size() : from.ids8.size())) return;
      render::ScenePrimitive s;
      s.texture = is32 ? from.ids32[i] : from.ids8[i];
      lists.AddQuad(s, render::SpriteQuadVertices(q, is32 ? from.rects32[i] : from.rects8[i]),
                    q.flags);
    };
    // An effect's triangle, in the primitives the effect keeps.
    const auto add_triangle =
        [&](std::uint32_t texture, std::uint32_t src_blend, std::uint32_t dest_blend,
            std::uint32_t mode, const std::array<render::ScreenVertex, 3>& v, std::uint32_t flags) {
          render::ScenePrimitive s;
          s.id = frame_state.pools.NewOwned();
          s.texture = texture;
          s.src_blend = src_blend;
          s.dest_blend = dest_blend;
          s.mode = mode;
          s.type = 4;
          lists.Add(s, {v.begin(), v.end()}, {}, flags);
        };
    // The actor's vf03: its shadow, then its body.
    if (has_actor) {
      actor_object.position = player;
      world::UpdateGameActorFrame(actor_state, actor.act, camera.yaw, now);
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
      for (const render::PrimTriangle& t : drawing.triangles)
        add_triangle(frame_state.textures.Get(t.texture), t.src_blend, t.dest_blend, t.mode,
                     t.vertices, t.flags);
    }
    if (has_effect) {
      for (std::uint32_t i = 0; i < steps && !has_prims; ++i) {
        if (!effect_player.Advance(camera.view, player, projection, 0.0f, -80.0f, 0.0f)) {
          effect_player.Reset();
          effect_player.Advance(camera.view, player, projection, 0.0f, -80.0f, 0.0f);
        }
        effect_player.Step();
      }
      for (const render::EffectTriangle& t : effect_player.Draw())
        add_triangle(effect_texture(static_cast<std::size_t>(t.layer), t.texture), t.src_blend,
                     t.dest_blend, t.mode, t.vertices,
                     render::scene_flags::kTranslucent | render::scene_flags::kStageMode);
    }
  };

  const auto frame = [&](std::uint32_t now, std::vector<render::DrawRecord>& draws) {
    // CGameMode::method_005159d0 keeps the attribute cell under the mouse, and
    // CView::DrawWorld draws the cursor over it after the visible nodes.
    std::int32_t picked_x = 0, picked_z = 0;
    has_pick = has_mouse && render::PickCell(projection, camera, mouse_cell_x, mouse_cell_y, graph,
                                             attr, picked_x, picked_z);
    if (has_pick) {
      render::PlaceCellCursor(frame_state.cursor, attr, picked_x, picked_z);
      pick_x = picked_x;
      pick_z = picked_z;
    }
    const std::vector<render::SceneCall> calls =
        render::DrawWorldFrame(scene, frame_state, now, frame_draws,
                               [&](render::FrameLists& lists) { draw_objects(lists, now); });
    upload_textures();
    // Each draw with the vertices it sent.
    draws = render::RecordDraws(state, calls);
    vertices.clear();
    spans.clear();
    for (std::size_t i = 0; i < draws.size(); ++i) {
      const render::FrameDraw& d = frame_draws[i];
      draws[i].primitive = static_cast<std::uint32_t>(spans.size());
      spans.emplace_back(static_cast<std::uint32_t>(vertices.size()), d.count);
      const std::size_t stride = d.count == 0 ? 0 : d.words.size() / d.count;
      const auto f = [&](std::size_t k) { return std::bit_cast<float>(d.words[k]); };
      for (std::size_t k = 0; k < d.words.size(); k += stride) {
        const bool two = stride == 10;
        vertices.push_back({f(k), f(k + 1), f(k + 2), f(k + 3), d.words[k + 4], d.words[k + 5],
                            f(k + 6), f(k + 7), two ? f(k + 8) : 0.0f, two ? f(k + 9) : 0.0f});
      }
    }
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
    set_texture(kTextTextureId, static_cast<std::uint32_t>(o.width),
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
  std::uint32_t cursor_texture = kCursorTextureIdBase;
  const SpriteTextureMaker screen_texture = [&](std::uint32_t w, std::uint32_t h,
                                                std::vector<std::uint32_t> argb) {
    set_texture(cursor_texture, w, h, argb);
    return SpriteTexture{cursor_texture++};
  };
  const bool has_cursor =
      o.screenshot.empty() && LoadActorSprite(files, "cursors", screen_texture, cursor_sprite);
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
    render::DrawActFrame(at, cursor_sprite.act, cursor_sprite.sprite, true, textures,
                         frame_state.bound, sprites);
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
    frame(actor_object.start_time, draws);
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
    if (o.digest) {
      Digest d;
      for (const render::DrawRecord& r : draws) {
        const render::PipelineKey& k = r.pipeline;
        for (const std::uint32_t v :
             {std::uint32_t{k.blend}, k.src_blend, k.dest_blend, std::uint32_t{k.z_test},
              std::uint32_t{k.z_write}, k.z_func, k.cull, k.primitive, std::uint32_t{k.two_coords}})
          d.Add(v);
        const render::StageUniforms& u = r.uniforms;
        for (const auto* a :
             {&u.stage0_color, &u.stage0_alpha, &u.stage1_color, &u.stage1_alpha, &u.alpha_test})
          for (const std::int32_t v : *a) d.Add(static_cast<std::uint32_t>(v));
        for (const float v : u.fog_color) d.Add(v);
        for (std::size_t t = 0; t < 2; ++t) {
          const render::SamplerKey& sk = r.samplers[t];
          for (const std::uint32_t v :
               {r.textures[t], sk.mag_filter, sk.min_filter, sk.address_u, sk.address_v})
            d.Add(v);
        }
        for (const render::GpuVertex& v : lookup(r.primitive)) {
          for (const float f : {v.x, v.y, v.z, v.rhw}) d.Add(f);
          d.Add(v.diffuse);
          d.Add(v.specular);
          for (const float f : {v.u0, v.v0, v.u1, v.v1}) d.Add(f);
        }
      }
      std::printf("draws %zu %016llx textures %016llx\n", draws.size(),
                  static_cast<unsigned long long>(d.h),
                  static_cast<unsigned long long>(texture_digest.h));
    }
    return 0;
  }

  // The actor's moves: a left click on a cell gives it message 0 from where it stands, the
  // timeGetTime standing in for the server's time; each frame walks it until it stands.
  world::PathFinder finder;
  finder.SetAttrMap(&attr);
  const world::StateContext state_context{&attr, &actor.act, 0.0f, 1, nullptr};
  const auto move_actor = [&](std::uint32_t now) {
    const auto [cell_x, cell_z] = world::AttrCellAt(attr, player[0], player[2]);
    // The sixteenths of the cell the actor stands in.
    const auto [centre_x, centre_z] = render::CellCentre(attr, cell_x, cell_z);
    const float cs = static_cast<float>(attr.cell_size);
    const auto sixteenth = [&](float at, float centre) {
      return std::clamp(core::Ftol((at - centre + cs * 0.5f) / cs * 16.0f), 0, 15);
    };
    world::StateContext context = state_context;
    context.camera_yaw = camera.yaw;
    actor_object.position = player;
    world::MoveGameActor(actor_state, finder,
                         {now, cell_x, cell_z, sixteenth(player[0], centre_x),
                          sixteenth(player[2], centre_z), pick_x, pick_z},
                         now, context, now);
  };
  const auto walk_actor = [&](std::uint32_t now) {
    if (actor_state.state != 1) return;
    world::StateContext context = state_context;
    context.camera_yaw = camera.yaw;
    if (world::ProcessWalk(actor_state, attr, actor.act, camera.yaw, now, now) ==
        world::WalkResult::kArrived)
      world::SetGameActorState(actor_state, 0, context, now);
    player = actor_object.position;
  };

  // The client's camera input, once a frame: holding the right button is its drag state,
  // which turns, with Shift pitches and with Ctrl zooms; a right double-click is its reset
  // state. The arrow keys walk the player a cell, and a left click walks the actor to the
  // cell under the mouse.
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
      if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT &&
          has_actor && has_pick)
        move_actor(static_cast<std::uint32_t>(SDL_GetTicks()));
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
      render::DragCamera(camera, frame_state.control, drag);
    } else if ((buttons & SDL_BUTTON_RMASK) != 0) {
      drag.kind = render::CameraDrag::Kind::kDrag;
      render::DragCamera(camera, frame_state.control, drag);
    }
    drag.last_x = drag.mouse_x;
    drag.last_y = drag.mouse_y;
    std::vector<render::DrawRecord> draws;
    const auto now = static_cast<std::uint32_t>(SDL_GetTicks());
    walk_actor(now);
    frame(now, draws);
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
