#pragma once

#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/crt_rand.h"
#include "render/actor.h"
#include "render/camera.h"
#include "render/ground.h"
#include "render/matrix.h"

namespace ragexe::res {
struct Act;
struct AttrMap;
struct Sprite;
}  // namespace ragexe::res

namespace ragexe::render {

// A CRagEffect as its primitives read it (docs/render/effect-prim.md).
struct RagEffect {
  std::int32_t id = 0;        // +0xac
  Vec3 position{};            // +4, the actor's when it started
  std::int32_t frame = 0;     // +0xbc
  std::int32_t duration = 0;  // +0xc0, EffectDuration
  float screen_x = 0.0f;      // +0xdc, where its processing last put the actor on screen
  float screen_y = 0.0f;      // +0xe0
  Vec3 target{};              // +0xe8, InitEffect's last three arguments
  std::int32_t level = 0;     // +0xb8, from a message (vf02, at most 50)
};

// The actor an effect plays on (+0xa0): where it stands and which way it faces (+0x38,
// CRenderObject's, in degrees).
struct EffectActor {
  Vec3 position{};
  float facing = 0.0f;
};

// A sprite and its actions, as SetSprite and SetAct load them.
struct PrimSprite {
  const res::Act* act = nullptr;
  const res::Sprite* sprite = nullptr;
};

// What primitives read of the world: the camera's yaw and pitch (CView+0x20, +0x1c) and
// the ground for their steps, and for their drawing vf03's view, the projection and
// g_spriteSlopeDown (0060564c).
struct PrimScene {
  float view_yaw = 0.0f;
  float view_pitch = 0.0f;
  const res::AttrMap* attr = nullptr;
  Matrix view{};
  Projection projection{};
  float slope_down = 0.0f;
};

// One of the fifty 0x8c-byte records from +0x358 a primitive keeps of where it has been.
struct PrimHistory {
  Vec3 position{};
  std::array<float, 12> points{};             // +0x328
  float distance = 0.0f;                      // +0x278
  float scale = 0.0f;                         // +0x248
  float heading = 0.0f;                       // +0x23c
  Matrix matrix{};                            // +0xc4
  std::int32_t red = 0, green = 0, blue = 0;  // set by ResetHistory only
  float alpha = 0.0f;
  std::uint32_t color = 0;  // the alpha, red, green and blue packed
};

// What AddPrimitive gets from a primitive: a screen-space triangle, flags 1 (translucent,
// sorted by its first vertex's rhw).
struct PrimTriangle {
  std::string texture;  // the primitive's texture of its current frame
  std::array<ScreenVertex, 3> vertices{};
  std::uint32_t flags = 1;       // AddPrimitive's
  std::uint32_t src_blend = 0;   // +0x14, read with flag 4
  std::uint32_t dest_blend = 0;  // +0x18
  std::uint32_t mode = 0;        // +0x1c
};

// One of the four 0xb8-byte ring records at +0x156b0 that the ring ids keep.
struct PrimRing {
  bool active = false;              // +0
  std::int16_t span = 0;            // +2, the degrees of ring drawn
  float height = 0.0f;              // +4, the walls' full height
  std::int32_t age = 0;             // +8
  std::int16_t angle = 0;           // +0xc, where the ring starts
  std::int16_t alpha = 0;           // +0xe
  std::int16_t kind = 0;            // +0x10, read by the updates
  std::array<float, 21> heights{};  // +0x14, each wall's
  std::int16_t tilt = 0;            // +0x68, the walls' lean out, degrees from flat
  float radius = 0.0f;              // +0x6c
  // +0x70, a byte per wall: settled, or how it moves (0x39); the first two also a mode and
  // a height (0x36), or the ring's offset along x and z (0x20, signed).
  std::array<std::uint8_t, 21> flags{};
  Vec3 center{};       // +0x88, 0x3a: where the ring stands, from the primitive
  float orbit = 0.0f;  // +0xac, 0x3a: how far out the centre circles
};

// The same records as the facing squares read them (ids 0x1f, 0x28, 0x2b, 0x2d, 0x2f, 0x33,
// 0x37 and 0x38): a square of four corners 90 degrees apart about a centre, turned to face
// the camera. The first record's colour and shade pick every square's colour.
struct PrimSquare {
  bool active = false;          // +0
  std::int16_t turn = 0;        // +2, the square's own rotation, degrees
  float range = 0.0f;           // +4, 0x28 and 0x33: how far out the target is
  std::int32_t age = 0;         // +8
  std::int16_t heading = 0;     // +0xc, of its motion; 0x1f: the age it starts fading
  std::int16_t alpha = 0;       // +0xe
  std::int16_t view_angle = 0;  // +0x10: the first record's 360 less the camera's yaw, the
                                // second's 450 less its pitch, within 360
  // +0x14 to +0x20: 0x1f walks two angles each a degree a frame towards a goal (+0x14 to
  // +0x18 and +0x1c to +0x20) and drifts the centre by their sines.
  float walk_x = 0.0f;       // +0x14
  float speed = 0.0f;        // +0x18, along the heading; 0x1f: walk_x's goal
  float climb = 0.0f;        // +0x1c, up or down each frame; 0x1f: the second angle
  float walk_z_goal = 0.0f;  // +0x20
  float floor = 0.0f;        // +0x2c, 0x1f: it brightens with the owner above (y less)
  float mode = 0.0f;         // +0x30; 2 colours the square (200, 0, 60)
  float phase = 0.0f;        // +0x34, degrees: the size swells by a twentieth on its sine
  float size = 0.0f;         // +0x6c, the half-diagonal
  std::uint8_t image = 0;    // +0x70, the index into textures
  std::uint8_t state = 0;    // +0x71, the update's stage, or the steps to the target (0x33)
  std::uint8_t color = 0;    // +0x72
  std::uint8_t shade = 0;    // +0x73, colour 9's
  Vec3 center{};             // +0x88
  float base = 0.0f;         // +0xa4, the height it bobs about
};

// The same records as ids 0x1b, 0x1e and 0x21 read them: up to four small squares (motes)
// at +0x88, +0x94, +0xa0 and +0xac, each turned to face the camera, and the values that
// move them at +0x14. Ids 0x26, 0x2c and 0x31 keep the point they stand at in the first
// record's third (+0xa0), 0x26 with its two walking angles and its fall (+4).
struct PrimMotes {
  bool active = false;          // +0
  float speed = 0.0f;           // +4, 0x1b: how fast its motes rise
  std::int32_t age = 0;         // +8
  std::int16_t alpha = 0;       // +0xe
  std::int16_t view_angle = 0;  // +0x10, as PrimSquare's
  // +0x14: 0x1b, eight angles (+0x14, +0x1c, ...) each walking to the goal after it; 0x1e,
  // three, then the phase (+0x2c), the climb (+0x30) and the swell phase (+0x34); 0x21,
  // per mote 16 bytes: a count, the alpha, the image and the angle round the centre.
  std::array<float, 21> values{};
  float size = 0.0f;             // +0x6c, the half-width
  std::uint8_t mode = 0;         // +0x70, 0x26: 1 drifts at half the pace
  std::array<Vec3, 4> points{};  // +0x88
};

// The same records as ids 0x29, 0x2a, 0x32, 0x34, 0x35 and 0x41 read them: one shape each,
// a square, a rectangle or a beam, most of them out along a direction (+2 up from the
// ground, +0x68 round it) at a distance (+0x6c).
struct PrimPiece {
  bool active = false;          // +0
  std::int16_t pitch = 0;       // +2, degrees
  float width = 0.0f;           // +4, 0x32: the squares' half-diagonal; 0x35: the half-width
  std::int32_t age = 0;         // +8
  std::int16_t turn = 0;        // +0xc, the square's own rotation
  std::int16_t alpha = 0;       // +0xe
  std::int16_t view_angle = 0;  // +0x10, as PrimSquare's
  float growth = 0.0f;          // +0x14, the distance's change a frame; 0x35 and 0x41: an offset
  // +0x18: 0x29, the square's half-diagonal and its tilt in degrees; 0x2a, the half-width;
  // 0x41, the alphas of its ten squares, the largest's first.
  std::array<float, 10> values{};
  std::int16_t yaw = 0;          // +0x68, degrees
  float distance = 0.0f;         // +0x6c; 0x32: its speed once it bursts; 0x41: the size
  std::uint8_t color = 0;        // +0x70; 0x41: the index into textures
  std::int8_t length = 0;        // +0x71, 0x2a: the half-length in half-widths; 0x32: its stage
  std::int8_t delay = 0;         // +0x72, 0x32: when it sparkles
  std::uint8_t image = 0;        // +0x73, 0x32: the index into textures
  std::array<Vec3, 4> points{};  // +0x88: 0x29 and 0x32, the centre; 0x32, its trail after it
};

// The same records as ids 0x23, 0x3d, 0x3f, 0x42 and 0x43 read them: a box of four walls
// (0x23, 0x3d), a band between two points (0x3f, 0x42) or a bouncing trail (0x43).
struct PrimBand {
  bool active = false;       // +0; 0x42 tests it as a signed byte above 0
  std::int16_t phase = 0;    // +2, 0x3d: turns a degree a frame, the height's pulse
  float size = 0.0f;         // +4: 0x23 and 0x3d the full height; the others the band's radius
  std::int32_t age = 0;      // +8
  std::int16_t angle = 0;    // +0xc, degrees
  std::int16_t alpha = 0;    // +0xe
  std::int16_t fade_at = 0;  // +0x10, 0x3d: the age it starts fading
  // +0x14: 0x23 and 0x3d, the box's half-length and half-width, its height and (0x3d) its
  // colour; 0x3f, the radius it pulses about; 0x42, the lift and how far back it starts;
  // 0x43, each segment's way through its bounce in degrees (to 180), the newest first.
  std::array<float, 21> values{};
  std::int16_t heading = 0;               // +0x68, 0x43: the way it bounces, degrees
  float length = 0.0f;                    // +0x6c, 0x42 and 0x43: the band's, the first bounce's
  std::array<std::int8_t, 21> bounces{};  // +0x70, 0x43: each segment's bounces made
  // +0x88: 0x42, its two ends; 0x3f, its ends at the second and fourth; 0x43, where it starts.
  std::array<Vec3, 4> points{};
};

// CEffectPrim (0x15990 bytes): one particle or shape an effect adds to the world, stepped
// by vf01 and drawn by vf03, both switching on the primitive id. The fields are those the
// ported ids use, at their offsets; the defaults are the constructor's (0057cfa0).
struct EffectPrim {
  // +0x13c, whose bits mean different things to different ids.
  static constexpr std::uint32_t kAnchored = 0x1;      // 0 to 2: the ray grows up from its base
  static constexpr std::uint32_t kStopGrowth = 0x2;    // 0 to 2: sizes held after +0x134 frames
  static constexpr std::uint32_t kAbsolute = 0x4;      // 0 to 2: at its offset, not the owner's
  static constexpr std::uint32_t kWander = 0x8;        // 0 to 2 and 5: the heading jittered
  static constexpr std::uint32_t kBounce = 0x80;       // all: alpha turns back at its limits
  static constexpr std::uint32_t kAlwaysMove = 0x1;    // 5: moving before age +0x14c too
  static constexpr std::uint32_t kDrift = 0x2;         // 5: a sideways drift, changed now and then
  static constexpr std::uint32_t kWanderWide = 0x4;    // 5: new speed and heading now and then
  static constexpr std::uint32_t kFaceCamera = 0x20;   // 5: the angle follows the camera
  static constexpr std::uint32_t kEndNear = 0x100;     // 5: ends within 3 of the target
  static constexpr std::uint32_t kItemColors = 0x200;  // 5 and 6: the frame's own colours
  static constexpr std::uint32_t kStraight = 0x400;    // 5: its own step and step speed
  static constexpr std::uint32_t kNarrowing = 0x1;     // 0xd: the top closes from fade_at

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
  Vec3 travel{};  // +0x2f8, the distance moved so far; for squares, the point they head for
  Vec3 offset{};  // +0x304, from the owner's point

