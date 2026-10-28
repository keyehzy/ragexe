#pragma once

#include <cstdint>

#include "render/ground.h"
#include "render/matrix.h"

namespace ragexe::res {
struct AttrMap;
}

namespace ragexe::render {

// The SceneRenderer's projection, which SetSize (0040db70) derives from the viewport
// (docs/render/camera.md). Points project to centre + scale · (x, y) / z.
struct Projection {
  float scale_x = 0.0f;       // +0, half the width over tan 7.5°, times the aspect
  float scale_y = 0.0f;       // +4, half the height over tan 7.5°
  float slope_x = 0.0f;       // +8, tan 82.5° times the aspect: the culling's side planes
  float slope_y = 0.0f;       // +0xc, tan 82.5°: its top and bottom planes
  float aspect = 0.0f;        // +0x10, height / width
  float width_ratio = 0.0f;   // +0x14, width / 640
  float height_ratio = 0.0f;  // +0x18, height / 480
  std::int32_t center_x = 0;  // +0x1c
  std::int32_t center_y = 0;  // +0x20
  std::int32_t width = 0;     // +0x24
  std::int32_t height = 0;    // +0x28
  std::int32_t origin_x = 0;  // +0x2c
  std::int32_t origin_y = 0;  // +0x30
};

// SceneRenderer::SetSize (0040db70), as C3dDevice::CreateRenderer calls it with the
// screen's size.
Projection SetSize(std::int32_t width, std::int32_t height);

// What the ground's drawing reads of the projection.
GroundViewport GroundViewportOf(const Projection& p);

// CView's camera (fields +0x1c to +0xf0). Each frame the current pitch, yaw, distance and
// point looked at move a tenth of the way to their targets, and the view is rebuilt from
// them. Angles are in degrees; y grows downward, so a negative pitch looks down.
struct Camera {
  float pitch = 0.0f;            // +0x1c
  float yaw = 0.0f;              // +0x20, in [0, 360)
  float distance = 0.0f;         // +0x24
  Vec3 at{};                     // +0x28
  float target_pitch = 0.0f;     // +0x34
  float target_yaw = 0.0f;       // +0x38
  float target_distance = 0.0f;  // +0x3c
  Vec3 target_at{};              // +0x40
  Vec3 eye{};                    // +0x7c
  Vec3 up{};                     // +0x88
  Matrix view{};                 // +0x94, world to view
  Matrix inverse{};              // +0xc4, view to world
};

// CView::vf01 (004cf1d0): pitch −45, yaw 0, distance 300, at the origin, up +y.
void ResetCamera(Camera& camera);

// CView::AddToAngle (004cf230).
void AddToAngle(Camera& camera, float degrees);

// CView 004cf7d0: the target yaw wrapped into [0, 360) and the yaw turned a tenth of the
// shorter way to it, then wrapped; the pitch, distance and point a tenth of the way to
// theirs, the point at once when fixed (g_isFixedCamera, 00685ecc).
void StepCamera(Camera& camera, bool fixed);

// CView 004cfa10: the eye, distance back from the point along the view's direction (the
// rotation by pitch about x, then yaw about y), kept above the attribute map's ground
// (C3dAttr::GetHeight) when there is one; then the look-at view and its inverse.
void BuildCameraView(Camera& camera, const res::AttrMap* attr);

// CView::vf04 (004cf260) for a frame: the target point is the player's position, then
// StepCamera and BuildCameraView. The view's shake (004cf2a0) is not ported.
void UpdateCamera(Camera& camera, const Vec3& player, bool fixed, const res::AttrMap* attr);

// A pitch and distance the player chose, kept per kind of map in the registry
// (ClientState::SaveOptions, 00588520).
struct ViewSetting {
  float latitude = -45.0f;
  float distance = 300.0f;
};

// What CGameMode keeps for the camera between frames and maps.
struct CameraControl {
  bool indoor = false;       // the map is in indoorRswTable.txt (CGameMode+0x250 clear)
  float indoor_yaw = 0.0f;   // +0x270, the yaw indoor turning stays within 20° of
  ViewSetting current;       // 00614884, 00614888
  ViewSetting indoor_view;   // g_indoorViewLatitude 0061488c, g_indoorViewDistance 00614890
  ViewSetting outdoor_view;  // g_outdoorViewLatitude 00614894, g_outdoorViewDistance 00614898
};

// CGameMode 0050d9e0, on entering a map: the point and its target at the player, and the
// setting saved for the kind of map made current. An indoor map also turns the yaw to 45°.
// The latitude is kept within 10° of −45 indoors and 20° outdoors, the distance within 230
// and 300 indoors or 400 outdoors; they become the camera's pitch and distance, current
// and target.
void PlaceCamera(Camera& camera, CameraControl& control, const Vec3& player, bool indoor);

// CGameMode 0050db70, on leaving a map: the current setting saved for its kind.
void LeaveMap(CameraControl& control);

// The mouse input CGameMode 005156d0 turns into camera targets.
struct CameraDrag {
  // The mouse state (00628b74) the handler acts on.
  enum class Kind {
    kDrag = 2,
    kReset = 4,
  };
  Kind kind = Kind::kDrag;
  std::int32_t mouse_x = 0;  // 00628b5c
  std::int32_t mouse_y = 0;  // 00628b60
  std::int32_t last_x = 0;   // the mouse at the previous frame's input (CGameMode+0xc4,
  std::int32_t last_y = 0;   // +0xc8), which 00515d10 sets after each
  bool shift = false;
  bool ctrl = false;
  bool free_turn = false;  // ClientState option 6: indoor maps turn freely
  bool free_zoom = false;  // ClientState option 5: the distance is not limited
};

// CGameMode 005156d0. A drag turns the target yaw by the mouse's x movement over the frame
// (indoors, within 20° of the indoor yaw); with Shift it pitches and with Ctrl it zooms by half its
// y movement, within the limits PlaceCamera keeps. A reset with Ctrl sets the target distance
// to 300, with Shift the target pitch to −45, and outdoors with neither the yaw to 0.
void DragCamera(Camera& camera, CameraControl& control, const CameraDrag& drag);

}  // namespace ragexe::render
