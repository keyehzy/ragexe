#include "world/actor_walk.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

#include "render/world_load.h"

namespace ragexe::world {

namespace {

constexpr float kFullTurn = 360.0f;                                  // 005e2a5c
constexpr float kDirectionStep = std::bit_cast<float>(0x3cb60b61u);  // 005e9518, 1/45
constexpr double kHalfDirection = 22.5;                              // 005e9530
constexpr double kLastHalfDirection = 337.5;                         // 005e9528
constexpr double kNearestStep = 1.0 / 45.0;                          // 005e9520
// 005e9664: frames per unit walked, times the delay.
constexpr float kStepsPerDistance = 1.48f;

// __ftol: toward zero, the integer indefinite out of range.
std::int32_t Ftol(float f) {
  if (std::isnan(f) || f <= -2147483904.0f || f >= 2147483648.0f)
    return std::numeric_limits<std::int32_t>::min();
  return static_cast<std::int32_t>(f);
}

// CActRes::GetFrameCount (00421a80): 1 for an action of fewer than two frames.
std::int32_t FrameCount(const res::Act& act, std::int32_t action) {
  if (action < 0 || static_cast<std::size_t>(action) >= act.actions.size()) return 1;
  const std::size_t n = act.actions[static_cast<std::size_t>(action)].size();
  return n < 2 ? 1 : static_cast<std::int32_t>(n);
}

// fcom's equal: true for equal values and for an unordered pair.
bool SameOrUnordered(float a, float b) { return !(a < b) && !(a > b); }

}  // namespace

std::int32_t GameActorJob(const GameActor& actor) {
  const bool mounted = (actor.effect_state >> 5 & 1) != 0;
  if (actor.job == 7 && mounted) return 13;
  if (actor.job == 14 && mounted) return 21;
  return actor.job;
}

std::int32_t GameActorDirection(const GameActor& actor, float facing, float camera_yaw) {
  float d = -camera_yaw - facing;
  while (d < 0.0f) d += kFullTurn;
  while (d >= kFullTurn) d -= kFullTurn;
  const std::int32_t job = GameActorJob(actor);
  const std::int32_t base = actor.object.base_action;
  const bool nearest =
      (job < 45 && (base == 0 || base == 8 || base == 16)) || (job > 45 && job < 1000 && base == 0);
  if (!nearest) return Ftol(d * kDirectionStep);
  if (!(static_cast<double>(d) >= kHalfDirection) || static_cast<double>(d) > kLastHalfDirection)
    return 0;
  const auto shifted = static_cast<float>(static_cast<double>(d) + kHalfDirection);
  return Ftol(static_cast<float>(static_cast<double>(shifted) * kNearestStep));
}

WalkResult ProcessWalk(GameActor& actor, const res::AttrMap& attr, const res::Act& act,
                       float camera_yaw, std::uint32_t now, std::uint32_t server_time) {
  render::RenderObject& object = actor.object;
  if (actor.hold_until != 0 && now > actor.hold_until) {
    object.held = false;
    actor.hold_until = 0;
  }
  // UpdateFade (00503830): the alpha falls with half the milliseconds left, from 255.
  if (actor.fade_until != 0) {
    if (now > actor.fade_until) return WalkResult::kFaded;
    std::uint32_t alpha = (actor.fade_until - now) >> 1;
    if (alpha > 0xff) alpha = 0xff;
    object.color = (object.color & 0x00ffffffu) | alpha << 24;
  }

  const render::Vec3 from = object.position;
  std::uint32_t step_time = 0;
  float cell_x = 0.0f;
  float cell_y = 0.0f;
  actor.segment = actor.path.FindSegment(server_time, &step_time, &cell_x, &cell_y);
  const std::array<float, 2> centre = render::CellCentre(attr, Ftol(cell_x), Ftol(cell_y));
  // From the last update: the time to the step and the time gone, as signed, at least 0.
  std::int32_t remaining = static_cast<std::int32_t>(step_time - actor.server_at);
  std::int32_t elapsed = static_cast<std::int32_t>(server_time - actor.server_at);
  if (remaining < 0) remaining = 0;
  if (elapsed < 0) elapsed = 0;
  bool moved = false;
  if (remaining > 0 && elapsed > 0) {
    moved = true;
    const auto gone = static_cast<float>(elapsed);
    const auto left = static_cast<float>(remaining);
    const float dz = centre[1] - from[2];
    object.position[0] = (centre[0] - from[0]) * gone / left + from[0];
    object.position[2] = gone * dz / left + from[2];
  }
  if (!actor.pushed) {
    if (!SameOrUnordered(from[0], object.position[0]) ||
        !SameOrUnordered(from[2], object.position[2])) {
      object.facing =
          render::AngleBetween(from[0], from[2], object.position[0], object.position[2]);
    }
    // UpdateDirectionalIndex (00503050).
    object.action = object.base_action + GameActorDirection(actor, object.facing, camera_yaw);
  }
  // AddTravelledDistance (00503190).
  const float dx = object.position[0] - from[0];
  const float dz = object.position[2] - from[2];
  actor.travelled = actor.travelled + std::sqrt(dx * dx + dz * dz);
  if (!actor.pushed) {
    object.frame =
        Ftol(actor.travelled * kStepsPerDistance / object.delay) % FrameCount(act, object.action);
  }
  WalkResult result = WalkResult::kWalking;
  if (actor.segment == -1 && server_time >= step_time) {
    object.position[0] = centre[0];
    object.position[2] = centre[1];
    moved = true;
    actor.pushed = false;
    object.held = false;
    result = WalkResult::kArrived;
  }
  actor.processed_at = now;
  actor.server_at = server_time;
  if (moved) object.position[1] = res::AttrHeight(attr, object.position[0], object.position[2]);
  return result;
}

}  // namespace ragexe::world