  float alpha = 254.0f;           // +0x254
  float alpha_speed = 0.0f;       // +0x258
  float fade_scale = 1.0f;        // +0x25c
  float alpha_max = 254.0f;       // +0x204
  float alpha_min = 0.0f;         // +0x208
  bool fading = false;            // +0xc0, the fade to its owner's end begun
  bool speed_keys = false;        // +0xb4: at each key frame the alpha speed is set
  bool alpha_keys = false;        // +0xb8: at each key frame the alpha and its limit are set
  bool distance_keys = false;     // +0xbc, id 7: at each key frame the distance's speed and
                                  // rate are set from the same two lists
  std::int32_t key = 0;           // +0x138
  std::vector<float> key_frames;  // +0x2a4
  std::vector<float> key_values;  // +0x2b4
  std::vector<float> key_limits;  // +0x2c4
  std::int32_t red = 0xff, green = 0xff, blue = 0xff;           // +0x19c
  std::int32_t red_speed = 0, green_speed = 0, blue_speed = 0;  // +0x1a8
  std::uint32_t color = 0xffffffff;  // +0x124, from the alpha and the three

  // Ids 5, 6 and 0xd move through space along their matrix (+0xc4, from the identity).
  Matrix matrix = kIdentity;
  float pitch = 0.0f;                           // +0x240, degrees
  float pitch_speed = 0.0f;                     // +0x264
  float pitch_rate = 0.0f;                      // +0x270
  float facing = 0.0f;                          // +0x38, CRenderObject's
  Vec3 step{};                                  // +0x2e0, the last frame's move
  Vec3 step_speed{};                            // +0x31c, for kStraight
  std::array<std::uint32_t, 6> step_changes{};  // +0x1d4: step and step_speed at change_at
  Vec3 drift{};                                 // +0x310, for kDrift (only x moves)
  std::int32_t move_after = -1;                 // +0x14c
  float fall_speed = 0.0f;                      // +0x21c, id 6's along y (down)
  float fall_rate = 0.0f;                       // +0x220
  // Id 8 circles an axis (+0x1c0) at the pitch's distance, by the angle (+0x244), through
  // its second matrix (+0xf4); the circle's point is the drift.
  Vec3 axis{};
  Matrix orbit = kIdentity;
  bool coin_sound = false;  // +0x168: id 6 rings EF_Coin%d on bouncing

