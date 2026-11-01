// ragres: list, extract and check the game's archives.
//
//   ragres list ARCHIVE                    one line per entry: size, stored, flags, offset, name
//   ragres extract ARCHIVE DIR [NAME...]   writes the files (all, or the named ones) under DIR;
//                                          a NAME of "*.bmp" selects every .bmp
//   ragres decode-images DIR FILE...       decodes images as CBitmapRes does into DIR/<n>.argb
//   ragres decode-palettes FILE...         decodes .pal files as CPaletteRes does
//   ragres decode-sprites [--halve-x] [--halve-y] FILE...
//                                          decodes .spr files as CSprRes does, as hashes
//   ragres decode-acts [--halve-x] [--halve-y] FILE...
//                                          decodes .act files as CActRes does, every field
//   ragres decode-gats FILE...             decodes .gat files as C3dAttr does, as hashes
//   ragres decode-gnds FILE...             decodes .gnd files as CGndRes does, as hashes
//   ragres find-paths GAT QUERIES          runs the path finder on a .gat's cells, one
//                                          query of QUERIES a line, and prints each result
//   ragres build-grounds [--lightmaps] LIGHT FILE...
//                                          builds .gnd files as C3dGround15::Build does,
//                                          with LIGHT nine floats: direction, diffuse,
//                                          ambient; prints hashes
//   ragres decode-rsws [--no-lightmaps] FILE...
//                                          decodes .rsw files as C3dWorldRes does
//   ragres load-world [--raw] INSTALL MAP CELL_X CELL_Y
//                                          loads a world as CWorld::LoadWorld does, the
//                                          player in that cell, from data.grf with the
//                                          .gpf patches over it, as hashes (--raw: each
//                                          model's fields and nodes)
//   ragres draw-frame [--raw] INSTALL MAP CELL_X CELL_Y
//                                          draws three frames of a world as CGameMode::OnFrame
//                                          does, the player and the cell cursor in that
//                                          cell, and prints the device calls (--raw: each
//                                          draw's vertices, not their hash)
//   ragres decode-rsms [--single] FILE...  decodes .rsm files as C3dModelRes does, with
//                                          --single at the x87's single precision
//   ragres decode-rsxs FILE...             decodes .rsx files as C3dAnimModelRes does
//   ragres decode-imfs FILE...             decodes .imf files as CImfRes does
//   ragres decode-rmas FILE...             decodes .rma files as CRmaRes does
//   ragres decode-strs FILE...             decodes .str files as CEZeffectRes does
//   ragres decode-ebms FILE...             decodes .ebm files as CEmblemRes does, with the
//                                          bytes it inflates
//   ragres decode-wavs FILE...             decodes .wav files as CWave does
//   ragres decode-name-pairs FILE...       decodes ClientState's name-pair tables
//                                          (mapNameTable.txt and the like)
//   ragres decode-number-records FILE...   decodes LevelUseskillSpAmount.txt and
//                                          skillAfterDelay.txt as ClientState does
//   ragres decode-line-records FILE...     decodes skillDescTable.txt as ClientState does
//   ragres decode-fields [--sorted] FILE... decodes msgStringTable.txt, or with --sorted
//                                          indoorRswTable.txt, as ClientState does
//   ragres decode-fog-parameters FILE...   decodes fogParameterTable.txt as ClientState does
//   ragres decode-xml FILE...              parses XML files as XmlDocument does
//   ragres decode-clientinfo [--connection N] FILE...
//                                          reads clientinfo.xml files as LoadClientInfo
//                                          does, then the Nth connection as the login
//                                          screen would
//   ragres decode-tips FILE...             reads tip-of-the-day files as TipOfTheDayList does
//   ragres decode-manner FILE [PROBES]     reads manner.txt as MannerFilter does, then tests
//                                          each line of PROBES with IsClean
//   ragres decode-item-tables DIR [--korea] [--code-page N]
//                                          loads the item and card tables from DIR's data
//                                          folder as LoadItemTables does
//   ragres check ARCHIVE...                reads every compressed entry and checks its size;
//                                          fails on an entry that cannot be read
//   ragres check-loaders [--expected FILE] ARCHIVE...
//                                          loads every entry through the loader its
//                                          extension selects, and the client's tables
//                                          through theirs, each archive over the ones
//                                          before it (data.grf, then the patches in
//                                          order); with FILE, the failures must be
//                                          exactly its lines
//
// Names are printed and written as stored (CP949) with bytes outside printable ASCII
// written as %XX, until the CP949 conversion arrives.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "render/fog.h"
#include "render/ground.h"
#include "render/world_frame.h"
#include "render/world_load.h"
#include "render/world_scene.h"
#include "res/act.h"
#include "res/bitmap.h"
#include "res/client_info.h"
#include "res/client_tables.h"
#include "res/data_files.h"
#include "res/ebm.h"
#include "res/gat.h"
#include "res/gnd.h"
#include "res/grf.h"
#include "res/imf.h"
#include "res/item_tables.h"
#include "res/manner.h"
#include "res/palette.h"
#include "res/resource.h"
#include "res/rma.h"
#include "res/rsm.h"
#include "res/rsw.h"
#include "res/rsx.h"
#include "res/sprite.h"
#include "res/str.h"
#include "res/tips.h"
#include "res/wav.h"
#include "res/xml.h"
#include "world/actor_arrival.h"
#include "world/actor_attack.h"
#include "world/actor_effects.h"
#include "world/actor_hit.h"
#include "world/actor_state.h"
#include "world/actor_update.h"
#include "world/actor_walk.h"
#include "world/actor_window.h"
#include "world/path_finder.h"
#include "world/player_orders.h"

namespace {

using ragexe::res::GrfArchive;
using ragexe::res::GrfEntry;

std::string Escape(std::string_view s) {
  std::string out;
  for (unsigned char c : s) {
    if (c < 0x20 || c >= 0x7f || c == '%' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|') {
      char buf[4];
      std::snprintf(buf, sizeof buf, "%%%02X", c);
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

// The archive name as a relative path under the output directory.
std::filesystem::path OutputPath(std::string_view name) {
  std::filesystem::path path;
  std::size_t start = 0;
  while (start <= name.size()) {
    std::size_t end = name.find_first_of("\\/", start);
    if (end == std::string_view::npos) end = name.size();
    std::string part = Escape(name.substr(start, end - start));
    if (part == "..") part = "%2E%2E";
    if (!part.empty() && part != ".") path /= part;
    start = end + 1;
  }
  return path;
}

std::unique_ptr<GrfArchive> OpenOrReport(const char* path) {
  std::string error;
  auto grf = GrfArchive::Open(path, &error);
  if (!grf) std::fprintf(stderr, "%s: %s\n", path, error.c_str());
  return grf;
}

int List(const char* path) {
  auto grf = OpenOrReport(path);
  if (!grf) return 1;
  for (const GrfEntry& e : grf->entries()) {
    std::printf("%u\t%u\t%u\t%u\t%s\n", e.size, e.stored_size, e.flags, e.offset,
                Escape(e.name).c_str());
  }
  return 0;
}

int Extract(const char* path, const std::filesystem::path& dir,
            const std::vector<std::string>& names) {
  auto grf = OpenOrReport(path);
  if (!grf) return 1;
  std::vector<const GrfEntry*> wanted;
  if (names.empty()) {
    for (const GrfEntry& e : grf->entries()) {
      if (e.compressed()) wanted.push_back(&e);
    }
  } else {
    for (const std::string& n : names) {
      // "*.ext": every compressed entry with that extension, in any case.
      if (n.starts_with("*.")) {
        const std::string ext = ragexe::res::LowerCaseName(n.substr(1));
        for (const GrfEntry& e : grf->entries()) {
          if (e.compressed() && e.key.name.ends_with(ext)) wanted.push_back(&e);
        }
        continue;
      }
      const GrfEntry* e = grf->Find(n);
      if (e == nullptr) {
        std::fprintf(stderr, "%s: not in the archive\n", n.c_str());
        return 1;
      }
      wanted.push_back(e);
    }
  }
  int failed = 0;
  for (const GrfEntry* e : wanted) {
    std::string error;
    auto data = grf->Read(*e, &error);
    if (!data) {
      std::fprintf(stderr, "%s\n", error.c_str());
      ++failed;
      continue;
    }
    const std::filesystem::path out = dir / OutputPath(e->name);
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    std::ofstream f(out, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data->data()),
            static_cast<std::streamsize>(data->size()));
    if (!f) {
      std::fprintf(stderr, "cannot write %s\n", out.string().c_str());
      ++failed;
    }
  }
  std::printf("%zu files, %d failed\n", wanted.size() - static_cast<std::size_t>(failed), failed);
  return failed == 0 ? 0 : 1;
}

int Check(const char* path) {
  auto grf = OpenOrReport(path);
  if (!grf) return 1;
  std::size_t files = 0, other = 0, unaligned = 0, failed = 0, damaged = 0;
  std::uint64_t bytes = 0;
  for (const GrfEntry& e : grf->entries()) {
    if (e.stored_size % 8 != 0) ++unaligned;
    if (!e.compressed()) {
      ++other;
      continue;
    }
    std::string error;
    bool broken = false;
    auto data = grf->Read(e, &error, &broken);
    if (broken) {
      std::printf("%s: %s: the compressed stream breaks off; zero-filled as the client does\n",
                  path, Escape(e.name).c_str());
      ++damaged;
    }
    if (!data || data->size() != e.size) {
      std::fprintf(stderr, "%s: %s\n", path,
                   data ? (Escape(e.name) + ": wrong size").c_str() : error.c_str());
      ++failed;
      continue;
    }
    ++files;
    bytes += data->size();
  }
  const auto duplicates = grf->DuplicateNames();
  for (const std::string& n : duplicates)
    std::fprintf(stderr, "%s: duplicate name %s\n", path, Escape(n).c_str());
  std::printf(
      "%s: version 0x%x, %zu entries: %zu files read (%llu bytes), %zu failed, %zu not compressed, "
      "%zu damaged, %zu stored sizes not a multiple of 8, %zu duplicate names\n",
      path, grf->version(), grf->entries().size(), files, static_cast<unsigned long long>(bytes),
      failed, other, damaged, unaligned, duplicates.size());
  return failed == 0 ? 0 : 1;
}

// The whole file, or nothing when it cannot be read.
std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(in.tellg()));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!in) return {};
  return bytes;
}

// Decodes image files as CBitmapRes does and writes DIR/<n>.argb for the n-th file: width,
// height and the alpha flag as 32-bit little-endian values, then the pixels. Prints one
// line per file: n, ok or failed, width, height, alpha flag, error.
int DecodeImages(const std::filesystem::path& dir, const std::vector<std::string>& files) {
  std::filesystem::create_directories(dir);
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    const std::vector<std::uint8_t> bytes = ReadFile(files[n]);
    ragexe::res::Bitmap bitmap;
    std::string error;
    const bool ok = ragexe::res::DecodeBitmap(files[n], bytes, bitmap, &error);
    std::printf("%zu\t%s\t%d\t%d\t%d\t%s\n", n, ok ? "ok" : "failed", bitmap.width, bitmap.height,
                bitmap.has_alpha ? 1 : 0, error.c_str());
    if (!ok) {
      ++failed;
      continue;
    }
    std::ofstream out(dir / (std::to_string(n) + ".argb"), std::ios::binary);
    auto put = [&](std::uint32_t v) {
      const char b[4] = {static_cast<char>(v), static_cast<char>(v >> 8),
                         static_cast<char>(v >> 16), static_cast<char>(v >> 24)};
      out.write(b, 4);
    };
    put(static_cast<std::uint32_t>(bitmap.width));
    put(static_cast<std::uint32_t>(bitmap.height));
    put(bitmap.has_alpha ? 1 : 0);
    for (std::uint32_t pixel : bitmap.pixels) put(pixel);
  }
  return failed == 0 ? 0 : 1;
}

// Decodes .pal files as CPaletteRes does; prints one line per file: n, ok or failed, then
// the 256 values in hex.
int DecodePalettes(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    const std::vector<std::uint8_t> bytes = ReadFile(files[n]);
    ragexe::res::PaletteRes palette;
    const bool ok = palette.LoadFromBytes(bytes);
    std::printf("%zu\t%s", n, ok ? "ok" : "failed");
    if (ok) {
      for (std::uint32_t v : palette.palette()) std::printf("\t%x", v);
    } else {
      ++failed;
    }
    std::printf("\n");
  }
  return failed == 0 ? 0 : 1;
}

std::uint64_t Fnv1a64(std::span<const std::uint8_t> bytes,
                      std::uint64_t h = 0xcbf29ce484222325ull) {
  for (std::uint8_t b : bytes) h = (h ^ b) * 0x100000001b3ull;
  return h;
}

std::uint64_t Fnv1a64(std::span<const std::uint32_t> values) {
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (std::uint32_t v : values) {
    const std::uint8_t le[4] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
                                static_cast<std::uint8_t>(v >> 16),
                                static_cast<std::uint8_t>(v >> 24)};
    h = Fnv1a64(le, h);
  }
  return h;
}

// OpenInstallArchives, reporting a failure.
bool OpenInstall(const std::filesystem::path& install,
                 std::vector<std::unique_ptr<ragexe::res::GrfArchive>>& archives,
                 ragexe::res::DataFiles& files) {
  std::string error;
  if (ragexe::res::OpenInstallArchives(install, archives, files, &error)) return true;
  std::fprintf(stderr, "%s\n", error.c_str());
  return false;
}

// LoadWorldScene with the player in the centre of the attribute cell given; the point it stands
// on in world x and z.
bool LoadSceneAtCell(const ragexe::res::DataFiles& files, const std::string& map,
                     const std::string& cell_x, const std::string& cell_y,
                     ragexe::render::WorldScene& scene, std::array<float, 2>& point) {
  std::string error;
  ragexe::res::World world;
  ragexe::res::AttrMap attr;
  auto bytes = files.Load("data\\" + map + ".rsw");
  if (!bytes || !ragexe::res::DecodeWorld(*bytes, true, world, &error)) {
    std::fprintf(stderr, "%s.rsw: %s\n", map.c_str(), error.c_str());
    return false;
  }
  bytes = files.Load("data\\" + world.attr);
  if (!bytes || !ragexe::res::DecodeAttrMap(*bytes, attr, &error)) {
    std::fprintf(stderr, "%s: %s\n", world.attr.c_str(), error.c_str());
    return false;
  }
  point = ragexe::render::CellCentre(attr, static_cast<std::int32_t>(std::stol(cell_x)),
                                     static_cast<std::int32_t>(std::stol(cell_y)));
  if (!ragexe::render::LoadWorldScene(files, map, point[0], point[1], scene, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return false;
  }
  return true;
}

// Loads a world as CWorld::LoadWorld leaves it, the player in the centre of the cell given,
// and prints "instances n" and "near n", the models placed at once, then per model of CWorld+0x48
// "I k record fields nodes": its .rsw model, the FNV-1a hashes of its fields +0xc0 to +0x1e4 and of
// its nodes depth first (alpha, matrix, box, each face's colours and texture coordinates, its
// ground colours); then "graph n" and per scene graph node depth first "G k box dirty cells
// attribute-cells models", the box its minimum, maximum, centre and half extents hashed, the models
// the instances' indices or "-". With --raw, a model's line holds its fields themselves,
// then "|" and its nodes' values.
int LoadWorld(std::vector<std::string> args) {
  bool raw = false;
  if (!args.empty() && args.front() == "--raw") {
    raw = true;
    args.erase(args.begin());
  }
  if (args.size() != 4) {
    std::fprintf(stderr, "usage: ragres load-world [--raw] INSTALL MAP CELL_X CELL_Y\n");
    return 2;
  }
  const std::filesystem::path install = args[0];
  std::vector<std::unique_ptr<ragexe::res::GrfArchive>> archives;
  ragexe::res::DataFiles files(install);
  if (!OpenInstall(install, archives, files)) return 1;
  const std::string& map = args[1];
  ragexe::render::WorldScene scene;
  std::array<float, 2> point{};
  if (!LoadSceneAtCell(files, map, args[2], args[3], scene, point)) return 1;
  const auto bits = [](float f) { return std::bit_cast<std::uint32_t>(f); };
  std::printf("instances %zu\nnear %zu\n", scene.models.size(), scene.near_count);
  for (std::size_t k = 0; k < scene.models.size(); ++k) {
    const ragexe::render::ModelInstance& m = scene.models[k];
    std::vector<std::uint32_t> fields;
    for (const float f : m.world) fields.push_back(bits(f));
    for (const float f : m.inverse) fields.push_back(bits(f));
    for (const float f : m.centre) fields.push_back(bits(f));
    fields.push_back(static_cast<std::uint32_t>(m.field_12c));
    for (const float f : m.half_extents) fields.push_back(bits(f));
    for (const float f : m.box_centre) fields.push_back(bits(f));
    for (const auto& axis : m.axes)
      for (const float f : axis) fields.push_back(bits(f));
    for (const auto& corner : m.corners)
      for (const float f : corner) fields.push_back(bits(f));
    for (const float f : m.bounds) fields.push_back(bits(f));
    std::vector<std::uint32_t> nodes;
    for (const ragexe::render::ModelNodeInstance& node : m.nodes) {
      nodes.push_back(bits(node.alpha));
      for (const float f : node.matrix) nodes.push_back(bits(f));
      for (const float f : node.box) nodes.push_back(bits(f));
      for (const ragexe::render::FaceShade& face : node.faces) {
        for (const std::uint32_t c : face.colors) nodes.push_back(c);
        for (const float f : face.uv) nodes.push_back(bits(f));
      }
      for (const std::int32_t t : node.tint) nodes.push_back(static_cast<std::uint32_t>(t));
    }
    if (raw) {
      std::printf("I %zu %zu", k, scene.records[k]);
      for (const std::uint32_t v : fields) std::printf(" %08x", v);
      std::printf(" |");
      for (const std::uint32_t v : nodes) std::printf(" %08x", v);
      std::printf("\n");
      continue;
    }
    std::printf("I %zu %zu %016llx %016llx\n", k, scene.records[k],
                static_cast<unsigned long long>(Fnv1a64(fields)),
                static_cast<unsigned long long>(Fnv1a64(nodes)));
  }
  std::printf("graph %zu\n", scene.graph.nodes.size());
  for (std::size_t k = 0; k < scene.graph.nodes.size(); ++k) {
    const ragexe::render::SceneGraphNode& n = scene.graph.nodes[k];
    std::vector<std::uint32_t> box;
    for (const auto* v : {&n.min, &n.max, &n.centre, &n.half})
      for (const float f : *v) box.push_back(bits(f));
    std::string members;
    for (const int i : n.models) members += (members.empty() ? "" : ",") + std::to_string(i);
    std::printf("G %zu %016llx %d %d %d %d %d %d %d %d %d %s\n", k,
                static_cast<unsigned long long>(Fnv1a64(box)), n.dirty ? 1 : 0, n.cells[0],
                n.cells[1], n.cells[2], n.cells[3], n.attr_cells[0], n.attr_cells[1],
                n.attr_cells[2], n.attr_cells[3], members.empty() ? "-" : members.c_str());
  }
  return 0;
}

// Decodes .spr files as CSprRes does, at a detail level, and prints FNV-1a hashes of what
// the client would hold: "F n ok|failed version images images32 palette-hash", then per
// 8-bit image "I width height halved-x halved-y decoded-hash rle-size" and per 32-bit
// image "T width height halved-x halved-y pixel-width pixel-height pixels-hash".
int DecodeSprites(const std::vector<std::string>& args) {
  ragexe::res::SpriteScale scale;
  std::vector<std::string> files;
  for (const std::string& a : args) {
    if (a == "--halve-x") {
      scale.halve_x = true;
    } else if (a == "--halve-y") {
      scale.halve_y = true;
    } else {
      files.push_back(a);
    }
  }
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    const std::vector<std::uint8_t> bytes = ReadFile(files[n]);
    ragexe::res::Sprite sprite;
    std::string error;
    const bool ok = ragexe::res::DecodeSprite(bytes, scale, sprite, &error);
    if (!ok) ++failed;
    std::printf("F %zu %s %x %zu %zu %016llx\n", n, ok ? "ok" : "failed", sprite.version,
                sprite.images.size(), sprite.images32.size(),
                static_cast<unsigned long long>(Fnv1a64(sprite.palette)));
    for (const auto& image : sprite.images) {
      std::printf("I %d %d %d %d %016llx %zu\n", image.width, image.height, image.halved_x,
                  image.halved_y, static_cast<unsigned long long>(Fnv1a64(image.Decode())),
                  image.rle.size());
    }
    for (const auto& image : sprite.images32) {
      std::printf("T %d %d %d %d %d %d %016llx\n", image.width, image.height, image.halved_x,
                  image.halved_y, image.pixel_width, image.pixel_height,
                  static_cast<unsigned long long>(Fnv1a64(image.pixels)));
    }
  }
  return failed == 0 ? 0 : 1;
}

