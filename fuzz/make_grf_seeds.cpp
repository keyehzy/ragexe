// Writes the seed inputs of the fuzz targets: small archives of each version, with both
// decode paths, directory entries and a stream that breaks off, their tables, and a
// ResNameTable.txt, small images of each kind the bitmap decoders read, sprites and
// actions, attribute maps, grounds, worlds, models, animated models, .imf
// and .rma files, effects, emblems, waves, item tables, ClientState's tables, XML,
// clientinfo.xml, the tips and manner files, and a palette.
//
//   make_grf_seeds DIR    writes DIR/grf_archive, DIR/grf_table, DIR/res_name_table,
//                         DIR/bitmap, DIR/sprite, DIR/act, DIR/gat, DIR/gnd,
//                         DIR/rsw, DIR/rsm, DIR/rsx, DIR/imf, DIR/rma, DIR/str,
//                         DIR/ebm, DIR/wav, DIR/item_tables, DIR/client_tables
//                         DIR/xml, DIR/clientinfo, DIR/tips_manner and DIR/palette

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "res/sprite.h"
#include "support/grf_builder.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

using ragexe::testing::BuildArchive;
using ragexe::testing::TestFile;

void Write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// The fuzz target's table input: version selector, count, then the table bytes.
std::vector<std::uint8_t> TableInput(const std::vector<std::uint8_t>& archive,
                                     std::uint32_t version, std::uint32_t count) {
  const std::uint32_t offset = static_cast<std::uint32_t>(archive[0x1e]) |
                               static_cast<std::uint32_t>(archive[0x1f]) << 8 |
                               static_cast<std::uint32_t>(archive[0x20]) << 16 |
                               static_cast<std::uint32_t>(archive[0x21]) << 24;
  std::vector<std::uint8_t> out = {static_cast<std::uint8_t>(version - 0x100)};
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(count >> (8 * i)));
  out.insert(out.end(), archive.begin() + 0x2e + offset, archive.end());
  return out;
}

void Put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  Put16(out, v & 0xffff);
  Put16(out, v >> 16);
}

// The bitmap fuzz target's input: the format selector, then the file.
std::vector<std::uint8_t> BitmapInput(std::uint8_t format, const std::vector<std::uint8_t>& file) {
  std::vector<std::uint8_t> out = {format};
  out.insert(out.end(), file.begin(), file.end());
  return out;
}

std::vector<std::uint8_t> Bmp(std::uint16_t bits, std::uint32_t width, std::uint32_t height) {
  const std::uint32_t palette = bits == 8 ? 256 : 0;
  const std::uint32_t stride = bits == 8 ? (width + 3) & ~3u : (width * 3 + 3) & ~3u;
  std::vector<std::uint8_t> out = {'B', 'M'};
  Put32(out, 54 + palette * 4 + stride * height);
  Put32(out, 0);
  Put32(out, 54 + palette * 4);
  for (std::uint32_t v : {40u, width, height}) Put32(out, v);
  Put16(out, 1);
  Put16(out, bits);
  for (std::uint32_t v : {0u, stride * height, 2835u, 2835u, palette, 0u}) Put32(out, v);
  for (std::uint32_t i = 0; i < palette * 4 + stride * height; ++i) {
    out.push_back(static_cast<std::uint8_t>(i * 7));
  }
  return out;
}

