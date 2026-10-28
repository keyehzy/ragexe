#include "render/actor.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include "render/camera.h"
#include "res/act.h"
#include "res/gat.h"
#include "res/sprite.h"

using namespace ragexe::render;

namespace {

struct SizeScaleVector {
  int width;
  int height;
  std::uint32_t scales[3];  // size, slope down, slope up
};

struct ViewScaleVector {
  std::uint32_t camera[3];  // pitch, yaw, distance
  std::uint32_t view[12];
  std::uint32_t scales[5];
};

struct ItemVector {
  int x, y, image, flags, r, g, b, a;
  std::uint32_t scale_x, scale_y;
  int angle, type;
};

struct FrameVector {
  const ItemVector* items;
  std::size_t count;
};

struct QuadVector {
  std::uint32_t values[8];  // top, left, bottom, right, rhw bottom, rhw top, colour, angle
  int image_type;
  int image;
  std::uint32_t flags;
};

struct ObjectVector {
  int view;
  std::uint32_t position[3];
  int action, frame;
  std::uint32_t scale, shadow_scale;
  int offset_x, angle, depth_index;
  std::uint32_t key_color, color;
  int flags[4];  // depth up, keyed, flat, shadow
  std::uint32_t ground[3];
  std::uint32_t shadow_argument;
  const QuadVector* body;
  std::size_t body_count;
  int has_shadow;
  QuadVector shadow;
  std::uint32_t pixel_scale;
};

struct SpriteQuadVector {
  std::uint32_t values[8];
  int is32;
  std::uint32_t uv[4];
  std::uint32_t flags;
  std::uint32_t vertices[32];
};

struct DirectionVector {
  std::uint32_t facing, yaw;
  int direction;
};

struct FrameStepVector {
  int base;
  std::uint32_t facing, yaw, delay, repeats;
  int play_mode;
  std::uint32_t start, now;
  int finished, held, frame, count;
  int action_after, frame_after, finished_after;
};

#include "actor_vectors.inc"

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }

constexpr int kAttrWidth = 40;
constexpr int kCellSize = 5;

