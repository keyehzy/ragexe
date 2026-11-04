#include "render/sprite_cache.h"

#include <utility>

namespace ragexe::render {

namespace {

constexpr float kTexel = 0.00390625f;  // 005e2668, 1/256

// 006055c8: each class's slot width and height.
// clang-format off
constexpr std::array<std::pair<std::int32_t, std::int32_t>, 16> kSlotSizes = {{
    {32, 32}, {32, 64}, {32, 128}, {32, 256},
    {64, 32}, {64, 64}, {64, 128}, {64, 256},
    {128, 32}, {128, 64}, {128, 128}, {128, 256},
    {256, 32}, {256, 64}, {256, 128}, {256, 256},
}};
// clang-format on

// Each side up to 32, 64, 128 or more, compared as signed 16-bit: the width picks the row of
// four classes, the height the class in it.
std::size_t SideClass(std::int16_t side) {
  if (side <= 32) return 0;
  if (side <= 64) return 1;
  if (side <= 128) return 2;
  return 3;
}

std::size_t SizeClass(std::int16_t width, std::int16_t height) {
  return SideClass(width) * 4 + SideClass(height);
}

// fild of a quadword whose high half is 0: the texel read as unsigned, so a negative one lands
// past 2^24.
float OverPage(std::int32_t texel) {
  return static_cast<float>(static_cast<std::uint32_t>(texel)) * kTexel;
}

}  // namespace

std::optional<CachedSprite> SpriteTextureCache::Find(Key image, Key palette, std::int16_t width,
                                                     std::int16_t height, std::uint32_t now) {
  for (Page& page : pages_[SizeClass(width, height)]) {
    for (Slot& slot : page.slots) {
      if (slot.image != image || slot.palette != palette) continue;
      slot.time = now;
      page.time = now;
      return CachedSprite{page.texture, slot.rect};
    }
  }
  return std::nullopt;
}

void SpriteTextureCache::MakeSpareTextures(const std::function<std::uint32_t()>& make_texture) {
  for (int k = 0; k < kSpareTextures; ++k) spares_.push_back(make_texture());
}

std::optional<SpriteSlot> SpriteTextureCache::Cache(
    Key image, Key palette, std::int16_t width, std::int16_t height, std::uint32_t now,
    const std::function<std::uint32_t()>& make_texture) {
  const std::size_t size_class = SizeClass(width, height);
  std::list<Page>& pages = pages_[size_class];
  const std::uint32_t unused_before = now - (kExpiry >> 2);
  SpriteSlot out;
  for (;;) {
    for (Page& page : pages) {
      for (Slot& slot : page.slots) {
        if (slot.time >= unused_before) continue;
        slot.image = image;
        slot.palette = palette;
        slot.rect.u1 = OverPage(width + slot.x);
        slot.rect.v1 = OverPage(height + slot.y);
        slot.time = now;
        page.time = now;
        out.sprite = {page.texture, slot.rect};
        out.x = slot.x;
        out.y = slot.y;
        out.width = page.width;
        out.height = page.height;
        return out;
      }
    }
    if (unused_before == 0) return std::nullopt;
    Page& page = pages.emplace_back();
    if (spares_.empty()) {
      page.texture = make_texture();
      out.texture_made = true;
    } else {
      page.texture = spares_.back();
      page.spare = true;
      spares_.pop_back();
    }
    out.page_made = page.texture;
    page.width = kSlotSizes[size_class].first;
    page.height = kSlotSizes[size_class].second;
    const std::int32_t columns = static_cast<std::int32_t>(kPageSide) / page.width;
    const std::int32_t rows = static_cast<std::int32_t>(kPageSide) / page.height;
    page.slots.resize(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    auto slot = page.slots.begin();
    for (std::int32_t row = 0; row < rows; ++row) {
      for (std::int32_t column = 0; column < columns; ++column, ++slot) {
        slot->x = page.width * column;
        slot->y = page.height * row;
        slot->rect.u0 = OverPage(slot->x);
        slot->rect.v0 = OverPage(slot->y);
      }
    }
  }
}

std::vector<std::uint32_t> SpriteTextureCache::ReleaseExpired(std::uint32_t now) {
  const std::uint32_t unused_before = now - kExpiry;
  std::vector<std::uint32_t> released;
  for (std::list<Page>& pages : pages_) {
    for (auto it = pages.begin(); it != pages.end();) {
      if (it->time >= unused_before) {
        ++it;
        continue;
      }
      (it->spare ? spares_ : released).push_back(it->texture);
      it = pages.erase(it);
    }
  }
  return released;
}

std::vector<std::uint32_t> SpriteTextureCache::ReleaseIfDue(std::uint32_t now) {
  if (now <= next_release_) return {};
  next_release_ = now + kExpiry;
  return ReleaseExpired(now);
}

}  // namespace ragexe::render
