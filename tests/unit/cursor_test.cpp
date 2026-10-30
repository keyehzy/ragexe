#include "render/cursor.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

using namespace ragexe;
using namespace ragexe::render;

namespace {

struct ItemVector {
  int x, y, image;
  std::uint32_t flags;
  std::uint8_t rgba[4];
  std::uint32_t scale_x, scale_y;
  int angle, type;
};

struct Image8Vector {
  int width, height, halved_x, halved_y, texture;
  std::uint32_t uv[4];
};

struct Image32Vector {
  int width, height, halved_x, halved_y, texture;
};

struct FrameVector {
  int action;
  std::size_t first, count;  // the frame's run of items
};

struct ActVector {
  int action_count;
  const ItemVector* items;
  const FrameVector* frames;
  std::size_t frame_count;
  const Image8Vector* images8;
  std::size_t image8_count;
  const Image32Vector* images32;
  std::size_t image32_count;
};

struct CallVector {
  SceneCall call;
  std::uint32_t vertices[32];  // a draw's four, x to v each
};

struct DrawVector {
  int x, y, action, frame;
  std::uint32_t scale, angle, color;
  int z_test, point;
};

struct ActFrameVector {
  const ActVector* act;
  DrawVector draw;
  int device_point;
  std::uint32_t bound;
  const CallVector* calls;
  std::size_t call_count;
  std::uint32_t bound_after;
};

struct CursorVector {
  int action, frame_before;
  std::uint32_t start, now, offset_x, offset_y;
  int mouse_x, mouse_y, button, frame_after;
  std::uint32_t start_after;
  struct {
    int x, y;
    std::uint32_t scale, angle, color;
    int z_test, point;
  } draw;
};

#include "cursor_vectors.inc"

float Float(std::uint32_t bits) { return std::bit_cast<float>(bits); }

res::Act MakeAct(const ActVector& v) {
  res::Act act;
  act.actions.resize(static_cast<std::size_t>(v.action_count));
  for (std::size_t f = 0; f < v.frame_count; ++f) {
    const FrameVector& fv = v.frames[f];
    res::ActFrame frame;
    for (std::size_t i = fv.first; i < fv.first + fv.count; ++i) {
      const ItemVector& it = v.items[i];
      res::ActItem item;
      item.x = it.x;
      item.y = it.y;
      item.image = it.image;
      item.flags = it.flags;
      item.color = {it.rgba[0], it.rgba[1], it.rgba[2], it.rgba[3]};
      item.scale_x = Float(it.scale_x);
      item.scale_y = Float(it.scale_y);
      item.angle = it.angle;
      item.image_type = it.type;
      frame.items.push_back(item);
    }
    act.actions[static_cast<std::size_t>(fv.action)].push_back(frame);
  }
  return act;
}

res::Sprite MakeSprite(const ActVector& v) {
  res::Sprite sprite;
  for (std::size_t i = 0; i < v.image8_count; ++i) {
    const Image8Vector& im = v.images8[i];
    res::SpriteImage image;
    image.width = static_cast<std::int16_t>(im.width);
    image.height = static_cast<std::int16_t>(im.height);
    image.halved_x = im.halved_x != 0;
    image.halved_y = im.halved_y != 0;
    sprite.images.push_back(image);
  }
  for (std::size_t i = 0; i < v.image32_count; ++i) {
    const Image32Vector& im = v.images32[i];
    res::SpriteImage32 image;
    image.width = static_cast<std::int16_t>(im.width);
    image.height = static_cast<std::int16_t>(im.height);
    image.halved_x = im.halved_x != 0;
    image.halved_y = im.halved_y != 0;
    sprite.images32.push_back(image);
  }
  return sprite;
}

ActImageTextures Textures(const ActVector& v) {
  return [&v](std::int32_t type, std::int32_t image) {
    const auto i = static_cast<std::size_t>(image);
    if (type != 0) return ActImageTexture{static_cast<std::uint32_t>(v.images32[i].texture), {}};
    const Image8Vector& im = v.images8[i];
    return ActImageTexture{static_cast<std::uint32_t>(im.texture),
                           {Float(im.uv[0]), Float(im.uv[1]), Float(im.uv[2]), Float(im.uv[3])}};
  };
}

}  // namespace

