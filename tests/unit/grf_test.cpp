#include "res/grf.h"

#include <zlib.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "res/grf_cipher.h"
#include "res/hashed_file_name.h"

using namespace ragexe::res;

namespace {

struct CipherVector {
  std::uint32_t seed;
  std::size_t size;
  int digits;
  std::uint64_t hash;
};
struct NameVector {
  const char* name;
  std::uint32_t hash;
};
#include "grf_cipher_vectors.inc"

// The generator of tools/grf_vectors.py.
std::vector<std::uint8_t> LcgBytes(std::uint32_t seed, std::size_t n) {
  std::vector<std::uint8_t> out(n);
  std::uint32_t x = seed;
  for (auto& b : out) {
    x = x * 1103515245u + 12345u;
    b = static_cast<std::uint8_t>(x >> 16);
  }
  return out;
}

std::uint64_t Fnv1a64(const std::vector<std::uint8_t>& data) {
  std::uint64_t h = 0xcbf29ce484222325ull;
  for (std::uint8_t b : data) h = (h ^ b) * 0x100000001b3ull;
  return h;
}

void PutU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

struct TestFile {
  std::string name;
  std::vector<std::uint8_t> data;
  std::uint8_t flags = 1;
};

// Writes an archive the way the format describes it, encoding with the inverse functions.
std::vector<std::uint8_t> BuildArchive(const std::vector<TestFile>& files,
                                       std::uint32_t version = 0x103) {
  std::vector<std::uint8_t> body, table;
  for (const TestFile& f : files) {
    std::vector<std::uint8_t> stored;
    std::int32_t compressed_size;
    if (f.flags == 1) {
      uLongf n = compressBound(static_cast<uLong>(f.data.size()));
      stored.resize(n);
      REQUIRE(compress(stored.data(), &n, f.data.data(), static_cast<uLong>(f.data.size())) ==
              Z_OK);
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

std::filesystem::path WriteTemp(const std::vector<std::uint8_t>& bytes, const char* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

}  // namespace

TEST_CASE("the cipher matches the original's DecryptBlocksMixed and DecryptFirstBlocks") {
  for (const CipherVector& v : kCipherVectors) {
    auto data = LcgBytes(v.seed, v.size);
    if (v.digits == 0) {
      grf::DecodeFirstBlocks(data);
    } else {
      grf::DecodeMixed(data, v.digits);
    }
    INFO("seed " << v.seed << ", size " << v.size << ", digits " << v.digits);
    CHECK(Fnv1a64(data) == v.hash);
  }
}

TEST_CASE("EncodeMixed undoes DecodeMixed") {
  for (int digits : {1, 3, 5, 7}) {
    const auto original = LcgBytes(99, 4096);
    auto data = original;
    grf::DecodeMixed(data, digits);
    grf::EncodeMixed(data, digits);
    CHECK(data == original);
  }
}

TEST_CASE("hashed names match the original's HashedFileName::Set") {
  for (const NameVector& v : kNameVectors) {
    INFO(v.name);
    CHECK(HashedFileName(v.name).hash == v.hash);
  }
  CHECK(HashedFileName("DATA\\Prontera.GAT").name == "data\\prontera.gat");
  CHECK(HashedFileName("\xC0\xAF").name == "\xC0\xAF");
}

TEST_CASE("decimal digits as _itoa writes them") {
  CHECK(grf::DecimalDigits(0) == 1);
  CHECK(grf::DecimalDigits(99999) == 5);
  CHECK(grf::DecimalDigits(-15000) == 6);
}

TEST_CASE("only .gnd, .gat, .act and .str get their first blocks encoded") {
  CHECK(grf::HasFirstBlocksExtension("data\\prontera.gat"));
  CHECK(grf::HasFirstBlocksExtension("data\\sprite\\X.ACT"));
  CHECK_FALSE(grf::HasFirstBlocksExtension("data\\prontera.rsw"));
  CHECK_FALSE(grf::HasFirstBlocksExtension("data\\a.gatx"));
  CHECK_FALSE(grf::HasFirstBlocksExtension("data\\effect"));
}

TEST_CASE("an archive written to the format reads back") {
  std::vector<std::uint8_t> big(70000);
  for (std::size_t i = 0; i < big.size(); ++i)
    big[i] = static_cast<std::uint8_t>((i * 7) ^ (i >> 5));
  const std::vector<TestFile> files = {
      {"data\\Prontera.gat", big, 1},
      {"data\\prontera.rsw", big, 1},
      {"data\\texture\\\xC0\xAF\xC0\xFA.bmp", {1, 2, 3, 4, 5}, 1},
      {"data\\effect", std::vector<std::uint8_t>(16, 9), 0},
      {"data\\plain.txt", std::vector<std::uint8_t>(40, 'x'), 0},
  };
  const auto path = WriteTemp(BuildArchive(files), "ragexe_grf_test.grf");
  std::string error;
  auto grf = GrfArchive::Open(path, &error);
  REQUIRE(grf != nullptr);
  CHECK(grf->version() == 0x103);
  REQUIRE(grf->entries().size() == files.size());
  CHECK(grf->DuplicateNames().empty());
  for (const TestFile& f : files) {
    INFO(f.name);
    const GrfEntry* e = grf->Find(f.name);
    REQUIRE(e != nullptr);
    CHECK(e->name == f.name);
    CHECK(e->flags == f.flags);
    auto data = grf->Read(*e, &error);
    REQUIRE(data.has_value());
    CHECK(*data == f.data);
  }
  CHECK(grf->Find("DATA\\PRONTERA.GAT") == grf->Find("data\\prontera.gat"));
  CHECK(grf->Find("data/prontera.gat") == nullptr);
  CHECK(grf->Find("data\\missing.gat") == nullptr);
  grf.reset();
  std::filesystem::remove(path);
}

TEST_CASE("a stream that breaks off is zero-filled to the recorded size") {
  // One stored deflate block of 12 bytes, cut after 4 of them: the padding to a whole
  // block gives 5 more, then the input ends.
  const std::vector<std::uint8_t> stream = {0x78, 0x01, 0x01, 0x0c, 0x00, 0xf3,
                                            0xff, 'R',  'I',  'F',  'F'};
  TestFile f{"data\\cut.wav", {}, 0};
  f.data = stream;
  auto bytes = BuildArchive({f});
  // BuildArchive stores it as is (flags 0); mark it compressed with size 12 instead.
  const std::size_t fields = bytes.size() - 17;
  const std::uint32_t size = 12;
  const std::uint32_t packed = static_cast<std::uint32_t>(stream.size()) + size + 0x2cb;
  for (int i = 0; i < 4; ++i) {
    bytes[fields + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(packed >> (8 * i));
    bytes[fields + 8 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(size >> (8 * i));
  }
  bytes[fields + 12] = 1;
  const auto path = WriteTemp(bytes, "ragexe_grf_cut.grf");
  std::string error;
  auto grf = GrfArchive::Open(path, &error);
  REQUIRE(grf != nullptr);
  bool damaged = false;
  auto data = grf->Read(grf->entries()[0], &error, &damaged);
  REQUIRE(data.has_value());
  CHECK(damaged);
  CHECK(*data == std::vector<std::uint8_t>{'R', 'I', 'F', 'F', 0, 0, 0, 0, 0, 0, 0, 0});
  grf.reset();
  std::filesystem::remove(path);
}

TEST_CASE("broken archives are refused") {
  std::string error;
  auto bytes = BuildArchive({{"data\\a.txt", {1, 2, 3}, 1}});

  SECTION("wrong magic") {
    bytes[0] = 'm';
    CHECK(GrfArchive::Open(WriteTemp(bytes, "ragexe_grf_bad.grf"), &error) == nullptr);
  }
  SECTION("version above 0x103") {
    bytes[0x2a] = 0x04;
    CHECK(GrfArchive::Open(WriteTemp(bytes, "ragexe_grf_bad.grf"), &error) == nullptr);
    CHECK(error.find("0x104") != std::string::npos);
  }
  SECTION("count larger than the table") {
    bytes[0x26] = 0xff;
    CHECK(GrfArchive::Open(WriteTemp(bytes, "ragexe_grf_bad.grf"), &error) == nullptr);
  }
  SECTION("truncated table") {
    bytes.resize(bytes.size() - 3);
    CHECK(GrfArchive::Open(WriteTemp(bytes, "ragexe_grf_bad.grf"), &error) == nullptr);
  }
  SECTION("table offset past the end") {
    bytes[0x21] = 0x7f;
    CHECK(GrfArchive::Open(WriteTemp(bytes, "ragexe_grf_bad.grf"), &error) == nullptr);
  }
  std::filesystem::remove(std::filesystem::temp_directory_path() / "ragexe_grf_bad.grf");
}

TEST_CASE("version 0x100 tables: swapped nibbles only, fields at the record length") {
  std::vector<std::uint8_t> table;
  const std::string name = "data\\old.txt";
  const auto len = static_cast<std::uint32_t>(4 + name.size() + 1);
  PutU32(table, len);
  for (char c : name) {
    const auto b = static_cast<std::uint8_t>(c);
    table.push_back(static_cast<std::uint8_t>((b >> 4) | (b << 4)));
  }
  table.push_back(0);
  PutU32(table, 3 + 8 + 0x2cb);
  PutU32(table, 8 + 0x92cb);
  PutU32(table, 8);
  table.push_back(1);
  PutU32(table, 0x40);
  std::string error;
  auto entries = ParseGrfTable(0x100, 1, table, &error);
  REQUIRE(entries.has_value());
  REQUIRE(entries->size() == 1);
  CHECK((*entries)[0].name == name);
  CHECK((*entries)[0].compressed_size == 3);
  CHECK((*entries)[0].stored_size == 8);
  CHECK((*entries)[0].offset == 0x40);
}
