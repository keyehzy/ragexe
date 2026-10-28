#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "render/camera.h"
#include "render/ground.h"
#include "render/matrix.h"
#include "res/act.h"
#include "res/sprite.h"

namespace ragexe::render {

// The globals the actors' sprites read, which SetSize and CView::Step leave
// (docs/render/actor.md).
struct SpriteScales {
  float size = 0.0f;        // 0061470c, the screen's height times 0.0017447917
  float slope_down = 0.0f;  // 0060564c, 0.16 tan(pitch) / the height ratio
  float slope_up = 0.0f;    // 00605650, 0.16 cot(pitch) / the height ratio
  float layer_step = 0.0f;  // 00614704, each layer's depth step
  float depth_bias = 0.0f;  // 00614708, the body's depth bias
};

// What SceneRenderer::SetSize (0040db70) sets of them: the size, and both slopes as
// 0.16 / the height ratio.
void SetSpriteScalesForSize(const Projection& projection, SpriteScales& scales);

// What CView::Step (004cf7d0) sets of them each frame from the camera's pitch and distance
// once it has moved: both slopes from the whole degrees of the pitch, and the layer step
// and depth bias from 300 / distance.
void StepSpriteScales(float pitch, float distance, const Projection& projection,
                      SpriteScales& scales);

// The CRenderObject fields the drawing and the animation read.
struct RenderObject {
  Vec3 position{};                       // +4
  std::int32_t base_action = 0;          // +0x20, the action facing the camera
  std::int32_t action = 0;               // +0x24, the action with its direction
  std::int32_t frame = 0;                // +0x28
  float facing = 0.0f;                   // +0x38, degrees
  float scale = 1.0f;                    // +0x3c
  float shadow_scale = 1.0f;             // +0x40
  std::int32_t offset_x = 0;             // +0x5c, in 640-wide pixels
  std::int32_t angle = 0;                // +0x60, added to each item's
  std::int32_t depth_index = 0;          // +0x64, which layers' depth steps it takes
  float delay = 1.0f;                    // +0x44, ticks of 24 ms per frame
  float repeats = 1.0f;                  // +0x4c, how many times a play-once action runs
  std::int32_t play_mode = 0;            // +0x54: 0 loops, 1 plays once, others hold
  std::uint32_t key_color = 0xffff0000;  // +0x68
  std::uint32_t start_time = 0;          // +0x70, timeGetTime when the action began
  std::uint32_t color = 0xffffffff;      // +0x7c, A << 24 | R << 16 | G << 8 | B
  bool depth_up = false;                 // +0x80, depth from the other slope
  bool finished = false;                 // +0x81, a play-once action has ended
  bool held = false;                     // +0x82
  bool keyed = false;                    // +0x83, drawn in red while key_color is set
  bool flat = false;                     // +0x86, every layer at one depth
  bool shadow = false;                   // +0x88
};

// CGameObject::vf04 (005000d0): which of eight directions a facing shows from the camera,
// the turn from it to the camera's yaw in [0, 360) over 45°.
std::int32_t ActorDirection(float facing, float camera_yaw);

// CRenderObject::vf08 (00500830) at time now (ms): the action becomes the base action
// plus the direction, and the frame follows the time since start over the delay, looping
// or, playing once, stopping at the last frame and finishing after the repeats. A
// finished or held action only keeps its frame within the action.
void UpdateActorFrame(RenderObject& object, const res::Act& act, float camera_yaw,
                      std::uint32_t now);

// A sprite quad as the drawing hands it to SceneRenderer 0040e980: screen edges, the rhw
// of the bottom and top vertices, the colour, the angle in degrees and the image.
struct SpriteQuad {
  float top = 0.0f, left = 0.0f, bottom = 0.0f, right = 0.0f;
  float rhw_bottom = 0.0f, rhw_top = 0.0f;
  std::uint32_t color = 0;
  float angle = 0.0f;
  std::int32_t image_type = 0;  // 0: the sprite's 8-bit images, 1: its 32-bit images
  std::int32_t image = 0;
  std::uint32_t flags = 0;  // for AddPrimitive: 0x101 when opaque, 1 otherwise
};

struct ActorBody {
  std::vector<SpriteQuad> quads;
  float pixel_scale = 0.0f;  // +0x48: the sprite's pixel at its depth, over SpriteScales::size
};

// CRenderObject::method_00500f70: the object's current frame, each item of it with an
// image placed at the object's projected position, scaled by its depth, tinted by the
// object's colour and the ground's (00500e60, red, green and blue), and kept when it
// reaches the screen.
ActorBody DrawActorBody(const RenderObject& object, const res::Act& act, const res::Sprite& sprite,
                        const Matrix& view, const Projection& projection,
                        const SpriteScales& scales, const std::array<float, 3>& ground_color);

// CRenderObject::method_00501550: the first image of shadow.spr placed as the first item of
// shadow.act's first frame, under the object on the ground (ground_height, from the
// attribute map), scaled by scale times the object's shadow scale; nothing when the
// object has no shadow, either scale is 0 or it misses the screen.
std::optional<SpriteQuad> DrawActorShadow(const RenderObject& object, float ground_height,
                                          float scale, const res::Act& shadow_act,
                                          const res::Sprite& shadow_sprite, const Matrix& view,
                                          const Projection& projection, const SpriteScales& scales);

// Where an image lies in its texture: the 32-bit images' own extent from 0, or the
// sprite texture cache's slot (0040e540, 0040e130).
struct SpriteTextureRect {
  float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

// SceneRenderer 0040e980: the quad's four vertices, top left, top right, bottom left and
// bottom right, half a pixel in, turned about its centre by the whole degrees of a
// non-zero angle, with the depth each rhw gives between near 10 and far 1500.
std::array<ScreenVertex, 4> SpriteQuadVertices(const SpriteQuad& quad,
                                               const SpriteTextureRect& rect);

}  // namespace ragexe::render
