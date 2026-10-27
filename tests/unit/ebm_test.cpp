#include "res/ebm.h"

#include <zlib.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "res/data_files.h"
#include "res/resource.h"

using namespace ragexe::res;

namespace {

using Bytes = std::vector<std::uint8_t>;

void Put(Bytes& out, std::uint32_t v, int size) {
  for (int k = 0; k < size; ++k) out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
}

// A 24-bit BMP whose pixel (x, y), counted from the top, is B = x, G = y, R = 7.
Bytes Bmp(std::int32_t width, std::int32_t height) {
  const std::int32_t stride = (width * 3 + 3) & ~3;
  Bytes out = {'B', 'M'};
  Put(out, static_cast<std::uint32_t>(54 + stride * height), 4);
  Put(out, 0, 4);
  Put(out, 54, 4);
  Put(out, 40, 4);
  Put(out, static_cast<std::uint32_t>(width), 4);
  Put(out, static_cast<std::uint32_t>(height), 4);
  Put(out, 1, 2);
  Put(out, 24, 2);
  out.resize(54, 0);
  for (std::int32_t row = height - 1; row >= 0; --row) {
    for (std::int32_t x = 0; x < width; ++x) {
      out.push_back(static_cast<std::uint8_t>(x));
      out.push_back(static_cast<std::uint8_t>(row));
      out.push_back(7);
    }
    out.resize(out.size() + static_cast<std::size_t>(stride - width * 3), 0);
  }
  return out;
}

Bytes Compress(const Bytes& data) {
  uLongf size = compressBound(static_cast<uLong>(data.size()));
  Bytes out(size);
  REQUIRE(compress(out.data(), &size, data.data(), static_cast<uLong>(data.size())) == Z_OK);
  out.resize(size);
  return out;
}

void PutAdler(Bytes& out, const Bytes& inflated) {
  const uLong adler =
      adler32(adler32(0, nullptr, 0), inflated.data(), static_cast<uInt>(inflated.size()));
  for (int k = 3; k >= 0; --k) out.push_back(static_cast<std::uint8_t>(adler >> (8 * k)));
}

// A deflate bit stream: values least significant bit first, Huffman codes most.
class Bits {
 public:
  void Put(std::uint32_t value, int n) {
    for (int k = 0; k < n; ++k) Bit((value >> k) & 1);
  }
  void Code(std::uint32_t code, int n) {
    for (int k = n - 1; k >= 0; --k) Bit((code >> k) & 1);
  }
  // The fixed code of a literal byte.
  void Literal(std::uint8_t byte) {
    if (byte < 144) {
      Code(0x30u + byte, 8);
    } else {
      Code(0x190u + byte - 144, 9);
    }
  }
  const Bytes& bytes() const { return bytes_; }

 private:
  void Bit(std::uint32_t bit) {
    if (count_ % 8 == 0) bytes_.push_back(0);
    bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | bit << (count_ % 8));
    ++count_;
  }
  Bytes bytes_;
  int count_ = 0;
};

// A zlib stream of image in one fixed-code block, with what `middle` writes after the
// first `split` bytes.
template <typename Middle>
Bytes FixedStream(const Bytes& image, std::size_t split, std::size_t resume, Middle middle) {
  Bits bits;
  bits.Put(1, 1);
  bits.Put(1, 2);
  for (std::size_t k = 0; k < split; ++k) bits.Literal(image[k]);
  middle(bits);
  for (std::size_t k = resume; k < image.size(); ++k) bits.Literal(image[k]);
  bits.Code(0, 7);
  Bytes out = {0x78, 0x9c};
  out.insert(out.end(), bits.bytes().begin(), bits.bytes().end());
  PutAdler(out, image);
  return out;
}

bool IsBlank(const Bitmap& emblem) {
  return emblem.width == 24 && emblem.height == 24 &&
         emblem.pixels == std::vector<std::uint32_t>(std::size_t{24} * 24, 0xffffffffu);
}

}  // namespace

TEST_CASE("an emblem is a zlib-compressed BMP") {
  Bitmap emblem;
  std::string error;
  REQUIRE(DecodeEmblem(Compress(Bmp(24, 24)), emblem, &error));
  CHECK(emblem.width == 24);
  CHECK(emblem.height == 24);
  CHECK(emblem.pixels[0] == 0xff070000u);
  CHECK(emblem.pixels[24 * 5 + 3] == 0xff070503u);
}

