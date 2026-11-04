#include "render/sprite_cache.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <vector>

using ragexe::render::CachedSprite;
using ragexe::render::SpriteSlot;
using ragexe::render::SpriteTextureCache;

namespace {

struct SpriteCacheImage {
  std::int16_t width, height;
};

struct SpriteCacheOp {
  int script, kind, image;
  std::uint32_t palette, now;
  std::uint32_t words[36];
  int count;
};

#include "sprite_cache_vectors.inc"

std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }

void Rect(const CachedSprite& s, std::vector<std::uint32_t>& out) {
  out.insert(out.end(), {B(s.rect.u0), B(s.rect.v0), B(s.rect.u1), B(s.rect.v1)});
}

void Slot(const SpriteSlot& s, std::vector<std::uint32_t>& out) {
  out.insert(out.end(), {s.page_made, s.sprite.texture, static_cast<std::uint32_t>(s.x),
                         static_cast<std::uint32_t>(s.y), static_cast<std::uint32_t>(s.width),
                         static_cast<std::uint32_t>(s.height)});
  Rect(s.sprite, out);
}

}  // namespace

TEST_CASE("The sprite texture cache is the original's", "[render]") {
  std::optional<SpriteTextureCache> cache;
  int script = -1;
  std::uint32_t pages = 0;
  const auto make_page = [&] { return ++pages; };
  for (const SpriteCacheOp& op : kSpriteCacheOps) {
    if (op.script != script) {
      script = op.script;
      cache.emplace();
      cache->set_next_release(kSpriteCacheReleases[script]);
    }
    const SpriteCacheImage& image = kSpriteCacheImages[op.image];
    const auto key = static_cast<SpriteTextureCache::Key>(op.image);
    std::vector<std::uint32_t> got;
    if (op.kind == 2) {
      const std::vector<std::uint32_t> released = cache->ReleaseIfDue(op.now);
      got.push_back(cache->next_release());
      got.insert(got.end(), released.begin(), released.end());
      got.push_back(0);
    } else {
      std::optional<CachedSprite> found;
      if (op.kind == 0) {
        found = cache->Find(key, op.palette, image.width, image.height, op.now);
        got.push_back(found ? found->texture : 0);
        if (found) Rect(*found, got);
      }
      if (!found) {
        const std::optional<SpriteSlot> slot =
            cache->Cache(key, op.palette, image.width, image.height, op.now, make_page);
        REQUIRE(slot);
        Slot(*slot, got);
      }
    }
    INFO("script " << op.script << ", kind " << op.kind << ", image " << op.image << " ("
                   << image.width << " x " << image.height << "), at " << op.now);
    REQUIRE(got == std::vector<std::uint32_t>(op.words, op.words + op.count));
  }
}

TEST_CASE("The sprite texture cache gives up where the original never returns", "[render]") {
  SpriteTextureCache cache;
  std::uint32_t pages = 0;
  const auto make_page = [&] { return ++pages; };
  constexpr std::uint32_t kHang = SpriteTextureCache::kExpiry / 4;
  CHECK_FALSE(cache.Cache(1, 1, 10, 10, kHang, make_page));
  CHECK(pages == 0);
  // A slot cached earlier is no older than 0 either.
  REQUIRE(cache.Cache(1, 1, 10, 10, kHang + 1, make_page));
  CHECK_FALSE(cache.Cache(2, 1, 10, 10, kHang, make_page));
  CHECK(pages == 1);
}
