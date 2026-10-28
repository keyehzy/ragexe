#include "render/camera.h"

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "res/gat.h"

using namespace ragexe::render;

namespace {

struct SizeVector {
  std::int32_t width, height;
  std::uint32_t fields[13];
};

struct HeightVector {
  std::uint32_t x, z, height;
};

struct CameraFrame {
  std::uint32_t player[3];
  std::uint32_t turn;
  int retarget;
  std::uint32_t target_pitch, target_distance;
  int fixed;
  std::uint32_t state[42];
};

struct PlaceVector {
  int indoor;
  std::uint32_t saved[4];
  std::uint32_t yaw;
  std::uint32_t player[3];
  std::uint32_t view[12];
  std::uint32_t settings[6];
  std::uint32_t outdoor_flag;
  std::uint32_t indoor_yaw;
  std::uint32_t current[2];
  std::uint32_t left[6];
};

struct DragVector {
  int kind, indoor, shift, ctrl, free_turn, free_zoom;
  std::int32_t mouse_x, mouse_y, last_x, last_y;
  std::uint32_t indoor_yaw;
  std::uint32_t targets[3];
  std::uint32_t after[5];
};

// The test map's height at a cell's corner (tools/camera_vectors.py).
float AttrTestHeight(std::int32_t cell, std::int32_t corner) {
  return static_cast<float>(20 - (cell * 7 + corner * 13) * 29 % 331);
}

#include "camera_vectors.inc"

float F(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t B(float f) { return std::bit_cast<std::uint32_t>(f); }
Vec3 V(const std::uint32_t* bits) { return {F(bits[0]), F(bits[1]), F(bits[2])}; }

ragexe::res::AttrMap TestAttrMap() {
  ragexe::res::AttrMap map;
  map.width = kAttrWidth;
  map.height = kAttrHeight;
  for (std::int32_t c = 0; c < kAttrWidth * kAttrHeight; ++c) {
    ragexe::res::AttrCell cell;
    for (std::int32_t k = 0; k < 4; ++k)
      cell.heights[static_cast<std::size_t>(k)] = AttrTestHeight(c, k);
    map.cells.push_back(cell);
  }
  return map;
}

// The fields +0x1c to +0x48 of CView.
std::vector<std::uint32_t> Targets(const Camera& c) {
  return {B(c.pitch),           B(c.yaw),          B(c.distance),     B(c.at[0]),
          B(c.at[1]),           B(c.at[2]),        B(c.target_pitch), B(c.target_yaw),
          B(c.target_distance), B(c.target_at[0]), B(c.target_at[1]), B(c.target_at[2])};
}

// The fields +0x1c to +0x48 and +0x7c to +0xf0.
std::vector<std::uint32_t> State(const Camera& c) {
  std::vector<std::uint32_t> out = Targets(c);
  for (const float f : c.eye) out.push_back(B(f));
  for (const float f : c.up) out.push_back(B(f));
  for (const float f : c.view) out.push_back(B(f));
  for (const float f : c.inverse) out.push_back(B(f));
  return out;
}

}  // namespace

TEST_CASE("SetSize derives the projection as the original does", "[camera]") {
  for (const SizeVector& v : kSizeVectors) {
    INFO(v.width << " x " << v.height);
    const Projection p = SetSize(v.width, v.height);
    const std::uint32_t got[13] = {B(p.scale_x),
                                   B(p.scale_y),
                                   B(p.slope_x),
                                   B(p.slope_y),
                                   B(p.aspect),
                                   B(p.width_ratio),
                                   B(p.height_ratio),
                                   static_cast<std::uint32_t>(p.center_x),
                                   static_cast<std::uint32_t>(p.center_y),
                                   static_cast<std::uint32_t>(p.width),
                                   static_cast<std::uint32_t>(p.height),
                                   static_cast<std::uint32_t>(p.origin_x),
                                   static_cast<std::uint32_t>(p.origin_y)};
    for (std::size_t i = 0; i < 13; ++i) {
      INFO("field " << i);
      CHECK(got[i] == v.fields[i]);
    }
  }
}

TEST_CASE("AttrHeight interpolates the cell under a point as GetHeight does", "[camera]") {
  const ragexe::res::AttrMap map = TestAttrMap();
  for (const HeightVector& v : kHeightVectors) {
    INFO(F(v.x) << ", " << F(v.z));
    CHECK(B(ragexe::res::AttrHeight(map, F(v.x), F(v.z))) == v.height);
  }
}

TEST_CASE("The camera follows the player frame by frame as CView does", "[camera]") {
  const ragexe::res::AttrMap map = TestAttrMap();
  Camera camera;
  ResetCamera(camera);
  std::size_t n = 0;
  std::size_t grounded = 0;
  for (const CameraFrame& f : kCameraFrames) {
    INFO("frame " << n++);
    if (F(f.turn) != 0.0f) AddToAngle(camera, F(f.turn));
    if (f.retarget != 0) {
      camera.target_pitch = F(f.target_pitch);
      camera.target_distance = F(f.target_distance);
    }
    UpdateCamera(camera, V(f.player), f.fixed != 0, &map);
    const std::vector<std::uint32_t> got = State(camera);
    for (std::size_t i = 0; i < got.size(); ++i) {
      INFO("value " << i);
      REQUIRE(got[i] == f.state[i]);
    }
    if (ragexe::res::AttrHeight(map, camera.eye[0], camera.eye[2]) == camera.eye[1]) ++grounded;
  }
  // Some frames put the eye below the ground, which lifts it.
  CHECK(grounded > 0);
}

TEST_CASE("Entering and leaving maps sets the camera as CGameMode does", "[camera]") {
  std::size_t n = 0;
  for (const PlaceVector& v : kPlaceVectors) {
    INFO("case " << n++);
    Camera camera;
    ResetCamera(camera);
    camera.yaw = F(v.yaw);
    camera.target_yaw = F(v.yaw);
    CameraControl control;
    control.indoor_view = {F(v.saved[0]), F(v.saved[1])};
    control.outdoor_view = {F(v.saved[2]), F(v.saved[3])};
    PlaceCamera(camera, control, V(v.player), v.indoor != 0);
    const std::vector<std::uint32_t> view = Targets(camera);
    for (std::size_t i = 0; i < 12; ++i) {
      INFO("field " << i);
      CHECK(view[i] == v.view[i]);
    }
    const std::uint32_t settings[6] = {
        B(control.current.latitude),      B(control.current.distance),
        B(control.indoor_view.latitude),  B(control.indoor_view.distance),
        B(control.outdoor_view.latitude), B(control.outdoor_view.distance)};
    for (std::size_t i = 0; i < 6; ++i) CHECK(settings[i] == v.settings[i]);
    CHECK(control.indoor == (v.outdoor_flag == 0));
    if (control.indoor) CHECK(B(control.indoor_yaw) == v.indoor_yaw);
    control.current = {F(v.current[0]), F(v.current[1])};
    LeaveMap(control);
    const std::uint32_t left[6] = {
        B(control.current.latitude),      B(control.current.distance),
        B(control.indoor_view.latitude),  B(control.indoor_view.distance),
        B(control.outdoor_view.latitude), B(control.outdoor_view.distance)};
    for (std::size_t i = 0; i < 6; ++i) CHECK(left[i] == v.left[i]);
  }
}

TEST_CASE("The mouse moves the camera's targets as CGameMode does", "[camera]") {
  std::size_t n = 0;
  for (const DragVector& v : kDragVectors) {
    INFO("case " << n++);
    Camera camera;
    camera.target_pitch = F(v.targets[0]);
    camera.target_yaw = F(v.targets[1]);
    camera.target_distance = F(v.targets[2]);
    CameraControl control;
    control.indoor = v.indoor != 0;
    control.indoor_yaw = F(v.indoor_yaw);
    control.current = {-1.0f, -2.0f};
    CameraDrag drag;
    drag.kind = v.kind == 4 ? CameraDrag::Kind::kReset : CameraDrag::Kind::kDrag;
    drag.mouse_x = v.mouse_x;
    drag.mouse_y = v.mouse_y;
    drag.last_x = v.last_x;
    drag.last_y = v.last_y;
    drag.shift = v.shift != 0;
    drag.ctrl = v.ctrl != 0;
    drag.free_turn = v.free_turn != 0;
    drag.free_zoom = v.free_zoom != 0;
    DragCamera(camera, control, drag);
    CHECK(B(camera.target_pitch) == v.after[0]);
    CHECK(B(camera.target_yaw) == v.after[1]);
    CHECK(B(camera.target_distance) == v.after[2]);
    CHECK(B(control.current.latitude) == v.after[3]);
    CHECK(B(control.current.distance) == v.after[4]);
  }
}