// The synthetic attribute map's corners (tools/actor_vectors.py) and C3dAttr::GetHeight's
// interpolation, as res::AttrHeight ports it.
ragexe::res::AttrMap TestAttr() {
  ragexe::res::AttrMap attr;
  attr.width = kAttrWidth;
  attr.height = kAttrWidth;
  attr.cell_size = kCellSize;
  for (int c = 0; c < kAttrWidth * kAttrWidth; ++c) {
    ragexe::res::AttrCell cell;
    for (int k = 0; k < 4; ++k)
      cell.heights[static_cast<std::size_t>(k)] = static_cast<float>((c * 11 + k * 5) % 23 - 15);
    attr.cells.push_back(cell);
  }
  return attr;
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

ragexe::res::ActItem ItemOf(const ItemVector& v) {
  ragexe::res::ActItem item;
  item.x = v.x;
  item.y = v.y;
  item.image = v.image;
  item.flags = static_cast<std::uint32_t>(v.flags);
  item.color = {static_cast<std::uint8_t>(v.r), static_cast<std::uint8_t>(v.g),
                static_cast<std::uint8_t>(v.b), static_cast<std::uint8_t>(v.a)};
  item.scale_x = F(v.scale_x);
  item.scale_y = F(v.scale_y);
  item.angle = v.angle;
  item.image_type = v.type;
  return item;
}

ragexe::res::Act TestAct() {
  ragexe::res::Act act;
  for (const auto& frames : kFrames) {
    std::vector<ragexe::res::ActFrame> action;
    for (const FrameVector& f : frames) {
      ragexe::res::ActFrame frame;
      for (std::size_t i = 0; i < f.count; ++i) frame.items.push_back(ItemOf(f.items[i]));
      action.push_back(frame);
    }
    act.actions.push_back(action);
  }
  return act;
}

void CheckQuad(const SpriteQuad& got, const QuadVector& want) {
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

Matrix ViewOf(const ViewScaleVector& v) {
  Matrix m{};
  for (std::size_t i = 0; i < 12; ++i) m[i] = F(v.view[i]);
  return m;
}

}  // namespace

TEST_CASE("The sprite scales follow the size and the camera as the original's", "[actor]") {
  for (const SizeScaleVector& v : kSizeScales) {
    INFO(v.width << " x " << v.height);
    SpriteScales s;
    SetSpriteScalesForSize(SetSize(v.width, v.height), s);
    CHECK(B(s.size) == v.scales[0]);
    CHECK(B(s.slope_down) == v.scales[1]);
    CHECK(B(s.slope_up) == v.scales[2]);
  }
  const Projection projection = SetSize(800, 600);
  std::size_t n = 0;
  for (const ViewScaleVector& v : kViewScales) {
    INFO("view " << n++);
    SpriteScales s;
    SetSpriteScalesForSize(projection, s);
    StepSpriteScales(F(v.camera[0]), F(v.camera[2]), projection, s);
    CHECK(B(s.size) == v.scales[0]);
    CHECK(B(s.slope_down) == v.scales[1]);
    CHECK(B(s.slope_up) == v.scales[2]);
    CHECK(B(s.layer_step) == v.scales[3]);
    CHECK(B(s.depth_bias) == v.scales[4]);
  }
}

TEST_CASE("Actors' bodies and shadows give the original's quads", "[actor]") {
  const Projection projection = SetSize(800, 600);
  const ragexe::res::Sprite sprite = TestSprite();
  const ragexe::res::Act act = TestAct();
  ragexe::res::Sprite shadow_sprite;
  shadow_sprite.images.resize(2);
  shadow_sprite.images[0].width = 28;
  shadow_sprite.images[0].height = 14;
  shadow_sprite.images[1].width = 9;
  shadow_sprite.images[1].height = 9;
  shadow_sprite.images[1].halved_x = true;
  shadow_sprite.images[1].halved_y = true;
  ragexe::res::Act shadow_act;
  ragexe::res::ActFrame shadow_frame;
  shadow_frame.items.push_back(
      ItemOf({-14, -7, 1, 0, 255, 255, 255, 160, 0x3f800000, 0x3f800000, 0, 0}));
  shadow_act.actions.push_back({shadow_frame});
  const ragexe::res::AttrMap attr = TestAttr();

  std::size_t n = 0;
  for (const ObjectVector& v : kObjects) {
    INFO("object " << n++);
    const ViewScaleVector& view = kViewScales[v.view];
    SpriteScales scales{F(view.scales[0]), F(view.scales[1]), F(view.scales[2]), F(view.scales[3]),
                        F(view.scales[4])};
    RenderObject o;
    o.position = {F(v.position[0]), F(v.position[1]), F(v.position[2])};
    o.action = v.action;
    o.frame = v.frame;
    o.scale = F(v.scale);
    o.shadow_scale = F(v.shadow_scale);
    o.offset_x = v.offset_x;
    o.angle = v.angle;
    o.depth_index = v.depth_index;
    o.key_color = v.key_color;
    o.color = v.color;
    o.depth_up = v.flags[0] != 0;
    o.keyed = v.flags[1] != 0;
    o.flat = v.flags[2] != 0;
    o.shadow = v.flags[3] != 0;
    const ActorBody body = DrawActorBody(o, act, sprite, ViewOf(view), projection, scales,
                                         {F(v.ground[0]), F(v.ground[1]), F(v.ground[2])});
    CHECK(B(body.pixel_scale) == v.pixel_scale);
    REQUIRE(body.quads.size() == v.body_count);
    for (std::size_t i = 0; i < v.body_count; ++i) {
      INFO("quad " << i);
      CheckQuad(body.quads[i], v.body[i]);
    }
    const float ground = ragexe::res::AttrHeight(attr, o.position[0], o.position[2]);
    const auto shadow = DrawActorShadow(o, ground, F(v.shadow_argument), shadow_act, shadow_sprite,
                                        ViewOf(view), projection, scales);
    REQUIRE(shadow.has_value() == (v.has_shadow != 0));
    if (shadow) CheckQuad(*shadow, v.shadow);
  }
}

TEST_CASE("Sprite quads get the original's vertices", "[actor]") {
  std::size_t n = 0;
  for (const SpriteQuadVector& v : kSpriteQuads) {
    INFO("quad " << n++);
    SpriteQuad q;
    q.top = F(v.values[0]);
    q.left = F(v.values[1]);
    q.bottom = F(v.values[2]);
    q.right = F(v.values[3]);
    q.rhw_bottom = F(v.values[4]);
    q.rhw_top = F(v.values[5]);
    q.color = v.values[6];
    q.angle = F(v.values[7]);
    const auto vertices = SpriteQuadVertices(q, {F(v.uv[0]), F(v.uv[1]), F(v.uv[2]), F(v.uv[3])});
    for (std::size_t i = 0; i < 4; ++i) {
      INFO("vertex " << i);
      const ScreenVertex& got = vertices[i];
      const std::uint32_t* want = v.vertices + i * 8;
      CHECK(B(got.x) == want[0]);
      CHECK(B(got.y) == want[1]);
      CHECK(B(got.z) == want[2]);
      CHECK(B(got.rhw) == want[3]);
      CHECK(got.diffuse == want[4]);
      CHECK(got.specular == want[5]);
      CHECK(B(got.u) == want[6]);
      CHECK(B(got.v) == want[7]);
    }
  }
}

TEST_CASE("Actors face and animate as the original's", "[actor]") {
  for (const DirectionVector& v : kDirections) {
    INFO("facing " << F(v.facing) << ", yaw " << F(v.yaw));
    CHECK(ActorDirection(F(v.facing), F(v.yaw)) == v.direction);
  }
  std::size_t n = 0;
  for (const FrameStepVector& v : kFrameSteps) {
    INFO("step " << n++);
    ragexe::res::Act act;
    act.actions.resize(static_cast<std::size_t>(v.base) + 8);
    for (int a = v.base; a < v.base + 8; ++a)
      act.actions[static_cast<std::size_t>(a)].resize(static_cast<std::size_t>(v.count));
    RenderObject o;
    o.base_action = v.base;
    o.facing = F(v.facing);
    o.delay = F(v.delay);
    o.repeats = F(v.repeats);
    o.play_mode = v.play_mode;
    o.start_time = v.start;
    o.finished = v.finished != 0;
    o.held = v.held != 0;
    o.frame = v.frame;
    UpdateActorFrame(o, act, F(v.yaw), v.now);
    CHECK(o.action == v.action_after);
    CHECK(o.frame == v.frame_after);
    CHECK(o.finished == (v.finished_after != 0));
  }
}