std::vector<std::uint8_t> Tga(std::uint8_t type, std::uint8_t depth) {
  std::vector<std::uint8_t> out = {0, 0, type, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  Put16(out, 4);
  Put16(out, 2);
  out.push_back(depth);
  out.push_back(0);
  const std::size_t px = depth / 8;
  if (type == 2) {
    for (std::size_t i = 0; i < 8 * px; ++i) out.push_back(static_cast<std::uint8_t>(i * 13));
  } else {
    for (int packet = 0; packet < 2; ++packet) {
      out.push_back(0x80 | 3);
      for (std::size_t i = 0; i < px; ++i)
        out.push_back(static_cast<std::uint8_t>(static_cast<std::size_t>(packet) * 50 + i));
      out.push_back(3);
      for (std::size_t i = 0; i < 4 * px; ++i) out.push_back(static_cast<std::uint8_t>(i * 9));
    }
  }
  return out;
}

std::vector<std::uint8_t> Jpeg() {
  std::vector<std::uint8_t> rgb;
  for (int i = 0; i < 16 * 16; ++i) {
    rgb.insert(rgb.end(), {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(255 - i), 128});
  }
  std::vector<std::uint8_t> out;
  stbi_write_jpg_to_func(
      [](void* context, void* data, int size) {
        auto* bytes = static_cast<std::vector<std::uint8_t>*>(context);
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes->insert(bytes->end(), p, p + size);
      },
      &out, 16, 16, 3, rgb.data(), 75);
  return out;
}

// A sprite with two 8-bit images and, from version 0x200, one 32-bit image; the sprite fuzz
// target's input, so with its detail-level byte first.
std::vector<std::uint8_t> SpriteInput(std::uint8_t level, std::uint16_t version) {
  std::vector<std::uint8_t> out = {level, 'S', 'P'};
  Put16(out, version);
  Put16(out, 2);
  if (version >= 0x200) Put16(out, 1);
  for (int size : {6, 9}) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size * size));
    for (std::size_t i = 0; i < pixels.size(); ++i)
      pixels[i] = i % 3 == 0 ? 0 : static_cast<std::uint8_t>(i);
    Put16(out, static_cast<std::uint32_t>(size));
    Put16(out, static_cast<std::uint32_t>(size));
    if (version < 0x201) {
      out.insert(out.end(), pixels.begin(), pixels.end());
    } else {
      const auto rle = ragexe::res::EncodeSpriteRle(pixels, size, size);
      Put16(out, static_cast<std::uint32_t>(rle.size()));
      out.insert(out.end(), rle.begin(), rle.end());
    }
  }
  if (version >= 0x200) {
    Put16(out, 3);
    Put16(out, 2);
    for (int i = 0; i < 3 * 2 * 4; ++i) out.push_back(static_cast<std::uint8_t>(i * 11));
  }
  if (version > 0x100) {
    for (int i = 0; i < 1024; ++i) out.push_back(static_cast<std::uint8_t>(i));
  }
  return out;
}

// An action of every field the version has: two actions, two frames, two items, an
// anchor, an event and delays; the act fuzz target's input, so with its four image bytes
// first (7 x 4, not halved, 4 images).
std::vector<std::uint8_t> ActInput(std::uint16_t version) {
  std::vector<std::uint8_t> out = {7, 4, 0, 4, 'A', 'C'};
  Put16(out, version);
  Put16(out, 2);
  out.resize(out.size() + 10, 0);
  for (int action = 0; action < 2; ++action) {
    Put32(out, 2);
    for (int frame = 0; frame < 2; ++frame) {
      for (int i = 0; i < 8; ++i) Put32(out, 0);
      Put32(out, 2);
      for (std::uint32_t item = 0; item < 2; ++item) {
        for (std::uint32_t v : {10u, 20u, item, 0u}) Put32(out, v);
        if (version >= 0x200) {
          out.insert(out.end(), {255, 255, 255, 255});
          Put32(out, 0x3f800000);  // 1.0
          if (version >= 0x204) Put32(out, 0x3f800000);
          Put32(out, 0);
          Put32(out, 0);
          if (version >= 0x205) {
            Put32(out, 7);
            Put32(out, 4);
          }
        }
      }
      if (version >= 0x200) Put32(out, 0);
      if (version >= 0x203) {
        Put32(out, 1);
        for (std::uint32_t v : {0u, 1u, 2u, 3u}) Put32(out, v);
      }
    }
  }
  if (version >= 0x201) {
    Put32(out, 1);
    out.insert(out.end(), {'a', 't', 'k'});
    out.resize(out.size() + 37, 0);
  }
  if (version >= 0x202) {
    Put32(out, 0x40800000);
    Put32(out, 0x40800000);
  }
  return out;
}

}  // namespace