  // Ids 5 and 6 draw a sprite's frame at each of their last positions.
  PrimSprite sprite;               // +0x98, +0x9c
  std::int32_t action = 0;         // +0x24
  std::uint32_t quad_flags = 1;    // +0x128, for AddSpriteQuad
  float scale = 1.0f;              // +0x248
  float scale_speed = 0.0f;        // +0x24c
  float scale_rate = 0.0f;         // +0x250
  std::array<float, 12> points{};  // +0x328, kept in the history
  std::int32_t history_count = 1;  // +0x154
  std::array<PrimHistory, 50> history{};

  // Id 0xd is a cylinder: a ring at the bottom and one height up.
  float bottom_radius = 0.0f;        // +0x224
  float top_radius = 0.0f;           // +0x228
  float bottom_radius_speed = 0.0f;  // +0x22c
  float top_radius_speed = 0.0f;     // +0x230
  float bottom_radius_rate = 0.0f;   // +0x234
  float top_radius_rate = 0.0f;      // +0x238
  float max_height = 100.0f;         // +0x29c
  float sweep = 36.0f;               // +0x1b8, degrees per side

  // Ids 0x1d and 0x27 draw rings of walls; the scale (+0x248) picks their colour.
  std::array<PrimRing, 4> rings{};
  // The facing squares read the same four records their own way.
  std::array<PrimSquare, 4> squares{};
  std::array<PrimMotes, 4> motes{};
  std::array<PrimPiece, 4> pieces{};
  std::array<PrimBand, 4> bands{};
};

// What vf03 hands the renderer.
struct PrimDrawing {
  std::vector<PrimTriangle> triangles;           // AddPrimitive, flags 1
  std::vector<SpriteQuad> quads;                 // AddSpriteQuad, the sprite's palette
  std::vector<const res::Sprite*> quad_sprites;  // the sprite each quad's image is of
};

// The constructor (0057cfa0) and InitPrimitive (00586380): owner, id, the start and the
// position at the owner's position, the offset added to its travel.
EffectPrim MakePrimitive(const RagEffect* owner, std::int32_t id, const Vec3& offset);

// ResetHistory (0057d4b0): every record of the history as the primitive is now.
void ResetHistory(EffectPrim& p);

// vf01 (0057dce0): false once the owner is gone, when the id's update ends it, or once its
// age passes its life. Ids not ported end at once.
bool UpdatePrimitive(EffectPrim& p, core::CrtRand& rand, const PrimScene& scene);

// vf03 (00586070): what a visible primitive draws.
void DrawPrimitive(const EffectPrim& p, const PrimScene& scene, PrimDrawing& out);

// CRagEffect::ProcessHitSpark (00550c60), effect id 1, each world step: the actor's point
// on screen, and on the first frame the hit sound and eight rays of lens1.tga and
// lens2.tga flying out from it 45 degrees apart; blue (never asked for by vf01) tints
// them.
void ProcessHitSpark(RagEffect& effect, const Vec3& actor, const Matrix& view,
                     const Projection& projection, core::CrtRand& rand, bool blue,
                     std::vector<EffectPrim>& added);

// The particle sprite effect ids 0 and 2 load (이팩트\particle1, 00618b98 and 00618b80).
inline constexpr const char* kParticleSprite = "\300\314\306\321\306\256\\particle1";

// The other sprites effects load, each a .spr and an .act of the name: 이팩트\ and particle2,
// ef_snow, sakura01, 단풍, 침투경_, 데몬스트레이션 and Waterball (00618d58, 00618f20, 00619df4,
// 00619e18, 0061a10c, 0061a154 and 006195fc).
inline constexpr std::array<const char*, 7> kEffectSprites = {
    "\300\314\306\321\306\256\\particle2",
    "\300\314\306\321\306\256\\ef_snow",
    "\300\314\306\321\306\256\\sakura01",
    "\300\314\306\321\306\256\\\264\334\307\263",
    "\300\314\306\321\306\256\\\304\247\305\365\260\346_",
    "\300\314\306\321\306\256\\\265\245\270\363\275\272\306\256\267\271\300\314\274\307",
    "\300\314\306\321\306\256\\Waterball"};

// CRagEffect::ProcessRingBurst (005505a0), effect id 0, on its first frame: a cylinder of
// ring_blue.tga rising from the actor, and unless minimal effects are on, two particles
// flying out ahead of the way the actor faces and two thrown up behind it.
void ProcessRingBurst(RagEffect& effect, const EffectActor& actor, bool minimal,
                      const PrimSprite& particle, core::CrtRand& rand,
                      std::vector<EffectPrim>& added);

// CRagEffect::ProcessHit3 (005510b0), effect id 2, on its first frame: the sound
// effect\EF_hit3.wav, two cylinders of lens2.tga and eight particles flying out ahead.
void ProcessHit3(RagEffect& effect, const EffectActor& actor, const PrimSprite& particle,
                 core::CrtRand& rand, std::vector<EffectPrim>& added);

// The ring effects (CRagEffect 00573840, 005739f0, 00568ec0 and 00569150), on their
// first frame: a primitive of four rings of walls of the texture given.
//  - AddRingWaves, ids 0xe1, 0xec to 0xee, 0x105 (slow) and 0x107: id 0x1d, rings of
//    radius 1 to 4 whose walls swell and turn, white;
//  - AddRisingRings, ids 0xe7, 0xf7, 0x103 and 0x104: id 0x27, rings that rise a second
//    apart, coloured by variant;
//  - AddFadingRings, id 0xf8: id 0x27, rings already risen, fading after 250 frames.
void AddRingWaves(RagEffect& effect, const EffectActor& actor, const char* texture,
                  std::int16_t kind, bool slow, std::vector<EffectPrim>& added);
void AddRisingRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                    std::int8_t variant, std::vector<EffectPrim>& added);
void AddFadingRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                    std::vector<EffectPrim>& added);

