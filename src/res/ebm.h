#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/bitmap.h"
#include "res/resource.h"

namespace ragexe::res {

// The client reads an emblem file into an 8 KB stack buffer and inflates it into
// another (docs/formats/ebm.md).
inline constexpr std::size_t kEmblemBufferSize = 0x2000;
// The side of the blank emblem, and the most either side of a decoded one keeps.
inline constexpr std::int32_t kEmblemSize = 24;

// CEmblemRes::Load (00428500) on a whole .ebm file: a zlib stream of a BMP.
// - A stream that does not inflate to under 8 KB, or a BMP that does not decode, gives
//   a white 24 × 24 emblem: that is still a success.
// - A decoded BMP keeps its pixels, but its width and height are clamped to 24, so a
//   wider one reads skewed, as in the client.
// Fails for a file larger than the client's buffer, and for a stream on which zlib 1.0.4
// runs away.
bool DecodeEmblem(std::span<const std::uint8_t> data, Bitmap& out, std::string* error);

enum class EmblemInflate {
  kInflated,
  // zlib 1.0.4 reports an error, which leaves the client's length at the buffer's size.
  kFailed,
  // A fixed-code length symbol 286 or 287: zlib 1.0.4 copies without end, and the
  // client's exception handler (004286f5) fails the load.
  kRunsAway,
};

// CPS.DLL's uncompress (zlib 1.0.4) into the client's 8 KB buffer.
EmblemInflate InflateEmblem(std::span<const std::uint8_t> data, std::vector<std::uint8_t>& out);

// CEmblemRes.
class EmblemRes : public Resource {
 public:
  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const Bitmap& bitmap() const { return bitmap_; }
  const std::string& error() const { return error_; }

 private:
  Bitmap bitmap_;
  std::string error_;
};

// The original's registration (static initializer 00428340).
void RegisterEmblemTypes(ResourceManager& manager);

}  // namespace ragexe::res