// A 2 x 2 ground of the given minor version: two textures, one lightmap, two surfaces.
std::vector<std::uint8_t> GndInput(std::uint8_t minor) {
  std::vector<std::uint8_t> out = {'G', 'R', 'G', 'N', 1, minor};
  Put32(out, 2);
  Put32(out, 2);
  Put32(out, 0x41200000);  // 10.0
  Put32(out, 2);
  Put32(out, 16);
  for (const char* name : {"grass.bmp", "wall.bmp"}) {
    const std::string record(name);
    out.insert(out.end(), record.begin(), record.end());
    out.resize(out.size() + 16 - record.size(), 0);
  }
  for (std::uint32_t v : {1u, 8u, 8u, 1u}) Put32(out, v);
  for (int i = 0; i < 256; ++i) out.push_back(static_cast<std::uint8_t>(i));
  Put32(out, 2);
  for (int k = 0; k < 2; ++k) {
    for (int i = 0; i < 8; ++i) Put32(out, 0x3f000000);  // 0.5
    Put32(out, static_cast<std::uint32_t>(k));           // texture k, lightmap 0
    Put32(out, 0xffffffff);
  }
  for (int c = 0; c < 4; ++c) {
    for (int i = 0; i < 4; ++i) Put32(out, 0);
    const std::uint32_t indices[3] = {static_cast<std::uint32_t>(c % 2), 0xffffffff, 1};
    for (std::uint32_t v : indices) {
      if (minor == 7) {
        Put32(out, v);
      } else {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
      }
    }
    if (minor != 7) out.insert(out.end(), {0, 0});
  }
  return out;
}

// The rsw_fuzz input: the ISLIGHTMAP byte, then a world of the given version with a
// model, an ignored object, a sound and an effect, and the scene graph's bounds at 2.1.
std::vector<std::uint8_t> RswInput(std::uint8_t major, std::uint8_t minor) {
  const auto at_least = [&](std::uint8_t ma, std::uint8_t mi) {
    return major > ma || (major == ma && minor >= mi);
  };
  const auto name = [](std::vector<std::uint8_t>& out, const std::string& s, std::size_t size) {
    out.insert(out.end(), s.begin(), s.end());
    out.resize(out.size() + size - s.size(), 0);
  };
  std::vector<std::uint8_t> out = {0, 'G', 'R', 'S', 'W', major, minor};
  name(out, "tool.ini", 40);
  name(out, "map.gnd", 40);
  if (at_least(1, 4)) name(out, "map.gat", 40);
  name(out, "", 40);
  if (at_least(1, 3)) Put32(out, 0xc1200000);  // -10.0
  if (at_least(1, 8)) {
    for (std::uint32_t v : {1u, 0x3f800000u, 0x40000000u, 0x42480000u}) Put32(out, v);
  }
  if (at_least(1, 9)) Put32(out, 3);
  if (at_least(1, 5)) {
    for (std::uint32_t v : {45u, 45u}) Put32(out, v);
    for (int i = 0; i < 6; ++i) Put32(out, 0x3f000000);  // 0.5
  }
  if (at_least(1, 7)) Put32(out, 0x3f000000);
  if (at_least(1, 6)) {
    for (int i = 0; i < 4; ++i) Put32(out, 0);
  }
  Put32(out, 4);
  Put32(out, 1);
  if (at_least(1, 3)) {
    name(out, "tree", 40);
    for (std::uint32_t v : {0u, 0x3f800000u, 0u}) Put32(out, v);
  }
  name(out, "tree.rsm", 80);
  name(out, "trunk", 80);
  for (int i = 0; i < 9; ++i) Put32(out, 0x3f800000);
  Put32(out, 2);
  out.resize(out.size() + 0x6c, 0);
  Put32(out, 3);
  name(out, "bird.wav", 80);
  name(out, "", 80);
  out.resize(out.size() + (major >= 2 ? 0x20 : 0x1c), 0);
  Put32(out, 4);
  out.resize(out.size() + 0x74, 0);
  if (major == 2 && minor == 1) out.resize(out.size() + std::size_t{1365} * 48, 0);
  return out;
}

