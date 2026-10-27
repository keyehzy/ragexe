#include "res/ebm.h"

#define ZLIB_CONST
#include <zlib.h>

#include <algorithm>
#include <array>
#include <string_view>

#include "res/data_file.h"

namespace ragexe::res {

namespace {

constexpr std::size_t kHeaderSize = 2;
constexpr std::size_t kTrailerSize = 4;
constexpr std::size_t kWindowSize = 0x8000;
constexpr int kFixedBlock = 1;

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

// The type of the block whose header follows, when inflate has just stopped after an end
// of block: its 3 header bits start at the unused bits of the last byte taken.
int NextBlockType(const z_stream& stream, const std::uint8_t* body) {
  const unsigned unused = static_cast<unsigned>(stream.data_type) & 7;
  unsigned bits = 0;
  if (unused > 0 && stream.next_in > body) bits = stream.next_in[-1] >> (8 - unused);
  bits |= static_cast<unsigned>(stream.next_in[0]) << unused;
  return static_cast<int>((bits >> 1) & 3);
}

}  // namespace

EmblemInflate InflateEmblem(std::span<const std::uint8_t> data, std::vector<std::uint8_t>& out) {
  out.clear();
  // The zlib header as zlib 1.0.4 checks it: deflate, a window of at most 32 KB, the
  // check bits, and no preset dictionary.
  if (data.size() < kHeaderSize) return EmblemInflate::kFailed;
  const unsigned cmf = data[0];
  const unsigned flg = data[1];
  if ((cmf & 0xf) != Z_DEFLATED || (cmf >> 4) + 8 > 15 || ((cmf << 8) | flg) % 31 != 0 ||
      (flg & 0x20) != 0) {
    return EmblemInflate::kFailed;
  }
  z_stream stream{};
  if (inflateInit2(&stream, -15) != Z_OK) return EmblemInflate::kFailed;
  // zlib 1.0.4 inflates through a zeroed 32 KB window and never checks a distance
  // against what it has written, so one that reaches before the start copies zeros.
  static constexpr std::array<Bytef, kWindowSize> kZeros{};
  inflateSetDictionary(&stream, kZeros.data(), static_cast<uInt>(kZeros.size()));
  out.resize(kEmblemBufferSize);
  stream.next_in = data.data() + kHeaderSize;
  stream.avail_in = static_cast<uInt>(data.size() - kHeaderSize);
  stream.next_out = out.data();
  stream.avail_out = static_cast<uInt>(out.size());
  // Block by block, to know the type of the one an error is in.
  const std::uint8_t* const body = stream.next_in;
  int block_type = data.size() > kHeaderSize ? (body[0] >> 1) & 3 : -1;
  int z = Z_OK;
  while ((z = inflate(&stream, Z_BLOCK)) == Z_OK) {
    if ((stream.data_type & 128) != 0 && stream.avail_in > 0)
      block_type = NextBlockType(stream, body);
  }
  // zlib marks the two length symbols the fixed code has but deflate does not use as
  // invalid; 1.0.4 takes them as a copy of length 0, which its fast loop decrements past
  // zero.
  const bool runs_away = z == Z_DATA_ERROR && block_type == kFixedBlock && stream.msg != nullptr &&
                         std::string_view(stream.msg) == "invalid literal/length code";
  const std::size_t size = stream.total_out;
  const std::span<const std::uint8_t> trailer(stream.next_in, stream.avail_in);
  inflateEnd(&stream);
  out.resize(size);
  if (runs_away) return EmblemInflate::kRunsAway;
  // Then the Adler-32 of what was inflated, big-endian.
  if (z != Z_STREAM_END || trailer.size() < kTrailerSize) return EmblemInflate::kFailed;
  const uLong stored = uLong{trailer[0]} << 24 | uLong{trailer[1]} << 16 | uLong{trailer[2]} << 8 |
                       uLong{trailer[3]};
  return adler32(adler32(0, nullptr, 0), out.data(), static_cast<uInt>(out.size())) == stored
             ? EmblemInflate::kInflated
             : EmblemInflate::kFailed;
}

bool DecodeEmblem(std::span<const std::uint8_t> data, Bitmap& out, std::string* error) {
  out = Bitmap{};
  if (data.size() > kEmblemBufferSize) {
    return Fail(error, "the file is larger than the client's 8 KB buffer");
  }
  // A length still at the buffer's size, by an error or by filling it, is no image.
  // Otherwise slot 1 of the CBitmapRes at +0x110 decodes it, named "_emblem.bmp"
  // (00605fcc), which picks the BMP decoder.
  std::vector<std::uint8_t> bmp;
  const EmblemInflate inflated = InflateEmblem(data, bmp);
  if (inflated == EmblemInflate::kRunsAway) {
    return Fail(error, "Failed - Emblem Uncompress!");  // 00605fd8
  }
  if (inflated == EmblemInflate::kInflated && bmp.size() != kEmblemBufferSize &&
      DecodeBmp(bmp, out, nullptr)) {
    out.width = std::min(out.width, kEmblemSize);
    out.height = std::min(out.height, kEmblemSize);
    return true;
  }
  // CBitmapRes::Create (00427b30).
  out = Bitmap{};
  out.width = kEmblemSize;
  out.height = kEmblemSize;
  out.pixels.assign(static_cast<std::size_t>(kEmblemSize) * kEmblemSize, 0xffffffffu);
  return true;
}

std::unique_ptr<Resource> EmblemRes::Clone() const { return std::make_unique<EmblemRes>(); }

bool EmblemRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "cannot open " + file.path();
    return false;
  }
  error_.clear();
  return DecodeEmblem(file.data(), bitmap_, &error_);
}

// Slot 4 (00428670) resets the bitmap.
void EmblemRes::Reset() { bitmap_ = Bitmap{}; }

void RegisterEmblemTypes(ResourceManager& manager) {
  manager.RegisterType("ebm", "..\\_tmpEmblem\\", std::make_unique<EmblemRes>());
}

}  // namespace ragexe::res
