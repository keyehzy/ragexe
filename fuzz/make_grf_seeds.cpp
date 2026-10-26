// Writes the seed inputs of the GRF fuzz targets: small archives of each version, with
// both decode paths, directory entries and a stream that breaks off, and their tables.
//
//   make_grf_seeds DIR    writes DIR/grf_archive/*.grf and DIR/grf_table/*.bin

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/grf_builder.h"

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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: make_grf_seeds DIR\n");
    return 2;
  }
  const std::filesystem::path dir = argv[1];
  std::filesystem::create_directories(dir / "grf_archive");
  std::filesystem::create_directories(dir / "grf_table");

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
  std::printf("wrote %d seeds to %s\n", n, dir.string().c_str());
  return 0;
}
