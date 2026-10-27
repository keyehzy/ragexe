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
//   ragres decode-rsws [--no-lightmaps] FILE...
//                                          decodes .rsw files as C3dWorldRes does
//   ragres decode-rsms FILE...             decodes .rsm files as C3dModelRes does
//   ragres decode-rsxs FILE...             decodes .rsx files as C3dAnimModelRes does
//   ragres decode-imfs FILE...             decodes .imf files as CImfRes does
//   ragres decode-rmas FILE...             decodes .rma files as CRmaRes does
//   ragres check ARCHIVE...                reads every compressed entry and checks its size;
//                                          fails on an entry that cannot be read
//
// Names are printed and written as stored (CP949) with bytes outside printable ASCII
// written as %XX, until the CP949 conversion arrives.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "res/act.h"
#include "res/bitmap.h"
#include "res/gat.h"
#include "res/gnd.h"
#include "res/grf.h"
#include "res/imf.h"
#include "res/palette.h"
#include "res/resource.h"
#include "res/rma.h"
#include "res/rsm.h"
#include "res/rsw.h"
#include "res/rsx.h"
#include "res/sprite.h"

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
int DecodeRsms(const std::vector<std::string>& files) {
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    ragexe::res::Model model;
    std::string error;
    if (!ragexe::res::DecodeModel(ReadFile(files[n]), model, &error)) {
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

int Usage() {
  std::fprintf(stderr,
               "usage: ragres list ARCHIVE\n"
               "       ragres extract ARCHIVE DIR [NAME...]\n"
               "       ragres check ARCHIVE...\n"
               "       ragres decode-images DIR FILE...\n"
               "       ragres decode-palettes FILE...\n"
               "       ragres decode-sprites [--halve-x] [--halve-y] FILE...\n"
               "       ragres decode-acts [--halve-x] [--halve-y] FILE...\n"
               "       ragres decode-gats FILE...\n"
               "       ragres decode-gnds FILE...\n"
               "       ragres decode-rsws [--no-lightmaps] FILE...\n"
               "       ragres decode-rsms FILE...\n"
               "       ragres decode-rsxs FILE...\n"
               "       ragres decode-imfs FILE...\n"
               "       ragres decode-rmas FILE...\n");
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
  if (command == "decode-rsms") return DecodeRsms(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-rsxs") return DecodeRsxs(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-imfs") return DecodeImfs(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "decode-rmas") return DecodeRmas(std::vector<std::string>(argv + 2, argv + argc));
  if (command == "check") {
    int status = 0;
    for (int i = 2; i < argc; ++i) status |= Check(argv[i]);
    return status;
  }
  return Usage();
}
