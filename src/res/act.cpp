#include "res/act.h"

#include <cmath>
#include <cstring>

#include "res/data_file.h"
#include "res/sprite.h"

namespace ragexe::res {

namespace {

constexpr std::uint32_t kMaxItems = 1000;  // ActFrame::SetItemCount (00420400)

bool Fail(std::string* error, std::string message) {
  if (error != nullptr) *error = std::move(message);
  return false;
}

class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

  bool Bytes(std::size_t n, std::span<const std::uint8_t>& out) {
    if (data_.size() - pos_ < n) return false;
    out = data_.subspan(pos_, n);
    pos_ += n;
    return true;
  }
  bool U32(std::uint32_t& v) {
    std::span<const std::uint8_t> b;
    if (!Bytes(4, b)) return false;
    v = std::uint32_t{b[0]} | std::uint32_t{b[1]} << 8 | std::uint32_t{b[2]} << 16 |
        std::uint32_t{b[3]} << 24;
    return true;
  }
  bool I32(std::int32_t& v) {
    std::uint32_t u = 0;
    if (!U32(u)) return false;
    v = static_cast<std::int32_t>(u);
    return true;
  }
  bool F32(float& v) {
    std::uint32_t u = 0;
    if (!U32(u)) return false;
    std::memcpy(&v, &u, 4);
    return true;
  }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t pos_ = 0;
};

// c(v) = v / 2 + v % 2, with C's signed division (00420eb9).
std::int32_t HalfRoundedAway(std::int32_t v) { return v / 2 + v % 2; }

// __ftol (005c2084): a 64-bit fistp with truncation, of which it returns the low dword.
// NaN, infinities and values past 64 bits give the integer indefinite 0x8000000000000000,
// so 0; values past 32 bits wrap.
std::int32_t Truncate(double v) {
  if (std::isnan(v) || v <= -9223372036854775808.0 || v >= 9223372036854775808.0) return 0;
  const auto whole = static_cast<std::int64_t>(std::trunc(v));
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(whole)));
}

bool ReadItem(Reader& r, std::uint16_t version, ActItem& item, ActImageSize& stored_size) {
  if (version < 0x200) {
    return r.I32(item.x) && r.I32(item.y) && r.I32(item.image) && r.U32(item.flags);
  }
  std::span<const std::uint8_t> color;
  if (!r.I32(item.x) || !r.I32(item.y) || !r.I32(item.image) || !r.U32(item.flags) ||
      !r.Bytes(4, color)) {
    return false;
  }
  std::memcpy(item.color.data(), color.data(), 4);
  if (version < 0x204) {
    // One scale for both directions.
    if (!r.F32(item.scale_x)) return false;
    item.scale_y = item.scale_x;
  } else if (!r.F32(item.scale_x) || !r.F32(item.scale_y)) {
    return false;
  }
  if (!r.I32(item.angle) || !r.I32(item.image_type)) return false;
  if (version >= 0x205) return r.I32(stored_size.width) && r.I32(stored_size.height);
  return true;
}

}  // namespace

void AdjustActItem(ActItem& item, const ActImageSize& size) {
  const std::int32_t w = size.halved_x ? size.width * 2 : size.width;
  const std::int32_t h = size.halved_y ? size.height * 2 : size.height;
  const double sx = item.scale_x;
  const double sy = item.scale_y;
  item.x = Truncate((item.x - HalfRoundedAway(w) * sx) / sx);
  item.y = Truncate((item.y - HalfRoundedAway(h) * sy) / sy);
}

