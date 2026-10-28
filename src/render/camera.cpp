#include "render/camera.h"

#include <cmath>

#include "res/gat.h"

namespace ragexe::render {

namespace {

constexpr float kPi = 3.1415920257568359375f;          // 005e2694 and 005e8a90
constexpr float kInverse180 = 0.0055555556900799274f;  // 005e2690
constexpr float kInverse360 = 0.0027777778450399637f;  // 005e26bc
constexpr float kFieldOfView = 15.0f;                  // 005e26a0 and 005e26a4, degrees
constexpr float kRightAngle = 90.0f;                   // 005e26b8
constexpr float kInverse640 = 0.0015625000232830644f;  // 005e26b4
constexpr float kInverse480 = 0.0020833334419876337f;  // 005e26b0
constexpr float kFullTurn = 360.0f;                    // 005e2a5c
constexpr float kHalfTurn = 180.0f;                    // 005e2a54
constexpr float kFollow = 0.1f;                        // 005e8ab8
constexpr float kDefaultPitch = -45.0f;                // CView::vf01's, and 0051572e
constexpr float kDefaultDistance = 300.0f;             // 00614880
constexpr float kOutdoorMaxDistance = 400.0f;          // 0061487c
constexpr float kMinDistance = 230.0f;                 // 00614878
constexpr float kIndoorPitchRange = 10.0f;             // 005e3bb4
constexpr float kOutdoorPitchRange = 20.0f;            // 005e9560
constexpr float kIndoorTurnRange = 20.0f;              // 005e9560
constexpr float kIndoorYaw = 45.0f;                    // 0050da85
constexpr float kPitchCentre = 45.0f;                  // 005e93c0, and 005e978c's −45

// fptan's result, which the precision control does not round.
double Tan(float radians) { return std::tan(static_cast<double>(radians)); }

float Radians(float degrees) { return (kPi * degrees) * kInverse180; }

// Wraps an angle into [0, 360) a turn at a time.
float WrapDegrees(float a) {
  while (a < 0.0f) a = a + kFullTurn;
  while (a >= kFullTurn) a = a - kFullTurn;
  return a;
}

float Follow(float current, float target) { return ((target - current) * kFollow) + current; }

// Within −45 ∓ range, as 0050dab8 and 005158b5 compare: below the bottom, then above the
// top.
float ClampPitch(float pitch, float range) {
  const float low = -kPitchCentre - range;
  if (pitch < low) pitch = low;
  const float high = range - kPitchCentre;
  if (pitch > high) pitch = high;
  return pitch;
}

float ClampDistance(float distance, float most) {
  if (distance < kMinDistance) distance = kMinDistance;
  if (distance > most) distance = most;
  return distance;
}

}  // namespace

Projection SetSize(std::int32_t width, std::int32_t height) {
  Projection p;
  const float aspect = static_cast<float>(height) / static_cast<float>(width);
  p.width = width;
  p.height = height;
  p.origin_x = width / 2;
  p.origin_y = height / 2;
  p.aspect = aspect;
  p.scale_x = static_cast<float>(p.origin_x / Tan((kPi * kFieldOfView) * kInverse360));
  p.scale_y = static_cast<float>(p.origin_y / Tan((kPi * kFieldOfView) * kInverse360));
  p.slope_x = static_cast<float>(Tan(((kRightAngle - (kFieldOfView * 0.5f)) * kPi) * kInverse180));
  p.slope_y = static_cast<float>(Tan(((kRightAngle - (kFieldOfView * 0.5f)) * kPi) * kInverse180));
  p.center_x = p.origin_x;
  p.center_y = p.origin_y;
  p.scale_x = aspect * p.scale_x;
  p.slope_x = aspect * p.slope_x;
  p.width_ratio = static_cast<float>(width) * kInverse640;
  p.height_ratio = static_cast<float>(height) * kInverse480;
  return p;
}

GroundViewport GroundViewportOf(const Projection& p) {
  return {p.scale_x, p.scale_y, p.center_x, p.center_y, p.width, p.height, p.origin_x, p.origin_y};
}

void ResetCamera(Camera& camera) {
  camera.distance = kDefaultDistance;
  camera.target_distance = kDefaultDistance;
  camera.pitch = kDefaultPitch;
  camera.target_pitch = kDefaultPitch;
  camera.yaw = 0.0f;
  camera.target_yaw = 0.0f;
  camera.at = {};
  camera.target_at = {};
  camera.up = {0.0f, 1.0f, 0.0f};
}

void AddToAngle(Camera& camera, float degrees) { camera.target_yaw = degrees + camera.target_yaw; }

void StepCamera(Camera& camera, bool fixed) {
  camera.target_yaw = WrapDegrees(camera.target_yaw);
  float turn = camera.target_yaw - camera.yaw;
  while (turn >= kHalfTurn) turn = turn - kFullTurn;
  while (turn < -kHalfTurn) turn = turn + kFullTurn;
  camera.yaw = WrapDegrees((turn * kFollow) + camera.yaw);
  camera.pitch = Follow(camera.pitch, camera.target_pitch);
  camera.distance = Follow(camera.distance, camera.target_distance);
  for (std::size_t i = 0; i < 3; ++i)
    camera.at[i] = fixed ? camera.target_at[i] : Follow(camera.at[i], camera.target_at[i]);
}

void BuildCameraView(Camera& camera, const res::AttrMap* attr) {
  const Matrix m = RotateY(MakeRotationX(Radians(camera.pitch)), Radians(camera.yaw));
  const float back = -camera.distance;
  for (std::size_t i = 0; i < 3; ++i) camera.eye[i] = ((m[6 + i] * back) + m[9 + i]) + camera.at[i];
  if (attr != nullptr) {
    const float ground = res::AttrHeight(*attr, camera.eye[0], camera.eye[2]);
    if (ground < camera.eye[1]) camera.eye[1] = ground;
  }
  camera.view = MakeLookAt(camera.eye, camera.at, camera.up);
  camera.inverse = Invert(camera.view);
}

void UpdateCamera(Camera& camera, const Vec3& player, bool fixed, const res::AttrMap* attr) {
  camera.target_at = player;
  StepCamera(camera, fixed);
  BuildCameraView(camera, attr);
}

void PlaceCamera(Camera& camera, CameraControl& control, const Vec3& player, bool indoor) {
  camera.at = player;
  camera.target_at = player;
  control.indoor = indoor;
  float range = kIndoorPitchRange;
  float most = kDefaultDistance;
  if (!indoor) {
    most = kOutdoorMaxDistance;
    range = kOutdoorPitchRange;
    control.current = control.outdoor_view;
  } else {
    control.indoor_yaw = kIndoorYaw;
    camera.yaw = kIndoorYaw;
    camera.target_yaw = control.indoor_yaw;
    control.current = control.indoor_view;
  }
  control.current.latitude = ClampPitch(control.current.latitude, range);
  control.current.distance = ClampDistance(control.current.distance, most);
  camera.pitch = control.current.latitude;
  camera.target_pitch = control.current.latitude;
  camera.distance = control.current.distance;
  camera.target_distance = control.current.distance;
}

void LeaveMap(CameraControl& control) {
  (control.indoor ? control.indoor_view : control.outdoor_view) = control.current;
}

void DragCamera(Camera& camera, CameraControl& control, const CameraDrag& drag) {
  if (drag.kind == CameraDrag::Kind::kReset) {
    if (drag.ctrl) camera.target_distance = kDefaultDistance;
    if (drag.shift) camera.target_pitch = kDefaultPitch;
    if (!control.indoor && !drag.shift && !drag.ctrl) camera.target_yaw = 0.0f;
    return;
  }
  if (control.indoor && !drag.free_turn) {
    float yaw =
        (camera.target_yaw + static_cast<float>(drag.mouse_x)) - static_cast<float>(drag.last_x);
    const float low = control.indoor_yaw - kIndoorTurnRange;
    if (yaw < low) yaw = low;
    const float high = control.indoor_yaw + kIndoorTurnRange;
    if (yaw > high) yaw = high;
    camera.target_yaw = yaw;
  } else {
    AddToAngle(camera, static_cast<float>(drag.mouse_x - drag.last_x));
  }
  const std::int32_t dy = (drag.mouse_y - drag.last_y) / 2;
  if (drag.shift) {
    const float range = control.indoor ? kIndoorPitchRange : kOutdoorPitchRange;
    control.current.latitude = ClampPitch(camera.target_pitch - static_cast<float>(dy), range);
    camera.target_pitch = control.current.latitude;
  }
  if (drag.ctrl) {
    float distance = camera.target_distance - static_cast<float>(dy);
    if (!drag.free_zoom)
      distance = ClampDistance(distance, control.indoor ? kDefaultDistance : kOutdoorMaxDistance);
    control.current.distance = distance;
    camera.target_distance = distance;
  }
}

}  // namespace ragexe::render
