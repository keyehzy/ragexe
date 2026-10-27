// ragres: list, extract and check the game's archives.
//
//   ragres list ARCHIVE                    one line per entry: size, stored, flags, offset, name
//   ragres extract ARCHIVE DIR [NAME...]   writes the files (all, or the named ones) under DIR;
//                                          a NAME of "*.bmp" selects every .bmp
//   ragres decode-images DIR FILE...       decodes images as CBitmapRes does into DIR/<n>.argb
//   ragres decode-palettes FILE...         decodes .pal files as CPaletteRes does
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
#include <string>
#include <vector>

#include "res/bitmap.h"
#include "res/grf.h"
#include "res/palette.h"
#include "res/resource.h"

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

// Decodes image files as CBitmapRes does and writes DIR/<n>.argb for the n-th file: width,
// height and the alpha flag as 32-bit little-endian values, then the pixels. Prints one
// line per file: n, ok or failed, width, height, alpha flag, error.
int DecodeImages(const std::filesystem::path& dir, const std::vector<std::string>& files) {
  std::filesystem::create_directories(dir);
  int failed = 0;
  for (std::size_t n = 0; n < files.size(); ++n) {
    std::ifstream in(files[n], std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
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
    std::ifstream in(files[n], std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
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

int Usage() {
  std::fprintf(stderr,
               "usage: ragres list ARCHIVE\n"
               "       ragres extract ARCHIVE DIR [NAME...]\n"
               "       ragres check ARCHIVE...\n"
               "       ragres decode-images DIR FILE...\n"
               "       ragres decode-palettes FILE...\n");
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
  if (command == "check") {
    int status = 0;
    for (int i = 2; i < argc; ++i) status |= Check(argv[i]);
    return status;
  }
  return Usage();
}