bool DecodeAct(std::span<const std::uint8_t> data, const ActImageLookup& lookup, Act& out,
               std::string* error) {
  out = Act{};
  Reader r(data);
  std::span<const std::uint8_t> header;
  if (!r.Bytes(16, header) || header[0] != 'A' || header[1] != 'C') {
    return Fail(error, "Illegal file format");
  }
  out.version = static_cast<std::uint16_t>(header[2] | header[3] << 8);
  const auto action_count = static_cast<std::uint16_t>(header[4] | header[5] << 8);
  if (out.version >= 0x206) return Fail(error, "Unsupported Version. Content may be corrupted");

  out.actions.resize(action_count);
  out.delays.assign(action_count, 4.0f);
  for (auto& frames : out.actions) {
    std::uint32_t frame_count = 0;
    if (!r.U32(frame_count)) return Fail(error, "the file is truncated");
    // Every frame takes at least 36 bytes, which bounds a corrupt count.
    if (frame_count > data.size() / 36) return Fail(error, "the file is truncated");
    frames.resize(frame_count);
    for (ActFrame& frame : frames) {
      std::span<const std::uint8_t> head;
      std::uint32_t item_count = 0;
      if (!r.Bytes(32, head) || !r.U32(item_count)) return Fail(error, "the file is truncated");
      std::memcpy(frame.header.data(), head.data(), 32);
      if (item_count > kMaxItems) return Fail(error, "a frame has more than 1000 items");
      out.max_items = std::max(out.max_items, static_cast<std::int32_t>(item_count));
      frame.items.resize(item_count);
      for (ActItem& item : frame.items) {
        ActImageSize stored{};
        if (!ReadItem(r, out.version, item, stored)) return Fail(error, "the file is truncated");
        if (out.version < 0x200 || item.image == -1) continue;
        if (out.version >= 0x205) {
          AdjustActItem(item, stored);
          continue;
        }
        const auto size = lookup ? lookup(item.image_type, item.image) : std::nullopt;
        if (!size) return Fail(error, "Error While Load Act");
        AdjustActItem(item, *size);
      }
      if (out.version >= 0x200) {
        if (!r.I32(frame.event)) return Fail(error, "the file is truncated");
        if (out.version == 0x200) frame.event = -1;
      }
      if (out.version >= 0x203) {
        std::uint32_t anchor_count = 0;
        if (!r.U32(anchor_count) || anchor_count > data.size() / 16) {
          return Fail(error, "the file is truncated");
        }
        frame.anchors.resize(anchor_count);
        for (ActAnchor& anchor : frame.anchors) {
          // The first dword is dropped: 00421689 reads the 16 bytes to ebp-0xd0 and
          // 0042169d keeps the three after it.
          std::uint32_t unused = 0;
          if (!r.U32(unused) || !r.I32(anchor.x) || !r.I32(anchor.y) || !r.I32(anchor.kind)) {
            return Fail(error, "the file is truncated");
          }
        }
      }
    }
  }
  if (out.version >= 0x201) {
    std::uint32_t event_count = 0;
    if (!r.U32(event_count) || event_count > data.size() / 40) {
      return Fail(error, "the file is truncated");
    }
    for (std::uint32_t i = 0; i < event_count; ++i) {
      std::span<const std::uint8_t> name;
      if (!r.Bytes(40, name)) return Fail(error, "the file is truncated");
      const auto* begin = reinterpret_cast<const char*>(name.data());
      out.events.emplace_back(begin, strnlen(begin, 40));
    }
  }
  if (out.version >= 0x202) {
    for (float& delay : out.delays) {
      if (!r.F32(delay)) return Fail(error, "the file is truncated");
    }
  }
  return true;
}

std::unique_ptr<Resource> ActRes::Clone() const { return std::make_unique<ActRes>(manager_); }

bool ActRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  if (!file.Open(files, path)) {
    error_ = "Cannot find File : " + std::string(path);
    return false;
  }
  // The matching sprite: the path without its last 4 characters, then ".spr".
  const std::string sprite_name =
      std::string(path.substr(0, path.size() >= 4 ? path.size() - 4 : 0)) + ".spr";
  const SprRes* sprite = nullptr;
  bool fetched = false;
  const ActImageLookup lookup = [&](std::int32_t type,
                                    std::int32_t index) -> std::optional<ActImageSize> {
    if (!fetched) {
      fetched = true;
      sprite =
          manager_ != nullptr ? dynamic_cast<const SprRes*>(manager_->Get(sprite_name)) : nullptr;
    }
    if (sprite == nullptr || index < 0) return std::nullopt;
    const auto& s = sprite->sprite();
    const auto i = static_cast<std::size_t>(index);
    if (type == 0 && i < s.images.size()) {
      const auto& image = s.images[i];
      return ActImageSize{image.width, image.height, image.halved_x, image.halved_y};
    }
    if (type == 1 && i < s.images32.size()) {
      const auto& image = s.images32[i];
      return ActImageSize{image.width, image.height, image.halved_x, image.halved_y};
    }
    return std::nullopt;
  };
  error_.clear();
  return DecodeAct(file.data(), lookup, act_, &error_);
}

void ActRes::Reset() { act_ = Act{}; }

void RegisterActTypes(ResourceManager& manager) {
  manager.RegisterType("act", "sprite\\", std::make_unique<ActRes>(&manager));
}

}  // namespace ragexe::res
