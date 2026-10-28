#include "render/player.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "render/actor.h"
#include "render/camera.h"
#include "render/player_names.h"
#include "res/act.h"
#include "res/imf.h"
#include "res/sprite.h"

using namespace ragexe::render;

namespace {

struct BodyNameVector {
  int job, sex;
  const char* act;
  const char* spr;
  const char* imf;
};

struct HeadNameVector {
  int hair, sex;
  const char* act;
  const char* spr;
  int hair_after;
};

struct HeadgearVector {
  int sex, headgear;
  const char* act;
  const char* spr;
};

struct WeaponVector {
  int job, sex, kind;
  const char* act;
  const char* spr;
  const char* glow_act;
  const char* glow_spr;
};

struct ShieldVector {
  int job, sex, shield;
  const char* act;
  const char* spr;
};

struct PaletteVector {
  int hair, job, sex, palette;
  const char* head;
  const char* body;
};

struct ItemVector {
  int x, y, image, flags, r, g, b, a;
  std::uint32_t scale_x, scale_y;
  int angle, type;
};

struct FrameVector {
  int action, frame;
  const ItemVector* items;
  std::size_t count;
  const int* anchors;  // x, y, kind each
  std::size_t anchor_count;
};

struct LayerVector {
  const FrameVector* frames;
  std::size_t count;
};

struct QuadVector {
  std::uint32_t values[8];  // top, left, bottom, right, rhw bottom, rhw top, colour, angle
  int image_type, image, layer;
  std::uint32_t flags;
};

struct ViewVector {
  std::uint32_t view[12];
  std::uint32_t scales[5];
};

struct PlayerVector {
  int set, view;
  std::uint32_t position[3];
  int base, direction, frame;
  std::uint32_t scale;
  int offset_x, angle, depth_index;
  std::uint32_t key_color, color;
  int flags[3];  // depth up, keyed, flat
  std::uint32_t ground[3];
  int head_before[2];
  const QuadVector* quads;
  std::size_t count;
  int head_after[2];
};

#include "player_vectors.inc"

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }

ragexe::res::Imf TestImf() {
  ragexe::res::Imf imf;
  for (const int* layer : {kImfLayer0, kImfLayer1}) {
    std::vector<std::vector<ragexe::res::ImfFrame>> actions;
    const int* p = layer;
    for (std::size_t a = 0; a < ragexe::res::kImfMaxActions; ++a) {
      const int n = *p++;
      std::vector<ragexe::res::ImfFrame> frames(static_cast<std::size_t>(n));
      for (ragexe::res::ImfFrame& frame : frames) frame.order = *p++;
      actions.push_back(frames);
    }
    imf.layers.push_back(actions);
  }
  return imf;
}

ragexe::res::Sprite TestSprite() {
  ragexe::res::Sprite s;
  for (const auto& im : kImages8) {
    ragexe::res::SpriteImage image;
    image.width = im[0];
    image.height = im[1];
    image.halved_x = im[2] != 0;
    image.halved_y = im[3] != 0;
    s.images.push_back(image);
  }
  for (const auto& im : kImages32) {
    ragexe::res::SpriteImage32 image;
    image.width = im[0];
    image.height = im[1];
    s.images32.push_back(image);
  }
  return s;
}

ragexe::res::Act LayerAct(const LayerVector& v) {
  ragexe::res::Act act;
  for (std::size_t i = 0; i < v.count; ++i) {
    const FrameVector& f = v.frames[i];
    const auto a = static_cast<std::size_t>(f.action);
    if (act.actions.size() <= a) act.actions.resize(a + 1);
    ragexe::res::ActFrame frame;
    for (std::size_t k = 0; k < f.count; ++k) {
      const ItemVector& it = f.items[k];
      ragexe::res::ActItem item;
      item.x = it.x;
      item.y = it.y;
      item.image = it.image;
      item.flags = static_cast<std::uint32_t>(it.flags);
      item.color = {static_cast<std::uint8_t>(it.r), static_cast<std::uint8_t>(it.g),
                    static_cast<std::uint8_t>(it.b), static_cast<std::uint8_t>(it.a)};
      item.scale_x = F(it.scale_x);
      item.scale_y = F(it.scale_y);
      item.angle = it.angle;
      item.image_type = it.type;
      frame.items.push_back(item);
    }
    for (std::size_t k = 0; k < f.anchor_count; ++k)
      frame.anchors.push_back({f.anchors[k * 3], f.anchors[k * 3 + 1], f.anchors[k * 3 + 2]});
    act.actions[a].push_back(frame);
  }
  return act;
}

}  // namespace

TEST_CASE("Player sprite names are the original's", "[player]") {
  for (const BodyNameVector& v : kBodyNames) {
    INFO("job " << v.job << ", sex " << v.sex);
    CHECK(PlayerBodyName(v.job, v.sex) + ".act" == v.act);
    CHECK(PlayerBodyName(v.job, v.sex) + ".spr" == v.spr);
    CHECK(PlayerImfName(v.job, v.sex) == v.imf);
  }
  for (const HeadNameVector& v : kHeadNames) {
    INFO("hair " << v.hair << ", sex " << v.sex);
    CHECK(PlayerHeadName(v.hair, v.sex) + ".act" == v.act);
    CHECK(PlayerHeadName(v.hair, v.sex) + ".spr" == v.spr);
  }
}

