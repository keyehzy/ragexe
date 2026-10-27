// Writes the seed inputs of the fuzz targets: small archives of each version, with both
// decode paths, directory entries and a stream that breaks off, their tables, and a
// ResNameTable.txt, small images of each kind the bitmap decoders read, and sprites.
//
//   make_grf_seeds DIR    writes DIR/grf_archive, DIR/grf_table, DIR/res_name_table,
//                         DIR/bitmap and DIR/sprite

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
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

}  // namespace

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
  std::filesystem::create_directories(dir / "sprite");

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
  std::printf("wrote %d seeds to %s\n", n, dir.string().c_str());
  return 0;
}
