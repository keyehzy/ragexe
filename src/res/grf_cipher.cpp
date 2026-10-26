#include "res/grf_cipher.h"

#include <array>
#include <cstring>
#include <string>

namespace ragexe::res::grf {

namespace {

// The DES tables of DesCipher, 1-based bit numbers counted from the most significant
// bit of the first byte.
// clang-format off
constexpr std::array<std::uint8_t, 64> kInitialPermutation = {  // 005e8b00
    58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9,  1, 59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7};
constexpr std::array<std::uint8_t, 64> kFinalPermutation = {  // 005e8b40
    40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9,  49, 17, 57, 25};
constexpr std::array<std::uint8_t, 48> kExpansion = {  // 005e8bf8
    32, 1,  2,  3,  4,  5,  4,  5,  6,  7,  8,  9,  8,  9,  10, 11,
    12, 13, 12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21,
    22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};
constexpr std::array<std::uint8_t, 32> kPermutation = {  // 005e8c28
    16, 7, 20, 21, 29, 12, 28, 17, 1,  15, 23, 26, 5,  18, 31, 10,
    2,  8, 24, 14, 32, 27, 3,  9,  19, 13, 30, 6,  22, 11, 4,  25};
// 005e8c48: eight tables of 64, each indexed directly by its 6-bit group.
constexpr std::array<std::uint8_t, 512> kSubstitution = {
    // S1
    14,  0,  4, 15, 13,  7,  1,  4,  2, 14, 15,  2, 11, 13,  8,  1,
     3, 10, 10,  6,  6, 12, 12, 11,  5,  9,  9,  5,  0,  3,  7,  8,
     4, 15,  1, 12, 14,  8,  8,  2, 13,  4,  6,  9,  2,  1, 11,  7,
    15,  5, 12, 11,  9,  3,  7, 14,  3, 10, 10,  0,  5,  6,  0, 13,
    // S2
    15,  3,  1, 13,  8,  4, 14,  7,  6, 15, 11,  2,  3,  8,  4, 14,
     9, 12,  7,  0,  2,  1, 13, 10, 12,  6,  0,  9,  5, 11, 10,  5,
     0, 13, 14,  8,  7, 10, 11,  1, 10,  3,  4, 15, 13,  4,  1,  2,
     5, 11,  8,  6, 12,  7,  6, 12,  9,  0,  3,  5,  2, 14, 15,  9,
    // S3
    10, 13,  0,  7,  9,  0, 14,  9,  6,  3,  3,  4, 15,  6,  5, 10,
     1,  2, 13,  8, 12,  5,  7, 14, 11, 12,  4, 11,  2, 15,  8,  1,
    13,  1,  6, 10,  4, 13,  9,  0,  8,  6, 15,  9,  3,  8,  0,  7,
    11,  4,  1, 15,  2, 14, 12,  3,  5, 11, 10,  5, 14,  2,  7, 12,
    // S4
     7, 13, 13,  8, 14, 11,  3,  5,  0,  6,  6, 15,  9,  0, 10,  3,
     1,  4,  2,  7,  8,  2,  5, 12, 11,  1, 12, 10,  4, 14, 15,  9,
    10,  3,  6, 15,  9,  0,  0,  6, 12, 10, 11,  1,  7, 13, 13,  8,
    15,  9,  1,  4,  3,  5, 14, 11,  5, 12,  2,  7,  8,  2,  4, 14,
    // S5
     2, 14, 12, 11,  4,  2,  1, 12,  7,  4, 10,  7, 11, 13,  6,  1,
     8,  5,  5,  0,  3, 15, 15, 10, 13,  3,  0,  9, 14,  8,  9,  6,
     4, 11,  2,  8,  1, 12, 11,  7, 10,  1, 13, 14,  7,  2,  8, 13,
    15,  6,  9, 15, 12,  0,  5,  9,  6, 10,  3,  4,  0,  5, 14,  3,
    // S6
    12, 10,  1, 15, 10,  4, 15,  2,  9,  7,  2, 12,  6,  9,  8,  5,
     0,  6, 13,  1,  3, 13,  4, 14, 14,  0,  7, 11,  5,  3, 11,  8,
     9,  4, 14,  3, 15,  2,  5, 12,  2,  9,  8,  5, 12, 15,  3, 10,
     7, 11,  0, 14,  4,  1, 10,  7,  1,  6, 13,  0, 11,  8,  6, 13,
    // S7
     4, 13, 11,  0,  2, 11, 14,  7, 15,  4,  0,  9,  8,  1, 13, 10,
     3, 14, 12,  3,  9,  5,  7, 12,  5,  2, 10, 15,  6,  8,  1,  6,
     1,  6,  4, 11, 11, 13, 13,  8, 12,  1,  3,  4,  7, 10, 14,  7,
    10,  9, 15,  5,  6,  0,  8, 15,  0, 14,  5,  2,  9,  3,  2, 12,
    // S8
    13,  1,  2, 15,  8, 13,  4,  8,  6, 10, 15,  3, 11,  7,  1,  4,
    10, 12,  9,  5,  3,  6, 14, 11,  5,  0,  0, 14, 12,  9,  7,  2,
     7,  2, 11,  1,  4, 14,  1,  7,  9,  4, 12, 10, 14,  8,  2, 13,
     0, 15,  6, 12, 10,  9, 13,  0, 15,  3,  3,  5,  5,  6,  8, 11};
// clang-format on

constexpr std::size_t kFirstBlocks = 20;

bool Bit(const std::uint8_t* bytes, int index) {
  return (bytes[index >> 3] & (0x80 >> (index & 7))) != 0;
}

void SetBit(std::uint8_t* bytes, int index) {
  bytes[index >> 3] = static_cast<std::uint8_t>(bytes[index >> 3] | (0x80 >> (index & 7)));
}

template <std::size_t N>
void Permute(const std::uint8_t* in, std::uint8_t* out, const std::array<std::uint8_t, N>& table) {
  std::memset(out, 0, N / 8);
  for (std::size_t i = 0; i < N; ++i) {
    if (Bit(in, table[i] - 1)) SetBit(out, static_cast<int>(i));
  }
}

// The byte pairs the shuffle exchanges in the last byte (jump tables 004dcb14, 004dcba4).
std::uint8_t SubstituteLastByte(std::uint8_t b) {
  switch (b) {
    case 0x00: return 0x2b;
    case 0x2b: return 0x00;
    case 0x01: return 0x68;
    case 0x68: return 0x01;
    case 0x48: return 0x77;
    case 0x77: return 0x48;
    case 0x60: return 0xff;
    case 0xff: return 0x60;
    case 0x6c: return 0x80;
    case 0x80: return 0x6c;
    case 0xb9: return 0xc0;
    case 0xc0: return 0xb9;
    case 0xeb: return 0xfe;
    case 0xfe: return 0xeb;
    default: return b;
  }
}

// 004dc8bb: output byte i takes input byte kShuffle[i].
constexpr std::array<std::uint8_t, 8> kShuffle = {3, 4, 6, 0, 1, 2, 5, 7};

void Shuffle(std::uint8_t* block) {
  std::array<std::uint8_t, 8> in;
  std::memcpy(in.data(), block, 8);
  for (std::size_t i = 0; i < 8; ++i) block[i] = in[kShuffle[i]];
  block[7] = SubstituteLastByte(block[7]);
}

void Unshuffle(std::uint8_t* block) {
  std::array<std::uint8_t, 8> in;
  std::memcpy(in.data(), block, 8);
  for (std::size_t i = 0; i < 8; ++i) block[kShuffle[i]] = in[i];
  block[7] = SubstituteLastByte(block[7]);
}

int Cycle(int digits) {
  if (digits < 3) return 1;
  if (digits < 5) return digits + 1;
  if (digits < 7) return digits + 9;
  return digits + 15;
}

template <typename PlainBlock>
void Mixed(std::span<std::uint8_t> data, int digits, PlainBlock shuffle) {
  const std::size_t cycle = static_cast<std::size_t>(Cycle(digits));
  int plain = 0;
  for (std::size_t i = 0; (i + 1) * kBlockSize <= data.size(); ++i) {
    std::uint8_t* block = data.data() + i * kBlockSize;
    if (i < kFirstBlocks || i % cycle == 0) {
      DecodeBlock(std::span<std::uint8_t, kBlockSize>(block, kBlockSize));
    } else if (plain == 7) {
      shuffle(block);
      plain = 1;
    } else {
      ++plain;
    }
  }
}

}  // namespace

void DecodeBlock(std::span<std::uint8_t, kBlockSize> block) {
  std::uint8_t b[8];
  Permute(block.data(), b, kInitialPermutation);

  std::uint8_t groups[8] = {};
  for (int i = 0; i < 48; ++i) {
    if (Bit(b, kExpansion[static_cast<std::size_t>(i)] - 1 + 32)) {
      groups[i / 6] = static_cast<std::uint8_t>(groups[i / 6] | (0x80 >> (i % 6)));
    }
  }
  std::uint8_t substituted[4] = {};
  for (std::size_t i = 0; i < 8; ++i) {
    const std::uint8_t v = kSubstitution[i * 64 + (groups[i] >> 2)];
    substituted[i / 2] = static_cast<std::uint8_t>(substituted[i / 2] | (i % 2 == 0 ? v << 4 : v));
  }
  std::uint8_t f[4];
  Permute(substituted, f, kPermutation);
  for (std::size_t i = 0; i < 4; ++i) b[i] ^= f[i];

  Permute(b, block.data(), kFinalPermutation);
}

void SwapNibbles(std::span<std::uint8_t> data) {
  for (std::uint8_t& c : data) c = static_cast<std::uint8_t>((c >> 4) | (c << 4));
}

void DecodeMixed(std::span<std::uint8_t> data, int digits) { Mixed(data, digits, Shuffle); }

void EncodeMixed(std::span<std::uint8_t> data, int digits) { Mixed(data, digits, Unshuffle); }

void DecodeFirstBlocks(std::span<std::uint8_t> data) {
  for (std::size_t i = 0; i < kFirstBlocks && (i + 1) * kBlockSize <= data.size(); ++i) {
    DecodeBlock(std::span<std::uint8_t, kBlockSize>(data.data() + i * kBlockSize, kBlockSize));
  }
}

int DecimalDigits(std::int32_t value) { return static_cast<int>(std::to_string(value).size()); }

bool HasFirstBlocksExtension(std::string_view name) {
  const std::size_t dot = name.rfind('.');
  if (dot == std::string_view::npos) return false;
  const std::string_view ext = name.substr(dot);
  if (ext.size() != 4) return false;
  for (std::string_view listed : {".gnd", ".gat", ".act", ".str"}) {  // 006125dc
    bool same = true;
    for (std::size_t i = 0; i < 4; ++i) {
      char c = ext[i];
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      if (c != listed[i]) same = false;
    }
    if (same) return true;
  }
  return false;
}

}  // namespace ragexe::res::grf