// A model of the given minor version: two nodes, the second a child of the first, with a
// mesh of four triangles on two textures and two smoothing groups.
std::vector<std::uint8_t> RsmInput(std::uint8_t minor) {
  const auto name = [](std::vector<std::uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.resize(out.size() + 40 - s.size(), 0);
  };
  std::vector<std::uint8_t> out = {'G', 'R', 'S', 'M', 1, minor};
  Put32(out, 100);
  Put32(out, 1);
  if (minor >= 4) out.push_back(255);
  out.resize(out.size() + 16, 0);
  Put32(out, 2);
  name(out, "a.bmp");
  name(out, "b.bmp");
  name(out, "root");
  Put32(out, 2);
  for (int k = 0; k < 2; ++k) {
    name(out, k == 0 ? "root" : "child");
    name(out, k == 0 ? "" : "root");
    Put32(out, 2);
    Put32(out, 0);
    Put32(out, 1);
    for (int i = 0; i < 12; ++i) Put32(out, i % 4 == 0 ? 0x3f800000 : 0);  // identity
    for (int i = 0; i < 7; ++i) Put32(out, 0);                             // position, angle, axis
    for (int i = 0; i < 3; ++i) Put32(out, 0x3f800000);
    Put32(out, 4);
    const std::uint32_t points[4][3] = {
        {0, 0, 0}, {0x3f800000, 0, 0}, {0, 0x3f800000, 0}, {0, 0, 0x3f800000}};
    for (const auto& p : points) {
      for (std::uint32_t c : p) Put32(out, c);
    }
    Put32(out, 1);
    if (minor != 1) Put32(out, 0xffffffff);
    Put32(out, 0);
    Put32(out, 0);
    Put32(out, 4);
    const std::uint16_t faces[4][3] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 3}, {1, 2, 3}};
    for (int f = 0; f < 4; ++f) {
      for (std::uint16_t v : faces[f]) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(0);
      }
      out.insert(out.end(), {0, 0, 0, 0, 0, 0, static_cast<std::uint8_t>(f % 2), 0});
      if (minor == 1) {
        out.resize(out.size() + 6, 0);
      } else {
        out.insert(out.end(), {0, 0, 0, 0, 0, 0});
        Put32(out, static_cast<std::uint32_t>(f / 2));
      }
    }
    if (minor >= 5) Put32(out, 0);
    Put32(out, 0);
  }
  if (minor < 5) Put32(out, 0);
  Put32(out, 0);
  return out;
}

// An animated model of version 1.3 with two frames and two nodes, the second a child of
// the first with shared texture coordinates.
std::vector<std::uint8_t> RsxInput() {
  const auto name = [](std::vector<std::uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.resize(out.size() + 40 - s.size(), 0);
  };
  std::vector<std::uint8_t> out = {'G', 'R', 'S', 'X', 1, 3};
  for (std::uint32_t v : {0x3f800000u, 0u, 0u, 0u, 255u, 1u, 2u, 160u}) Put32(out, v);
  out.resize(out.size() + 16, 0);
  Put32(out, 1);
  name(out, "gate.bmp");
  name(out, "base");
  Put32(out, 2);
  for (int k = 0; k < 2; ++k) {
    Put32(out, 0x12345678);
    name(out, k == 0 ? "base" : "arm");
    name(out, k == 0 ? "" : "base");
    Put32(out, 1);
    Put32(out, 0);
    Put32(out, 3);
    for (int i = 0; i < 2 * 3 * 3; ++i) Put32(out, i % 4 == 0 ? 0x3f800000 : 0);
    Put32(out, 1);
    Put32(out, static_cast<std::uint32_t>(k));
    for (int set = 0; set < (k == 0 ? 2 : 1); ++set) {
      for (std::uint32_t v : {0xffffffffu, 0u, 0x3f000000u}) Put32(out, v);
    }
    Put32(out, 1);
    for (std::uint32_t v : {0x00010000u, 2u, 0u, 0u, 0u, 0u}) Put32(out, v);
    Put32(out, 0x12345678);
  }
  return out;
}

