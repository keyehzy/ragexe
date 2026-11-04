#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <list>
#include <optional>
#include <vector>

#include "render/actor.h"

namespace ragexe::render {

// Where the sprite texture cache holds an 8-bit image: the page's texture and the image's
// corners on it, over 256.
struct CachedSprite {
  std::uint32_t texture = 0;
  SpriteTextureRect rect;
};

// What CacheSpriteImage did to hold an image: the page it made for it (0 for none), which the
// original clears (CTexture::FillColor, vf02, with 0), whether that page's texture was made
// rather than a spare taken, and the slot it drew the image into (CTexture::DrawRleImage,
// 00416d80): its corner and the slot's size, zeroed past the image.
struct SpriteSlot {
  CachedSprite sprite;
  std::uint32_t page_made = 0;
  bool texture_made = false;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

// SceneRenderer's sprite texture cache (docs/render/sprite_cache.md): 16 lists of 256 × 256
// pages (+0x194), one for each size class of image, each page cut into slots of its class's
// size (006055c8) that hold an image through a palette.
class SpriteTextureCache {
 public:
  // An image or a palette, which the original tells apart by its address.
  using Key = std::uintptr_t;

  // The time a page or slot lasts unused, 15 s (00612710).
  static constexpr std::uint32_t kExpiry = 15000;
  // The spare page textures SceneRenderer makes.
  static constexpr int kSpareTextures = 50;

  // SceneRenderer 0040deb0, from its constructor: kSpareTextures textures from `make_texture`
  // (CTexture(256, 256, 0)) queued at +0x258, each referenced (IncrementUseCount, +0x120).
  void MakeSpareTextures(const std::function<std::uint32_t()>& make_texture);

  // FindCachedSprite (0040e540): the first slot of the image's size class holding the image
  // with the palette, its slot and page stamped with the time. None when no slot holds it.
  std::optional<CachedSprite> Find(Key image, Key palette, std::int16_t width, std::int16_t height,
                                   std::uint32_t now);

  // CacheSpriteImage (0040e130): the image into the first slot of its size class unused since
  // a quarter of kExpiry before now (timeGetTime, unsigned), whatever it held. Without one, a
  // page goes at the end of the class's list, cut into slots row by row, each at its corner
  // over 256 and its time 0; then the first slot again. Its texture is the last spare queued,
  // taken off the queue, or with none a new one from `make_texture` (CTexture(256, 256, 0)).
  // The image's far corner is its width and height past the slot's, read as unsigned, over
  // 256; the slot and page are stamped with the time. None where the original never returns:
  // at a time of exactly a quarter of kExpiry no slot is older than 0, and it makes pages
  // without end.
  std::optional<SpriteSlot> Cache(Key image, Key palette, std::int16_t width, std::int16_t height,
                                  std::uint32_t now,
                                  const std::function<std::uint32_t()>& make_texture);

  // ReleaseExpiredPooledTextures (0040e030): each page unused since kExpiry before now
  // (unsigned) let go, class by class and in order. A spare's texture, still referenced, goes
  // back at the end of the queue; any other is deleted: those, in order.
  std::vector<std::uint32_t> ReleaseExpired(std::uint32_t now);

  // CGameMode::ReleaseSpritePagesPeriodically (0050d3d0), each frame: ReleaseExpired once the
  // time is past the next release (g_nextSpritePageRelease, 00685ee0, 0 at the start), which
  // becomes kExpiry from now.
  std::vector<std::uint32_t> ReleaseIfDue(std::uint32_t now);
  void set_next_release(std::uint32_t time) { next_release_ = time; }
  [[nodiscard]] std::uint32_t next_release() const { return next_release_; }

 private:
  struct Slot {              // 0x24 bytes
    Key image = 0;           // +0
    Key palette = 0;         // +4
    std::uint32_t time = 0;  // +8
    SpriteTextureRect rect;  // +0xc
    std::int32_t x = 0;      // +0x1c
    std::int32_t y = 0;      // +0x20
  };
  struct Page {                 // 0x2c bytes
    std::uint32_t texture = 0;  // +0
    bool spare = false;         // the texture's +0x120 not 0
    std::int32_t width = 0;     // +0xc, the slots'
    std::int32_t height = 0;    // +0x10
    std::vector<Slot> slots;    // +0x14
    std::uint32_t time = 0;     // +0x28
  };

  std::array<std::list<Page>, 16> pages_;
  std::vector<std::uint32_t> spares_;  // +0x254 (+0x258, +0x25c)
  std::uint32_t next_release_ = 0;
};

}  // namespace ragexe::render