TEST_CASE("Action frames on the screen make the original's calls", "[cursor]") {
  std::size_t n = 0;
  for (const ActFrameVector& v : kActFrames) {
    INFO("case " << n++);
    const res::Act act = MakeAct(*v.act);
    const res::Sprite sprite = MakeSprite(*v.act);
    ActFrameDraw draw;
    draw.x = v.draw.x;
    draw.y = v.draw.y;
    draw.action = v.draw.action;
    draw.frame = v.draw.frame;
    draw.scale = Float(v.draw.scale);
    draw.angle = Float(v.draw.angle);
    draw.color = v.draw.color;
    draw.z_test = v.draw.z_test != 0;
    draw.point_filter = v.draw.point != 0;
    BoundTextures bound{v.bound, 0};
    ScreenSprites out;
    DrawActFrame(draw, act, sprite, v.device_point != 0, Textures(*v.act), bound, out);
    REQUIRE(out.calls.size() == v.call_count);
    for (std::size_t i = 0; i < out.calls.size(); ++i) {
      INFO("call " << i);
      const SceneCall& got = out.calls[i];
      const SceneCall& want = v.calls[i].call;
      CHECK(got.kind == want.kind);
      CHECK(got.stage == want.stage);
      CHECK(got.state == want.state);
      CHECK(got.fvf == want.fvf);
      if (got.kind != SceneCall::Kind::kDraw) {
        CHECK(got.value == want.value);
        continue;
      }
      REQUIRE(got.value < out.quads.size());
      const auto& quad = out.quads[got.value];
      const std::uint32_t* vertices = v.calls[i].vertices;
      for (std::size_t k = 0; k < 4; ++k) {
        INFO("vertex " << k);
        const ScreenVertex& sv = quad[k];
        CHECK(std::bit_cast<std::uint32_t>(sv.x) == vertices[k * 8]);
        CHECK(std::bit_cast<std::uint32_t>(sv.y) == vertices[k * 8 + 1]);
        CHECK(std::bit_cast<std::uint32_t>(sv.z) == vertices[k * 8 + 2]);
        CHECK(std::bit_cast<std::uint32_t>(sv.rhw) == vertices[k * 8 + 3]);
        CHECK(sv.diffuse == vertices[k * 8 + 4]);
        CHECK(sv.specular == vertices[k * 8 + 5]);
        CHECK(std::bit_cast<std::uint32_t>(sv.u) == vertices[k * 8 + 6]);
        CHECK(std::bit_cast<std::uint32_t>(sv.v) == vertices[k * 8 + 7]);
      }
    }
    CHECK(bound.stage0 == v.bound_after);
  }
}

TEST_CASE("The cursor steps and draws as the original's", "[cursor]") {
  const res::Act act = MakeAct(kCursorAct);
  std::size_t n = 0;
  for (const CursorVector& v : kCursors) {
    INFO("case " << n++ << ": action " << v.action << ", " << v.now - v.start << " ms");
    Cursor cursor;
    cursor.action = v.action;
    cursor.frame = v.frame_before;
    cursor.start = v.start;
    cursor.offset_x = Float(v.offset_x);
    cursor.offset_y = Float(v.offset_y);
    const ActFrameDraw draw = StepCursor(cursor, act, v.now, v.button != 0, v.mouse_x, v.mouse_y);
    CHECK(cursor.frame == v.frame_after);
    CHECK(cursor.start == v.start_after);
    CHECK(draw.x == v.draw.x);
    CHECK(draw.y == v.draw.y);
    CHECK(draw.action == v.action);
    CHECK(draw.frame == v.frame_after);
    CHECK(std::bit_cast<std::uint32_t>(draw.scale) == v.draw.scale);
    CHECK(std::bit_cast<std::uint32_t>(draw.angle) == v.draw.angle);
    CHECK(draw.color == v.draw.color);
    CHECK(draw.z_test == (v.draw.z_test != 0));
    CHECK(draw.point_filter == (v.draw.point != 0));
  }
}

TEST_CASE("An item whose image is not in the sprite is skipped", "[cursor]") {
  // The original reads such an image from past its table; the port draws the others.
  res::Act act;
  res::ActFrame frame;
  res::ActItem missing;
  missing.image = 5;
  res::ActItem present;
  present.image = 0;
  frame.items = {missing, present, missing};
  act.actions = {{frame}};
  res::Sprite sprite;
  res::SpriteImage image;
  image.width = 4;
  image.height = 4;
  sprite.images.push_back(image);
  const ActImageTextures textures = [](std::int32_t, std::int32_t) {
    return ActImageTexture{7, {0.0f, 0.0f, 1.0f, 1.0f}};
  };
  BoundTextures bound;
  ScreenSprites out;
  DrawActFrame({}, act, sprite, true, textures, bound, out);
  CHECK(out.quads.size() == 1);
  CHECK(out.quads[0][3].x == 3.5f);  // width - 1 past the point, plus half a pixel
}