// An .imf file of version 1.01: two layers of three actions of up to two frames.
std::vector<std::uint8_t> ImfInput() {
  std::vector<std::uint8_t> out;
  for (std::uint32_t v : {0x3f8147aeu, 0u, 1u}) Put32(out, v);
  for (int layer = 0; layer < 2; ++layer) {
    Put32(out, 3);
    for (std::uint32_t frames = 0; frames < 3; ++frames) {
      Put32(out, frames);
      for (std::uint32_t k = 0; k < frames; ++k) {
        for (std::uint32_t v : {static_cast<std::uint32_t>(layer), k, 0xfffffff0u}) Put32(out, v);
      }
    }
  }
  return out;
}

// An effect of two layers: two textures and three keys, then none and one.
std::vector<std::uint8_t> StrInput() {
  std::vector<std::uint8_t> out = {'S', 'T', 'R', 'M'};
  for (std::uint32_t v : {0x94u, 60u, 30u, 2u, 0u, 0u, 0u, 0u}) Put32(out, v);
  const std::pair<int, int> layers[2] = {{2, 3}, {0, 1}};
  for (auto [textures, keys] : layers) {
    Put32(out, static_cast<std::uint32_t>(textures));
    for (int k = 0; k < textures; ++k) {
      const std::string name = "spark" + std::to_string(k) + ".bmp";
      out.insert(out.end(), name.begin(), name.end());
      out.resize(out.size() + 0x80 - name.size(), 0);
    }
    Put32(out, static_cast<std::uint32_t>(keys));
    for (int k = 0; k < keys; ++k) {
      Put32(out, static_cast<std::uint32_t>(10 * k));
      Put32(out, static_cast<std::uint32_t>(k % 2));
      for (int v = 0; v < 29; ++v) Put32(out, 0x3f800000);
    }
  }
  return out;
}

// An emblem: a BMP, zlib-compressed.
std::vector<std::uint8_t> EbmInput(const std::vector<std::uint8_t>& bmp) {
  uLongf size = compressBound(static_cast<uLong>(bmp.size()));
  std::vector<std::uint8_t> out(size);
  compress(out.data(), &size, bmp.data(), static_cast<uLong>(bmp.size()));
  out.resize(size);
  return out;
}

