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
#include "res/palette.h"
#include "res/resource.h"
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
               "       ragres decode-gnds FILE...\n");
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
  if (command == "check") {
    int status = 0;
    for (int i = 2; i < argc; ++i) status |= Check(argv[i]);
    return status;
  }
  return Usage();
}
