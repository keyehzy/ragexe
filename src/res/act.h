#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/x87.h"
#include "res/resource.h"

namespace ragexe::res {

using core::FpuPrecision;

// An item of a frame: one sprite image placed, as CActRes keeps it (docs/formats/act.md).
struct ActItem {
  std::int32_t x = 0;  // the image's top-left corner once loaded
  std::int32_t y = 0;
  std::int32_t image = -1;  // index in the sprite's images of image_type, -1 for none
  std::uint32_t flags = 0;  // bit 0: mirrored horizontally
  std::array<std::uint8_t, 4> color = {0xff, 0xff, 0xff, 0xff};  // R, G, B, A
  float scale_x = 1.0f;
  float scale_y = 1.0f;
  std::int32_t angle = 0;
  std::int32_t image_type = 0;  // 0: 8-bit images, 1: 32-bit images
};

struct ActAnchor {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t kind = 0;
};

struct ActFrame {
  std::array<std::int32_t, 8> header{};  // copied from the file; no consumer found yet
  std::vector<ActItem> items;
  std::int32_t event = -1;
  std::vector<ActAnchor> anchors;
};

struct Act {
  std::uint16_t version = 0;
  std::vector<std::vector<ActFrame>> actions;
  std::vector<std::string> events;
  std::vector<float> delays;  // one per action, 4.0 unless the file gives them
  std::int32_t max_items = 0;
};

// What the item adjustment needs of an image (a sprite image record).
struct ActImageSize {
  std::int32_t width = 0;
  std::int32_t height = 0;
  bool halved_x = false;
  bool halved_y = false;
};

// The size of image `index` of type `image_type` in the matching sprite; nullopt when the
// sprite or the image is missing.
using ActImageLookup =
    std::function<std::optional<ActImageSize>(std::int32_t image_type, std::int32_t index)>;

// CActRes::Load (00420f60) on a whole .act file. Versions 0x200 to 0x204 look their
// images up with lookup. The items are adjusted at the x87's precision: the client's main
// thread, which loads sprites and actions, runs at FPUSETUP's single.
bool DecodeAct(std::span<const std::uint8_t> data, const ActImageLookup& lookup, Act& out,
               std::string* error, FpuPrecision precision = FpuPrecision::kDouble);

// CActRes::AdjustItem (00420e70): from the stored centre to the top-left corner.
void AdjustActItem(ActItem& item, const ActImageSize& size,
                   FpuPrecision precision = FpuPrecision::kDouble);

// CActRes. Its matching sprite comes from the manager it was registered with.
class ActRes : public Resource {
 public:
  explicit ActRes(ResourceManager* manager, FpuPrecision precision = FpuPrecision::kDouble)
      : manager_(manager), precision_(precision) {}

  std::unique_ptr<Resource> Clone() const override;
  bool Load(const DataFiles& files, std::string_view path) override;
  void Reset() override;

  const Act& act() const { return act_; }
  const std::string& error() const override { return error_; }

 private:
  ResourceManager* manager_;
  FpuPrecision precision_;
  Act act_;
  std::string error_;
};

// The original's registration (static initializer 004207a0), its actions loaded at the
// precision given.
void RegisterActTypes(ResourceManager& manager, FpuPrecision precision = FpuPrecision::kDouble);

}  // namespace ragexe::res