// A wave: format tag, channels, bits and block size, with a "fact" chunk when samples is
// not 0, and data of the given size.
std::vector<std::uint8_t> WavInput(std::uint16_t tag, std::uint16_t channels, std::uint16_t bits,
                                   std::uint16_t align, std::uint32_t samples,
                                   std::uint32_t data_size) {
  std::vector<std::uint8_t> out = {'R', 'I', 'F', 'F'};
  const std::uint32_t fact = samples != 0 ? 12 : 0;
  Put32(out, 4 + 24 + fact + 8 + data_size);
  for (char c : std::string("WAVEfmt ")) out.push_back(static_cast<std::uint8_t>(c));
  Put32(out, 16);
  Put16(out, tag);
  Put16(out, channels);
  Put32(out, 22050);
  Put32(out, 22050u * align);
  Put16(out, align);
  Put16(out, bits);
  if (samples != 0) {
    for (char c : std::string("fact")) out.push_back(static_cast<std::uint8_t>(c));
    Put32(out, 4);
    Put32(out, samples);
  }
  for (char c : std::string("data")) out.push_back(static_cast<std::uint8_t>(c));
  Put32(out, data_size);
  for (std::uint32_t i = 0; i < data_size; ++i) {
    // Each block header's step index (its third byte) stays in range.
    out.push_back(static_cast<std::uint8_t>(i % 4 == 2 ? i % 89 : i * 13));
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: make_grf_seeds DIR\n");
    return 2;
  }
  const std::filesystem::path dir = argv[1];
  std::filesystem::create_directories(dir / "grf_archive");
  std::filesystem::create_directories(dir / "grf_table");
  std::filesystem::create_directories(dir / "res_name_table");
  std::filesystem::create_directories(dir / "bitmap");
  std::filesystem::create_directories(dir / "palette");
  std::filesystem::create_directories(dir / "sprite");
  std::filesystem::create_directories(dir / "act");
  std::filesystem::create_directories(dir / "gat");
  std::filesystem::create_directories(dir / "gnd");
  std::filesystem::create_directories(dir / "rsw");
  std::filesystem::create_directories(dir / "rsm");
  std::filesystem::create_directories(dir / "rsx");
  std::filesystem::create_directories(dir / "imf");
  std::filesystem::create_directories(dir / "rma");
  std::filesystem::create_directories(dir / "str");
  std::filesystem::create_directories(dir / "ebm");
  std::filesystem::create_directories(dir / "wav");
  std::filesystem::create_directories(dir / "item_tables");
  std::filesystem::create_directories(dir / "client_tables");
  std::filesystem::create_directories(dir / "xml");
  std::filesystem::create_directories(dir / "clientinfo");
  std::filesystem::create_directories(dir / "tips_manner");

  std::vector<std::uint8_t> text(300);
  for (std::size_t i = 0; i < text.size(); ++i) text[i] = static_cast<std::uint8_t>('a' + i % 26);
  std::vector<std::uint8_t> noise(700);
  std::uint32_t x = 1;
  for (auto& b : noise) {
    x = x * 1103515245u + 12345u;
    b = static_cast<std::uint8_t>(x >> 16);
  }
  const std::vector<TestFile> files = {
      {"data\\prontera.gat", text, 1},
      {"data\\prontera.rsw", noise, 1},
      {"data\\texture\\\xC0\xAF\xC0\xFA.bmp", {1, 2, 3}, 1},
      {"data\\effect", std::vector<std::uint8_t>(16, 0), 0},
      {"data\\plain.txt", text, 0},
  };
  int n = 0;
  for (std::uint32_t version : {0x101u, 0x102u, 0x103u}) {
    const auto archive = BuildArchive(files, version);
    Write(dir / "grf_archive" / ("v" + std::to_string(version) + ".grf"), archive);
    Write(dir / "grf_table" / ("v" + std::to_string(version) + ".bin"),
          TableInput(archive, version, static_cast<std::uint32_t>(files.size())));
    n += 2;
  }
  // One entry whose stored deflate block ends early.
  auto cut = BuildArchive({{"data\\cut.wav", {0x78, 0x01, 0x01, 0x0c, 0x00, 0xf3, 0xff, 'R'}, 0}});
  const std::size_t fields = cut.size() - 17;
  cut[fields] = static_cast<std::uint8_t>(cut[fields] + 4);  // size 8 -> 12
  cut[fields + 8] = 12;
  cut[fields + 12] = 1;
  Write(dir / "grf_archive" / "cut.grf", cut);
  ++n;
  const std::string table = "new_1-1.gnd#new_zone01.gnd#\r\nsplit#\r\nacross.gnd#\r\n##x#y# tail\n";
  Write(dir / "res_name_table" / "sample.txt",
        std::vector<std::uint8_t>(table.begin(), table.end()));
  ++n;
  std::vector<std::uint8_t> palette(1024);
  for (std::size_t i = 0; i < palette.size(); ++i) palette[i] = static_cast<std::uint8_t>(i * 7);
  palette[0] = 0xff;  // the first entry magenta, the transparent key
  palette[1] = 0x00;
  palette[2] = 0xff;
  Write(dir / "palette" / "sample.pal", palette);
  ++n;
  Write(dir / "bitmap" / "bmp8.bin", BitmapInput(0, Bmp(8, 5, 3)));
  Write(dir / "bitmap" / "bmp24.bin", BitmapInput(0, Bmp(24, 5, 3)));
  Write(dir / "bitmap" / "tga32.bin", BitmapInput(1, Tga(2, 32)));
  Write(dir / "bitmap" / "tga24_rle.bin", BitmapInput(1, Tga(10, 24)));
  Write(dir / "bitmap" / "tga32_rle.bin", BitmapInput(1, Tga(10, 32)));
  Write(dir / "bitmap" / "jpg.bin", BitmapInput(2, Jpeg()));
  n += 6;
  Write(dir / "sprite" / "v100.bin", SpriteInput(0, 0x100));
  Write(dir / "sprite" / "v200.bin", SpriteInput(0, 0x200));
  Write(dir / "sprite" / "v201.bin", SpriteInput(0, 0x201));
  Write(dir / "sprite" / "v201_halved.bin", SpriteInput(3, 0x201));
  n += 4;
  for (int version : {0x101, 0x200, 0x201, 0x202, 0x203, 0x204, 0x205}) {
    char name[32];
    std::snprintf(name, sizeof name, "v%x.bin", static_cast<unsigned>(version));
    Write(dir / "act" / name, ActInput(static_cast<std::uint16_t>(version)));
    ++n;
  }
  // A 3 x 2 attribute map, version 1.2.
  std::vector<std::uint8_t> gat = {'G', 'R', 'A', 'T', 1, 2};
  Put32(gat, 3);
  Put32(gat, 2);
  for (int i = 0; i < 6 * 20; ++i) gat.push_back(static_cast<std::uint8_t>(i % 5 == 4 ? 1 : 0));
  Write(dir / "gat" / "small.bin", gat);
  ++n;
  Write(dir / "gnd" / "v1.6.bin", GndInput(6));
  Write(dir / "gnd" / "v1.7.bin", GndInput(7));
  n += 2;
  for (auto [major, minor] : {std::pair{1, 2}, {1, 9}, {2, 0}, {2, 1}}) {
    const auto name = "v" + std::to_string(major) + "." + std::to_string(minor) + ".bin";
    Write(dir / "rsw" / name,
          RswInput(static_cast<std::uint8_t>(major), static_cast<std::uint8_t>(minor)));
    ++n;
  }
  for (int minor : {1, 4, 5}) {
    Write(dir / "rsm" / ("v1." + std::to_string(minor) + ".bin"),
          RsmInput(static_cast<std::uint8_t>(minor)));
    ++n;
  }
  Write(dir / "rsx" / "v1.3.bin", RsxInput());
  ++n;
  Write(dir / "imf" / "v1.01.bin", ImfInput());
  ++n;
  const std::string rma =
      "// an .rma\n#rsxname door.rsx\n#rsxname \"gate b.rsx\"\n#action 0 4 1 2 3 4\n"
      "#action 2 4 5 6\n#interiorzoom 1.5\n#interioroffsetx -4\n#interioroffsety 2\n"
      "#interiorframes 3 7\n#shadowzoom .5\n";
  Write(dir / "rma" / "sample.bin", std::vector<std::uint8_t>(rma.begin(), rma.end()));
  ++n;
  Write(dir / "str" / "v0x94.bin", StrInput());
  ++n;
  Write(dir / "ebm" / "bmp24.bin", EbmInput(Bmp(24, 24, 24)));
  Write(dir / "ebm" / "bmp8.bin", EbmInput(Bmp(8, 24, 24)));
  Write(dir / "ebm" / "wide.bin", EbmInput(Bmp(24, 30, 20)));
  n += 3;
  Write(dir / "wav" / "pcm.bin", WavInput(1, 1, 16, 2, 0, 64));
  Write(dir / "wav" / "ima_mono.bin", WavInput(0x11, 1, 4, 64, 0, 128));
  Write(dir / "wav" / "ima_stereo.bin", WavInput(0x11, 2, 4, 64, 100, 128));
  std::vector<std::uint8_t> mp3 = {0xff, 0xfb, 0x90, 0x00};
  mp3.resize(420, 0);
  Write(dir / "wav" / "mp3.bin", mp3);
  n += 4;
  // The item tables in LoadItemTables' order, each after a 0x01, behind a settings byte.
  const std::string tables[] = {"501#Red_Potion#\r\n",
                                "501#red_potion#\r\n",
                                "501#\r\nHeals.\r\n#\r\n",
                                "501#Red_Potion#\r\n",
                                "501#red_potion#\r\n",
                                "501#\r\nHeals.\r\n#\r\n",
                                "501#8#\r\n",
                                "501#1#2#\r\n",
                                "501#2#\r\n",
                                "4001#",
                                "4001#poring#",
                                "4001#Lucky#",
                                "4001#",
                                "1000\r\nOre\r\n"};
  std::string item_tables(1, '\0');
  for (std::size_t k = 0; k < std::size(tables); ++k) {
    if (k > 0) item_tables.push_back('\x01');
    item_tables += tables[k];
  }
  Write(dir / "item_tables" / "small.bin",
        std::vector<std::uint8_t>(item_tables.begin(), item_tables.end()));
  ++n;
  const std::string client_tables =
      "prontera.rsw#Prontera#\r\ngeffen.rsw#Geffen#\r\nk##v#\r\nSM_BASH#\r\n8#\r\n@\r\n"
      "NV_BASIC#\r\nBasic Skill\r\n#\r\ngef.rsw#\r\n0.2#\r\n0.45#\r\n0x2275A9FF#\r\n0.5#\r\n";
  Write(dir / "client_tables" / "small.bin",
        std::vector<std::uint8_t>(client_tables.begin(), client_tables.end()));
  ++n;
  const std::string xml =
      "<?xml version=\"1.0\"?>\r\n<clientinfo>\r\n\t<servicetype>america</servicetype>\r\n"
      "\t<connection k=\"v\"><display>iRO &amp; &lt;test&gt;</display><aid/></connection>\r\n"
      "<!-- comment --></clientinfo>\r\n";
  Write(dir / "xml" / "small.bin", std::vector<std::uint8_t>(xml.begin(), xml.end()));
  ++n;
  const std::string clientinfo =
      "<clientinfo><servicetype>america</servicetype><servertype>sakray</servertype>"
      "<hideaccountlist/><connection><display>A</display><address>1.2.3.4</address>"
      "<port>6900</port><version>18</version><langtype>1</langtype>"
      "<aid><admin>5</admin><admin>-3</admin></aid></connection>"
      "<connection><langtype>4</langtype></connection></clientinfo>";
  Write(dir / "clientinfo" / "small.bin",
        std::vector<std::uint8_t>(clientinfo.begin(), clientinfo.end()));
  ++n;
  const std::string tips_manner =
      "Fuck\r\n  ass\r\nfirst tip\r\n#\r\nsecond\r\n#\r\n\x01you FUCK \xb0\xa1 ass";
  Write(dir / "tips_manner" / "small.bin",
        std::vector<std::uint8_t>(tips_manner.begin(), tips_manner.end()));
  ++n;
  std::printf("wrote %d seeds to %s\n", n, dir.string().c_str());
  return 0;
}
