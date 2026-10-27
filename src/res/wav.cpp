#include "res/wav.h"

#include <array>
#include <cstring>

#include "res/data_file.h"

namespace ragexe::res {

namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatImaAdpcm = 0x11;
constexpr std::uint16_t kFormatMpeg = 0x55;
constexpr std::uint32_t kFirstChunk = 12;
constexpr std::size_t kWaveHeaderSize = 44;
// The port's limit on a decoded sample; the client asks GlobalAlloc for any size.
constexpr std::uint32_t kMaxSampleBytes = 1u << 26;
// The MPEG scan looks at the first 4 KB.
constexpr std::uint32_t kScanWindow = 0x1000;

// The IMA ADPCM step sizes. Mss32.dll's tables (2114b840, the difference for each step
// index and nibble; 2114c360, the next index) are built from these and the index changes
// below, entry for entry.
constexpr std::array<std::int32_t, 89> kImaSteps = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,   21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,   73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,  253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,  876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749, 3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr std::array<std::int32_t, 8> kImaIndexChange = {-1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::uint32_t kMaxImaIndex = 88;

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

// The file as Mss32.dll reads it, at 32-bit offsets from its start.
class Bytes {
 public:
  explicit Bytes(std::span<const std::uint8_t> data) : data_(data) {}

  bool Has(std::uint64_t pos, std::uint64_t n) const {
    return pos <= data_.size() && n <= data_.size() - pos;
  }
  std::uint64_t size() const { return data_.size(); }
  std::uint8_t U8(std::uint64_t pos) const { return data_[pos]; }
  std::uint16_t U16(std::uint64_t pos) const {
    return static_cast<std::uint16_t>(data_[pos] | data_[pos + 1] << 8);
  }
  std::uint32_t U32(std::uint64_t pos) const {
    return static_cast<std::uint32_t>(U16(pos)) | static_cast<std::uint32_t>(U16(pos + 2)) << 16;
  }

  // Mss32.dll's case-insensitive comparison (2111f180): character by character until one
  // differs, so it reads past the data only when what comes before matches. nullopt
  // where it does.
  std::optional<bool> Matches(std::uint64_t pos, std::string_view text) const {
    for (std::size_t k = 0; k < text.size(); ++k) {
      if (!Has(pos + k, 1)) return std::nullopt;
      if (Upper(data_[pos + k]) != Upper(static_cast<std::uint8_t>(text[k]))) return false;
    }
    return true;
  }

 private:
  static std::uint8_t Upper(std::uint8_t c) {
    return c >= 'a' && c <= 'z' ? static_cast<std::uint8_t>(c - 'a' + 'A') : c;
  }

  std::span<const std::uint8_t> data_;
};

enum class Found { kYes, kNo, kFaults };

// Walks the chunks from offset 12 for one named `tag`, as AIL_WAV_info does: the first is
// compared before any check, and the walk stops at the RIFF size, measured from the
// start of the file, compared as a signed 32-bit difference. Steps are 32-bit, so a size
// can bring the walk back where it was (0xfffffff8 stays put), and then it never ends.
// Every step that goes on reads inside the file, so more steps than the file has bytes
// mean it has come back.
Found FindChunk(const Bytes& in, std::uint32_t riff_size, std::string_view tag,
                std::uint32_t& pos) {
  pos = kFirstChunk;
  for (std::uint64_t steps = 0; steps <= in.size(); ++steps) {
    const auto match = in.Matches(pos, tag);
    if (!match) return Found::kFaults;
    if (*match) return Found::kYes;
    if (!in.Has(pos + 4, 4)) return Found::kFaults;
    const std::uint32_t size = in.U32(pos + 4);
    pos += (size & 1) + size + 8;
    if (static_cast<std::int32_t>(pos - riff_size) >= 0) return Found::kNo;
  }
  return Found::kFaults;
}

std::int32_t ImaDifference(std::uint32_t index, std::uint32_t nibble) {
  const std::int32_t step = kImaSteps[index];
  std::int32_t diff = step >> 3;
  if ((nibble & 4) != 0) diff += step;
  if ((nibble & 2) != 0) diff += step >> 1;
  if ((nibble & 1) != 0) diff += step >> 2;
  return (nibble & 8) != 0 ? -diff : diff;
}

// One channel of the decoder: its sample and step index.
struct ImaChannel {
  std::int16_t sample = 0;
  std::uint32_t index = 0;

  // The original adds in 16 bits and saturates on the side the nibble's sign points to.
  std::int16_t Next(std::uint32_t nibble) {
    const std::int32_t sum = sample + ImaDifference(index, nibble);
    sample = static_cast<std::int16_t>(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
    const std::int32_t next = static_cast<std::int32_t>(index) + kImaIndexChange[nibble & 7];
    index = static_cast<std::uint32_t>(next < 0 ? 0 : next > 88 ? 88 : next);
    return sample;
  }
};

enum class Decoded { kOk, kNone, kFaults };

// The block decoders of AIL_decompress_ADPCM (mono 2111e288, stereo 2111e395), over the
// data chunk into `out` after its 44-byte header. Each block starts with a header per
// channel: the first sample, written out at once without checking for room, and the step
// index, not range-checked. Then 4-byte words of 8 nibbles, low first, alternating between
// the channels in stereo, until the block's bytes are used up: counted down by the words
// read and compared with zero, so a block whose data is not a multiple of a word runs on
// into the next without its header. Input is checked before each word, output before
// each sample.
Decoded DecodeIma(const Bytes& in, const WaveInfo& info, std::vector<std::uint8_t>& out) {
  const std::uint64_t frame_channels = info.channels == 2 ? 2 : 1;
  const std::uint64_t out_end = out.size();
  std::uint64_t o = kWaveHeaderSize;
  std::uint64_t i = info.data_offset;
  const std::uint64_t in_end = std::uint64_t{info.data_offset} + info.data_size;
  std::array<ImaChannel, 2> channels{};
  const auto put = [&](std::int16_t sample) {
    const auto bits = static_cast<std::uint16_t>(sample);
    out[o] = static_cast<std::uint8_t>(bits);
    out[o + 1] = static_cast<std::uint8_t>(bits >> 8);
    o += 2;
  };
  do {
    if (!in.Has(i, 4 * frame_channels) || o + 2 * frame_channels > out_end) {
      return Decoded::kFaults;
    }
    for (std::uint64_t c = 0; c < frame_channels; ++c) {
      const std::uint32_t header = in.U32(i);
      i += 4;
      channels[c].sample = static_cast<std::int16_t>(header & 0xffff);
      channels[c].index = (header >> 16) & 0xff;
      if (channels[c].index > kMaxImaIndex) return Decoded::kFaults;
      put(channels[c].sample);
    }
    auto remaining = static_cast<std::int32_t>(info.block_align - 4 * frame_channels);
    do {
      if (i >= in_end) return Decoded::kOk;
      if (!in.Has(i, 4 * frame_channels)) return Decoded::kFaults;
      std::array<std::uint32_t, 2> words{};
      for (std::uint64_t c = 0; c < frame_channels; ++c) {
        words[c] = in.U32(i);
        i += 4;
      }
      remaining -= static_cast<std::int32_t>(4 * frame_channels);
      for (int n = 0; n < 8; ++n) {
        if (o >= out_end) return Decoded::kOk;
        for (std::uint64_t c = 0; c < frame_channels; ++c) {
          put(channels[c].Next(words[c] & 0xf));
          words[c] >>= 4;
        }
      }
      if (o >= out_end) return Decoded::kOk;
    } while (remaining != 0);
  } while (i < in_end);
  return Decoded::kOk;
}

// AIL_decompress_ADPCM (21129480): a 16-bit PCM wave of channels × samples, zeroed
// first, so what the data does not reach stays silent.
Decoded DecompressAdpcm(const Bytes& in, const WaveInfo& info, std::vector<std::uint8_t>& out) {
  if (info.samples == 0) return Decoded::kNone;
  const std::uint32_t pcm_bytes =
      ((static_cast<std::uint32_t>(info.channels) * info.samples) & 0x0fffffff) * 2;
  if (pcm_bytes > kMaxSampleBytes) return Decoded::kFaults;
  out.assign(kWaveHeaderSize + pcm_bytes, 0);
  const auto put16 = [&](std::size_t pos, std::uint32_t v) {
    out[pos] = static_cast<std::uint8_t>(v);
    out[pos + 1] = static_cast<std::uint8_t>(v >> 8);
  };
  const auto put32 = [&](std::size_t pos, std::uint32_t v) {
    put16(pos, v & 0xffff);
    put16(pos + 2, v >> 16);
  };
  std::memcpy(out.data(), "RIFF", 4);
  put32(4, pcm_bytes + 36);
  std::memcpy(out.data() + 8, "WAVEfmt ", 8);
  put32(16, 16);
  put16(20, kFormatPcm);
  put16(22, info.channels);
  put32(24, info.rate);
  put32(28, ((static_cast<std::uint32_t>(info.channels) * info.rate) & 0x0fffffff) << 1);
  put16(32, static_cast<std::uint32_t>(info.channels) * 2);
  put16(34, 16);
  std::memcpy(out.data() + 36, "data", 4);
  put32(40, pcm_bytes);
  return DecodeIma(in, info, out);
}

}  // namespace

WaveInfoResult ReadWaveInfo(std::span<const std::uint8_t> data) {
  const Bytes in(data);
  WaveInfoResult result;
  if (!in.Has(4, 4)) return {true, std::nullopt};
  const std::uint32_t riff_size = in.U32(4);
  const auto wave = in.Matches(8, "WAVE");
  if (!wave) return {true, std::nullopt};
  if (!*wave) return result;
  std::uint32_t pos = 0;
  Found found = FindChunk(in, riff_size, "fmt ", pos);
  if (found != Found::kYes) return {found == Found::kFaults, std::nullopt};
  if (!in.Has(pos + 8, 16)) return {true, std::nullopt};
  WaveInfo info;
  info.format = in.U16(pos + 8);
  info.channels = in.U16(pos + 10);
  info.rate = in.U32(pos + 12);
  info.block_align = in.U16(pos + 0x14);
  info.bits = in.U16(pos + 0x16);
  found = FindChunk(in, riff_size, "data", pos);
  if (found != Found::kYes) return {found == Found::kFaults, std::nullopt};
  if (!in.Has(pos + 4, 4)) return {true, std::nullopt};
  info.data_offset = pos + 8;
  info.data_size = in.U32(pos + 4);
  if (info.format == kFormatImaAdpcm && info.bits == 4) {
    found = FindChunk(in, riff_size, "fact", pos);
    if (found == Found::kFaults) return {true, std::nullopt};
    if (found == Found::kYes) {
      if (!in.Has(pos + 8, 4)) return {true, std::nullopt};
      info.samples = in.U32(pos + 8);
    } else {
      // Without "fact", from the block size: the 4-byte header per channel holds the first
      // sample, the rest 2 samples a byte. The divisions by zero this allows fault.
      const std::uint32_t header =
          (info.channels / 2 & 0x1f) < 30 ? 4u << (info.channels / 2 & 0x1f) : 0;
      if (header == 0 || info.block_align == 0) return {true, std::nullopt};
      const std::uint32_t align = info.block_align;
      info.samples = ((align - header) * 8 / header + 1) * ((info.data_size - 1 + align) / align);
    }
  } else {
    info.samples = info.bits == 0 ? 0 : info.data_size * 8 / info.bits;
  }
  result.info = info;
  return result;
}

std::optional<WaveFileType> ClassifyWave(std::span<const std::uint8_t> data) {
  const Bytes in(data);
  if (data.size() < 8) return WaveFileType::kOther;
  const WaveInfoResult wave = ReadWaveInfo(data);
  if (wave.faults) return std::nullopt;
  std::uint64_t base = 0;
  std::uint64_t size = data.size();
  if (wave.info) {
    if (wave.info->format == kFormatPcm) return WaveFileType::kPcm;
    if (wave.info->format == kFormatImaAdpcm) {
      return wave.info->bits == 4 ? WaveFileType::kImaAdpcm : WaveFileType::kOther;
    }
    // Other tags go to an ASI codec that takes them, if any; Mp3dec.asi takes 0x55, whose
    // data chunk is then checked as below.
    if (wave.info->format != kFormatMpeg) return WaveFileType::kOther;
    base = wave.info->data_offset;
    size = wave.info->data_size;
  }
  // VOC, XMIDI, DLS and MLS signatures each give a type of their own.
  const auto voc = in.Matches(base, "Creative");
  if (!voc) return std::nullopt;
  if (*voc) return WaveFileType::kOther;
  const auto form = in.Matches(base, "FORM");
  if (!form) return std::nullopt;
  if (*form) {
    const auto xdir = in.Matches(base + 8, "XDIR");
    if (!xdir) return std::nullopt;
    if (*xdir) return WaveFileType::kOther;
  }
  if (!in.Has(base, 4)) return std::nullopt;
  if (in.U32(base) == 0x46464952) {  // "RIFF"
    if (!in.Has(base + 8, 4)) return std::nullopt;
    const std::uint32_t kind = in.U32(base + 8);
    if (kind == 0x20534c44 || kind == 0x20534c4d) return WaveFileType::kOther;  // "DLS ", "MLS "
  }
  // MPEG frame syncs in the first 4 KB: 0xff, then a byte whose top 4 bits are set; not
  // four 0xff, nor a third byte whose top 6 bits are set. Layer III answers at once.
  // After a sync the scan skips 3 more bytes, so it can read past its window.
  if (size >= 4) {
    std::uint32_t count = size > kScanWindow ? kScanWindow : static_cast<std::uint32_t>(size);
    std::uint64_t p = base;
    do {
      --count;
      if (!in.Has(p, 2)) return std::nullopt;
      if ((in.U16(p) & 0xf0ff) == 0xf0ff) {
        if (!in.Has(p, 4)) return std::nullopt;
        const std::uint32_t word = in.U32(p);
        if (word != 0xffffffff && (word & 0xfc0000) != 0xfc0000 && (word >> 9 & 3) == 1) {
          return WaveFileType::kMpegLayer3;
        }
        p += 3;
      }
      ++p;
    } while (count > 3);
  }
  // Then a MIDI header anywhere; any other answer is not one CWave tells apart.
  for (std::uint64_t left = size, p = base; left >= 4; --left, ++p) {
    const auto midi = in.Matches(p, "MThd");
    if (!midi) return std::nullopt;
    if (*midi) break;
  }
  return WaveFileType::kOther;
}

bool DecodeWave(std::span<const std::uint8_t> data, Wave& out, std::string* error) {
  out = Wave{};
  const auto type = ClassifyWave(data);
  if (!type) return Fail(error, "Mss32.dll reads past the file or walks its chunks forever");
  out.type = *type;
  if (*type != WaveFileType::kImaAdpcm) {
    out.sample.assign(data.begin(), data.end());
    return true;
  }
  const WaveInfoResult wave = ReadWaveInfo(data);
  switch (DecompressAdpcm(Bytes(data), *wave.info, out.sample)) {
    case Decoded::kOk: return true;
    case Decoded::kNone: out.sample.clear(); return true;
    case Decoded::kFaults: break;
  }
  out.sample.clear();
  return Fail(error, "the ADPCM decoder reads or writes past its buffers");
}

std::unique_ptr<Resource> WaveRes::Clone() const { return std::make_unique<WaveRes>(); }

bool WaveRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "Wave :: Cannot find File : " + std::string(path);  // 00606524
    return false;
  }
  Reset();
  error_.clear();
  return DecodeWave(file.data(), wave_, &error_);
}

// Slot 4 (00435460) frees the sample, with AIL_mem_free_lock when Miles made it.
void WaveRes::Reset() { wave_ = Wave{}; }

void RegisterWaveTypes(ResourceManager& manager) {
  manager.RegisterType("wav", "wav\\", std::make_unique<WaveRes>());
}

}  // namespace ragexe::res
