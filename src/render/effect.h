#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "render/camera.h"
#include "render/ground.h"
#include "render/matrix.h"
#include "res/str.h"

namespace ragexe::render {

// One layer's values as CRagEffect keeps them (+0x760, 0x74 bytes a layer), as a key of
// the .str stores them (docs/render/effect.md). Most are floats; the texture mode (0x13)
// and the blends (0x1a to 0x1c) are copied as they are.
struct StrLayerState {
  std::array<std::uint32_t, 29> values{};

  float Get(std::size_t i) const;
  void Set(std::size_t i, float f);
};

namespace str_value {
inline constexpr std::size_t kX = 0;  // the layer's position, in a 640 x 480 frame
inline constexpr std::size_t kY = 1;
inline constexpr std::size_t kU = 2;  // the texture's corner and extent
inline constexpr std::size_t kV = 3;
inline constexpr std::size_t kUSize = 4;
inline constexpr std::size_t kVSize = 5;
inline constexpr std::size_t kCornerX = 0xa;  // four x, then four y, of the corners
inline constexpr std::size_t kCornerY = 0xe;
inline constexpr std::size_t kTexture = 0x12;  // the texture's index, a float
inline constexpr std::size_t kTextureMode = 0x13;
inline constexpr std::size_t kTextureStep = 0x14;
inline constexpr std::size_t kAngle = 0x15;  // in 1/1024 turns
inline constexpr std::size_t kRed = 0x16;    // red, green, blue and alpha, 0 to 255
inline constexpr std::size_t kSourceBlend = 0x1a;
inline constexpr std::size_t kDestBlend = 0x1b;
inline constexpr std::size_t kStageMode = 0x1c;
}  // namespace str_value

// A triangle a layer draws: AddPrimitive with flags 5 (translucent, its own stage mode).
struct EffectTriangle {
  std::int32_t layer = 0;
  std::int32_t texture = 0;  // the layer's texture index (GetEffectEntryTexture)
  std::uint32_t src_blend = 0;
  std::uint32_t dest_blend = 0;
  std::uint32_t mode = 0;
  std::array<ScreenVertex, 3> vertices{};
};

// CRagEffect's playing of a .str (CEZeffectRes): each world step Advance moves every
// layer's keys on to the frame, then Draw gives the layers' triangles, and the frame
// counts on.
class StrPlayer {
 public:
  // LoadStrFile (0054fae0) and 0054fb40: frame 0, every layer at its first key. The
  // effect ends when as many layers have run out of keys as it has layers, one fewer when
  // layer 0 has no keys.
  explicit StrPlayer(const res::Str& str);
  void Reset();

  // CRagEffect 0054fba0: the screen point of the position offset by (x, y, z), then each
  // layer whose current key has come: on to its next key when the frame reaches it, its
  // values set (type 0) or added to, and the texture index moved by the key's mode. False
  // once the effect has ended.
  bool Advance(const Matrix& view, const Vec3& position, const Projection& projection,
               float offset_x, float offset_y, float offset_z);

  // CRagEffect::vf01's last step: the next frame.
  void Step() { ++frame_; }

  // CRagEffect::vf03 (0054ee50) and 00550070: two triangles for each layer from 1 on that
  // Advance moved, the quad of its corners turned by its angle about its position.
  std::vector<EffectTriangle> Draw() const;

  std::int32_t frame() const { return frame_; }
  std::int32_t finished() const { return finished_; }
  const std::vector<StrLayerState>& states() const { return states_; }
  const std::vector<std::int32_t>& keys() const { return keys_; }
  const std::vector<bool>& moved() const { return moved_; }
  float screen_x() const { return screen_x_; }
  float screen_y() const { return screen_y_; }
  float screen_z() const { return screen_z_; }

 private:
  const res::Str* str_;
  std::int32_t frame_ = 0;             // +0xbc
  std::int32_t finished_ = 0;          // +0x4160
  std::int32_t limit_ = 0;             // +0x4164
  float screen_x_ = 0.0f;              // +0xdc
  float screen_y_ = 0.0f;              // +0xe0
  float screen_z_ = 0.0f;              // +0xe4
  std::vector<StrLayerState> states_;  // +0x760
  std::vector<std::int32_t> keys_;     // +0x41e8, -1 once a layer has run out
  std::vector<bool> moved_;            // +0x4168
};

}  // namespace ragexe::render
