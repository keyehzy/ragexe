#pragma once

#include <cstdint>

namespace ragexe::core {

// The client's fixed world step (docs/architecture.md §4, "Frame timing"). The original
// keeps the base time and period in globals: ResetFrameTimer (0x420350) stores the time,
// SetFrameRate (0x420360) also sets the period to 1000 / fps whole milliseconds, and
// GetFrameStepsDue (0x420390) returns the whole periods elapsed since the base and
// advances the base by them. Times are 32-bit milliseconds that wrap, like timeGetTime.
class FrameTimer {
 public:
  void Reset(std::uint32_t now_ms);
  void SetFrameRate(std::uint32_t fps, std::uint32_t now_ms);

  // With catch-up off (the flag at 0x61ec58 cleared by a chat command) every call
  // returns 1, although the base still advances by the periods elapsed.
  std::uint32_t StepsDue(std::uint32_t now_ms);

  void SetCatchUp(bool on) { catch_up_ = on; }
  bool catch_up() const { return catch_up_; }
  std::uint32_t period_ms() const { return period_ms_; }
  std::uint32_t base_ms() const { return base_ms_; }

 private:
  std::uint32_t base_ms_ = 0;
  std::uint32_t period_ms_ = 1000 / 60;
  bool catch_up_ = true;
};

}  // namespace ragexe::core