std::uint32_t FloatBits(float f) {
  std::uint32_t u = 0;
  std::memcpy(&u, &f, 4);
  return u;
}

// What made an effect, as tools/path_oracle.py names it: 0 AddSpriteEffectByType, 1
// AddEmotionEffect, 3 ShowNumberEffect, 4 AddMsgSpriteEffect (2 is an effect made before).
std::uint32_t MakerCode(ragexe::world::EffectMaker maker) {
  switch (maker) {
    case ragexe::world::EffectMaker::kSprite: return 0;
    case ragexe::world::EffectMaker::kEmotion: return 1;
    case ragexe::world::EffectMaker::kNumber: return 3;
    case ragexe::world::EffectMaker::kMsgSprite: return 4;
  }
  return 0;
}

// Decodes .act files as CActRes does, taking image sizes from the .spr of the same name
// beside each (decoded at the detail level given), and prints every field: "A n version
// actions events max-items", "a frames" per action, "f header... event items anchors" per
// frame, then "i x y image flags r g b a scale-x scale-y angle type" per item (scales as
// float bits) and "n x y kind" per anchor, "e name-hex" per event, "d delay-bits" per
// action; "A n failed error" when the file does not load.
int DecodeActs(const std::vector<std::string>& args) {
  ragexe::res::SpriteScale scale;
  std::vector<std::string> files;
  for (const std::string& a : args) {
    if (a == "--halve-x") {
      scale.halve_x = true;
    } else if (a == "--halve-y") {
      scale.halve_y = true;
    } else {
      files.push_back(a);
    }
  }
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    std::filesystem::path sprite_path = files[n];
    sprite_path.replace_extension(".spr");
    std::optional<ragexe::res::Sprite> sprite;
    const ragexe::res::ActImageLookup lookup =
        [&](std::int32_t type, std::int32_t index) -> std::optional<ragexe::res::ActImageSize> {
      if (!sprite) {
        sprite.emplace();
        std::string ignored;
        if (!std::filesystem::exists(sprite_path) ||
            !ragexe::res::DecodeSprite(ReadFile(sprite_path), scale, *sprite, &ignored)) {
          *sprite = ragexe::res::Sprite{};
        }
      }
      const auto i = static_cast<std::size_t>(index);
      if (index >= 0 && type == 0 && i < sprite->images.size()) {
        const auto& image = sprite->images[i];
        return ragexe::res::ActImageSize{image.width, image.height, image.halved_x, image.halved_y};
      }
      if (index >= 0 && type == 1 && i < sprite->images32.size()) {
        const auto& image = sprite->images32[i];
        return ragexe::res::ActImageSize{image.width, image.height, image.halved_x, image.halved_y};
      }
      return std::nullopt;
    };
    ragexe::res::Act act;
    std::string error;
    if (!ragexe::res::DecodeAct(ReadFile(files[n]), lookup, act, &error)) {
      std::printf("A %zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::printf("A %zu %x %zu %zu %d\n", n, act.version, act.actions.size(), act.events.size(),
                act.max_items);
    for (const auto& frames : act.actions) {
      std::printf("a %zu\n", frames.size());
      for (const auto& frame : frames) {
        std::printf("f");
        for (std::int32_t h : frame.header) std::printf(" %d", h);
        std::printf(" %d %zu %zu\n", frame.event, frame.items.size(), frame.anchors.size());
        for (const auto& item : frame.items) {
          std::printf("i %d %d %d %u %u %u %u %u %08x %08x %d %d\n", item.x, item.y, item.image,
                      item.flags, item.color[0], item.color[1], item.color[2], item.color[3],
                      FloatBits(item.scale_x), FloatBits(item.scale_y), item.angle,
                      item.image_type);
        }
        for (const auto& anchor : frame.anchors) {
          std::printf("n %d %d %d\n", anchor.x, anchor.y, anchor.kind);
        }
      }
    }
    for (const std::string& event : act.events) {
      std::printf("e ");
      for (unsigned char c : event) std::printf("%02x", c);
      std::printf("\n");
    }
    for (float delay : act.delays) std::printf("d %08x\n", FloatBits(delay));
  }
  return failed == 0 ? 0 : 1;
}

