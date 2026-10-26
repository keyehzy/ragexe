#pragma once

// Writes GRF archives for the tests and the fuzz seeds (docs/formats/grf.md).

#include <zlib.h>

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "res/grf_cipher.h"

namespace ragexe::testing {

namespace grf = ragexe::res::grf;

inline void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// One file of a test archive; flags 1 compresses and encodes it, anything else stores it.
struct TestFile {
  std::string name;
  std::vector<std::uint8_t> data;
  std::uint8_t flags = 1;
};

// Writes an archive the way the format describes it, encoding with the inverse functions.
inline std::vector<std::uint8_t> BuildArchive(const std::vector<TestFile>& files,
                                              std::uint32_t version = 0x103) {
  std::vector<std::uint8_t> body, table;
  for (const TestFile& f : files) {
    std::vector<std::uint8_t> stored;
    std::int32_t compressed_size;
    if (f.flags == 1) {
      uLongf n = compressBound(static_cast<uLong>(f.data.size()));
      stored.resize(n);
      if (compress(stored.data(), &n, f.data.data(), static_cast<uLong>(f.data.size())) != Z_OK) {
        return {};
      }
      stored.resize(n);
      compressed_size = static_cast<std::int32_t>(n);
    } else {
      stored = f.data;
      compressed_size = static_cast<std::int32_t>(stored.size());
    }
    stored.resize((stored.size() + 7) / 8 * 8);
    if (grf::HasFirstBlocksExtension(f.name)) {
      if (f.flags == 1) grf::DecodeFirstBlocks(stored);
    } else {
      grf::EncodeMixed(stored, grf::DecimalDigits(compressed_size));
    }
    const auto offset = static_cast<std::uint32_t>(body.size());
    body.insert(body.end(), stored.begin(), stored.end());

    std::vector<std::uint8_t> name(f.name.begin(), f.name.end());
    name.push_back(0);
    name.resize((name.size() + 7) / 8 * 8);
    grf::DecodeMixed(name, 1);  // the block function is its own inverse
    grf::SwapNibbles(name);
    PutU32(table, static_cast<std::uint32_t>(name.size() + 6));
    table.push_back(0xaa);
    table.push_back(0xbb);
    table.insert(table.end(), name.begin(), name.end());
    PutU32(table, 0xdeadbeef);
    const auto size = static_cast<std::uint32_t>(f.data.size());
    PutU32(table, static_cast<std::uint32_t>(compressed_size) + size + 0x2cb);
    PutU32(table, static_cast<std::uint32_t>(stored.size()) + 0x92cb);
    PutU32(table, size);
    table.push_back(f.flags);
    PutU32(table, offset);
  }
  static constexpr char kMagic[] = "Master of Magic";
  std::vector<std::uint8_t> out(std::begin(kMagic), std::end(kMagic));
  for (std::uint8_t i = 1; i <= 14; ++i) out.push_back(i);
  const std::uint32_t seed = 1234;
  PutU32(out, static_cast<std::uint32_t>(body.size()));
  PutU32(out, seed);
  PutU32(out, static_cast<std::uint32_t>(files.size()) + seed + 7);
  PutU32(out, version);
  out.insert(out.end(), body.begin(), body.end());
  out.insert(out.end(), table.begin(), table.end());
  return out;
}

}  // namespace ragexe::testing
