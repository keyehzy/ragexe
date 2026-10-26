#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The encoding of GRF names and entries (docs/formats/grf.md, "The cipher"): one DES
// round with an all-zero key, applied to some of the 8-byte blocks. Every function works
// in place on whole blocks; a trailing partial block is left alone, so callers pad the
// data to a multiple of 8 with the bytes that follow it.
namespace ragexe::res::grf {

inline constexpr std::size_t kBlockSize = 8;

// The block function of DesCipher (004dc96b); it is its own inverse.
void DecodeBlock(std::span<std::uint8_t, kBlockSize> block);

// SwapNibbles (004dd790) on every byte.
void SwapNibbles(std::span<std::uint8_t> data);

// DecryptBlocksMixed (004dc820): the block function on the first 20 blocks and every
// cycle-th one, a byte shuffle on every 7th of the others.
void DecodeMixed(std::span<std::uint8_t> data, int digits);

// The inverse of DecodeMixed, for writing archives.
void EncodeMixed(std::span<std::uint8_t> data, int digits);

// DecryptFirstBlocks (004dcc40): the block function on the first 20 blocks; its own inverse.
void DecodeFirstBlocks(std::span<std::uint8_t> data);

// The number of characters of value in decimal, as _itoa writes it (at least 1); the
// client derives the mixed cycle from the compressed size this way (004de928).
int DecimalDigits(std::int32_t value);

// Entries whose names end in one of these get only their first blocks encoded (004dec80).
bool HasFirstBlocksExtension(std::string_view name);

}  // namespace ragexe::res::grf
