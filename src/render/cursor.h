#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "render/actor.h"
#include "render/ground.h"
#include "render/scene.h"
#include "res/act.h"
#include "res/sprite.h"

namespace ragexe::render {

// An action frame drawn straight onto the screen, outside the scene lists, and the mouse
// cursor drawn that way (docs/render/actor.md#on-the-screen).

// What SceneRenderer::DrawActFrame (00410620) is asked to draw.
struct ActFrameDraw {
  std::int32_t x = 0;  // the frame's origin on the screen
  std::int32_t y = 0;
  std::int32_t action = 0;
  std::int32_t frame = 0;
  float scale = 1.0f;
  float angle = 0.0f;                // degrees; turned by its whole degrees unless 0 or NaN
  std::uint32_t color = 0xffffffff;  // ARGB, every vertex's
  bool z_test = false;
  bool point_filter = false;
};

// The texture an item's image is drawn with: for an 8-bit image the sprite texture cache's
// (FindCachedSprite 0040e540, else CacheSpriteImage 0040e130) and where it put the image;
// for a 32-bit image its own CTexture, whose rect is not read.
struct ActImageTexture {
  std::uint32_t texture = 0;
  SpriteTextureRect rect;
};
using ActImageTextures = std::function<ActImageTexture(std::int32_t type, std::int32_t image)>;

// The device calls an on-screen drawing makes, in order; a draw's value is its quad's index.
// Each quad is a TRIANGLESTRIP of the 0x1c4 vertex format: top left, top right, bottom
// left, bottom right.
struct ScreenSprites {
  std::vector<SceneCall> calls;
  std::vector<std::array<ScreenVertex, 4>> quads;
};

// SceneRenderer::DrawActFrame (00410620):
// - ALPHATESTENABLE when the colour, unsigned, is above 0xff000000; ALPHABLENDENABLE;
//   ZENABLE as asked; with point filtering asked for and the device not already filtering
//   by points (C3dDevice+0x1e4), stage 0's MINFILTER and MAGFILTER POINT, LINEAR again
//   after.
// - For each item of the frame (none when the action or frame is out of range): its texture
//   when it is not the bound one (bound.stage0, the renderer's +0x50), stage 0's ALPHAOP
//   MODULATE and stage 1's COLOROP DISABLE, then the quad: the image's corners from the
//   item's point, (halved + 1) × size - 1 further, times the item's scales and the scale,
//   placed half a pixel past (x, y). Mirrored items swap left and right; a turned frame
//   turns the corners about (x, y) and ignores the mirror. The vertices are at z 1e-6 and
//   rhw 0.999999 (0x3f7fffef), the colour diffuse and specular 0xff000000.
// - A 32-bit image takes the coordinates the last 8-bit image left, 0 before any.
// An item whose image is not in the sprite, which the original reads from past its image
// table, is skipped.
void DrawActFrame(const ActFrameDraw& draw, const res::Act& act, const res::Sprite& sprite,
                  bool device_point_filter, const ActImageTextures& textures, BoundTextures& bound,
                  ScreenSprites& out);

// CMode's cursor: the action of cursors.act it shows (+0x38), the frame (+0x3c), when the
// action started (+0x24, timeGetTime's), and the offset added to the mouse (+0x30, +0x34).
struct Cursor {
  std::int32_t action = 0;
  std::int32_t frame = 0;
  std::uint32_t start = 0;
  float offset_x = 0.0f;
  float offset_y = 0.0f;
};

// CMode::DrawCursor (004cb830), before it draws: the frame from the time since the start in
// 24 ms units over the action's step (20 for action 5, 10 for 7, 1 for 9, 6 for 10, 3 for 11,
// otherwise 4), and the draw at the mouse plus the offset (truncated) with the scale 1, no
// angle, colour 0xfaffffff, the z test and point filtering.
// - Action 1 plays once: the frame steps through action 1's count and stops at the last
//   once the steps pass the action's count.
// - Actions 2 and 9 play the same way while the mouse button (g_DirectInputMouse+0x28) is
//   held; released, they start again from now at frame 0.
// - Any other action loops.
// The original also passes the frame to UIWindowMgr (004b3970, message 0x10).
ActFrameDraw StepCursor(Cursor& cursor, const res::Act& act, std::uint32_t now, bool button_held,
                        std::int32_t mouse_x, std::int32_t mouse_y);

}  // namespace ragexe::render
