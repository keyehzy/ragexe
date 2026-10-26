#include "core/frame_timer.h"

namespace ragexe::core {

void FrameTimer::Reset(std::uint32_t now_ms) { base_ms_ = now_ms; }

void FrameTimer::SetFrameRate(std::uint32_t fps, std::uint32_t now_ms) {
  base_ms_ = now_ms;
  period_ms_ = 1000 / fps;
}

std::uint32_t FrameTimer::StepsDue(std::uint32_t now_ms) {
  const std::uint32_t steps = (now_ms - base_ms_) / period_ms_;
  base_ms_ += steps * period_ms_;
  return catch_up_ ? steps : 1;
}

}  // namespace ragexe::core