// Decodes .gat files as C3dAttr does and prints, per file, "n ok major minor width height
// cell-size cells-hash" (FNV-1a over each cell's four float bits and attribute) or
// "n failed error".
int DecodeGats(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    const std::vector<std::uint8_t> bytes = ReadFile(files[n]);
    ragexe::res::AttrMap map;
    std::string error;
    if (!ragexe::res::DecodeAttrMap(bytes, map, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::vector<std::uint32_t> values;
    values.reserve(map.cells.size() * 5);
    for (const auto& cell : map.cells) {
      for (float h : cell.heights) values.push_back(FloatBits(h));
      values.push_back(static_cast<std::uint32_t>(cell.attribute));
    }
    std::printf("%zu ok %u %u %d %d %d %016llx\n", n, map.major, map.minor, map.width, map.height,
                map.cell_size, static_cast<unsigned long long>(Fnv1a64(values)));
  }
  return failed == 0 ? 0 : 1;
}

// Runs the path finder on a .gat's cells (docs/world/path.md), one query a line, all on one
// PathFinder and one MovePath, as an actor's:
//   F TIME SX SY SUB_X SUB_Y GX GY SPEED       FindPath
//   M MOVE_TIME NOW SX SY SUB_X SUB_Y GX GY SPEED
//                                              FindMovePath
//   I NOW SX SY SUB_X SUB_Y GX GY              IsPathFound, into a path of its own
//   S TIME                                     FindSegment
//   Z VALUE                                    sets the path's +0x10
//   W FIELDS... (SERVER TICK)...               CGameActor::ProcessState walking the path, update by
//                                              update (tools/path_oracle.py's WALK_FIELDS)
//   G FIELDS... (SERVER TICK)...               CGameActor::vf02's message 0, then the walk to
//                                              standing (GO_FIELDS)
//   P FIELDS... (SERVER TICK)...               G for a CPc (PLAYER_FIELDS)
//   T and U FIELDS... SPEED (SERVER TICK)...   G and P with message 0x13 from where the actor
//                                              stands to the goal at the speed
//   N and Y FIELDS... STAND... (SERVER TICK)... G and P standing, CGameActor::ProcessState
//                                              update by update (STAND_FIELDS)
//   E and Q FIELDS... ENTER... (SERVER TICK)... G and P given a state through vf10, or left in
//                                              theirs, then updated (ENTER_FIELDS)
//   A FIELDS... ARRIVE... RECORD... (SERVER TICK)...
//                                              G coming into view through message 3, 4, 5 or
//                                              6 with its record, then updated (ARRIVE_FIELDS)
//   B FIELDS... ARRIVE... EFFECTS... RECORD... (SERVER TICK)...
//                                              A with the effects its messages to itself make
//                                              and end (EFFECT_FIELDS)
//   X and K FIELDS... EFFECTS... COUNT (MESSAGE ARG SENDER)... SERVER TICK
//                                              G and P sent messages that make and end their
//                                              effects
//   D and H FIELDS... DEATH... EFFECTS... (SERVER TICK)...
//                                              G and P dying, then updated (DEATH_FIELDS)
//   V and J FIELDS... ATTACK... RECORD... (SERVER TICK)...
//                                              G and P acting on a target by message 0xb, 0x1b
//                                              or vf10, then updated (ATTACK_FIELDS)
//   L and O FIELDS... LAND... HITS... (SERVER TICK)...
//                                              G and P with hits queued and delivered by
//                                              00503f80 each update, or the first sent at
//                                              once (LAND_FIELDS)
//   C and R FIELDS... CAST... (SERVER TICK)...
//                                              G and P sent a message of the windows and given
//                                              a state, then updated by 00503210 and
//                                              ProcessState (CAST_FIELDS)
//   u and p FIELDS... UPDATE... (SERVER TICK)...
//                                              G and P updated whole by CNpc::Update and
//                                              CPc::Update (UPDATE_FIELDS)
//   o FIELDS... UPDATE... OWN... (SERVER TICK)...
//                                              P updated whole as the own player by
//                                              CPlayer::Update (OWN_FIELDS)
//   m FIELDS... OWN... ORDERS... COUNT (ID A B C WORDS*7 SERVER TICK)...
//                                              P as the own player sent messages through
//                                              CPlayer::SendMsg, message 0's order and 0x67's
//                                              skill from the words (ORDER_FIELDS)
// Prints for F and M "RESULT MOVE_TIME FIELD_0X10 START_X START_Y COUNT HASH" (the result
// found, same, none or overflow; the move time "-" for F; the start as float bits; the
// hash FNV-1a over each step's x, y, dir and time), for I the result, and for S "INDEX
// TIME X Y", or "-" when the path has fewer than two steps; for Z the value; for W "END
// UPDATES X Y Z FACING ACTION FRAME TRAVELLED HASH" (the end walking, arrived or faded, the
// floats as bits, the hash FNV-1a over each update's result and fields), or "-"; for G "END
// UPDATES X Y Z FACING ACTION FRAME STATE HASH", the end standing (no path), arrived,
// walking or faded; for E, Q, A, B, D, H, V, J, L and O the same with the end staying,
// changed (the state did; not L and O, which run to the end), faded, gone (D and H, with a
// corpse) or unported, the state unsigned; for C and R the same with the end staying, faded or
// settled (out of state 13 with no window left); for u and p the same with the end staying or
// faded, the action unsigned, and for o as for p; for X, K and m the same with "sent" and the
// messages' count.
int FindPaths(const std::string& gat, const std::string& queries) {
  ragexe::res::AttrMap map;
  std::string error;
  if (!ragexe::res::DecodeAttrMap(ReadFile(gat), map, &error)) {
    std::fprintf(stderr, "%s: %s\n", gat.c_str(), error.c_str());
    return 1;
  }
  ragexe::world::PathFinder finder;
  finder.SetAttrMap(&map);
  ragexe::world::MovePath path;
  const auto name = [](ragexe::world::PathResult r) {
    switch (r) {
      case ragexe::world::PathResult::kFound: return "found";
      case ragexe::world::PathResult::kSameCell: return "same";
      case ragexe::world::PathResult::kNoPath: return "none";
      case ragexe::world::PathResult::kOverflow: return "overflow";
    }
    return "?";
  };
  const auto print_path = [&](ragexe::world::PathResult r, const std::string& move_time) {
    std::vector<std::uint32_t> values;
    for (const auto& step : path.steps) {
      values.push_back(static_cast<std::uint32_t>(step.x));
      values.push_back(static_cast<std::uint32_t>(step.y));
      values.push_back(static_cast<std::uint32_t>(step.dir));
      values.push_back(step.time);
    }
    std::printf("%s %s %d %08x %08x %zu %016llx\n", name(r), move_time.c_str(), path.field_0x10,
                FloatBits(path.start_x), FloatBits(path.start_y), path.steps.size(),
                static_cast<unsigned long long>(Fnv1a64(values)));
  };
  std::ifstream in(queries);
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    std::string kind;
    fields >> kind;
    std::uint32_t time = 0;
    std::uint32_t now = 0;
    std::int32_t sx = 0, sy = 0, sub_x = 0, sub_y = 0, gx = 0, gy = 0, speed = 0;
    if (kind == "F") {
      fields >> time >> sx >> sy >> sub_x >> sub_y >> gx >> gy >> speed;
      print_path(finder.FindPath(time, sx, sy, sub_x, sub_y, gx, gy, speed, path), "-");
    } else if (kind == "M") {
      fields >> time >> now >> sx >> sy >> sub_x >> sub_y >> gx >> gy >> speed;
      const auto r =
          ragexe::world::FindMovePath(finder, path, time, now, sx, sy, sub_x, sub_y, gx, gy, speed);
      print_path(r, std::to_string(time));
    } else if (kind == "I") {
      fields >> now >> sx >> sy >> sub_x >> sub_y >> gx >> gy;
      std::printf("%d\n",
                  ragexe::world::IsPathFound(finder, now, sx, sy, sub_x, sub_y, gx, gy) ? 1 : 0);
    } else if (kind == "S") {
      fields >> time;
      if (path.steps.size() < 2) {
        std::printf("-\n");
        continue;
      }
      std::uint32_t step_time = 0;
      float x = 0.0f, y = 0.0f;
      const int index = path.FindSegment(time, &step_time, &x, &y);
      std::printf("%d %u %08x %08x\n", index, step_time, FloatBits(x), FloatBits(y));
    } else if (kind == "W") {
      if (path.steps.size() < 2) {
        std::printf("-\n");
        continue;
      }
      std::vector<std::uint32_t> v;
      for (std::string token; fields >> token;)
        v.push_back(static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
      constexpr std::size_t kFields = 17;
      if (v.size() < kFields) {
        std::fprintf(stderr, "short walk: %s\n", line.c_str());
        return 2;
      }
      ragexe::world::GameActor actor;
      actor.path = path;
      actor.state = 1;
      auto& o = actor.object;
      o.position = {std::bit_cast<float>(v[0]), 0.0f, std::bit_cast<float>(v[1])};
      o.facing = std::bit_cast<float>(v[2]);
      const float yaw = std::bit_cast<float>(v[3]);
      o.delay = std::bit_cast<float>(v[4]);
      actor.travelled = std::bit_cast<float>(v[5]);
      o.base_action = static_cast<std::int32_t>(v[6]);
      actor.job = static_cast<std::int32_t>(v[7]);
      actor.effect_state = v[8];
      actor.pushed = v[9] != 0;
      ragexe::res::Act act;
      act.actions.assign(256, std::vector<ragexe::res::ActFrame>(v[10]));
      actor.hold_until = v[11];
      actor.fade_until = v[12];
      o.color = v[13];
      o.held = v[14] != 0;
      actor.server_at = v[15];
      actor.processed_at = v[16];
      std::uint64_t h = 0xcbf29ce484222325ull;
      int updates = 0;
      const char* end = "walking";
      for (std::size_t k = kFields; k + 1 < v.size(); k += 2) {
        const auto r = ragexe::world::ProcessWalk(actor, map, act, yaw, v[k + 1], v[k]);
        ++updates;
        const std::uint32_t ret = r == ragexe::world::WalkResult::kFaded ? 0 : 1;
        const std::uint32_t state = r == ragexe::world::WalkResult::kArrived ? 0 : 0xffffffffu;
        const std::uint32_t values[] = {ret,
                                        FloatBits(o.position[0]),
                                        FloatBits(o.position[1]),
                                        FloatBits(o.position[2]),
                                        FloatBits(o.facing),
                                        static_cast<std::uint32_t>(o.action),
                                        static_cast<std::uint32_t>(o.frame),
                                        static_cast<std::uint32_t>(actor.segment),
                                        FloatBits(actor.travelled),
                                        actor.processed_at,
                                        actor.server_at,
                                        o.color,
                                        o.held ? 1u : 0u,
                                        actor.pushed ? 1u : 0u,
                                        state};
        for (const std::uint32_t x : values) {
          const std::uint8_t le[4] = {
              static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(x >> 8),
              static_cast<std::uint8_t>(x >> 16), static_cast<std::uint8_t>(x >> 24)};
          h = Fnv1a64(le, h);
        }
        if (r == ragexe::world::WalkResult::kFaded) {
          end = "faded";
          break;
        }
        if (r == ragexe::world::WalkResult::kArrived) {
          end = "arrived";
          break;
        }
      }
      std::printf("%s %d %08x %08x %08x %08x %d %d %08x %016llx\n", end, updates,
                  FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                  FloatBits(o.facing), o.action, o.frame, FloatBits(actor.travelled),
                  static_cast<unsigned long long>(h));
    } else if (kind == "G" || kind == "P" || kind == "T" || kind == "U" || kind == "N" ||
               kind == "Y" || kind == "E" || kind == "Q" || kind == "A" || kind == "B" ||
               kind == "X" || kind == "K" || kind == "D" || kind == "H" || kind == "V" ||
               kind == "J" || kind == "L" || kind == "O" || kind == "C" || kind == "R" ||
               kind == "u" || kind == "p" || kind == "o" || kind == "m") {
      const bool player = kind == "P" || kind == "U" || kind == "Y" || kind == "Q" || kind == "K" ||
                          kind == "H" || kind == "J" || kind == "O" || kind == "R" || kind == "p" ||
                          kind == "o" || kind == "m";
      const bool to = kind == "T" || kind == "U";
      const bool stand = kind == "N" || kind == "Y";
      const bool enter = kind == "E" || kind == "Q";
      const bool arrive = kind == "A" || kind == "B";
      const bool die = kind == "D" || kind == "H";
      const bool attack = kind == "V" || kind == "J";
      const bool land = kind == "L" || kind == "O";
      const bool cast = kind == "C" || kind == "R";
      const bool orders = kind == "m";
      const bool own = kind == "o" || orders;
      const bool update = kind == "u" || kind == "p" || kind == "o";
      const bool with_effects = kind == "B" || kind == "X" || kind == "K" || die;
      std::vector<std::uint32_t> v;
      for (std::string token; fields >> token;)
        v.push_back(static_cast<std::uint32_t>(std::stoll(token, nullptr, 0)));
      const std::size_t base_fields = player ? 44 : 39;
      constexpr std::size_t kArriveFields = 12;
      constexpr std::size_t kRecordWords = 28;
      constexpr std::size_t kEffectFields = 3;
      constexpr std::size_t kDeathFields = 6;
      constexpr std::size_t kAttackFields = 17 + 10;  // and the record
      constexpr std::size_t kLandFields = 27;         // then 10 words a hit
      constexpr std::size_t kCastFields = 21;
      constexpr std::size_t kUpdateFields = 30;
      constexpr std::size_t kOwnFields = 32;
      constexpr std::size_t kOrderFields = 7;
      if (land && v.size() < base_fields + kLandFields) {
        std::fprintf(stderr, "short go: %s\n", line.c_str());
        return 2;
      }
      const std::size_t hit_words = land ? 10 * std::size_t{v[base_fields + kLandFields - 1]} : 0;
      const std::size_t kFields =
          base_fields + (to ? 1 : 0) + (stand ? 7 : 0) + (enter ? 9 : 0) +
          (arrive ? kArriveFields + kRecordWords : 0) + (with_effects ? kEffectFields : 0) +
          (die ? kDeathFields : 0) + (attack ? kAttackFields : 0) +
          (land ? kLandFields + hit_words : 0) + (cast ? kCastFields : 0) +
          (update ? kUpdateFields : 0) + (own ? kOwnFields : 0) + (orders ? kOrderFields : 0);
      // B, X and K: EFFECT_FIELDS, after ARRIVE_FIELDS for B.
      const std::size_t effect_at =
          base_fields + (arrive ? kArriveFields : 0) + (die ? kDeathFields : 0);
      const std::size_t record_at =
          base_fields + kArriveFields + (with_effects ? kEffectFields : 0);
      if (v.size() < kFields + 2) {
        std::fprintf(stderr, "short go: %s\n", line.c_str());
        return 2;
      }
      ragexe::world::PlayerActor actor;
      actor.path = path;
      auto& o = actor.object;
      o.position = {std::bit_cast<float>(v[0]), 0.0f, std::bit_cast<float>(v[1])};
      o.facing = std::bit_cast<float>(v[2]);
      const float yaw = std::bit_cast<float>(v[3]);
      o.delay = std::bit_cast<float>(v[4]);
      actor.travelled = std::bit_cast<float>(v[5]);
      o.base_action = static_cast<std::int32_t>(v[6]);
      actor.job = static_cast<std::int32_t>(v[7]);
      actor.effect_state = v[8];
      actor.pushed = v[9] != 0;
      ragexe::res::Act act;
      act.actions.assign(256, std::vector<ragexe::res::ActFrame>(v[10]));
      actor.hold_until = v[11];
      actor.fade_until = v[12];
      o.color = v[13];
      o.held = v[14] != 0;
      actor.server_at = v[15];
      actor.processed_at = v[16];
      actor.state = static_cast<std::int32_t>(v[17]);
      actor.speed = static_cast<std::int32_t>(v[18]);
      actor.field_0xd0 = static_cast<std::int32_t>(v[19]);
      actor.field_0xd8 = static_cast<std::int32_t>(v[20]);
      act.delays.assign(9, 4.0f);
      act.delays[0] = std::bit_cast<float>(v[22]);
      act.delays[8] = std::bit_cast<float>(v[23]);
      act.delays.resize(v[37]);
      o.delay_scale = std::bit_cast<float>(v[24]);
      actor.target_x = std::bit_cast<float>(v[25]);
      actor.target_z = std::bit_cast<float>(v[26]);
      actor.id = static_cast<std::int32_t>(v[34]);
      o.repeats = std::bit_cast<float>(v[35]);
      o.finished = v[36] != 0;
      actor.segment = static_cast<std::int32_t>(v[38]);
      if (player) {
        actor.body_state = static_cast<std::int32_t>(v[39]);
        actor.saved_frame = static_cast<std::int32_t>(v[40]);
        actor.cast_gauge = static_cast<std::int32_t>(v[41]);
        actor.field_0x224 = static_cast<std::int32_t>(v[42]);
        actor.field_0xcc = static_cast<std::int32_t>(v[43]);
      }
      const ragexe::world::MoveOrder order{v[27],
                                           static_cast<std::int32_t>(v[28]),
                                           static_cast<std::int32_t>(v[29]),
                                           static_cast<std::int32_t>(v[30]),
                                           static_cast<std::int32_t>(v[31]),
                                           static_cast<std::int32_t>(v[32]),
                                           static_cast<std::int32_t>(v[33])};
      // N and Y: srand's seed, the pet and its lines, +0x1d4, +0x54 and +0x1f4.
      ragexe::core::CrtRand rand(stand ? v[base_fields] : 1);
      ragexe::world::PetTalk pet;
      if (stand) {
        pet = {static_cast<std::int32_t>(v[base_fields + 1]),
               static_cast<std::int32_t>(v[base_fields + 2]),
               static_cast<std::int32_t>(v[base_fields + 3])};
        actor.pet_talk_at = v[base_fields + 4];
        o.play_mode = static_cast<std::int32_t>(v[base_fields + 5]);
        actor.field_0x1f4 = static_cast<std::int32_t>(v[base_fields + 6]);
      }
      // E and Q: whether to set the state and which, +0xcc, +0x1a8, whether the world has that
      // actor and where, +0x54 and +0x70.
      ragexe::render::Vec3 target{};
      if (enter) {
        actor.field_0xcc = static_cast<std::int32_t>(v[base_fields + 2]);
        actor.target_id = static_cast<std::int32_t>(v[base_fields + 3]);
        target = {std::bit_cast<float>(v[base_fields + 5]), 0.0f,
                  std::bit_cast<float>(v[base_fields + 6])};
        o.play_mode = static_cast<std::int32_t>(v[base_fields + 7]);
        o.start_time = v[base_fields + 8];
      }
      std::vector<ragexe::world::ModeMessage> messages;
      std::vector<std::int32_t> windows;
      std::int32_t mode_field_0x2a0 = 0x5a5a5a5a;
      ragexe::world::StateContext context{&map,
                                          &act,
                                          yaw,
                                          static_cast<std::int32_t>(v[21]),
                                          &messages,
                                          &windows,
                                          &mode_field_0x2a0,
                                          stand ? &pet : nullptr,
                                          &rand,
                                          enter && v[base_fields + 4] != 0 ? &target : nullptr};
      // A: the status words, the four flags, the guild and its emblem before; the player's id,
      // whether the emblem's file is there, the world's player and where; then the record.
      std::vector<ragexe::world::ModeMessage> actor_messages;
      std::vector<ragexe::world::Sound> sounds;
      std::vector<std::string> asked;
      std::int32_t ground_water_fade = 0x5a5a5a5a;
      ragexe::render::Vec3 player_at{};
      std::array<std::int32_t, kRecordWords> record{};
      if (arrive) {
        const std::size_t b = base_fields;
        actor.body_state = static_cast<std::int32_t>(v[b + 2]);
        actor.health_state = v[b + 3];
        o.visible = (v[b + 4] & 1) != 0;
        o.body_shown = (v[b + 4] & 2) != 0;
        o.tinted = (v[b + 4] & 4) != 0;
        o.shadow = (v[b + 4] & 8) != 0;
        actor.guild = static_cast<std::int32_t>(v[b + 5]);
        actor.emblem = static_cast<std::int32_t>(v[b + 6]);
        context.own_id = static_cast<std::int32_t>(v[b + 7]);
        const bool exists = v[b + 8] != 0;
        context.resource_exists = [&asked, exists](const std::string& name) {
          asked.push_back(name);
          return exists;
        };
        player_at = {std::bit_cast<float>(v[b + 10]), 0.0f, std::bit_cast<float>(v[b + 11])};
        context.player = v[b + 9] != 0 ? &player_at : nullptr;
        context.actor_messages = with_effects ? nullptr : &actor_messages;
        context.sounds = &sounds;
        context.ground_water_fade = &ground_water_fade;
        context.server_name = "Chaos";
        for (std::size_t k = 0; k < kRecordWords; ++k)
          record[k] = static_cast<std::int32_t>(v[record_at + k]);
      }
      // The effects before: one in each slot of `slots`' bits (one for all with `same`), each
      // in the list in that order, then `listed` more.
      ragexe::world::ActorEffects effects;
      std::size_t made_before = 0;
      if (with_effects) {
        const std::uint32_t slots = v[effect_at];
        std::int32_t shared = 0;
        for (std::size_t k = 0; k < actor.effect_slots.size(); ++k) {
          if ((slots >> k & 1) == 0) continue;
          if (v[effect_at + 2] == 0 || shared == 0) {
            effects.made.emplace_back();
            shared = static_cast<std::int32_t>(effects.made.size());
            actor.effects.push_back(shared);
          }
          actor.effect_slots[k] = shared;
        }
        for (std::uint32_t k = 0; k < v[effect_at + 1]; ++k) {
          effects.made.emplace_back();
          actor.effects.push_back(static_cast<std::int32_t>(effects.made.size()));
        }
        made_before = effects.made.size();
        context.effects = &effects;
      }
      std::uint64_t h = 0xcbf29ce484222325ull;
      const auto fold = [&h](std::uint32_t x) {
        const std::uint8_t le[4] = {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(x >> 8),
                                    static_cast<std::uint8_t>(x >> 16),
                                    static_cast<std::uint8_t>(x >> 24)};
        h = Fnv1a64(le, h);
      };
      // What the move, the walk and the states write, as tools/path_oracle.py's fields().
      const auto fold_fields = [&] {
        for (const std::uint32_t x : {static_cast<std::uint32_t>(actor.state),
                                      FloatBits(o.position[0]),
                                      FloatBits(o.position[1]),
                                      FloatBits(o.position[2]),
                                      FloatBits(o.facing),
                                      static_cast<std::uint32_t>(o.base_action),
                                      static_cast<std::uint32_t>(o.action),
                                      static_cast<std::uint32_t>(o.frame),
                                      FloatBits(o.delay),
                                      FloatBits(o.repeats),
                                      FloatBits(o.delay_scale),
                                      static_cast<std::uint32_t>(o.play_mode),
                                      o.start_time,
                                      FloatBits(actor.field_0x1ac),
                                      static_cast<std::uint32_t>(actor.goal_x),
                                      static_cast<std::uint32_t>(actor.goal_y),
                                      static_cast<std::uint32_t>(actor.segment),
                                      FloatBits(actor.travelled),
                                      actor.processed_at,
                                      actor.server_at,
                                      o.color,
                                      actor.move_time,
                                      o.finished ? 1u : 0u,
                                      o.held ? 1u : 0u,
                                      actor.pushed ? 1u : 0u}) {
          fold(x);
        }
      };
      const auto fold_player = [&] {
        if (!player) return;
        for (const std::int32_t x :
             {mode_field_0x2a0, actor.cast_gauge, actor.saved_frame, actor.field_0xcc})
          fold(static_cast<std::uint32_t>(x));
        for (const std::int32_t x : windows) fold(static_cast<std::uint32_t>(x));
        windows.clear();
      };
      const auto fold_messages = [&] {
        for (const auto& m : messages) {
          for (const std::int32_t x : {m.id, m.a, m.b, m.c}) fold(static_cast<std::uint32_t>(x));
        }
        messages.clear();
      };
      // The slots and the list by number, the effects made and what they were sent since, as
      // tools/path_oracle.py's effects().
      const auto fold_effects = [&] {
        if (!with_effects) return;
        for (const std::int32_t number : actor.effect_slots)
          fold(static_cast<std::uint32_t>(number));
        fold(static_cast<std::uint32_t>(actor.effects.size()));
        fold(static_cast<std::uint32_t>(actor.effects.size()));
        for (const std::int32_t number : actor.effects) fold(static_cast<std::uint32_t>(number));
        fold(static_cast<std::uint32_t>(effects.made.size()));
        for (std::size_t k = 0; k < effects.made.size(); ++k) {
          const auto& e = effects.made[k];
          for (const std::uint32_t x :
               {k < made_before ? 2u : MakerCode(e.maker), static_cast<std::uint32_t>(e.type),
                e.position == &o.position ? 1u : 0u, static_cast<std::uint32_t>(e.actor_id),
                FloatBits(e.field_0x44)}) {
            fold(x);
          }
        }
        fold(static_cast<std::uint32_t>(6 * effects.messages.size()));
        for (const auto& m : effects.messages) {
          for (const std::int32_t x : {m.effect, m.id == 0x32 ? 1 : 0, m.id, 0, 0, 0})
            fold(static_cast<std::uint32_t>(x));
        }
        effects.messages.clear();
      };
      // V and J: by `how`, vf10's state, message 0xb with the record or 0x1b with it as a hit; the
      // actor the world finds and the player, each another or the actor itself; the act's frames'
      // events and the hits queued before; then the updates, as tools/path_oracle.py's attack().
      if (attack) {
        auto& base = static_cast<ragexe::world::GameActor&>(actor);
        const std::size_t b = base_fields;
        const std::uint32_t how = v[b];
        const std::uint32_t enter_state = v[b + 1];
        actor.field_0xcc = static_cast<std::int32_t>(v[b + 2]);
        actor.target_id = static_cast<std::int32_t>(v[b + 3]);
        ragexe::world::GameActor foe;
        foe.object.position = {std::bit_cast<float>(v[b + 5]), 0.0f,
                               std::bit_cast<float>(v[b + 6])};
        context.own_id = static_cast<std::int32_t>(v[b + 7]);
        ragexe::world::GameActor pc;
        pc.object.position = {std::bit_cast<float>(v[b + 9]), 0.0f,
                              std::bit_cast<float>(v[b + 10])};
        actor.field_0x1bc = static_cast<std::int32_t>(v[b + 11]);
        actor.weapon = static_cast<std::int32_t>(v[b + 12]);
        const std::uint32_t events = v[b + 13];
        o.play_mode = static_cast<std::int32_t>(v[b + 14]);
        o.start_time = v[b + 15];
        if (!player) actor.body_state = 0;
        // Each frame's event by 3 bits of `events`, the first 10 frames: 1 to 4 the act's four,
        // 5 one past them, 6 0x7fffffff, the rest none.
        act.events = {"atk", "atk2", "at", ""};
        for (auto& frames : act.actions) {
          for (std::size_t k = 0; k < frames.size() && k < 10; ++k) {
            const std::uint32_t code = events >> (3 * k) & 7;
            frames[k].event = code == 0 || code == 7 ? -1
                              : code == 5            ? 4
                              : code == 6            ? 0x7fffffff
                                                     : static_cast<std::int32_t>(code) - 1;
          }
        }
        const auto hit_of = [](const std::uint32_t* w) {
          return ragexe::world::QueuedHit{static_cast<std::int32_t>(w[0]),
                                          static_cast<std::int32_t>(w[1]),
                                          w[2],
                                          static_cast<std::int32_t>(w[3]),
                                          static_cast<std::int32_t>(w[4]),
                                          static_cast<std::int32_t>(w[5]),
                                          static_cast<std::int32_t>(w[6]),
                                          static_cast<std::int32_t>(w[7]),
                                          static_cast<std::int32_t>(w[8]),
                                          std::bit_cast<float>(w[9])};
        };
        for (ragexe::world::GameActor* at : {&base, &foe, &pc}) {
          for (std::uint32_t k = 0; k < v[b + 16]; ++k) {
            std::uint32_t w[10];
            for (std::uint32_t j = 0; j < 10; ++j) w[j] = (k + 1) << 16 | j;
            at->hits.push_back(hit_of(w));
          }
        }
        const std::uint32_t found = v[b + 4];
        const std::uint32_t player_is = v[b + 8];
        context.target_actor = found == 1 ? &foe : found == 2 ? &base : nullptr;
        context.player_actor = player_is == 1 ? &pc : player_is == 2 ? &base : nullptr;
        context.target =
            context.target_actor != nullptr ? &context.target_actor->object.position : nullptr;
        context.player =
            context.player_actor != nullptr ? &context.player_actor->object.position : nullptr;
        std::vector<ragexe::world::Arrow> arrows;
        std::vector<std::int32_t> launched;
        context.arrows = &arrows;
        context.launched = &launched;
        const std::uint32_t* record = &v[b + 17];
        const std::uint32_t at = v[kFields + 1];
        std::uint32_t logged = 0;
        if (how == 0) {
          if (player)
            ragexe::world::SetPlayerState(actor, static_cast<std::int32_t>(enter_state), context,
                                          at);
          else
            ragexe::world::SetGameActorState(base, static_cast<std::int32_t>(enter_state), context,
                                             at);
        } else if (how == 0x1b) {
          ragexe::world::QueueGameActorHit(base, hit_of(record),
                                           static_cast<std::int32_t>(enter_state));
        } else {
          const ragexe::world::TargetAction action{
              static_cast<std::int32_t>(record[0]), static_cast<std::int32_t>(record[1]),
              static_cast<std::int32_t>(record[2]), static_cast<std::int32_t>(record[3]),
              static_cast<std::int32_t>(record[5]), static_cast<std::int32_t>(record[6]),
              static_cast<std::int32_t>(record[7]), static_cast<std::int32_t>(record[8]),
              static_cast<std::int32_t>(record[9])};
          const bool known = player ? ragexe::world::ActOnTarget(actor, action, context, at)
                                    : ragexe::world::ActOnTarget(base, action, context, at);
          if (!known) logged = 1;
        }
        // +0x1a8, +0xcc, and what was logged, launched and shot since, and the three queues, as
        // tools/path_oracle.py's attacked().
        const auto fold_attacked = [&] {
          fold(static_cast<std::uint32_t>(actor.target_id));
          fold(static_cast<std::uint32_t>(actor.field_0xcc));
          fold(logged);
          logged = 0;
          fold(static_cast<std::uint32_t>(launched.size()));
          for (const std::int32_t id : launched) fold(static_cast<std::uint32_t>(id));
          launched.clear();
          fold(static_cast<std::uint32_t>(3 * arrows.size()));
          for (const auto& arrow : arrows) {
            fold(arrow.from == &o.position ? 1u : 0u);
            fold(static_cast<std::uint32_t>(arrow.target_id));
            fold(FloatBits(arrow.speed));
          }
          arrows.clear();
          for (const ragexe::world::GameActor* queue : {&base, &foe, &pc}) {
            fold(static_cast<std::uint32_t>(queue->hits.size()));
            for (const auto& hit : queue->hits) {
              for (const std::uint32_t x :
                   {static_cast<std::uint32_t>(hit.message), static_cast<std::uint32_t>(hit.damage),
                    hit.time, static_cast<std::uint32_t>(hit.attacker_id),
                    static_cast<std::uint32_t>(hit.cell_x), static_cast<std::uint32_t>(hit.cell_y),
                    static_cast<std::uint32_t>(hit.field_0x18),
                    static_cast<std::uint32_t>(hit.field_0x1c),
                    static_cast<std::uint32_t>(hit.field_0x20), FloatBits(hit.field_0x24)}) {
                fold(x);
              }
            }
          }
        };
        fold(6);
        fold_fields();
        fold_messages();
        fold_player();
        fold_attacked();
        const std::int32_t state = actor.state;
        int updates = 0;
        const char* end = "staying";
        for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
          const auto r = player
                             ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                             : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
          ++updates;
          const bool over =
              r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
          fold(over ? 0u : 1u);
          fold_fields();
          fold_messages();
          fold_player();
          fold_attacked();
          if (over) {
            end = "faded";
            break;
          }
          if (actor.state != state) {
            end = "changed";
            break;
          }
        }
        std::printf("%s %d %08x %08x %08x %08x %d %u %u %016llx\n", end, updates,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, static_cast<std::uint32_t>(o.frame),
                    static_cast<std::uint32_t>(actor.state), static_cast<unsigned long long>(h));
        continue;
      }
      // L and O: the hits, queued and delivered or the first sent; the attacker, another or the
      // player or the actor itself; the player's statuses, the guild war, effects on, the msg
      // sprites, the effects listed before; then the updates, each delivering what is due, as
      // tools/path_oracle.py's land().
      if (land) {
        auto& base = static_cast<ragexe::world::GameActor&>(actor);
        const std::size_t b = base_fields;
        const std::uint32_t how = v[b];
        const std::uint32_t attacker = v[b + 1];
        const std::uint32_t pcs = v[b + 2];
        context.own_id = static_cast<std::int32_t>(v[b + 6]);
        ragexe::world::GameActor foe;
        ragexe::world::GameActor pc;
        for (ragexe::world::GameActor* other : {&foe, &pc}) {
          other->job = static_cast<std::int32_t>(v[b + 3]);
          other->effect_state = v[b + 4];
        }
        foe.id = 0x5a5a0001;
        pc.id = 0x5a5a0002;
        foe.object.position = {std::bit_cast<float>(v[b + 22]), 0.0f,
                               std::bit_cast<float>(v[b + 23])};
        pc.object.position = {std::bit_cast<float>(v[b + 24]), 0.0f,
                              std::bit_cast<float>(v[b + 25])};
        const auto weapon_of = [&](std::uint32_t bit) {
          return (pcs & bit) != 0 ? static_cast<std::int32_t>(v[b + 5]) : -1;
        };
        std::vector<std::int32_t> statuses;
        if ((v[b + 7] & 2) != 0) statuses.push_back(5);
        if ((v[b + 7] & 1) != 0) statuses.push_back(0x13);
        context.own_statuses = &statuses;
        context.guild_war_map = v[b + 8] != 0;
        context.effects_on = v[b + 9] != 0;
        context.msg_sprites = v[b + 10] != 0;
        rand.Seed(v[b + 11]);
        actor.field_0x210 = static_cast<std::int32_t>(v[b + 12]);
        const std::uint32_t events = v[b + 16];
        o.play_mode = static_cast<std::int32_t>(v[b + 17]);
        o.start_time = v[b + 18];
        actor.target_id = static_cast<std::int32_t>(v[b + 19]);
        actor.weapon = static_cast<std::int32_t>(v[b + 20]);
        actor.field_0x1bc = static_cast<std::int32_t>(v[b + 21]);
        actor.target_x = 0.0f;
        actor.target_z = 0.0f;
        if (!player) actor.body_state = 0;
        const std::int32_t own_weapon = player ? actor.weapon : -1;
        context.target_actor = attacker == 1 ? &foe : attacker == 3 ? &base : nullptr;
        context.target_weapon = attacker == 3 ? own_weapon : weapon_of(1);
        context.player_actor = attacker == 4 ? &base : &pc;
        context.player_weapon = attacker == 4 ? own_weapon : weapon_of(2);
        context.target =
            context.target_actor != nullptr ? &context.target_actor->object.position : nullptr;
        context.player = &context.player_actor->object.position;
        // The effects before: `listed` numbers, of type 0x16 by `kinds`' bits, the first in the
        // slot `slot` below 11.
        ragexe::world::ActorEffects effects;
        for (std::uint32_t k = 0; k < v[b + 13]; ++k) {
          ragexe::world::ActorEffect e;
          if ((v[b + 14] >> k & 1) != 0) {
            e.maker = ragexe::world::EffectMaker::kNumber;
            e.type = 0x16;
          }
          effects.made.push_back(e);
          const auto number = static_cast<std::int32_t>(effects.made.size());
          actor.effects.push_back(number);
          if (k == 0 && v[b + 15] < actor.effect_slots.size())
            actor.effect_slots[v[b + 15]] = number;
        }
        const std::size_t made_before = effects.made.size();
        context.effects = &effects;
        act.events = {"atk", "atk2", "at", ""};
        for (auto& frames : act.actions) {
          for (std::size_t k = 0; k < frames.size() && k < 10; ++k) {
            const std::uint32_t code = events >> (3 * k) & 7;
            frames[k].event = code == 0 || code == 7 ? -1
                              : code == 5            ? 4
                              : code == 6            ? 0x7fffffff
                                                     : static_cast<std::int32_t>(code) - 1;
          }
        }
        std::vector<ragexe::world::Arrow> arrows;
        std::vector<std::int32_t> launched;
        std::vector<ragexe::world::EffectMessage> launched_messages;
        context.arrows = &arrows;
        context.launched = &launched;
        context.launched_messages = &launched_messages;
        context.sounds = &sounds;
        ragexe::world::QueuedHit first;
        for (std::size_t k = 0; k < v[b + 26]; ++k) {
          const std::uint32_t* w = &v[b + kLandFields + 10 * k];
          const ragexe::world::QueuedHit hit{static_cast<std::int32_t>(w[0]),
                                             static_cast<std::int32_t>(w[1]),
                                             w[2],
                                             static_cast<std::int32_t>(w[3]),
                                             static_cast<std::int32_t>(w[4]),
                                             static_cast<std::int32_t>(w[5]),
                                             static_cast<std::int32_t>(w[6]),
                                             static_cast<std::int32_t>(w[7]),
                                             static_cast<std::int32_t>(w[8]),
                                             std::bit_cast<float>(w[9])};
          if (k == 0 && how != 0) {
            first = hit;
          } else {
            actor.hits.push_back(hit);
          }
        }
        const std::uint32_t at = v[kFields + 1];
        if (how == 0x6c) {
          if (player)
            ragexe::world::RestartGameActorAttack(actor, context, at);
          else
            ragexe::world::RestartGameActorAttack(base, context, at);
        } else if (how != 0) {
          if (player)
            ragexe::world::LandGameActorHit(actor, static_cast<std::int32_t>(how), first, context,
                                            at);
          else
            ragexe::world::LandGameActorHit(base, static_cast<std::int32_t>(how), first, context,
                                            at);
        }
        const auto where = [&](const ragexe::render::Vec3* p) {
          return p == &o.position ? 1u : p == &foe.object.position ? 2u : 3u;
        };
        const auto who = [&](const ragexe::world::GameActor* sender) {
          return sender == nullptr ? 0u : sender == &base ? 1u : sender == &foe ? 2u : 3u;
        };
        const auto fold_message = [&](const ragexe::world::EffectMessage& m) {
          for (const std::uint32_t x :
               {static_cast<std::uint32_t>(m.effect), who(m.sender),
                static_cast<std::uint32_t>(m.id),
                m.at != nullptr ? where(m.at) : static_cast<std::uint32_t>(m.a),
                static_cast<std::uint32_t>(m.b), static_cast<std::uint32_t>(m.c)})
            fold(x);
        };
        const auto fold_list = [&](const ragexe::world::GameActor& a) {
          fold(static_cast<std::uint32_t>(a.effects.size()));
          for (const std::int32_t number : a.effects) fold(static_cast<std::uint32_t>(number));
        };
        // What a hit's landing writes beyond the fields, and the queue; what was played,
        // launched and sent to it since; the slots, the three lists, every effect and what they
        // were sent and which the world deleted since, as tools/path_oracle.py's landed().
        const auto fold_landed = [&] {
          for (const std::uint32_t x :
               {o.keyed_at, static_cast<std::uint32_t>(actor.field_0xa8), FloatBits(actor.target_x),
                FloatBits(actor.target_z), static_cast<std::uint32_t>(actor.field_0x210),
                static_cast<std::uint32_t>(actor.target_id),
                static_cast<std::uint32_t>(actor.field_0xcc), o.keyed ? 1u : 0u,
                static_cast<std::uint32_t>(actor.hits.size())})
            fold(x);
          for (const auto& hit : actor.hits) {
            for (const std::uint32_t x :
                 {static_cast<std::uint32_t>(hit.message), static_cast<std::uint32_t>(hit.damage),
                  hit.time, static_cast<std::uint32_t>(hit.attacker_id),
                  static_cast<std::uint32_t>(hit.cell_x), static_cast<std::uint32_t>(hit.cell_y),
                  static_cast<std::uint32_t>(hit.field_0x18),
                  static_cast<std::uint32_t>(hit.field_0x1c),
                  static_cast<std::uint32_t>(hit.field_0x20), FloatBits(hit.field_0x24)}) {
              fold(x);
            }
          }
          fold(static_cast<std::uint32_t>(8 * sounds.size()));
          for (const auto& sound : sounds) {
            const std::uint64_t n = Fnv1a64(std::span(
                reinterpret_cast<const std::uint8_t*>(sound.name.data()), sound.name.size()));
            fold(static_cast<std::uint32_t>(n));
            fold(static_cast<std::uint32_t>(n >> 32));
            for (const std::uint32_t x :
                 {FloatBits(sound.x), 0u, FloatBits(sound.z), 250u, 40u, FloatBits(1.0f)})
              fold(x);
          }
          sounds.clear();
          fold(static_cast<std::uint32_t>(launched.size()));
          for (const std::int32_t id : launched) fold(static_cast<std::uint32_t>(id));
          launched.clear();
          fold(static_cast<std::uint32_t>(6 * launched_messages.size()));
          for (const auto& m : launched_messages) fold_message(m);
          launched_messages.clear();
          for (const std::int32_t number : actor.effect_slots)
            fold(static_cast<std::uint32_t>(number));
          fold_list(base);
          fold_list(foe);
          fold_list(pc);
          fold(static_cast<std::uint32_t>(effects.made.size()));
          for (std::size_t k = 0; k < effects.made.size(); ++k) {
            const auto& e = effects.made[k];
            const bool before = k < made_before;
            const bool number = e.maker == ragexe::world::EffectMaker::kNumber;
            const bool sprite = e.maker == ragexe::world::EffectMaker::kSprite ||
                                e.maker == ragexe::world::EffectMaker::kEmotion;
            for (const std::uint32_t x :
                 {before ? 2u : MakerCode(e.maker),
                  !before && sprite ? static_cast<std::uint32_t>(e.type) : 0u,
                  before || number ? 0u : where(e.position),
                  before || number ? 0u : static_cast<std::uint32_t>(e.actor_id),
                  FloatBits(e.field_0x44), FloatBits(e.field_0x3c), e.color,
                  number ? static_cast<std::uint32_t>(e.type) : 0u}) {
              fold(x);
            }
          }
          fold(static_cast<std::uint32_t>(6 * effects.messages.size()));
          for (const auto& m : effects.messages) fold_message(m);
          effects.messages.clear();
          fold(static_cast<std::uint32_t>(effects.deleted.size()));
          for (const std::int32_t number : effects.deleted)
            fold(static_cast<std::uint32_t>(number));
          effects.deleted.clear();
        };
        fold(7);
        fold_fields();
        fold_messages();
        fold_player();
        fold_landed();
        int updates = 0;
        const char* end = "staying";
        for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
          if (player)
            ragexe::world::DeliverGameActorHits(actor, context, v[k + 1]);
          else
            ragexe::world::DeliverGameActorHits(base, context, v[k + 1]);
          const auto r = player
                             ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                             : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
          ++updates;
          const bool over =
              r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
          fold(over ? 0u : 1u);
          fold_fields();
          fold_messages();
          fold_player();
          fold_landed();
          if (over) {
            end = "faded";
            break;
          }
        }
        std::printf("%s %d %08x %08x %08x %08x %d %u %u %016llx\n", end, updates,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, static_cast<std::uint32_t>(o.frame),
                    static_cast<std::uint32_t>(actor.state), static_cast<unsigned long long>(h));
        continue;
      }
      // C and R: the message (0x52, 0x53, 0x21 or 0x36) and the state; +0xcc, the windows and
      // their times, the point on screen, +0x48 and the screen's size; then the updates, each
      // 00503210 then ProcessState, as tools/path_oracle.py's cast().
      if (cast) {
        auto& base = static_cast<ragexe::world::GameActor&>(actor);
        const std::size_t b = base_fields;
        const std::uint32_t how = v[b];
        const auto duration = static_cast<std::int32_t>(v[b + 1]);
        const std::uint32_t enter_state = v[b + 2];
        actor.field_0xcc = static_cast<std::int32_t>(v[b + 3]);
        ragexe::world::ActorWindows actor_windows;
        for (std::int32_t* window :
             {&actor.balloon, &actor.chat_title, &actor.shop_title, &actor.cast_gauge}) {
          const auto k = static_cast<std::uint32_t>(window - &actor.balloon);
          if ((v[b + 4] >> k & 1) == 0) continue;
          actor_windows.sizes.push_back(k == 0   ? std::array{static_cast<std::int32_t>(v[b + 5]),
                                                              static_cast<std::int32_t>(v[b + 6])}
                                        : k == 3 ? std::array{0x3c, 6}
                                                 : std::array{0x8c, 0x22});
          *window = static_cast<std::int32_t>(actor_windows.sizes.size());
        }
        actor.balloon_width = static_cast<std::int32_t>(v[b + 7]);
        actor.balloon_height = static_cast<std::int32_t>(v[b + 8]);
        actor.balloon_at = v[b + 9];
        actor.cast_end = v[b + 10];
        actor.cast_start = v[b + 11];
        o.screen_x = static_cast<std::int32_t>(v[b + 12]);
        o.screen_y = static_cast<std::int32_t>(v[b + 13]);
        o.pixel_scale = std::bit_cast<float>(v[b + 14]);
        context.windows = &actor_windows;
        context.screen_width = static_cast<std::int32_t>(v[b + 15]);
        context.screen_height = static_cast<std::int32_t>(v[b + 16]);
        actor.field_0x1bc = static_cast<std::int32_t>(v[b + 17]);
        actor.weapon = static_cast<std::int32_t>(v[b + 18]);
        o.play_mode = static_cast<std::int32_t>(v[b + 19]);
        o.start_time = v[b + 20];
        if (!player) actor.body_state = 0;
        // The windows by number, the cast's times, +0x1a4 and +0xcc; for a player CGameMode+0x2a0
        // and +0x294; what the windows were told and which were queued for removal since, as
        // tools/path_oracle.py's casted().
        const auto fold_cast = [&] {
          for (const std::int32_t x :
               {actor.balloon, actor.chat_title, actor.shop_title, actor.cast_gauge,
                static_cast<std::int32_t>(actor.balloon_at),
                static_cast<std::int32_t>(actor.cast_end),
                static_cast<std::int32_t>(actor.cast_start), actor.field_0xcc})
            fold(static_cast<std::uint32_t>(x));
          if (player) {
            fold(static_cast<std::uint32_t>(mode_field_0x2a0));
            fold(static_cast<std::uint32_t>(actor.saved_frame));
          }
          fold(static_cast<std::uint32_t>(4 * actor_windows.calls.size()));
          for (const auto& call : actor_windows.calls) {
            for (const std::int32_t x :
                 {static_cast<std::int32_t>(call.kind), call.window, call.a, call.b})
              fold(static_cast<std::uint32_t>(x));
          }
          actor_windows.calls.clear();
          fold(static_cast<std::uint32_t>(windows.size()));
          for (const std::int32_t x : windows) fold(static_cast<std::uint32_t>(x));
          windows.clear();
        };
        const std::uint32_t at = v[kFields + 1];
        if (how == 0x52)
          ragexe::world::StartGameActorCast(base, duration, context, at);
        else if (how != 0)
          ragexe::world::CloseGameActorWindow(base, static_cast<std::int32_t>(how), context);
        if (enter_state != 0) {
          if (player)
            ragexe::world::SetPlayerState(actor, static_cast<std::int32_t>(enter_state), context,
                                          at);
          else
            ragexe::world::SetGameActorState(base, static_cast<std::int32_t>(enter_state), context,
                                             at);
        }
        fold(8);
        fold_fields();
        fold_messages();
        fold_cast();
        int updates = 0;
        const char* end = "staying";
        for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
          if (player)
            ragexe::world::StepGameActorWindows(actor, context, v[k + 1]);
          else
            ragexe::world::StepGameActorWindows(base, context, v[k + 1]);
          fold(9);
          fold_fields();
          fold_messages();
          fold_cast();
          const auto r = player
                             ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                             : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
          ++updates;
          const bool over =
              r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
          fold(over ? 0u : 1u);
          fold_fields();
          fold_messages();
          fold_cast();
          if (over) {
            end = "faded";
            break;
          }
          if (actor.state != 13 && actor.balloon == 0 && actor.chat_title == 0 &&
              actor.shop_title == 0 && actor.cast_gauge == 0) {
            end = "settled";
            break;
          }
        }
        std::printf("%s %d %08x %08x %08x %08x %d %u %u %016llx\n", end, updates,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, static_cast<std::uint32_t>(o.frame),
                    static_cast<std::uint32_t>(actor.state), static_cast<unsigned long long>(h));
        continue;
      }
      // o and m: the CPlayer's fields, the ping, the attack range, the player's id, what CWorld
      // finds of the one pursued (found's bits) and where it stands, its state, cell, +0x238, job
      // and +0x21c, and CGameMode+0x198's list before, as tools/path_oracle.py's ready_own().
      ragexe::world::GameActor pursued;
      std::vector<std::int32_t> picked;
      std::vector<ragexe::world::MoveOrder> own_moves;
      if (own) {
        const std::size_t w = base_fields + (update ? kUpdateFields : 0);
        const auto i = [&](std::size_t k) { return static_cast<std::int32_t>(v[w + k]); };
        ragexe::world::OwnPlayer& p = actor.own.emplace();
        actor.reports = false;
        p.goal_x = i(0);
        p.goal_y = i(1);
        p.acted_at = v[w + 2];
        p.premove_at = v[w + 3];
        p.pursuit = i(4);
        p.premove = i(5);
        p.attack_mode = i(6);
        p.acted = i(7);
        p.move_queued = i(8);
        p.target_id = i(9);
        p.acts = i(10);
        p.asked_at = v[w + 11];
        p.skill = i(12);
        p.range = i(13);
        p.cell_x = i(14);
        p.cell_y = i(15);
        p.queued_x = i(16);
        p.queued_y = i(17);
        context.ping = v[w + 18];
        context.attack_range = i(19);
        context.own_id = i(20);
        const std::uint32_t found = v[w + 21];
        pursued.object.position = {std::bit_cast<float>(v[w + 22]), std::bit_cast<float>(v[w + 23]),
                                   std::bit_cast<float>(v[w + 24])};
        pursued.state = i(25);
        pursued.goal_x = i(26);
        pursued.goal_y = i(27);
        pursued.model_kind = i(28);
        pursued.job = i(29);
        pursued.effect_state = v[w + 30];
        if ((found & 1) != 0) {
          context.target_actor = &pursued;
          context.target = &pursued.object.position;
        }
        if ((found & 2) != 0) context.item = &pursued.object.position;
        if ((found & 4) != 0) context.skill_object = &pursued;
        if (v[w + 31] != 0) picked.push_back(p.target_id + (v[w + 31] == 2 ? 1 : 0));
        context.picked = &picked;
        context.own_moves = &own_moves;
        context.actor_messages = &actor_messages;
      }
      // What the own player's update writes beyond p's, as tools/path_oracle.py's own().
      const auto fold_own = [&] {
        if (!own) return;
        const ragexe::world::OwnPlayer& p = *actor.own;
        for (const std::int32_t x :
             {p.goal_x, p.goal_y, static_cast<std::int32_t>(p.acted_at),
              static_cast<std::int32_t>(p.premove_at), p.pursuit, p.premove, p.attack_mode, p.acted,
              p.move_queued, p.target_id, p.acts, static_cast<std::int32_t>(p.asked_at), p.skill,
              p.range, p.cell_x, p.cell_y, p.queued_x, p.queued_y})
          fold(static_cast<std::uint32_t>(x));
        fold(static_cast<std::uint32_t>(4 * actor_messages.size()));
        for (const auto& m : actor_messages) {
          for (const std::int32_t x : {m.id, m.a, m.b, m.c}) fold(static_cast<std::uint32_t>(x));
        }
        actor_messages.clear();
        fold(static_cast<std::uint32_t>(7 * own_moves.size()));
        for (const auto& m : own_moves) {
          for (const std::int32_t x : {static_cast<std::int32_t>(m.time), m.start_x, m.start_y,
                                       m.sub_x, m.sub_y, m.goal_x, m.goal_y})
            fold(static_cast<std::uint32_t>(x));
        }
        own_moves.clear();
        fold(static_cast<std::uint32_t>(picked.size()));
        fold(static_cast<std::uint32_t>(std::min<std::size_t>(picked.size(), 16)));
        for (std::size_t k = 0; k < picked.size() && k < 16; ++k)
          fold(static_cast<std::uint32_t>(picked[k]));
      };
      // m: after the own fields +0x328, +0x33c and +0x34c, the player's speed, CGameMode+0x364, and
      // the one pursued's speed and +0x1f4, the messages through CPlayer::SendMsg, as
      // tools/path_oracle.py's orders().
      if (orders) {
        const std::size_t r = base_fields + kOwnFields;
        ragexe::world::OwnPlayer& p = *actor.own;
        p.walk_asked = static_cast<std::int32_t>(v[r]);
        p.walk_asked_at = v[r + 1];
        p.level = static_cast<std::int32_t>(v[r + 2]);
        context.own_speed = static_cast<std::int32_t>(v[r + 3]);
        context.skill_range = static_cast<std::int32_t>(v[r + 4]);
        pursued.speed = static_cast<std::int32_t>(v[r + 5]);
        pursued.field_0x1f4 = static_cast<std::int32_t>(v[r + 6]);
        std::vector<ragexe::world::CellSkill> cell_skills;
        context.cell_skills = &cell_skills;
        std::uint32_t tick = 0;
        std::uint32_t server = 0;
        context.send_own = [&](std::int32_t id, std::int32_t a, std::int32_t b) {
          ragexe::world::SendOwnPlayerMessage(actor, finder, {id, a, b}, context, tick, server);
        };
        const std::uint32_t count = v[kFields];
        constexpr std::size_t kRecord = 13;
        if (v.size() < kFields + 1 + kRecord * std::size_t{count}) {
          std::fprintf(stderr, "short go: %s\n", line.c_str());
          return 2;
        }
        for (std::uint32_t n = 0; n < count; ++n) {
          const std::size_t k = kFields + 1 + kRecord * n;
          const auto i = [&](std::size_t j) { return static_cast<std::int32_t>(v[k + j]); };
          ragexe::world::OwnMessage message{i(0), i(1), i(2), i(3)};
          message.order = {v[k + 4], i(5), i(6), i(7), i(8), i(9), i(10)};
          message.object_skill = {i(5), i(6), i(7)};
          server = v[k + 11];
          tick = v[k + 12];
          ragexe::world::SendOwnPlayerMessage(actor, finder, message, context, tick, server);
          std::vector<std::uint32_t> steps;
          for (const auto& step : actor.path.steps) {
            steps.push_back(static_cast<std::uint32_t>(step.x));
            steps.push_back(static_cast<std::uint32_t>(step.y));
            steps.push_back(static_cast<std::uint32_t>(step.dir));
            steps.push_back(step.time);
          }
          const std::uint64_t digest = Fnv1a64(steps);
          fold(v[k]);
          fold_fields();
          fold(static_cast<std::uint32_t>(actor.path.steps.size()));
          fold(static_cast<std::uint32_t>(digest));
          fold(static_cast<std::uint32_t>(digest >> 32));
          fold_messages();
          fold_player();
          fold_own();
          for (const std::int32_t x :
               {p.walk_asked, static_cast<std::int32_t>(p.walk_asked_at), p.level})
            fold(static_cast<std::uint32_t>(x));
          fold(static_cast<std::uint32_t>(cell_skills.size()));
          for (const auto& asked_for : cell_skills) {
            if (asked_for.window) {
              for (const std::int32_t x : {0x54, 0, 0x44, asked_for.skill, asked_for.x, asked_for.y,
                                           asked_for.level, 0, 0})
                fold(static_cast<std::uint32_t>(x));
            } else {
              for (const std::int32_t x :
                   {0x4d, asked_for.skill, asked_for.x, asked_for.y, asked_for.level, 0, 0})
                fold(static_cast<std::uint32_t>(x));
            }
          }
          cell_skills.clear();
        }
        path = actor.path;
        std::printf("sent %u %08x %08x %08x %08x %d %d %u %016llx\n", count,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                    static_cast<unsigned long long>(h));
        continue;
      }
      // u and p: the actor's whole update (CNpc::Update, CPc::Update) after +0x230, the key colour,
      // +0x87, +0x1ec, +0x290, +0x1f4, +0x218, effects on, the act's frames' events, +0x2c, +0x30,
      // +0x28, the world's player, the gauges, the point on screen, +0x48, the screen's height, the
      // cast's times, +0xcc, +0x1bc, the weapon, +0x54, +0x70 and +0x24, as
      // tools/path_oracle.py's update().
      if (update) {
        auto& base = static_cast<ragexe::world::GameActor&>(actor);
        const std::size_t b = base_fields;
        actor.sight_ticks = v[b];
        o.keyed = v[b + 1] != 0;
        o.keyed_at = v[b + 2];
        o.key_color = v[b + 3];
        o.tinted = v[b + 4] != 0;
        actor.field_0x1ec = static_cast<std::int32_t>(v[b + 5]);
        actor.repeated_at = v[b + 6];
        actor.field_0x1f4 = static_cast<std::int32_t>(v[b + 7]);
        actor.body_state = static_cast<std::int32_t>(v[b + 8]);
        context.effects_on = v[b + 9] != 0;
        const std::uint32_t events = v[b + 10];
        o.sounded_action = static_cast<std::int32_t>(v[b + 11]);
        o.sounded_frame = static_cast<std::int32_t>(v[b + 12]);
        o.frame = static_cast<std::int32_t>(v[b + 13]);
        player_at = {std::bit_cast<float>(v[b + 14]), 0.0f, std::bit_cast<float>(v[b + 15])};
        context.player = &player_at;
        ragexe::world::ActorWindows actor_windows;
        actor.cast_gauge = 0;
        if (v[b + 21] != 0) {
          actor_windows.sizes.push_back({0x3c, 6});
          actor.cast_gauge = static_cast<std::int32_t>(actor_windows.sizes.size());
        }
        if (player && v[b + 16] != 0) {
          actor_windows.sizes.push_back({0, 0});
          actor.gauge = static_cast<std::int32_t>(actor_windows.sizes.size());
        }
        o.screen_x = static_cast<std::int32_t>(v[b + 17]);
        o.screen_y = static_cast<std::int32_t>(v[b + 18]);
        o.pixel_scale = std::bit_cast<float>(v[b + 19]);
        context.windows = &actor_windows;
        context.screen_height = static_cast<std::int32_t>(v[b + 20]);
        actor.cast_end = v[b + 22];
        actor.cast_start = v[b + 23];
        actor.field_0xcc = static_cast<std::int32_t>(v[b + 24]);
        actor.field_0x1bc = static_cast<std::int32_t>(v[b + 25]);
        actor.weapon = static_cast<std::int32_t>(v[b + 26]);
        o.play_mode = static_cast<std::int32_t>(v[b + 27]);
        o.start_time = v[b + 28];
        o.action = static_cast<std::int32_t>(v[b + 29]);
        // Each frame's event by 3 bits of `events`, the first 10 frames, as the attacks' but named
        // for sounds.
        act.events = {"foot.wav", "a.wav.b", "a.wa", "X.WAV"};
        for (auto& frames : act.actions) {
          for (std::size_t k = 0; k < frames.size() && k < 10; ++k) {
            const std::uint32_t code = events >> (3 * k) & 7;
            frames[k].event = code == 0 || code == 7 ? -1
                              : code == 5            ? 4
                              : code == 6            ? 0x7fffffff
                                                     : static_cast<std::int32_t>(code) - 1;
          }
        }
        std::vector<std::int32_t> launched;
        std::vector<ragexe::world::EffectMessage> launched_messages;
        context.launched = &launched;
        context.launched_messages = &launched_messages;
        context.sounds = &sounds;
        // What the whole update writes beyond the fields, as tools/path_oracle.py's updated().
        const auto fold_updated = [&] {
          for (const std::uint32_t x :
               {actor.sight_ticks, actor.repeated_at, o.key_color, o.keyed_at,
                static_cast<std::uint32_t>(o.sounded_action),
                static_cast<std::uint32_t>(o.sounded_frame), actor.cast_end, actor.cast_start,
                static_cast<std::uint32_t>(actor.field_0xcc), o.keyed ? 1u : 0u,
                static_cast<std::uint32_t>(actor.cast_gauge)})
            fold(x);
          if (player) {
            fold(static_cast<std::uint32_t>(actor.gauge));
            fold(static_cast<std::uint32_t>(mode_field_0x2a0));
            fold(static_cast<std::uint32_t>(actor.saved_frame));
          }
          fold(static_cast<std::uint32_t>(8 * sounds.size()));
          for (const auto& sound : sounds) {
            const std::uint64_t n = Fnv1a64(std::span(
                reinterpret_cast<const std::uint8_t*>(sound.name.data()), sound.name.size()));
            fold(static_cast<std::uint32_t>(n));
            fold(static_cast<std::uint32_t>(n >> 32));
            for (const std::uint32_t x :
                 {FloatBits(sound.x), 0u, FloatBits(sound.z), 250u, 40u, FloatBits(1.0f)})
              fold(x);
          }
          sounds.clear();
          fold(static_cast<std::uint32_t>(launched.size()));
          for (const std::int32_t id : launched) fold(static_cast<std::uint32_t>(id));
          launched.clear();
          fold(static_cast<std::uint32_t>(6 * launched_messages.size()));
          for (const auto& m : launched_messages) {
            for (const std::int32_t x : {m.effect, 0, m.id, m.a, m.b, m.c})
              fold(static_cast<std::uint32_t>(x));
          }
          launched_messages.clear();
          fold(static_cast<std::uint32_t>(4 * actor_windows.calls.size()));
          for (const auto& call : actor_windows.calls) {
            for (const std::int32_t x :
                 {static_cast<std::int32_t>(call.kind), call.window, call.a, call.b})
              fold(static_cast<std::uint32_t>(x));
          }
          actor_windows.calls.clear();
          fold(static_cast<std::uint32_t>(windows.size()));
          for (const std::int32_t x : windows) fold(static_cast<std::uint32_t>(x));
          windows.clear();
        };
        int updates = 0;
        const char* end = "staying";
        for (std::size_t k = kFields; k + 1 < v.size(); k += 2) {
          const auto r = own      ? ragexe::world::UpdateOwnPlayer(actor, context, v[k + 1], v[k])
                         : player ? ragexe::world::UpdatePlayer(actor, context, v[k + 1], v[k])
                                  : ragexe::world::UpdateNpc(base, context, v[k + 1], v[k]);
          ++updates;
          const bool over =
              r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
          fold(over ? 0u : 1u);
          fold_fields();
          fold_messages();
          fold_updated();
          fold_own();
          if (over) {
            end = "faded";
            break;
          }
        }
        std::printf("%s %d %08x %08x %08x %08x %u %u %u %016llx\n", end, updates,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), static_cast<std::uint32_t>(o.action),
                    static_cast<std::uint32_t>(o.frame), static_cast<std::uint32_t>(actor.state),
                    static_cast<unsigned long long>(h));
        continue;
      }
      // D and H: the death, by vf10 (0), 0xc or 0x1c, after +0x218, +0x20c, +0x210, a sprite or
      // none and +0xcc; then the updates, as tools/path_oracle.py's die().
      if (die) {
        auto& base = static_cast<ragexe::world::GameActor&>(actor);
        const std::uint32_t how = v[base_fields];
        actor.body_state = static_cast<std::int32_t>(v[base_fields + 1]);
        actor.hits.resize(v[base_fields + 2]);
        actor.field_0x210 = static_cast<std::int32_t>(v[base_fields + 3]);
        actor.field_0xcc = static_cast<std::int32_t>(v[base_fields + 5]);
        std::vector<ragexe::world::Corpse> corpses;
        context.corpses = &corpses;
        context.act_name = "poring.act";
        if (v[base_fields + 4] != 0) context.sprite_name = "poring.spr";
        const std::uint32_t at = v[kFields + 1];
        if (how == 0) {
          if (player)
            ragexe::world::SetPlayerState(actor, 3, context, at);
          else
            ragexe::world::SetGameActorState(base, 3, context, at);
        } else if (how == 0xc) {
          if (player)
            ragexe::world::KillGameActorAfterQueue(actor, context, at);
          else
            ragexe::world::KillGameActorAfterQueue(base, context, at);
        } else if (player) {
          ragexe::world::KillGameActor(actor, context, at);
        } else {
          ragexe::world::KillGameActor(base, context, at);
        }
        const auto fold_name = [&](std::string_view name) {
          if (name.data() == nullptr) {
            fold(0);
            fold(0);
            return;
          }
          std::uint64_t n = 0xcbf29ce484222325ull;
          for (const char c : name) n = (n ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
          fold(static_cast<std::uint32_t>(n));
          fold(static_cast<std::uint32_t>(n >> 32));
        };
        const auto fold_died = [&] {
          fold(static_cast<std::uint32_t>(actor.id));
          fold(static_cast<std::uint32_t>(actor.field_0x210));
          fold(o.depth_up ? 1u : 0u);
          fold(static_cast<std::uint32_t>(actor.field_0xcc));
          fold(static_cast<std::uint32_t>(10 * corpses.size()));
          for (const auto& c : corpses) {
            for (const float x : c.position) fold(FloatBits(x));
            fold_name(c.act);
            fold_name(c.sprite);
            fold(static_cast<std::uint32_t>(c.action));
            fold(static_cast<std::uint32_t>(c.frame));
            fold(FloatBits(c.facing));
          }
          corpses.clear();
        };
        fold(5);
        fold_fields();
        fold_messages();
        fold_player();
        fold_effects();
        fold_died();
        const std::int32_t state = actor.state;
        int updates = 0;
        const char* end = "staying";
        for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
          const auto r = player
                             ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                             : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
          ++updates;
          const bool over =
              r == ragexe::world::StateResult::kFaded || r == ragexe::world::StateResult::kGone;
          fold(over ? 0u : 1u);
          fold_fields();
          fold_messages();
          fold_player();
          fold_effects();
          fold_died();
          if (over) {
            end = r == ragexe::world::StateResult::kGone ? "gone" : "faded";
            break;
          }
          if (actor.state != state) {
            end = "changed";
            break;
          }
        }
        std::printf("%s %d %08x %08x %08x %08x %d %d %u %016llx\n", end, updates,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                    static_cast<unsigned long long>(h));
        continue;
      }
      if (with_effects && !arrive) {
        const std::size_t count = v[kFields];
        if (v.size() < kFields + 3 * count + 3) {
          std::fprintf(stderr, "short effects: %s\n", line.c_str());
          return 2;
        }
        now = v[kFields + 3 * count + 2];
        for (std::size_t k = 0; k < count; ++k) {
          const auto message = static_cast<std::int32_t>(v[kFields + 1 + 3 * k]);
          const auto arg = static_cast<std::int32_t>(v[kFields + 2 + 3 * k]);
          const auto sender = static_cast<std::int32_t>(v[kFields + 3 + 3 * k]);
          if (player)
            ragexe::world::SendGameActorEffectMessage(actor, message, sender, arg, context, now);
          else
            ragexe::world::SendGameActorEffectMessage(static_cast<ragexe::world::GameActor&>(actor),
                                                      message, sender, arg, context, now);
          fold(static_cast<std::uint32_t>(message));
          fold_fields();
          fold_messages();
          fold_player();
          fold_effects();
        }
        std::printf("sent %zu %08x %08x %08x %08x %d %d %u %016llx\n", count,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                    static_cast<unsigned long long>(h));
        continue;
      }
      if (stand) {
        int updates = 0;
        const char* end = "standing";
        for (std::size_t k = kFields; k + 1 < v.size(); k += 2) {
          const auto r =
              player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                     : ragexe::world::ProcessGameActorState(
                           static_cast<ragexe::world::GameActor&>(actor), context, v[k + 1], v[k]);
          ++updates;
          fold(r == ragexe::world::StateResult::kFaded ? 0u : 1u);
          fold_fields();
          fold_messages();
          fold_player();
          fold(actor.pet_talk_at);
          if (r == ragexe::world::StateResult::kFaded) {
            end = "faded";
            break;
          }
        }
        std::printf("%s %d %08x %08x %08x %08x %d %d %d %016llx\n", end, updates,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, o.frame, actor.state,
                    static_cast<unsigned long long>(h));
        continue;
      }
      if (arrive) {
        auto& base = static_cast<ragexe::world::GameActor&>(actor);
        const auto message = static_cast<std::int32_t>(v[base_fields]);
        const auto how = static_cast<std::int32_t>(v[base_fields + 1]);
        const auto& r = record;
        bool ported = true;
        if (message == 3 || message == 4) {
          const ragexe::world::StandingArrival arrival{r[0],
                                                       r[1],
                                                       r[2],
                                                       r[3],
                                                       r[4],
                                                       r[5],
                                                       r[6],
                                                       r[7],
                                                       r[8],
                                                       r[9],
                                                       r[13],
                                                       static_cast<std::uint32_t>(r[14]),
                                                       static_cast<std::uint32_t>(r[15]),
                                                       r[16],
                                                       r[17],
                                                       r[18],
                                                       r[19],
                                                       r[23],
                                                       r[24],
                                                       r[25],
                                                       r[26]};
          ported =
              ragexe::world::ArriveGameActor(base, message, arrival, how, context, v[kFields + 1]);
        } else {
          const ragexe::world::WalkingArrival arrival{
              r[0],
              r[1],
              r[2],
              {static_cast<std::uint32_t>(r[3]), r[4], r[5], r[6], r[7], r[8], r[9]},
              r[10],
              r[11],
              r[14],
              static_cast<std::uint32_t>(r[15]),
              static_cast<std::uint32_t>(r[16]),
              r[17],
              r[24],
              r[25],
              r[26],
              r[27]};
          ragexe::world::ArriveWalkingGameActor(base, finder, message, arrival, v[kFields], context,
                                                v[kFields + 1]);
        }
        // What arriving writes beyond the fields, and what it sent, played and asked for, as
        // tools/path_oracle.py's arrival().
        const auto fold_arrival = [&] {
          for (const std::uint32_t x :
               {static_cast<std::uint32_t>(actor.id), static_cast<std::uint32_t>(actor.field_0x1ec),
                static_cast<std::uint32_t>(actor.field_0x1f0),
                static_cast<std::uint32_t>(actor.field_0x1f4),
                static_cast<std::uint32_t>(actor.body_state), actor.effect_state,
                actor.health_state, static_cast<std::uint32_t>(actor.field_0x224),
                static_cast<std::uint32_t>(actor.guild), static_cast<std::uint32_t>(actor.emblem),
                static_cast<std::uint32_t>(actor.speed), o.visible ? 1u : 0u,
                o.body_shown ? 1u : 0u, o.tinted ? 1u : 0u, o.shadow ? 1u : 0u,
                static_cast<std::uint32_t>(ground_water_fade),
                static_cast<std::uint32_t>(mode_field_0x2a0)}) {
            fold(x);
          }
          const auto fold_name = [&](std::string_view name) {
            const std::uint64_t n =
                Fnv1a64(std::span(reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
            fold(static_cast<std::uint32_t>(n));
            fold(static_cast<std::uint32_t>(n >> 32));
          };
          fold(static_cast<std::uint32_t>(5 * actor_messages.size()));
          for (const auto& m : actor_messages) {
            for (const std::int32_t x : {0, m.id, m.a, m.b, m.c})
              fold(static_cast<std::uint32_t>(x));
          }
          fold(static_cast<std::uint32_t>(8 * sounds.size()));
          for (const auto& sound : sounds) {
            fold_name(sound.name);
            for (const std::uint32_t x :
                 {FloatBits(sound.x), 0u, FloatBits(sound.z), 250u, 40u, FloatBits(1.0f)})
              fold(x);
          }
          fold(static_cast<std::uint32_t>(2 * asked.size()));
          for (const auto& name : asked) fold_name(name);
          actor_messages.clear();
          sounds.clear();
          asked.clear();
        };
        std::vector<std::uint32_t> steps;
        for (const auto& step : actor.path.steps) {
          steps.push_back(static_cast<std::uint32_t>(step.x));
          steps.push_back(static_cast<std::uint32_t>(step.y));
          steps.push_back(static_cast<std::uint32_t>(step.dir));
          steps.push_back(step.time);
        }
        const std::uint64_t digest = Fnv1a64(steps);
        fold(4);
        fold_fields();
        fold(static_cast<std::uint32_t>(actor.path.steps.size()));
        fold(static_cast<std::uint32_t>(digest));
        fold(static_cast<std::uint32_t>(digest >> 32));
        fold_messages();
        fold_arrival();
        fold_effects();
        const std::int32_t state = actor.state;
        int updates = 0;
        const char* end = ported ? "staying" : "unported";
        for (std::size_t k = kFields + 2; ported && k + 1 < v.size(); k += 2) {
          const auto result = ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
          ++updates;
          fold(result == ragexe::world::StateResult::kFaded ? 0u : 1u);
          fold_fields();
          fold_messages();
          fold_arrival();
          fold_effects();
          if (result == ragexe::world::StateResult::kFaded) {
            end = "faded";
            break;
          }
          if (actor.state != state) {
            end = "changed";
            break;
          }
        }
        path = actor.path;
        std::printf("%s %d %08x %08x %08x %08x %d %d %u %016llx\n", end, updates,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                    static_cast<unsigned long long>(h));
        continue;
      }
      if (enter) {
        auto& base = static_cast<ragexe::world::GameActor&>(actor);
        if (v[base_fields] != 0) {
          const auto state = static_cast<std::int32_t>(v[base_fields + 1]);
          if (player)
            ragexe::world::SetPlayerState(actor, state, context, v[kFields + 1]);
          else
            ragexe::world::SetGameActorState(base, state, context, v[kFields + 1]);
        }
        fold(3);
        fold_fields();
        fold_messages();
        fold_player();
        fold(static_cast<std::uint32_t>(actor.field_0xcc));
        const std::int32_t state = actor.state;
        int updates = 0;
        const char* end = "staying";
        for (std::size_t k = kFields + 2; k + 1 < v.size(); k += 2) {
          const auto r = player
                             ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                             : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
          ++updates;
          fold(r == ragexe::world::StateResult::kFaded ? 0u : 1u);
          fold_fields();
          fold_messages();
          fold_player();
          fold(static_cast<std::uint32_t>(actor.field_0xcc));
          if (r == ragexe::world::StateResult::kFaded) {
            end = "faded";
            break;
          }
          if (actor.state != state) {
            end = "changed";
            break;
          }
        }
        std::printf("%s %d %08x %08x %08x %08x %d %d %u %016llx\n", end, updates,
                    FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                    FloatBits(o.facing), o.action, o.frame, static_cast<std::uint32_t>(actor.state),
                    static_cast<unsigned long long>(h));
        continue;
      }
      const auto to_speed = static_cast<std::int32_t>(v[kFields - 1]);
      auto& base = static_cast<ragexe::world::GameActor&>(actor);
      if (to && player)
        ragexe::world::MoveGameActorTo(actor, finder, order.goal_x, order.goal_y, to_speed,
                                       v[kFields], context, v[kFields + 1]);
      else if (to)
        ragexe::world::MoveGameActorTo(base, finder, order.goal_x, order.goal_y, to_speed,
                                       v[kFields], context, v[kFields + 1]);
      else if (player)
        ragexe::world::MoveGameActor(actor, finder, order, v[kFields], context, v[kFields + 1]);
      else
        ragexe::world::MoveGameActor(base, finder, order, v[kFields], context, v[kFields + 1]);
      std::vector<std::uint32_t> steps;
      for (const auto& step : actor.path.steps) {
        steps.push_back(static_cast<std::uint32_t>(step.x));
        steps.push_back(static_cast<std::uint32_t>(step.y));
        steps.push_back(static_cast<std::uint32_t>(step.dir));
        steps.push_back(step.time);
      }
      const std::uint64_t digest = Fnv1a64(steps);
      fold(2);
      fold_fields();
      fold(static_cast<std::uint32_t>(actor.path.steps.size()));
      fold(static_cast<std::uint32_t>(digest));
      fold(static_cast<std::uint32_t>(digest >> 32));
      fold_messages();
      fold_player();
      int updates = 0;
      const char* end = actor.state == 1 ? "walking" : "standing";
      for (std::size_t k = kFields + 2; actor.state == 1 && k + 1 < v.size(); k += 2) {
        const auto r = player ? ragexe::world::ProcessGameActorState(actor, context, v[k + 1], v[k])
                              : ragexe::world::ProcessGameActorState(base, context, v[k + 1], v[k]);
        ++updates;
        fold(r == ragexe::world::StateResult::kFaded ? 0u : 1u);
        fold_fields();
        fold_messages();
        fold_player();
        if (r == ragexe::world::StateResult::kFaded) {
          end = "faded";
          break;
        }
        if (actor.state != 1) {
          end = "arrived";
          break;
        }
      }
      path = actor.path;
      std::printf("%s %d %08x %08x %08x %08x %d %d %d %016llx\n", end, updates,
                  FloatBits(o.position[0]), FloatBits(o.position[1]), FloatBits(o.position[2]),
                  FloatBits(o.facing), o.action, o.frame, actor.state,
                  static_cast<unsigned long long>(h));
    } else if (kind == "Z") {
      fields >> path.field_0x10;
      std::printf("%d\n", path.field_0x10);
    } else {
      std::fprintf(stderr, "unknown query: %s\n", line.c_str());
      return 2;
    }
  }
  return 0;
}

