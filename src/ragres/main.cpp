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
#include <string>
#include <string_view>
#include <vector>

#include "render/ground.h"
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
  // The patcher merges each .gpf into data.grf in name order, so the last one's files win.
  const std::filesystem::path install = args[0];
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::directory_iterator(install))
    if (entry.path().extension() == ".gpf") paths.push_back(entry.path());
  std::sort(paths.begin(), paths.end());
  paths.insert(paths.begin(), install / "data.grf");
  std::string error;
  std::vector<std::unique_ptr<ragexe::res::GrfArchive>> archives;
  std::vector<const ragexe::res::GrfArchive*> layers;
  for (const std::filesystem::path& path : paths) {
    archives.push_back(ragexe::res::GrfArchive::Open(path, &error));
    if (!archives.back()) {
      std::fprintf(stderr, "%s: %s\n", path.string().c_str(), error.c_str());
      return 1;
    }
    layers.insert(layers.begin(), archives.back().get());
  }
  ragexe::res::DataFiles files(install);
  files.set_layers(std::move(layers));
  const std::string& map = args[1];
  const auto cell_x = static_cast<std::int32_t>(std::stol(args[2]));
  const auto cell_y = static_cast<std::int32_t>(std::stol(args[3]));
  ragexe::res::World world;
  ragexe::res::AttrMap attr;
  auto bytes = files.Load("data\\" + map + ".rsw");
  if (!bytes || !ragexe::res::DecodeWorld(*bytes, true, world, &error)) {
    std::fprintf(stderr, "%s.rsw: %s\n", map.c_str(), error.c_str());
    return 1;
  }
  bytes = files.Load("data\\" + world.attr);
  if (!bytes || !ragexe::res::DecodeAttrMap(*bytes, attr, &error)) {
    std::fprintf(stderr, "%s: %s\n", world.attr.c_str(), error.c_str());
    return 1;
  }
  const auto point = ragexe::render::CellCentre(attr, cell_x, cell_y);
  ragexe::render::WorldScene scene;
  if (!ragexe::render::LoadWorldScene(files, map, point[0], point[1], scene, &error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
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
               "       ragres build-grounds [--lightmaps] LIGHT FILE...\n"
               "       ragres decode-rsws [--no-lightmaps] FILE...\n"
               "       ragres decode-rsms [--single] FILE...\n"
               "       ragres load-world [--raw] INSTALL MAP CELL_X CELL_Y\n"
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
  if (command == "decode-gnds") return DecodeGnds(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-rsws") return DecodeRsws(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "build-grounds") {
    return BuildGrounds(std::vector<std::string>(argv + 2, argv + argc));
  }
  if (command == "load-world") return LoadWorld(std::vector<std::string>(argv + 2, argv + argc));
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
