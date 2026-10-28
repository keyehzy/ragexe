#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "core/crt_rand.h"
#include "render/camera.h"
#include "render/ground.h"
#include "render/matrix.h"

namespace ragexe::render {

// A CRagEffect as its primitives read it (docs/render/effect-prim.md).
struct RagEffect {
  std::int32_t id = 0;        // +0xac
  Vec3 position{};            // +4, the actor's when it started
  std::int32_t frame = 0;     // +0xbc
  std::int32_t duration = 0;  // +0xc0, EffectDuration
  float screen_x = 0.0f;      // +0xdc, where its processing last put the actor on screen
  float screen_y = 0.0f;      // +0xe0
};

// What AddPrimitive gets from a primitive: a screen-space triangle, flags 1 (translucent,
// sorted by its first vertex's rhw).
struct PrimTriangle {
  std::string texture;  // the primitive's texture of its current frame
  std::array<ScreenVertex, 3> vertices{};
};

// CEffectPrim (0x15990 bytes): one particle or shape an effect adds to the world, stepped
// by vf01 and drawn by vf03, both switching on the primitive id. The fields are those ids 0
// to 2 use, at their offsets; the defaults are the constructor's (0057cfa0).
struct EffectPrim {
  // +0x13c
  static constexpr std::uint32_t kAnchored = 0x1;    // the ray grows up from its base
  static constexpr std::uint32_t kStopGrowth = 0x2;  // sizes held after +0x134 frames
  static constexpr std::uint32_t kAbsolute = 0x4;    // at its offset, not the owner's point
  static constexpr std::uint32_t kWander = 0x8;      // heading jittered every +0x134 frames
  static constexpr std::uint32_t kBounce = 0x80;     // alpha turns back at its limits

  const RagEffect* owner = nullptr;   // +0xa4, cleared when the effect ends (vf02 0x18)
  std::int32_t id = 0;                // +0x130
  bool visible = true;                // +0x84, CRenderObject's
  Vec3 position{};                    // +4: x and y on screen for these ids
  std::int32_t image = 0;             // +0x28, the index into textures
  std::vector<std::string> textures;  // +0xa8, as many as image_count
  std::int32_t image_count = 1;       // +0x164
  std::int32_t image_delay = 4;       // +0x148, frames per image
  bool image_loop = true;             // +0x1b4

  std::uint32_t flags = 0;      // +0x13c
  std::int32_t age = 0;         // +0x140
  std::int32_t life = 100;      // +0x144: vf01 ends it once age passes this
  std::int32_t period = 0;      // +0x134, for kStopGrowth and kWander
  std::int32_t fade_at = -1;    // +0x150, the age it starts fading out
  std::int32_t change_at = -1;  // +0x160, the age the changes below take hold

  float speed = 0.0f;         // +0x1ec, along the heading
  float acceleration = 0.0f;  // +0x1f0
  float heading = 0.0f;       // +0x23c, degrees
  float angle = 0.0f;         // +0x244, degrees, the drawing's
  float spin = 0.0f;          // +0x268
  float spin_rate = 0.0f;     // +0x274
  float width = 0.0f;         // +0x284
  float width_speed = 0.0f;   // +0x288
  float width_rate = 0.0f;    // +0x28c
  float height = 0.0f;        // +0x290
  float height_speed = 0.0f;  // +0x294
  float height_rate = 0.0f;   // +0x298

  // Id 1 orbits at a distance.
  float heading_speed = 0.0f;   // +0x260
  float heading_rate = 0.0f;    // +0x26c
  float distance = 0.0f;        // +0x278
  float distance_speed = 0.0f;  // +0x27c
  float distance_rate = 0.0f;   // +0x280

  // Values taken at change_at unless they hold 0.0001 (0x38d1b717).
  std::array<std::uint32_t, 6> changes{0x38d1b717, 0x38d1b717, 0x38d1b717,
                                       0x38d1b717, 0x38d1b717, 0x38d1b717};
  // +0x1f8 width_speed, +0x1fc height_speed, +0x1cc speed, +0x1f4 width_rate,
  // +0x200 height_rate, +0x1d0 acceleration: the order UpdatePrim_0 applies them in.

  Vec3 start{};   // +0x2ec, the owner's position; for id 1 its point on screen
  Vec3 travel{};  // +0x2f8, the distance moved so far
  Vec3 offset{};  // +0x304, from the owner's point

  float alpha = 254.0f;           // +0x254
  float alpha_speed = 0.0f;       // +0x258
  float fade_scale = 1.0f;        // +0x25c
  float alpha_max = 254.0f;       // +0x204
  float alpha_min = 0.0f;         // +0x208
  bool fading = false;            // +0xc0, the fade to its owner's end begun
  bool speed_keys = false;        // +0xb4: at each key frame the alpha speed is set
  bool alpha_keys = false;        // +0xb8: at each key frame the alpha and its limit are set
  std::int32_t key = 0;           // +0x138
  std::vector<float> key_frames;  // +0x2a4
  std::vector<float> key_values;  // +0x2b4
  std::vector<float> key_limits;  // +0x2c4
  std::int32_t red = 0xff, green = 0xff, blue = 0xff;           // +0x19c
  std::int32_t red_speed = 0, green_speed = 0, blue_speed = 0;  // +0x1a8
  std::uint32_t color = 0xffffffff;  // +0x124, from the alpha and the three
};

// The constructor (0057cfa0) and InitPrimitive (00586380): owner, id, the start and the
// position at the owner's position, the offset added to its travel.
EffectPrim MakePrimitive(const RagEffect* owner, std::int32_t id, const Vec3& offset);

// vf01 (0057dce0): false once the owner is gone, when the id's update ends it, or once its
// age passes its life. Ids other than 0 to 2 are not ported and end at once.
bool UpdatePrimitive(EffectPrim& p, core::CrtRand& rand);

// vf03 (00586070): the triangles of a visible primitive (ids 0 to 2).
void DrawPrimitive(const EffectPrim& p, std::vector<PrimTriangle>& out);

// CRagEffect::ProcessHitSpark (00550c60), effect id 1, each world step: the actor's point
// on screen, and on the first frame the hit sound and eight rays of lens1.tga and
// lens2.tga flying out from it 45 degrees apart; blue (never asked for by vf01) tints
// them.
void ProcessHitSpark(RagEffect& effect, const Vec3& actor, const Matrix& view,
                     const Projection& projection, core::CrtRand& rand, bool blue,
                     std::vector<EffectPrim>& added);

// The world's object list as effects use it (CWorld::UpdateGameObjects, 004c4a10): each
// world step every object's vf01 in order, a primitive an effect adds joining the end and
// running the same step; an object whose vf01 answers false is deleted, and an effect's
// deletion tells its primitives.
class EffectWorld {
 public:
  explicit EffectWorld(std::uint32_t seed = 1) : rand_(seed) {}

  // CAbleToMakeEffect::LaunchEffect (00501e10) and InitEffect: effect id at the actor, its
  // duration from the table, appended to the list.
  RagEffect& Launch(std::int32_t id, const Vec3& actor);

  void Step(const Matrix& view, const Projection& projection);
  std::vector<PrimTriangle> Draw() const;

  bool empty() const { return objects_.empty(); }
  core::CrtRand& rand() { return rand_; }

  struct Object {
    std::unique_ptr<RagEffect> effect;
    Vec3 actor{};
    std::unique_ptr<EffectPrim> prim;
  };
  const std::list<Object>& objects() const { return objects_; }

 private:
  std::list<Object> objects_;
  core::CrtRand rand_;
};

}  // namespace ragexe::render