// Decodes .gnd files as CGndRes does and prints, per file, "n ok major minor width height
// cell-size-bits", then count:hash for the textures (each name and a NUL), the lightmaps
// (their 256 bytes), the surfaces (eight float bits, the texture index sign-extended, the
// lightmap index, the colour) and the cells (four float bits, three surface indices); or
// "n failed error".
int DecodeGnds(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    const std::vector<std::uint8_t> bytes = ReadFile(files[n]);
    ragexe::res::Ground ground;
    std::string error;
    if (!ragexe::res::DecodeGround(bytes, ground, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::vector<std::uint8_t> names;
    for (const std::string& name : ground.textures) {
      names.insert(names.end(), name.begin(), name.end());
      names.push_back(0);
    }
    std::vector<std::uint8_t> lightmaps;
    for (const auto& lightmap : ground.lightmaps) {
      lightmaps.insert(lightmaps.end(), lightmap.alpha.begin(), lightmap.alpha.end());
      lightmaps.insert(lightmaps.end(), lightmap.color.begin(), lightmap.color.end());
    }
    std::vector<std::uint32_t> surfaces;
    for (const auto& surface : ground.surfaces) {
      for (float u : surface.u) surfaces.push_back(FloatBits(u));
      for (float v : surface.v) surfaces.push_back(FloatBits(v));
      surfaces.push_back(static_cast<std::uint32_t>(std::int32_t{surface.texture}));
      surfaces.push_back(surface.lightmap);
      surfaces.push_back(surface.color);
    }
    std::vector<std::uint32_t> cells;
    for (const auto& cell : ground.cells) {
      for (float h : cell.heights) cells.push_back(FloatBits(h));
      for (std::int32_t s : cell.surfaces) cells.push_back(static_cast<std::uint32_t>(s));
    }
    std::printf("%zu ok %u %u %d %d %08x %zu:%016llx %zu:%016llx %zu:%016llx %zu:%016llx\n", n,
                ground.major, ground.minor, ground.width, ground.height,
                FloatBits(ground.cell_size), ground.textures.size(),
                static_cast<unsigned long long>(Fnv1a64(names)), ground.lightmaps.size(),
                static_cast<unsigned long long>(Fnv1a64(lightmaps)), ground.surfaces.size(),
                static_cast<unsigned long long>(Fnv1a64(surfaces)), ground.cells.size(),
                static_cast<unsigned long long>(Fnv1a64(cells)));
  }
  return failed == 0 ? 0 : 1;
}

std::string Hex(std::string_view bytes) {
  std::string out;
  for (unsigned char c : bytes) {
    char digits[3];
    std::snprintf(digits, sizeof digits, "%02x", c);
    out += digits;
  }
  return out.empty() ? "-" : out;
}

int Usage();

// Builds .gnd files as C3dGround15::Build does and prints, per file, "n ok", then
// count:hash (FNV-1a over little-endian values) for the cells (top, north and east surface
// indices, the lightmap colour's three bytes), the surfaces (whether used; if so the
// texture index, the lightmap block or -1, the colour and the four vertices' eight
// values), the atlas blocks (page, colour bytes, eight uv bits) and each atlas page (its
// texels; of a first page with room left, only its blocks' texels, block by block); or
// "n failed error".
int BuildGrounds(std::vector<std::string> args) {
  bool lightmaps = false;
  if (!args.empty() && args.front() == "--lightmaps") {
    lightmaps = true;
    args.erase(args.begin());
  }
  if (args.size() < 9) return Usage();
  ragexe::render::GroundLight light;
  for (std::size_t k = 0; k < 3; ++k) {
    light.direction[k] = std::stof(args[k]);
    light.diffuse[k] = std::stof(args[3 + k]);
    light.ambient[k] = std::stof(args[6 + k]);
  }
  int failed = 0;
  for (std::size_t n = 9; n < args.size(); ++n) {
    ragexe::res::Ground ground;
    ragexe::render::GroundMesh mesh;
    std::string error;
    if (!ragexe::res::DecodeGround(ReadFile(args[n]), ground, &error) ||
        !ragexe::render::BuildGround(ground, light, lightmaps, mesh, &error)) {
      std::printf("%zu failed %s\n", n - 9, error.c_str());
      ++failed;
      continue;
    }
    std::vector<std::uint8_t> bytes;
    const auto u32 = [&](std::uint32_t v) {
      for (int k = 0; k < 4; ++k) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
    };
    const auto hash = [&](std::size_t count) {
      char out[48];
      std::snprintf(out, sizeof out, " %zu:%016llx", count,
                    static_cast<unsigned long long>(Fnv1a64(bytes)));
      bytes.clear();
      return std::string(out);
    };
    std::string line = std::to_string(n - 9) + " ok";
    for (const auto& c : mesh.cells) {
      for (std::int32_t s : c.surfaces) u32(static_cast<std::uint32_t>(s));
      bytes.insert(bytes.end(), c.lightmap_color.begin(), c.lightmap_color.end());
    }
    line += hash(mesh.cells.size());
    for (const auto& s : mesh.surfaces) {
      bytes.push_back(s.used ? 1 : 0);
      if (!s.used) continue;
      u32(static_cast<std::uint32_t>(s.texture));
      u32(static_cast<std::uint32_t>(s.lightmap_block));
      u32(s.color);
      for (const auto& v : s.vertices) {
        for (float f : {v.x, v.y, v.z}) u32(FloatBits(f));
        u32(v.color);
        for (float f : {v.u, v.v, v.lightmap_u, v.lightmap_v}) u32(FloatBits(f));
      }
    }
    line += hash(mesh.surfaces.size());
    for (const auto& b : mesh.atlas.blocks) {
      u32(b.page);
      bytes.insert(bytes.end(), b.color.begin(), b.color.end());
      for (float f : b.uv) u32(FloatBits(f));
    }
    line += hash(mesh.atlas.blocks.size());
    const std::size_t blocks = mesh.atlas.blocks.size();
    for (std::size_t p = 0; p < mesh.atlas.pages.size(); ++p) {
      const auto& page = mesh.atlas.pages[p];
      if (p == 0 && blocks < ragexe::render::LightmapAtlas::kBlocksPerPage) {
        for (std::size_t i = 0; i < blocks; ++i) {
          for (std::size_t y = 0; y < 8; ++y) {
            for (std::size_t x = 0; x < 8; ++x) u32(page[(i / 32 * 8 + y) * 256 + i % 32 * 8 + x]);
          }
        }
      } else {
        for (std::uint32_t v : page) u32(v);
      }
      line += hash(p);
    }
    std::printf("%s\n", line.c_str());
  }
  return failed == 0 ? 0 : 1;
}

// Decodes .rsw files as C3dWorldRes does and prints, per file, "n ok major minor", the
// three names in hex, the water and light values (floats as bits), the four ints at
// +0x1b0, then count:hash for the models (name, file and node each with a NUL, the nine
// floats' bits, +0xec, +0xf0 and the speed's bits), the sound and effect records (their
// bytes) and the scene-graph bounds (their floats' bits); or "n failed error".
int DecodeRsws(std::vector<std::string> files) {
  bool lightmaps = true;
  if (!files.empty() && files.front() == "--no-lightmaps") {
    lightmaps = false;
    files.erase(files.begin());
  }
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::World world;
    std::string error;
    if (!ragexe::res::DecodeWorld(ReadFile(files[n]), lightmaps, world, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::printf("%zu ok %u %u %s %s %s %08x %d %08x %08x %08x %d %d %d", n, world.major,
                world.minor, Hex(world.ground).c_str(), Hex(world.attr).c_str(),
                Hex(world.extra).c_str(), FloatBits(world.water_level), world.water_type,
                FloatBits(world.water_param_170), FloatBits(world.water_phase_step),
                FloatBits(world.water_param_178), world.water_frame_interval, world.light_angle_y,
                world.light_angle_x);
    for (const auto* v : {&world.light_vector, &world.diffuse, &world.ambient}) {
      for (float f : *v) std::printf(" %08x", FloatBits(f));
    }
    for (std::int32_t v : world.field_1b0) std::printf(" %d", v);
    std::vector<std::uint8_t> models;
    for (const auto& model : world.models) {
      for (const std::string* s : {&model.name, &model.file, &model.node}) {
        models.insert(models.end(), s->begin(), s->end());
        models.push_back(0);
      }
      std::vector<std::uint32_t> values;
      for (const auto* v : {&model.position, &model.rotation, &model.scale}) {
        for (float f : *v) values.push_back(FloatBits(f));
      }
      values.push_back(static_cast<std::uint32_t>(model.field_ec));
      values.push_back(model.field_f0);
      values.push_back(FloatBits(model.animation_speed));
      for (std::uint32_t v : values) {
        for (int k = 0; k < 4; ++k) models.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
      }
    }
    std::vector<std::uint8_t> sounds;
    for (const auto& sound : world.sounds) sounds.insert(sounds.end(), sound.begin(), sound.end());
    std::vector<std::uint8_t> effects;
    for (const auto& effect : world.effects)
      effects.insert(effects.end(), effect.begin(), effect.end());
    std::vector<std::uint32_t> bounds;
    for (const auto& node : world.bounds) {
      for (const auto* v : {&node.max, &node.min, &node.half_extent, &node.centre}) {
        for (float f : *v) bounds.push_back(FloatBits(f));
      }
    }
    std::printf(" %zu:%016llx %zu:%016llx %zu:%016llx %zu:%016llx\n", world.models.size(),
                static_cast<unsigned long long>(Fnv1a64(models)), world.sounds.size(),
                static_cast<unsigned long long>(Fnv1a64(sounds)), world.effects.size(),
                static_cast<unsigned long long>(Fnv1a64(effects)), world.bounds.size(),
                static_cast<unsigned long long>(Fnv1a64(bounds)));
  }
  return failed == 0 ? 0 : 1;
}

// Decodes .rsm files as C3dModelRes does and prints, per file, "n ok animation-length
// +0x190 alpha root-hex node-count volumes-count:hash", then per node "N index name-hex
// parent children textures-hash fields-hash vertices tex-coords faces face-normals
// corner-normals translation-keys rotation-keys", each a count:hash of the values' bytes
// (names with a NUL each); or "n failed error".
// A CP949 name as the oracles print it: bytes above 0x7f as %XX.
std::string ArchiveName(std::string_view name) {
  std::string out;
  for (const char c : name) {
    const auto b = static_cast<unsigned char>(c);
    if (b < 0x80) {
      out += c;
    } else {
      char hex[4];
      std::snprintf(hex, sizeof hex, "%%%02X", b);
      out += hex;
    }
  }
  return out;
}

// Draws three frames of a world as CGameMode::OnFrame draws them (docs/render/frame.md), the
// player in the centre of the cell given and the cell cursor over that cell, the map's fog on as
// the client's FOG setting has it by default: the camera as PlaceCamera places it, then turned
// by 120 and 240 degrees. Prints "X k name" for each texture
// in the order made (an unnamed one, a lightmap page, as "#k"), then per frame "frame k" and
// each call DrawScene makes on the device: "R state value", "S stage state value", "T stage
// texture" and "D type fvf count" with the FNV-1a hash of the vertices' dwords (--raw: the
// dwords).
int DrawFrame(std::vector<std::string> args) {
  bool raw = false;
  if (!args.empty() && args.front() == "--raw") {
    raw = true;
    args.erase(args.begin());
  }
  if (args.size() != 4) {
    std::fprintf(stderr, "usage: ragres draw-frame [--raw] INSTALL MAP CELL_X CELL_Y\n");
    return 2;
  }
  const std::filesystem::path install = args[0];
  std::vector<std::unique_ptr<ragexe::res::GrfArchive>> archives;
  ragexe::res::DataFiles files(install);
  if (!OpenInstall(install, archives, files)) return 1;
  const std::string& map = args[1];
  ragexe::render::WorldScene scene;
  std::array<float, 2> point{};
  if (!LoadSceneAtCell(files, map, args[2], args[3], scene, point)) return 1;
  std::vector<std::string> indoor_maps;
  const bool indoor =
      ragexe::res::LoadFields(files, ragexe::res::kIndoorRswTable, true, indoor_maps) &&
      std::binary_search(indoor_maps.begin(), indoor_maps.end(), map + ".rsw");

  ragexe::render::WorldFrameState state(files);
  ragexe::render::EnterWorld(scene, point[0], point[1], indoor, state);
  ragexe::res::LineRecords fog_table;
  ragexe::render::SetFogEnabled(state,
                                ragexe::res::LoadFogParameters(files, fog_table) &&
                                    ragexe::render::FindMapFog(fog_table, map + ".rsw", state.fog));
  state.cursor = {static_cast<std::int32_t>(std::stol(args[2])),
                  static_cast<std::int32_t>(std::stol(args[3])), false, 0x6600ffaa};
  std::vector<std::vector<ragexe::render::SceneCall>> frames;
  std::vector<std::vector<ragexe::render::FrameDraw>> frame_draws;
  const float placed = state.camera.yaw;
  for (const float turn : {0.0f, 120.0f, 240.0f}) {
    if (turn != 0.0f) {
      state.camera.yaw = placed + turn;
      state.camera.target_yaw = state.camera.yaw;
    }
    frame_draws.emplace_back();
    frames.push_back(ragexe::render::DrawWorldFrame(scene, state, 0, frame_draws.back()));
  }

  std::vector<std::uint32_t> unnamed;
  for (std::size_t k = 0; k < state.textures.size(); ++k) {
    const std::string& name = state.textures.at(static_cast<std::uint32_t>(k + 1)).name;
    if (name.empty()) unnamed.push_back(static_cast<std::uint32_t>(k + 1));
    std::printf("X %zu %s\n", k,
                name.empty() ? ("#" + std::to_string(unnamed.size() - 1)).c_str()
                             : ArchiveName(name).c_str());
  }
  const auto texture_name = [&](std::uint32_t id) -> std::string {
    if (id == 0) return "-";
    const std::string& name = state.textures.at(id).name;
    if (!name.empty()) return ArchiveName(name);
    return "#" + std::to_string(std::find(unnamed.begin(), unnamed.end(), id) - unnamed.begin());
  };
  using Kind = ragexe::render::SceneCall::Kind;
  for (std::size_t f = 0; f < frames.size(); ++f) {
    std::printf("frame %zu\n", f);
    std::size_t draw = 0;
    for (const ragexe::render::SceneCall& call : frames[f]) {
      switch (call.kind) {
        case Kind::kRenderState: std::printf("R %u %u\n", call.state, call.value); break;
        case Kind::kStageState:
          std::printf("S %d %u %u\n", call.stage, call.state, call.value);
          break;
        case Kind::kTexture:
          std::printf("T %d %s\n", call.stage, texture_name(call.value).c_str());
          break;
        case Kind::kDraw: {
          const ragexe::render::FrameDraw& d = frame_draws[f][draw++];
          std::printf("D %u %#x %u", d.type, d.fvf, d.count);
          if (raw) {
            for (const std::uint32_t w : d.words) std::printf(" %08x", w);
            std::printf("\n");
          } else {
            std::printf(" %016llx\n", static_cast<unsigned long long>(Fnv1a64(d.words)));
          }
          break;
        }
      }
    }
  }
  return 0;
}

int DecodeRsms(std::vector<std::string> files) {
  auto precision = ragexe::res::FpuPrecision::kDouble;
  if (!files.empty() && files.front() == "--single") {
    precision = ragexe::res::FpuPrecision::kSingle;
    files.erase(files.begin());
  }
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::Model model;
    std::string error;
    if (!ragexe::res::DecodeModel(ReadFile(files[n]), model, &error, precision)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    const auto bytes = [](const auto& values) {
      std::vector<std::uint8_t> out(sizeof values[0] * values.size());
      if (!out.empty()) std::memcpy(out.data(), values.data(), out.size());
      return out;
    };
    const auto hash = [](std::size_t count, const std::vector<std::uint8_t>& b) {
      char text[48];
      std::snprintf(text, sizeof text, "%zu:%016llx", count,
                    static_cast<unsigned long long>(Fnv1a64(b)));
      return std::string(text);
    };
    std::vector<std::uint8_t> volumes;
    for (const auto& volume : model.volumes) {
      const auto b = bytes(volume.values);
      volumes.insert(volumes.end(), b.begin(), b.end());
      for (int k = 0; k < 4; ++k)
        volumes.push_back(
            static_cast<std::uint8_t>(static_cast<std::uint32_t>(volume.flag) >> (8 * k)));
    }
    std::printf("%zu ok %d %d %u %s %zu %s\n", n, model.animation_length, model.field_190,
                model.alpha, Hex(model.root).c_str(), model.nodes.size(),
                hash(model.volumes.size(), volumes).c_str());
    for (std::size_t k = 0; k < model.nodes.size(); ++k) {
      const auto& node = model.nodes[k];
      std::string children;
      for (int c : node.children) children += (children.empty() ? "" : ",") + std::to_string(c);
      std::vector<std::uint8_t> names;
      for (std::int32_t id : node.textures) {
        const std::string& name = model.textures[static_cast<std::size_t>(id)];
        names.insert(names.end(), name.begin(), name.end());
        names.push_back(0);
      }
      std::vector<std::uint8_t> fields;
      for (const auto* v : {&node.position, &node.rotation_axis}) {
        const auto b = bytes(*v);
        fields.insert(fields.end(), b.begin(), b.end());
      }
      const auto angle = bytes(std::array<float, 1>{node.rotation_angle});
      fields.insert(fields.end(), angle.begin(), angle.end());
      const auto scale = bytes(node.scale);
      fields.insert(fields.end(), scale.begin(), scale.end());
      fields.push_back(node.alpha);
      const auto& mesh = node.mesh;
      std::vector<std::uint8_t> faces;
      for (const auto& f : mesh.faces) {
        std::uint8_t record[24];
        std::memcpy(record, f.vertices.data(), 6);
        std::memcpy(record + 6, f.tex_coords.data(), 6);
        std::memcpy(record + 12, &f.texture, 2);
        std::memcpy(record + 14, &f.field_0e, 2);
        std::memcpy(record + 16, &f.field_10, 4);
        std::memcpy(record + 20, &f.smoothing_group, 4);
        faces.insert(faces.end(), record, record + 24);
      }
      std::vector<std::uint8_t> tkeys;
      for (const auto& key : node.translation_keys) {
        std::uint8_t record[16];
        std::memcpy(record, &key.frame, 4);
        std::memcpy(record + 4, key.value.data(), 12);
        tkeys.insert(tkeys.end(), record, record + 16);
      }
      std::vector<std::uint8_t> rkeys;
      for (const auto& key : node.rotation_keys) {
        std::uint8_t record[20];
        std::memcpy(record, &key.frame, 4);
        std::memcpy(record + 4, key.value.data(), 16);
        rkeys.insert(rkeys.end(), record, record + 20);
      }
      std::printf("N %zu %s %d %s %s %s %s %s %s %s %s %s %s\n", k, Hex(node.name).c_str(),
                  node.parent, children.empty() ? "-" : children.c_str(),
                  hash(node.textures.size(), names).c_str(), hash(1, fields).c_str(),
                  hash(mesh.vertices.size(), bytes(mesh.vertices)).c_str(),
                  hash(mesh.tex_coords.size(), bytes(mesh.tex_coords)).c_str(),
                  hash(mesh.faces.size(), faces).c_str(),
                  hash(mesh.face_normals.size(), bytes(mesh.face_normals)).c_str(),
                  hash(mesh.corner_normals.size(), bytes(mesh.corner_normals)).c_str(),
                  hash(node.translation_keys.size(), tkeys).c_str(),
                  hash(node.rotation_keys.size(), rkeys).c_str());
    }
  }
  return failed == 0 ? 0 : 1;
}

// Decodes .rsx files as C3dAnimModelRes does and prints, per file, "n ok +0x140
// frame-count +0x148 +0x14c-bits +0x154..+0x15c-bits +0x160 textures root-hex
// node-count", then per node "N index name-hex parent children textures vertex-count
// vertices tex-coord-count shared tex-coords faces", the lists as count:hash of their
// bytes (names with a NUL each); or "n failed error".
int DecodeRsxs(const std::vector<std::string>& files) {
  int failed = 0;
  const auto hash = [](std::size_t count, const void* data, std::size_t size) {
    char text[48];
    std::snprintf(text, sizeof text, "%zu:%016llx", count,
                  static_cast<unsigned long long>(
                      Fnv1a64(std::span(static_cast<const std::uint8_t*>(data), size))));
    return std::string(text);
  };
  const auto names = [&](const std::vector<std::string>& all,
                         const std::vector<std::int32_t>& ids) {
    std::vector<std::uint8_t> b;
    for (std::int32_t id : ids) {
      const std::string& name = all[static_cast<std::size_t>(id)];
      b.insert(b.end(), name.begin(), name.end());
      b.push_back(0);
    }
    return hash(ids.size(), b.data(), b.size());
  };
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::AnimModel model;
    std::string error;
    if (!ragexe::res::DecodeAnimModel(ReadFile(files[n]), model, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::vector<std::int32_t> all(model.textures.size());
    for (std::size_t k = 0; k < all.size(); ++k) all[k] = static_cast<std::int32_t>(k);
    std::printf("%zu ok %d %d %d %08x %08x %08x %08x %d %s %s %zu\n", n, model.field_140,
                model.frame_count, model.field_148, FloatBits(model.field_14c),
                FloatBits(model.field_154[0]), FloatBits(model.field_154[1]),
                FloatBits(model.field_154[2]), model.field_160, names(model.textures, all).c_str(),
                Hex(model.root).c_str(), model.nodes.size());
    for (std::size_t k = 0; k < model.nodes.size(); ++k) {
      const auto& node = model.nodes[k];
      std::string children;
      for (int c : node.children) children += (children.empty() ? "" : ",") + std::to_string(c);
      std::vector<std::uint8_t> faces;
      for (const auto& f : node.faces) {
        std::uint8_t record[24];
        std::memcpy(record, f.vertices.data(), 6);
        std::memcpy(record + 6, f.tex_coords.data(), 6);
        std::memcpy(record + 12, &f.texture, 2);
        std::memcpy(record + 14, &f.field_0e, 2);
        std::memcpy(record + 16, &f.field_10, 4);
        std::memcpy(record + 20, &f.smoothing_group, 4);
        faces.insert(faces.end(), record, record + 24);
      }
      std::printf(
          "N %zu %s %d %s %s %d %s %d %d %s %s\n", k, Hex(node.name).c_str(), node.parent,
          children.empty() ? "-" : children.c_str(), names(model.textures, node.textures).c_str(),
          node.vertex_count,
          hash(node.vertices.size(), node.vertices.data(), node.vertices.size() * 12).c_str(),
          node.tex_coord_count, node.shared_tex_coords,
          hash(node.tex_coords.size(), node.tex_coords.data(), node.tex_coords.size() * 12).c_str(),
          hash(node.faces.size(), faces.data(), faces.size()).c_str());
    }
  }
  return failed == 0 ? 0 : 1;
}

// Decodes .imf files as CImfRes does and prints, per file, "n ok version-bits +4
// layer-count actions frames hash", the hash an FNV-1a of each layer's action count, each
// action's frame count and each frame's three ints; or "n failed error".
int DecodeImfs(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::Imf imf;
    std::string error;
    if (!ragexe::res::DecodeImf(ReadFile(files[n]), imf, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::vector<std::uint32_t> values;
    std::size_t actions = 0;
    std::size_t frames = 0;
    for (const auto& layer : imf.layers) {
      values.push_back(static_cast<std::uint32_t>(layer.size()));
      actions += layer.size();
      for (const auto& action : layer) {
        values.push_back(static_cast<std::uint32_t>(action.size()));
        frames += action.size();
        for (const auto& frame : action) {
          for (std::int32_t v : {frame.order, frame.x, frame.y})
            values.push_back(static_cast<std::uint32_t>(v));
        }
      }
    }
    std::printf("%zu ok %08x %u %zu %zu %zu %016llx\n", n, FloatBits(imf.version), imf.field_4,
                imf.layers.size(), actions, frames,
                static_cast<unsigned long long>(Fnv1a64(values)));
  }
  return failed == 0 ? 0 : 1;
}

// Decodes .rma files as CRmaRes does and prints, per file, "n ok interior-zoom-bits
// shadow-zoom-bits offset-x offset-y frames frames message-count names actions", names
// as count:hash of each name and a NUL, actions as count:hash of each action's three
// values, list length, list and name index; or "n failed error".
int DecodeRmas(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::Rma rma;
    std::string error;
    if (!ragexe::res::DecodeRma(ReadFile(files[n]), rma, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::vector<std::uint8_t> names;
    for (const std::string& name : rma.rsx_names) {
      names.insert(names.end(), name.begin(), name.end());
      names.push_back(0);
    }
    std::vector<std::uint32_t> actions;
    for (const auto& action : rma.actions) {
      for (std::int32_t v : {action.value_0, action.value_4, action.value_8,
                             static_cast<std::int32_t>(action.list.size())})
        actions.push_back(static_cast<std::uint32_t>(v));
      for (std::int32_t v : action.list) actions.push_back(static_cast<std::uint32_t>(v));
      actions.push_back(static_cast<std::uint32_t>(action.rsx));
    }
    std::printf("%zu ok %08x %08x %d %d %d %d %zu %zu:%016llx %zu:%016llx\n", n,
                FloatBits(rma.interior_zoom), FloatBits(rma.shadow_zoom), rma.interior_offset_x,
                rma.interior_offset_y, rma.interior_frames[0], rma.interior_frames[1],
                rma.messages.size(), rma.rsx_names.size(),
                static_cast<unsigned long long>(Fnv1a64(names)), rma.actions.size(),
                static_cast<unsigned long long>(Fnv1a64(actions)));
  }
  return failed == 0 ? 0 : 1;
}

// Decodes .str files as CEZeffectRes does and prints, per file, "n ok +0x110 +0x114
// layer-count", then per layer "L index texture-count names keys", names as count:hash of
// each name and a NUL and keys as count:hash of their 0x7c bytes; or "n failed error".
int DecodeStrs(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::Str str;
    std::string error;
    if (!ragexe::res::DecodeStr(ReadFile(files[n]), str, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::printf("%zu ok %d %d %zu\n", n, str.field_110, str.field_114, str.layers.size());
    for (std::size_t k = 0; k < str.layers.size(); ++k) {
      const auto& layer = str.layers[k];
      std::vector<std::uint8_t> names;
      for (const std::string& name : layer.textures) {
        names.insert(names.end(), name.begin(), name.end());
        names.push_back(0);
      }
      std::vector<std::uint8_t> keys;
      for (const auto& key : layer.keys) {
        std::uint8_t record[0x7c];
        std::memcpy(record, &key.frame, 4);
        std::memcpy(record + 4, &key.type, 4);
        std::memcpy(record + 8, key.values.data(), sizeof key.values);
        keys.insert(keys.end(), record, record + sizeof record);
      }
      std::printf("L %zu %d %zu:%016llx %zu:%016llx\n", k, layer.texture_count,
                  layer.textures.size(), static_cast<unsigned long long>(Fnv1a64(names)),
                  layer.keys.size(), static_cast<unsigned long long>(Fnv1a64(keys)));
    }
  }
  return failed == 0 ? 0 : 1;
}

int DecodeEbms(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::Bitmap emblem;
    std::string error;
    if (!ragexe::res::DecodeEmblem(ReadFile(files[n]), emblem, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::vector<std::uint8_t> pixels(emblem.pixels.size() * 4);
    std::memcpy(pixels.data(), emblem.pixels.data(), pixels.size());
    std::printf("%zu ok %d %d %zu:%016llx\n", n, emblem.width, emblem.height, emblem.pixels.size(),
                static_cast<unsigned long long>(Fnv1a64(pixels)));
    // The inflated bytes the client hands to its bitmap, or "-" where it hands none.
    std::vector<std::uint8_t> inflated;
    if (ragexe::res::InflateEmblem(ReadFile(files[n]), inflated) ==
            ragexe::res::EmblemInflate::kInflated &&
        inflated.size() != ragexe::res::kEmblemBufferSize) {
      std::printf("I %zu:%016llx\n", inflated.size(),
                  static_cast<unsigned long long>(Fnv1a64(inflated)));
    } else {
      std::printf("I -\n");
    }
  }
  return failed == 0 ? 0 : 1;
}

int DecodeWavs(const std::vector<std::string>& files) {
  static constexpr const char* kTypes[] = {"other", "pcm", "ima-adpcm", "mpeg-layer3"};
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::Wave wave;
    std::string error;
    if (!ragexe::res::DecodeWave(ReadFile(files[n]), wave, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::printf("%zu ok %s %zu:%016llx\n", n, kTypes[static_cast<int>(wave.type)],
                wave.sample.size(), static_cast<unsigned long long>(Fnv1a64(wave.sample)));
  }
  return failed == 0 ? 0 : 1;
}

std::string TextHash(std::string_view text) {
  char out[17];
  std::snprintf(out, sizeof out, "%016llx",
                static_cast<unsigned long long>(Fnv1a64(std::span<const std::uint8_t>(
                    reinterpret_cast<const std::uint8_t*>(text.data()), text.size()))));
  return out;
}

std::string LinesHash(const std::vector<std::string>& lines) {
  std::string joined;
  for (const std::string& line : lines) joined.append(line).push_back('\0');
  return std::to_string(lines.size()) + ":" + TextHash(joined);
}

void PrintItem(const char* id, const ragexe::res::ItemInfo& item) {
  std::vector<std::uint32_t> params(item.params.begin(), item.params.end());
  std::printf("I %s %s %s %s %s %s %s %d %d %zu:%016llx\n", id,
              TextHash(item.identified_name).c_str(), TextHash(item.identified_resource).c_str(),
              LinesHash(item.identified_description).c_str(), TextHash(item.name).c_str(),
              TextHash(item.resource).c_str(), LinesHash(item.description).c_str(), item.slot,
              item.slot_count, params.size(), static_cast<unsigned long long>(Fnv1a64(params)));
}

void PrintCard(const char* id, const ragexe::res::CardInfo& card) {
  std::printf("C %s %s %s %s\n", id, TextHash(card.illustration).c_str(),
              TextHash(card.prefix).c_str(), TextHash(card.postfix).c_str());
}

int DecodeNamePairs(const std::vector<std::string>& files) {
  for (std::size_t n = 0; n < files.size(); ++n) {
    const ragexe::res::NamePairs pairs = ragexe::res::ParseNamePairs(ReadFile(files[n]));
    std::printf("%zu ok %zu\n", n, pairs.size());
    for (const auto& [first, second] : pairs) {
      std::printf("P %s %s\n", TextHash(first).c_str(), TextHash(second).c_str());
    }
  }
  return 0;
}

int DecodeNumberRecords(const std::vector<std::string>& files) {
  for (std::size_t n = 0; n < files.size(); ++n) {
    const auto records = ragexe::res::ParseNumberRecords(ReadFile(files[n]));
    std::printf("%zu ok %zu\n", n, records.size());
    for (const auto& [name, numbers] : records) {
      std::vector<std::uint32_t> values(numbers.begin(), numbers.end());
      std::printf("R %s %zu:%016llx\n", TextHash(name).c_str(), values.size(),
                  static_cast<unsigned long long>(Fnv1a64(values)));
    }
  }
  return 0;
}

int DecodeLineRecords(const std::vector<std::string>& files) {
  for (std::size_t n = 0; n < files.size(); ++n) {
    const auto records = ragexe::res::ParseLineRecords(ReadFile(files[n]));
    std::printf("%zu ok %zu\n", n, records.size());
    for (const auto& [name, lines] : records) {
      std::printf("R %s %s\n", TextHash(name).c_str(), LinesHash(lines).c_str());
    }
  }
  return 0;
}

int DecodeFields(std::vector<std::string> files) {
  const bool sorted = !files.empty() && files[0] == "--sorted";
  if (sorted) files.erase(files.begin());
  for (std::size_t n = 0; n < files.size(); ++n) {
    const auto fields = ragexe::res::ParseFields(ReadFile(files[n]), sorted);
    std::printf("%zu ok %zu\n", n, fields.size());
    for (const std::string& field : fields) std::printf("F %s\n", TextHash(field).c_str());
  }
  return 0;
}

int DecodeFogParameters(const std::vector<std::string>& files) {
  for (std::size_t n = 0; n < files.size(); ++n) {
    const auto records = ragexe::res::ParseFogParameters(ReadFile(files[n]));
    std::printf("%zu ok %zu\n", n, records.size());
    for (const auto& [name, values] : records) {
      std::printf("R %s %s\n", TextHash(name).c_str(), LinesHash(values).c_str());
    }
  }
  return 0;
}

// Each element in document order: its depth, name, text and number of children.
void PrintXml(const ragexe::res::XmlElement& element, int depth) {
  std::printf("E %d %s %s %zu\n", depth, TextHash(element.name).c_str(),
              TextHash(element.text).c_str(), element.children.size());
  for (const auto& child : element.children) PrintXml(child, depth + 1);
}

int DecodeXml(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::XmlElement document;
    std::string error;
    if (!ragexe::res::ParseXml(ReadFile(files[n]), document, &error)) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::printf("%zu ok\n", n);
    PrintXml(document, 0);
  }
  return failed == 0 ? 0 : 1;
}

// A string in double quotes, with '"', '\\' and bytes outside 0x20..0x7e escaped.
std::string Quoted(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    const auto b = static_cast<unsigned char>(c);
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (b < 0x20 || b > 0x7e) {
      char hex[5];
      std::snprintf(hex, sizeof hex, "\\x%02x", b);
      out += hex;
    } else {
      out.push_back(c);
    }
  }
  return out + "\"";
}

void PrintClientInfo(const ragexe::res::ClientInfo& info) {
  static constexpr const char* kLanguages[] = {"default", "traditional-chinese", "thai"};
  std::printf("service_type %d\nserver_type %d\n", info.service_type, info.server_type);
  std::printf("hide_account_list %d\npassword_encrypt %d\npassword_encrypt2 %d\n",
              info.hide_account_list, info.password_encrypt, info.password_encrypt2);
  std::printf("address %s\nport %s\nregistration_web %s\nversion %d\n",
              Quoted(info.address).c_str(), Quoted(info.port).c_str(),
              Quoted(info.registration_web).c_str(), info.version);
  for (const auto& [name, ids] :
       {std::pair{"admins_a", &info.admins_a}, std::pair{"admins_b", &info.admins_b}}) {
    std::printf("%s %zu", name, ids->size());
    for (const std::int32_t id : *ids) std::printf(" %d", id);
    std::printf("\n");
  }
  std::printf("charset %d\ncode_page %u\nlanguage %s\n", info.charset, info.code_page,
              kLanguages[static_cast<int>(info.language)]);
  std::printf("flag_67c9cf %d\nflag_67c9d0 %d\nvalue_60bbd8 %d\n", info.flag_67c9cf,
              info.flag_67c9d0, info.value_60bbd8);
  for (const auto& [name, text] :
       {std::pair{"licence", &info.licence}, std::pair{"registry_path", &info.registry_path},
        std::pair{"game_name", &info.game_name}, std::pair{"executable", &info.executable},
        std::pair{"patcher", &info.patcher}, std::pair{"run_message", &info.run_message}}) {
    std::printf("%s %s\n", name, Quoted(*text).c_str());
  }
  for (const std::string& message : info.messages)
    std::printf("message %s\n", Quoted(message).c_str());
}

int DecodeClientInfo(std::vector<std::string> args) {
  int connection = -1;
  if (args.size() >= 2 && args[0] == "--connection") {
    connection = std::stoi(args[1]);
    args.erase(args.begin(), args.begin() + 2);
  }
  int failed = 0;
  for (std::size_t n = 0; n < args.size(); ++n) {
    ragexe::res::XmlElement document;
    ragexe::res::ClientInfo info;
    std::string error;
    if (!ragexe::res::ParseXml(ReadFile(args[n]), document, &error) ||
        !ragexe::res::ReadClientInfo(document, info, &error) ||
        (connection >= 0 && !ragexe::res::SelectConnection(document, connection, info, &error))) {
      std::printf("%zu failed %s\n", n, error.c_str());
      ++failed;
      continue;
    }
    std::printf("%zu ok\n", n);
    PrintClientInfo(info);
    for (const auto& entry : ragexe::res::ListConnections(document)) {
      std::printf("connection %s %s\n", Quoted(entry.display).c_str(), Quoted(entry.desc).c_str());
    }
  }
  return failed == 0 ? 0 : 1;
}

int DecodeTips(const std::vector<std::string>& files) {
  for (std::size_t n = 0; n < files.size(); ++n) {
    const auto tips = ragexe::res::ParseTips(ragexe::res::ReadAsText(ReadFile(files[n])));
    std::printf("%zu ok %zu\n", n, tips.size());
    for (const auto& tip : tips) {
      std::printf("tip %zu\n", tip.size());
      for (const std::string& line : tip) std::printf("line %s\n", Quoted(line).c_str());
    }
  }
  return 0;
}

int DecodeManner(const std::vector<std::string>& args) {
  if (args.empty() || args.size() > 2) return Usage();
  ragexe::res::MannerFilter filter;
  filter.Parse(ReadFile(args[0]));
  std::printf("words %zu\n", filter.words().size());
  for (const std::string& word : filter.words()) std::printf("word %s\n", Quoted(word).c_str());
  if (args.size() == 2) {
    const std::vector<std::uint8_t> probes = ReadFile(args[1]);
    std::size_t start = 0;
    for (std::size_t i = 0; i <= probes.size(); ++i) {
      if (i < probes.size() && probes[i] != '\n') continue;
      const std::string probe(reinterpret_cast<const char*>(probes.data()) + start, i - start);
      if (i < probes.size() || !probe.empty()) {
        std::printf("probe %s %s\n", Quoted(probe).c_str(),
                    filter.IsClean(probe) ? "clean" : "banned");
      }
      start = i + 1;
    }
  }
  return 0;
}

int DecodeItemTables(const std::vector<std::string>& args) {
  if (args.empty()) return Usage();
  ragexe::res::ItemTableSettings settings;
  for (std::size_t k = 1; k < args.size(); ++k) {
    if (args[k] == "--korea") {
      settings.korean_service = true;
    } else if (args[k] == "--code-page" && k + 1 < args.size()) {
      settings.code_page = static_cast<unsigned>(std::stoul(args[++k]));
    } else {
      return Usage();
    }
  }
  const ragexe::res::DataFiles files(args[0]);
  ragexe::res::ItemTables tables;
  std::string error;
  if (!ragexe::res::LoadItemTables(files, settings, tables, &error)) {
    std::printf("failed %s\n", error.c_str());
    return 1;
  }
  for (const auto& [id, item] : tables.items) PrintItem(std::to_string(id).c_str(), item);
  PrintItem("fallback", tables.unknown_item);
  for (const auto& [id, card] : tables.cards) PrintCard(std::to_string(id).c_str(), card);
  PrintCard("fallback", tables.unknown_card);
  for (const auto& [id, lines] : tables.metal_process) {
    std::printf("M %d %s\n", id, LinesHash(lines).c_str());
  }
  return 0;
}

// The resource types of docs/formats/resources.md, in the order the original registers
// them. gr2 (C3dGrannyModelRes, through granny2.dll) is not ported.
void RegisterResourceTypes(ragexe::res::ResourceManager& manager) {
  namespace res = ragexe::res;
  res::RegisterActTypes(manager);
  res::RegisterRmaTypes(manager);
  res::RegisterAttrTypes(manager);
  res::RegisterBitmapTypes(manager);
  res::RegisterEmblemTypes(manager);
  res::RegisterStrTypes(manager);
  res::RegisterGroundTypes(manager);
  res::RegisterImfTypes(manager);
  res::RegisterModelTypes(manager);
  res::RegisterAnimModelTypes(manager);
  res::RegisterPaletteTypes(manager);
  res::RegisterSpriteTypes(manager);
  res::RegisterWaveTypes(manager);
  res::RegisterWorldTypes(manager);
}

// A table the client opens by name under data\, and its loader.
struct TableLoader {
  std::string name;
  std::function<bool(const ragexe::res::DataFiles&, std::string*)> load;
};

std::vector<TableLoader> TableLoaders(ragexe::res::ResourceManager& manager) {
  namespace res = ragexe::res;
  std::vector<TableLoader> out;
  for (std::string_view name : res::kNamePairTables) {
    out.push_back({std::string(name), [name](const res::DataFiles& files, std::string*) {
                     res::NamePairs pairs;
                     return res::LoadNamePairs(files, name, pairs);
                   }});
  }
  for (std::string_view name : res::kNumberRecordTables) {
    out.push_back({std::string(name), [name](const res::DataFiles& files, std::string*) {
                     res::NumberRecords records;
                     return res::LoadNumberRecords(files, name, records);
                   }});
  }
  out.push_back({std::string(res::kSkillDescTable), [](const res::DataFiles& files, std::string*) {
                   res::LineRecords records;
                   return res::LoadLineRecords(files, res::kSkillDescTable, records);
                 }});
  for (const bool sorted : {false, true}) {
    const std::string_view name = sorted ? res::kIndoorRswTable : res::kMsgStringTable;
    out.push_back({std::string(name), [name, sorted](const res::DataFiles& files, std::string*) {
                     std::vector<std::string> fields;
                     return res::LoadFields(files, name, sorted, fields);
                   }});
  }
  out.push_back(
      {std::string(res::kFogParameterTable), [](const res::DataFiles& files, std::string*) {
         res::LineRecords records;
         return res::LoadFogParameters(files, records);
       }});
  out.push_back({"ResNameTable.txt", [&manager](const res::DataFiles&, std::string*) {
                   return manager.LoadNameTable();
                 }});
  out.push_back({"manner.txt", [](const res::DataFiles& files, std::string* error) {
                   res::MannerFilter filter;
                   return filter.Load(files, "manner.txt", error);
                 }});
  out.push_back(
      {std::string(res::kClientInfoFile), [](const res::DataFiles& files, std::string* error) {
         res::XmlElement document;
         res::ClientInfo info;
         return res::LoadClientInfo(files, res::kClientInfoFile, document, info, error);
       }});
  out.push_back(
      {std::string(res::kPetTalkTable), [](const res::DataFiles& files, std::string* error) {
         res::XmlElement document;
         return res::LoadPetTalkTable(files, document, error);
       }});
  // LoadItemTables reads all its files at once, a missing one as empty.
  for (std::string_view name : res::kItemTableFiles) {
    out.push_back({std::string(name), [](const res::DataFiles& files, std::string* error) {
                     res::ItemTables tables;
                     return res::LoadItemTables(files, res::ItemTableSettings{}, tables, error);
                   }});
  }
  return out;
}

struct LoaderCounts {
  std::size_t loaded = 0;
  std::size_t failed = 0;
};

// Loads every compressed entry of archives.back() under data\ through a fresh resource
// of the type its extension selects, as GetResource would clone it, and each of the
// client's tables it holds through that table's loader. Files are read from it, then from
// the archives before it, nearest first, as from the data.grf the patcher merges them
// into, then from the archive's directory. Adds "archive-name<TAB>entry-name" to failures
// for each that fails.
void CheckArchiveLoaders(const std::filesystem::path& path,
                         const std::vector<std::unique_ptr<GrfArchive>>& archives,
                         std::set<std::string>& failures) {
  namespace res = ragexe::res;
  const GrfArchive& archive = *archives.back();
  const std::string archive_name = path.filename().string();
  res::DataFiles files(path.parent_path());
  std::vector<const GrfArchive*> layers;
  for (const auto& a : archives) layers.insert(layers.begin(), a.get());
  files.set_layers(std::move(layers));
  res::ResourceManager manager(files, [] { return 0u; });
  RegisterResourceTypes(manager);
  const auto fail = [&](std::string_view name, const std::string& why) {
    std::printf("%s: %s: %s\n", archive_name.c_str(), Escape(name).c_str(), why.c_str());
    failures.insert(archive_name + "\t" + Escape(name));
  };

  std::map<std::string, LoaderCounts> by_type;
  std::map<std::string, std::size_t> no_loader;
  std::size_t not_ported = 0;
  std::set<std::string> tables;
  LoaderCounts table_counts;
  for (const TableLoader& table : TableLoaders(manager)) {
    const std::string entry = "data\\" + res::LowerCaseName(table.name);
    if (archive.Find(entry) == nullptr) continue;
    tables.insert(entry);
    std::string why;
    if (table.load(files, &why)) {
      ++table_counts.loaded;
    } else {
      ++table_counts.failed;
      fail(entry, why.empty() ? "does not load" : why);
    }
  }
  for (const GrfEntry& e : archive.entries()) {
    if (!e.compressed()) continue;
    const std::string& lowered = e.key.name;
    const std::size_t slash = lowered.rfind('\\');
    const std::size_t dot = lowered.rfind('.');
    const std::string extension =
        dot == std::string::npos || (slash != std::string::npos && dot < slash)
            ? std::string("(none)")
            : lowered.substr(dot + 1);
    if (tables.contains(lowered)) continue;
    if (extension == "gr2") {
      ++not_ported;
      continue;
    }
    auto resource = manager.Create(lowered);
    if (!resource || !lowered.starts_with("data\\")) {
      ++no_loader[extension];
      continue;
    }
    LoaderCounts& counts = by_type[extension];
    if (resource->Load(files, std::string_view(lowered).substr(5))) {
      ++counts.loaded;
    } else {
      ++counts.failed;
      fail(e.name, resource->error().empty() ? "does not load" : resource->error());
    }
    manager.Clear();
  }

  std::size_t loaded = table_counts.loaded, failed = table_counts.failed, unloaded_count = 0;
  for (const auto& [extension, counts] : by_type) {
    loaded += counts.loaded;
    failed += counts.failed;
  }
  for (const auto& [extension, count] : no_loader) unloaded_count += count;
  std::printf("%s: %zu files: %zu loaded, %zu failed, %zu not ported, %zu with no loader\n",
              archive_name.c_str(), loaded + failed + not_ported + unloaded_count, loaded, failed,
              not_ported, unloaded_count);
  std::printf("%s: %zu tables loaded, %zu failed\n", archive_name.c_str(), table_counts.loaded,
              table_counts.failed);
  for (const auto& [extension, counts] : by_type) {
    std::printf("%s: %s %zu loaded, %zu failed\n", archive_name.c_str(), extension.c_str(),
                counts.loaded, counts.failed);
  }
  if (not_ported != 0) {
    std::printf("%s: gr2 %zu not ported (C3dGrannyModelRes, through granny2.dll)\n",
                archive_name.c_str(), not_ported);
  }
  std::string unloaded;
  for (const auto& [extension, count] : no_loader) {
    unloaded += " " + extension + " " + std::to_string(count);
  }
  std::printf("%s: no loader:%s\n", archive_name.c_str(),
              unloaded.empty() ? " none" : unloaded.c_str());
}

int CheckLoaders(std::vector<std::string> args) {
  std::optional<std::set<std::string>> expected;
  if (args.size() >= 2 && args[0] == "--expected") {
    std::ifstream f(args[1]);
    if (!f) {
      std::fprintf(stderr, "cannot read %s\n", args[1].c_str());
      return 1;
    }
    expected.emplace();
    for (std::string line; std::getline(f, line);) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty() && line[0] != '#') expected->insert(line);
    }
    args.erase(args.begin(), args.begin() + 2);
  }
  if (args.empty()) return Usage();
  std::set<std::string> failures;
  std::vector<std::unique_ptr<GrfArchive>> archives;
  for (const std::string& a : args) {
    archives.push_back(OpenOrReport(a.c_str()));
    if (!archives.back()) return 1;
    CheckArchiveLoaders(a, archives, failures);
  }
  if (!expected) {
    std::printf("%zu failed\n", failures.size());
    return failures.empty() ? 0 : 1;
  }
  std::size_t surprises = 0;
  for (const std::string& f : failures) {
    if (!expected->contains(f)) {
      std::printf("unexpected failure: %s\n", f.c_str());
      ++surprises;
    }
  }
  for (const std::string& f : *expected) {
    if (!failures.contains(f)) {
      std::printf("expected to fail, but loads: %s\n", f.c_str());
      ++surprises;
    }
  }
  std::printf("%zu failed, all expected: %s\n", failures.size(), surprises == 0 ? "yes" : "no");
  return surprises == 0 ? 0 : 1;
}

int Usage() {
  std::fprintf(stderr,
               "usage: ragres list ARCHIVE\n"
               "       ragres extract ARCHIVE DIR [NAME...]\n"
               "       ragres check ARCHIVE...\n"
               "       ragres check-loaders [--expected FILE] ARCHIVE...\n"
               "       ragres decode-images DIR FILE...\n"
               "       ragres decode-palettes FILE...\n"
               "       ragres decode-sprites [--halve-x] [--halve-y] FILE...\n"
               "       ragres decode-acts [--halve-x] [--halve-y] FILE...\n"
               "       ragres decode-gats FILE...\n"
               "       ragres decode-gnds FILE...\n"
               "       ragres find-paths GAT QUERIES\n"
               "       ragres build-grounds [--lightmaps] LIGHT FILE...\n"
               "       ragres decode-rsws [--no-lightmaps] FILE...\n"
               "       ragres decode-rsms [--single] FILE...\n"
               "       ragres load-world [--raw] INSTALL MAP CELL_X CELL_Y\n"
               "       ragres draw-frame [--raw] INSTALL MAP CELL_X CELL_Y\n"
               "       ragres decode-rsxs FILE...\n"
               "       ragres decode-imfs FILE...\n"
               "       ragres decode-rmas FILE...\n"
               "       ragres decode-strs FILE...\n"
               "       ragres decode-ebms FILE...\n"
               "       ragres decode-wavs FILE...\n"
               "       ragres decode-name-pairs FILE...\n"
               "       ragres decode-number-records FILE...\n"
               "       ragres decode-line-records FILE...\n"
               "       ragres decode-fields [--sorted] FILE...\n"
               "       ragres decode-fog-parameters FILE...\n"
               "       ragres decode-xml FILE...\n"
               "       ragres decode-clientinfo [--connection N] FILE...\n"
               "       ragres decode-tips FILE...\n"
               "       ragres decode-manner FILE [PROBES]\n"
               "       ragres decode-item-tables DIR [--korea] [--code-page N]\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) return Usage();
  const std::string command = argv[1];
  if (command == "list" && argc == 3) return List(argv[2]);
  if (command == "extract" && argc >= 4)
    return Extract(argv[2], argv[3], std::vector<std::string>(argv + 4, argv + argc));
  if (command == "decode-images" && argc >= 4) {
    return DecodeImages(argv[2], std::vector<std::string>(argv + 3, argv + argc));
  }
  if (command == "decode-palettes") {
    return DecodePalettes(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-sprites") {
    return DecodeSprites(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-acts") {
    return DecodeActs(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-gats") return DecodeGats(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "find-paths" && argc == 4) return FindPaths(argv[2], argv[3]);
  if (command == "decode-gnds") return DecodeGnds(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-rsws") return DecodeRsws(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "build-grounds") {
    return BuildGrounds(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "load-world") return LoadWorld(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "draw-frame") return DrawFrame(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-rsms") return DecodeRsms(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-rsxs") return DecodeRsxs(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-imfs") return DecodeImfs(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-rmas") return DecodeRmas(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-strs") return DecodeStrs(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-ebms") return DecodeEbms(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-wavs") return DecodeWavs(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-name-pairs") {
    return DecodeNamePairs(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-number-records") {
    return DecodeNumberRecords(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-line-records") {
    return DecodeLineRecords(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-fields") {
    return DecodeFields(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-fog-parameters") {
    return DecodeFogParameters(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-xml") return DecodeXml(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-clientinfo") {
    return DecodeClientInfo(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "decode-tips") return DecodeTips(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-manner")
    return DecodeManner(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-item-tables") {
    return DecodeItemTables(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "check-loaders") {
    return CheckLoaders(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "check") {
    int status = 0;
    for (int i = 2; i < argc; ++i) status |= Check(argv[i]);
    return status;
  }
  return Usage();
}