TEST_CASE("a larger BMP keeps its pixels, but its size is clamped to 24") {
  Bitmap emblem;
  std::string error;
  REQUIRE(DecodeEmblem(Compress(Bmp(30, 20)), emblem, &error));
  CHECK(emblem.width == 24);
  CHECK(emblem.height == 20);
  CHECK(emblem.pixels.size() == 30u * 20u);
  // Read with a stride of 24, the second row starts inside the first.
  CHECK(emblem.pixels[24] == 0xff070018u);
  REQUIRE(DecodeEmblem(Compress(Bmp(10, 40)), emblem, &error));
  CHECK(emblem.width == 10);
  CHECK(emblem.height == 24);
}

TEST_CASE("what does not inflate or decode gives the blank emblem") {
  Bitmap emblem;
  std::string error;
  const Bytes good = Compress(Bmp(24, 24));
  const auto with_header = [&](std::uint8_t cmf, std::uint8_t flg) {
    Bytes out = good;
    out[0] = cmf;
    out[1] = flg;
    return out;
  };
  Bytes bad_check = good;
  bad_check.back() ^= 1;
  const std::vector<Bytes> cases = {
      {},
      {0x78},
      Bytes(good.begin(), good.end() - 1),  // no complete Adler-32
      Bytes(good.begin(), good.end() - 8),
      bad_check,
      with_header(0x88, 0x1a),  // a 64 KB window
      with_header(0x77, 0x22),  // method 7
      with_header(0x78, 0xbb),  // a preset dictionary
      with_header(0x78, 0x9d),  // the check bits
      Compress(Bytes(100, 'x')),
  };
  for (std::size_t k = 0; k < cases.size(); ++k) {
    INFO("case " << k);
    REQUIRE(DecodeEmblem(cases[k], emblem, &error));
    CHECK(IsBlank(emblem));
  }
  Bytes trailing = good;
  trailing.push_back(1);
  REQUIRE(DecodeEmblem(trailing, emblem, &error));
  CHECK_FALSE(IsBlank(emblem));
}

TEST_CASE("the 8 KB buffers: what fills the inflated one is no image, a larger file fails") {
  Bitmap emblem;
  std::string error;
  Bytes image = Bmp(24, 24);
  image.resize(0x2000 - 1, 0x55);
  REQUIRE(DecodeEmblem(Compress(image), emblem, &error));
  CHECK_FALSE(IsBlank(emblem));
  image.push_back(0x55);
  REQUIRE(DecodeEmblem(Compress(image), emblem, &error));
  CHECK(IsBlank(emblem));
  image.push_back(0x55);
  REQUIRE(DecodeEmblem(Compress(image), emblem, &error));
  CHECK(IsBlank(emblem));
  Bytes large = Compress(Bmp(24, 24));
  large.resize(0x2000, 0);
  CHECK(DecodeEmblem(large, emblem, &error));
  large.push_back(0);
  CHECK_FALSE(DecodeEmblem(large, emblem, &error));
}

TEST_CASE("zlib 1.0.4's inflate: distances before the start, and lengths 286 and 287") {
  const Bytes image = Bmp(24, 24);
  Bitmap plain;
  Bitmap emblem;
  std::string error;
  REQUIRE(DecodeEmblem(Compress(image), plain, &error));
  // The 4 reserved header bytes, zero, as a copy from 10 back, at offset 6.
  const Bytes far = FixedStream(image, 6, 10, [](Bits& bits) {
    bits.Code(2, 7);
    bits.Code(6, 5);
    bits.Put(1, 2);
  });
  std::vector<std::uint8_t> inflated;
  CHECK(InflateEmblem(far, inflated) == EmblemInflate::kInflated);
  CHECK(inflated == image);
  REQUIRE(DecodeEmblem(far, emblem, &error));
  CHECK(emblem.pixels == plain.pixels);
  for (std::uint32_t code : {0xc6u, 0xc7u}) {
    const Bytes stream = FixedStream(image, 100, 100, [code](Bits& bits) {
      bits.Code(code, 8);
      bits.Code(0, 5);
    });
    CHECK(InflateEmblem(stream, inflated) == EmblemInflate::kRunsAway);
    CHECK_FALSE(DecodeEmblem(stream, emblem, &error));
    CHECK(error == "Failed - Emblem Uncompress!");
  }
}

TEST_CASE("EmblemRes loads from _tmpEmblem beside data") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_ebm_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data");
  std::filesystem::create_directories(dir / "_tmpEmblem");
  {
    const Bytes bytes = Compress(Bmp(24, 24));
    std::ofstream f(dir / "_tmpEmblem" / "server_7_3.ebm", std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  }
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterEmblemTypes(manager);
  auto* emblem = static_cast<EmblemRes*>(manager.Get("server_7_3.ebm"));
  REQUIRE(emblem != nullptr);
  CHECK(emblem->bitmap().width == 24);
  CHECK_FALSE(IsBlank(emblem->bitmap()));
  CHECK(manager.Get("server_7_4.ebm") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