// A name as CPc hands it on: the extension added, or nothing for none.
std::string WithExtension(const std::string& name, const char* extension) {
  return name.empty() ? name : name + extension;
}

TEST_CASE("Players are dressed with the original's names", "[player]") {
  for (const HeadgearVector& v : kHeadgear) {
    INFO("sex " << v.sex << ", headgear " << v.headgear);
    const PlayerSpriteNames names = PlayerHeadgearNames(v.sex, v.headgear);
    CHECK(WithExtension(names.act, ".act") == v.act);
    CHECK(WithExtension(names.sprite, ".spr") == v.spr);
  }
  for (const WeaponVector& v : kWeapons) {
    INFO("job " << v.job << ", sex " << v.sex << ", kind " << v.kind);
    const auto names = PlayerWeaponNames(v.job, v.sex, v.kind);
    CHECK(WithExtension(names[0].act, ".act") == v.act);
    CHECK(WithExtension(names[0].sprite, ".spr") == v.spr);
    CHECK(WithExtension(names[1].act, ".act") == v.glow_act);
    CHECK(WithExtension(names[1].sprite, ".spr") == v.glow_spr);
  }
  for (const ShieldVector& v : kShields) {
    INFO("job " << v.job << ", sex " << v.sex << ", shield " << v.shield);
    const PlayerSpriteNames names = PlayerShieldNames(v.job, v.sex, v.shield);
    CHECK(WithExtension(names.act, ".act") == v.act);
    CHECK(WithExtension(names.sprite, ".spr") == v.spr);
  }
  for (const PaletteVector& v : kPalettes) {
    INFO("hair " << v.hair << ", job " << v.job << ", sex " << v.sex << ", palette " << v.palette);
    CHECK(PlayerHeadPaletteName(v.hair, v.job, v.sex, v.palette) == v.head);
    CHECK(PlayerBodyPaletteName(v.job, v.sex, v.palette) == v.body);
  }
}

TEST_CASE("FindImfLayer finds the original's layers", "[player]") {
  const ragexe::res::Imf imf = TestImf();
  for (const auto& v : kImfFinds) {
    INFO("value " << v[0] << ", action " << v[1] << ", frame " << v[2]);
    CHECK(FindImfLayer(imf, v[0], v[1], v[2]) == v[3]);
  }
}

TEST_CASE("Players' layers give the original's quads", "[player]") {
  const Projection projection = SetSize(800, 600);
  const ragexe::res::Imf imf = TestImf();
  const ragexe::res::Sprite sprite = TestSprite();
  std::vector<std::array<ragexe::res::Act, kPlayerLayerCount>> sets;
  for (const auto& set : kLayerSets) {
    std::array<ragexe::res::Act, kPlayerLayerCount> acts;
    for (std::size_t l = 0; l < kPlayerLayerCount; ++l)
      if (set[l].frames != nullptr) acts[l] = LayerAct(set[l]);
    sets.push_back(acts);
  }
  std::size_t n = 0;
  for (const PlayerVector& v : kPlayers) {
    INFO("player " << n++);
    const ViewVector& view = kViews[v.view];
    Matrix m{};
    for (std::size_t i = 0; i < 12; ++i) m[i] = F(view.view[i]);
    const SpriteScales scales{F(view.scales[0]), F(view.scales[1]), F(view.scales[2]),
                              F(view.scales[3]), F(view.scales[4])};
    std::array<PlayerLayer, kPlayerLayerCount> layers;
    for (std::size_t l = 0; l < kPlayerLayerCount; ++l) {
      if (kLayerSets[v.set][l].frames == nullptr) continue;
      layers[l] = {&sets[static_cast<std::size_t>(v.set)][l], &sprite};
    }
    RenderObject o;
    o.position = {F(v.position[0]), F(v.position[1]), F(v.position[2])};
    o.base_action = v.base;
    o.action = v.base + v.direction;
    o.frame = v.frame;
    o.scale = F(v.scale);
    o.offset_x = v.offset_x;
    o.angle = v.angle;
    o.depth_index = v.depth_index;
    o.key_color = v.key_color;
    o.color = v.color;
    o.depth_up = v.flags[0] != 0;
    o.keyed = v.flags[1] != 0;
    o.flat = v.flags[2] != 0;
    PlayerHeadOffset head{v.head_before[0], v.head_before[1]};
    const ActorBody body = DrawPlayer(o, layers, imf, m, projection, scales,
                                      {F(v.ground[0]), F(v.ground[1]), F(v.ground[2])}, head);
    CHECK(head.x == v.head_after[0]);
    CHECK(head.y == v.head_after[1]);
    REQUIRE(body.quads.size() == v.count);
    for (std::size_t i = 0; i < v.count; ++i) {
      INFO("quad " << i);
      const SpriteQuad& got = body.quads[i];
      const QuadVector& want = v.quads[i];
      CHECK(got.layer == want.layer);
      CHECK(B(got.top) == want.values[0]);
      CHECK(B(got.left) == want.values[1]);
      CHECK(B(got.bottom) == want.values[2]);
      CHECK(B(got.right) == want.values[3]);
      CHECK(B(got.rhw_bottom) == want.values[4]);
      CHECK(B(got.rhw_top) == want.values[5]);
      CHECK(got.color == want.values[6]);
      CHECK(B(got.angle) == want.values[7]);
      CHECK(got.image_type == want.image_type);
      CHECK(got.image == want.image);
      CHECK(got.flags == want.flags);
    }
  }
}