// The other ring effects, on their first frame (docs/render/effect-prim.md#more-rings):
//  - AddRingTrio (0056d2c0), id 200: id 0x17, three rising open rings of ring_blue;
//  - AddTurningRings (00572d90), ids 0xde and 0xfc: id 0x18, four wide turning rings;
//  - AddRandomRing (00573bb0), ids 0xef to 0xf1: id 0x22, one ring at a random angle and
//    alpha that grows and starts again;
//  - AddTwirlRing (00573f80), ids 0x118 and 0x11d: id 0x30, one open ring of random height
//    and angles that wobbles in and out;
//  - AddTeleportationRings (00575b60), id 0x130: id 0x36, four rings that rise and fall
//    along a sine, playing effect\EF_Teleportation.wav;
//  - AddTeleportationWalls (005770d0), id 0x13a: id 0x39, four rings of waving walls,
//    playing the same;
//  - AddReadyPortal (00578150), id 0x13c: id 0x3c, three low rings spreading one after the
//    other, effect\EF_ReadyPortal.wav every 14 steps;
//  - AddGlassWall (CRagEffect::UpdateGlassWall, 00579790), id 0x13b: id 0x39, three rings
//    at random angles, effect\EF_GlassWall.wav, beside its SafetyWall.str.
void AddRingTrio(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added);
void AddTurningRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::vector<EffectPrim>& added);
void AddRandomRing(RagEffect& effect, const EffectActor& actor, const char* texture,
                   std::int8_t preset, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddTwirlRing(RagEffect& effect, const EffectActor& actor, const char* texture,
                  std::int8_t preset, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddTeleportationRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                           std::int16_t lift, std::vector<EffectPrim>& added);
void AddTeleportationWalls(RagEffect& effect, const EffectActor& actor, const char* texture,
                           std::vector<EffectPrim>& added);
void AddReadyPortal(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added);
void AddGlassWall(RagEffect& effect, const EffectActor& actor, const char* texture,
                  core::CrtRand& rand, std::vector<EffectPrim>& added);

// The square effects (docs/render/effect-prim.md#facing-squares), on their first frame
// unless said:
//  - AddClouds (005686c0), ids 0xe5 (40 a step, variant 0), 0xe6 (60, 1) and 0xe9 (40, 2):
//    id 0x1f, four clouds of cloud4, cloud1 or cloud2.tga about the player, low (0, 2) or
//    high (1); like AddRingTrio it starts again at frame 99999998;
//  - AddBoomerang (00569610 and 00579af0), ids 0xf9 (shield_boomerang.bmp) and 0x109
//    (blue_ivy.bmp, ivy: its size by the level, flying off past the target): id 0x28, one
//    square out to the target and back with a trail of three, playing a sound;
//  - AddThrownSparks (00576090), ids 0x108 (5 a step) and 0x113 (20, effect\rog_back
//    stap.wav at frame 20): id 0x2b, four sparks of thunder_center.bmp from the target on;
//  - AddBobbingSquare (00574520), ids 0x116 (zz.bmp, 1), 0x117 (pocket.bmp, 3), 0x119
//    (melody_a or b, 2), 0x11a (twirl.bmp, 7), 0x11c (one of three stones, 5), 0x11e
//    (melody_b, 0), 0x120 (one of spell_01 to 08, 8), 0x121 (idun_apple.bmp, 6), 0x123
//    (melody_a, 0) and 0x125 (kiss.bmp, 4): id 0x2d, one square over the actor;
//  - AddSinkingSquares (00574890), id 0x11b (safeline.bmp, 0): id 0x2f, two squares;
//  - AddArcingSquare (00569ae0), ids 0x12a (an empty bottle, 0), 299 (an empty potion
//    bottle, 1, shaded by the level) and 0x134 (a stone, 2): id 0x33, thrown to the target;
//  - AddPulsingSquare (005698b0), id 0x135: id 0x37 of pikapika2.bmp, _heal_effect.wav;
//  - AddThunderSparks (00576310), id 0x136 (20 a step): id 0x38, four sparks of
//    thunder_center.bmp;
//  - AddHealingSparks (00576590), ids 0x14b (0) and 0x14c (1): id 0x38 every 4 frames to
//    20, a sound on the first;
//  - AddHealingWalls (005768c0), ids 0x138 (0) and 0x140 (1): id 0x39, two rings of
//    alpha_down.tga, and id 0x38 every 6 frames to 50 (25 for 1), _heal_effect.wav;
//  - AddHealingRings (00577480), ids 0x139 (ring_white, 0), 0x145 (ring_white, 1) and 0xfd
//    (ring_blue, 2): id 0x39, four rings, and below variant 2 id 0x38 every 6 frames to 50
//    and _heal_effect.wav; variant 1 adds four rings of alpha_down.tga on frame 1 and more
//    sparks every 9 frames from 2.
void AddClouds(RagEffect& effect, const Vec3& player, std::int8_t variant, core::CrtRand& rand,
               std::vector<EffectPrim>& added);
void AddBoomerang(RagEffect& effect, const EffectActor& actor, const char* texture, bool ivy,
                  std::vector<EffectPrim>& added);
void AddThrownSparks(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                     std::vector<EffectPrim>& added);
void AddBobbingSquare(RagEffect& effect, const EffectActor& actor, const char* texture,
                      std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddSinkingSquares(RagEffect& effect, const EffectActor& actor, const char* texture,
                       std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddArcingSquare(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::int8_t variant, std::vector<EffectPrim>& added);
void AddPulsingSquare(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                      std::vector<EffectPrim>& added);
void AddThunderSparks(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                      std::vector<EffectPrim>& added);
void AddHealingSparks(RagEffect& effect, const EffectActor& actor, std::int8_t variant,
                      core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddHealingWalls(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddHealingRings(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added);

// The flat square effects (docs/render/effect-prim.md#flat-squares), on their first frame:
//  - AddTwinSquares (0056d990), id 0xc9: id 0x1a, two squares of pikapika2.bmp of radius
//    15 swelling half a turn apart, scale 4; like AddRingTrio it starts again at frame
//    99999998;
//  - AddSwellingSquare (00574ae0), id 0xf2 (aaa copy.bmp): id 0x25, one square of radius
//    7 swelling from a random phase, scale 5.
void AddTwinSquares(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added);
void AddSwellingSquare(RagEffect& effect, const EffectActor& actor, const char* texture,
                       core::CrtRand& rand, std::vector<EffectPrim>& added);

// The mote effects (docs/render/effect-prim.md#motes), on their first frame:
//  - AddRisingMotes (0056db60), id 0xca: id 0x1b, four records of four motes of
//    freezing_circle.bmp rising from up to 99 below; starts again at frame 99999998;
//  - AddPulsingMotes (0057a6c0), id 0xe4: as many primitives of id 0x1e as the target's x
//    rounds up to (none unless above 0), each a mote of thunder_center.bmp circling 7 out,
//    their phases 72 degrees apart;
//  - AddPoppingMotes (0056d5c0), ids 0xcb (one record) and 0xfe (four): id 0x21, motes of
//    super1 to super5.bmp popping up round the actor.
void AddRisingMotes(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                    std::vector<EffectPrim>& added);
void AddPulsingMotes(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                     std::vector<EffectPrim>& added);
void AddPoppingMotes(RagEffect& effect, const EffectActor& actor, std::int8_t count,
                     core::CrtRand& rand, std::vector<EffectPrim>& added);

// The effects of single squares and rectangles
// (docs/render/effect-prim.md#single-squares-and-rectangles), on their first frame unless said:
//  - AddBurstSquares (005752b0), id 0xe3 (20 a step, effect\EF_Bash.wav, and at frame 35
//    effect\rog_intimidate.wav): id 0x29, four squares of white01.bmp flying out from the
//    target, each its own way;
//  - AddRays (005756b0, and with short_rays AddShortRays, 00575ed0), ids 0x106 (lens1.tga,
//    20 a step, colour 0), 0x10a (lens1, 24, 1) and 0x114 (lens2.tga, 20, 2, short): id
//    0x2a, four rays out from the actor;
//  - AddFireworks (00568ba0), id 0x129: id 0x32, four rockets of pok1, pok3 and pok2.tga
//    rising from 15 short of the player and bursting;
//  - AddBeams (00575870), id 300 (twice, at 30 and 60 degrees): id 0x34, four beams of
//    shockwave_c.bmp a quarter turn apart;
//  - AddSlashLines (005759b0), id 0x12f (twice, on either side), at frame 100: id 0x35,
//    four parallel lines of blue_ivy.bmp;
//  - AddSpreadingRings (005730c0): id 0x1c, four rings at its alpha and up, a quarter turn
//    apart;
//  - AddAsuraStrike (00567740), id 0x148: at least 280 frames; effect\EF_BeginSpell.wav and
//    two AddSpreadingRings of ring_white (alphas 45 and 25, preset 2), and at frames 1 and
//    21 id 0x41, three squares each of asura3 to 1 and then asura4 to 6.tga.
void AddBurstSquares(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                     std::vector<EffectPrim>& added);
void AddRays(RagEffect& effect, const EffectActor& actor, const char* texture, std::uint8_t color,
             bool short_rays, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddFireworks(RagEffect& effect, const Vec3& player, core::CrtRand& rand,
                  std::vector<EffectPrim>& added);
void AddBeams(RagEffect& effect, const EffectActor& actor, std::int16_t angle,
              std::vector<EffectPrim>& added);
void AddSlashLines(RagEffect& effect, const EffectActor& actor, bool right,
                   std::vector<EffectPrim>& added);
void AddSpreadingRings(RagEffect& effect, const EffectActor& actor, std::int16_t alpha,
                       const char* texture, std::int8_t preset, std::vector<EffectPrim>& added);
void AddAsuraStrike(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added);

// The effects of the other walls and bands (docs/render/effect-prim.md#walls-and-bands),
// on their first frame unless said:
//  - AddGrandCross (00574da0), id 0xe2 with AddBox: id 0x20, four quarter rings of
//    ring_white.tga moved 23 out along each diagonal, each wall leaning out at its angle
//    mirrored about the quarter's middle, closing in; effect\cru_grand cross.wav;
//  - AddBox (00575100), id 0xe2: id 0x23, two long thin boxes of ring_yellow.tga crossed,
//    rising and growing as they fade;
//  - AddCloudRings (005734b0), id 0xe0: id 0x3b, four open rings of cloud11.tga 8 up,
//    turning and opening to a third of a turn, _heal_effect.wav;
//  - AddOpeningPortal (005784a0), id 0x13d: id 0x39, three rings of magic_violet.tga at
//    random angles; at frame 1, id 0x3c, three low spreading rings of ring_blue.tga; at
//    frame 2, id 0x3b, four short arcs of cloud11.tga, each higher and further in;
//  - AddBoxWalls (00573d60), ids 0x13e (ring_red.tga, 0) and 0x13f (alpha_down.tga, 1): id
//    0x3d, a box of four walls pulsing in height, white or (1) green;
//  - AddPentagonBand (00569330), ids 0x133 (0), 0x142 (1), 0x143 (2) and 0x144 (10), all
//    cloud11.tga: id 0x3f, four five-sided bands from the actor up to 300 above (60 for
//    10), leaning 70 (15) out along x and z, which it writes to the target;
//  - AddArcBands (00579da0), id 0x149, at frames 0 and 1: id 0x42, four bands of
//    cloud11.tga thrown towards the target from about the actor, effect\EF_hit2.wav at
//    frames 10 and 30 and effect\EF_hit4.wav at 20;
//  - AddBouncingBands (00567bd0), id 0x14a, at frames 0 to 3: id 0x43, four trails of
//    white02.bmp bouncing out from the actor, each bounce 0.7 of the last.
void AddGrandCross(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added);
void AddBox(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added);
void AddCloudRings(RagEffect& effect, const EffectActor& actor, std::vector<EffectPrim>& added);
void AddOpeningPortal(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                      std::vector<EffectPrim>& added);
void AddBoxWalls(RagEffect& effect, const EffectActor& actor, const char* texture,
                 std::int8_t variant, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddPentagonBand(RagEffect& effect, const EffectActor& actor, const char* texture,
                     std::int8_t variant, std::vector<EffectPrim>& added);
void AddArcBands(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                 std::vector<EffectPrim>& added);
void AddBouncingBands(RagEffect& effect, const EffectActor& actor, core::CrtRand& rand,
                      std::vector<EffectPrim>& added);

// The effects of sprite trails (docs/render/effect-prim.md#sprite-trails), each a
// primitive drawn as the sprite's frame at its last positions; on their first frame unless
// said:
//  - AddHealingColumn (00552420), id 7: a cylinder of alpha_down.tga (id 0xd), and unless
//    effects are minimal every 9 frames id 7, a particle1 circling up 3 out; the effect
//    lasts 100 frames; _heal_effect.wav;
//  - AddBlueColumns (00554700), id 0xe: three blue cylinders of alpha_down.tga, and every
//    other frame id 7, a particle2 circling up; _heal_effect.wav;
//  - AddSoulStrike (00554cc0), id 0xf: by the level, one to five shots of id 8 at frames 6,
//    17, ... towards the target, each circling its line; effect\EF_SoulStrike.wav;
//  - AddSpiralSpark (0055c5d0), id 0x47: every 130 frames id 7, a particle1 spiralling out;
//    the effect lasts 200 frames;
//  - AddWideningSpirals (0055c770), id 0x48: every 10 frames the same, a distance key at
//    age 95 turning its widening to −0.15, the effect ending at 500 less the frame;
//  - AddRemoveTrap (0055f760), id 100: at frames 0 and 7, twelve particle1s thrown up and
//    falling; effect\hunter_removetrap.wav;
//  - AddAspersio (00565bd0), id 0x56: every 5 frames from 65 to 135 a particle1 circling
//    up, the effect lasting 400 frames, false once its frame meets the duration;
//    effect\priest_aspersio.wav at 70;
//  - AddWaterBall (005607e0), id 0x75: a shot of id 8 of Waterball towards the target;
//    effect\wizard_waterball_chulung.wav;
//  - AddTripleShot (00564160 and 005643e0), ids 0xd8 (red) and 0xd9 (blue): three shots
//    of id 8 towards the target circling 90 degrees apart;
//  - AddSnowfall (00557600), id 0xa2: while 160 frames are left, two flakes of ef_snow
//    (id 0x15) a frame, 100 above the ground about the player, falling;
//  - AddFallingLeaves (00567e10 and 00568090), ids 0xa3 (sakura01, to frame 300) and 0x14d
//    (단풍, to 200): every other frame id 0x26, a leaf drifting down from 200 above the
//    actor; like AddRingTrio they start again at frame 99999998;
//  - AddTargetSprite (0057a190), id 0x10b: id 0x2c, 침투경_ at the target, its action by
//    the way the actor lies from it on screen; effect\mon_침투경.wav;
//  - AddDemonstration (0057a400), id 0x12e: id 0x31, 데몬스트레이션 1 above the actor.
void AddHealingColumn(RagEffect& effect, const EffectActor& actor, bool minimal,
                      const PrimSprite& particle, core::CrtRand& rand,
                      std::vector<EffectPrim>& added);
void AddBlueColumns(RagEffect& effect, const EffectActor& actor, const PrimSprite& sparks,
                    core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddSoulStrike(RagEffect& effect, const PrimSprite& particle, std::vector<EffectPrim>& added);
void AddSpiralSpark(RagEffect& effect, const PrimSprite& particle, std::vector<EffectPrim>& added);
void AddWideningSpirals(RagEffect& effect, const EffectActor& actor, const PrimSprite& particle,
                        std::vector<EffectPrim>& added);
void AddRemoveTrap(RagEffect& effect, const PrimSprite& particle, core::CrtRand& rand,
                   std::vector<EffectPrim>& added);
bool AddAspersio(RagEffect& effect, const PrimSprite& particle, core::CrtRand& rand,
                 std::vector<EffectPrim>& added);
void AddWaterBall(RagEffect& effect, const PrimSprite& ball, std::vector<EffectPrim>& added);
void AddTripleShot(RagEffect& effect, const PrimSprite& particle, bool blue,
                   std::vector<EffectPrim>& added);
void AddSnowfall(RagEffect& effect, const Vec3& player, const res::AttrMap* attr,
                 const PrimSprite& snow, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddFallingLeaves(RagEffect& effect, const EffectActor& actor, const PrimSprite& leaf,
                      bool maple, core::CrtRand& rand, std::vector<EffectPrim>& added);
void AddTargetSprite(RagEffect& effect, const EffectActor& actor, const PrimSprite& sprite,
                     float view_yaw, std::vector<EffectPrim>& added);
void AddDemonstration(RagEffect& effect, const EffectActor& actor, const PrimSprite& sprite,
                      std::vector<EffectPrim>& added);

// The world's object list as effects use it (CWorld::UpdateGameObjects, 004c4a10): each
// world step every object's vf01 in order, a primitive an effect adds joining the end and
// running the same step; an object whose vf01 answers false is deleted, and an effect's
// deletion tells its primitives.
class EffectWorld {
 public:
  explicit EffectWorld(std::uint32_t seed = 1) : rand_(seed) {}

  // The particle sprite, and whether minimal effects are on (g_ClientState+0x1840).
  void SetParticle(const PrimSprite& particle) { SetSprite(kParticleSprite, particle); }
  // A sprite effects load by name (SetSprite and SetAct); an unset one draws nothing.
  void SetSprite(const std::string& name, const PrimSprite& sprite) { sprites_[name] = sprite; }
  PrimSprite Sprite(const std::string& name) const;
  void SetMinimal(bool minimal) { minimal_ = minimal; }
  // Where the player stands (the game mode's world, +0x3c), which the clouds follow.
  void SetPlayer(const Vec3& player) { player_ = player; }

  // CAbleToMakeEffect::LaunchEffect (00501e10) and InitEffect: effect id at the actor, its
  // duration from the table, appended to the list.
  RagEffect& Launch(std::int32_t id, const EffectActor& actor);

  void Step(const PrimScene& scene);
  PrimDrawing Draw(const PrimScene& scene) const;

  bool empty() const { return objects_.empty(); }
  core::CrtRand& rand() { return rand_; }

  struct Object {
    std::unique_ptr<RagEffect> effect;
    EffectActor actor;
    std::unique_ptr<EffectPrim> prim;
  };
  const std::list<Object>& objects() const { return objects_; }

 private:
  std::list<Object> objects_;
  core::CrtRand rand_;
  std::map<std::string, PrimSprite> sprites_;
  bool minimal_ = false;
  Vec3 player_{};
};

}  // namespace ragexe::render
