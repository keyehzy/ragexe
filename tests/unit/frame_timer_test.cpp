#include "core/frame_timer.h"

#include <catch2/catch_test_macros.hpp>

using ragexe::core::FrameTimer;

TEST_CASE("SetFrameRate(60) gives the original's 16 ms period") {
  FrameTimer timer;
  timer.SetFrameRate(60, 1000);
  CHECK(timer.period_ms() == 16);
  CHECK(timer.base_ms() == 1000);
}

TEST_CASE("Steps due are the whole periods elapsed; the remainder carries over") {
  FrameTimer timer;
  timer.SetFrameRate(60, 0);
  CHECK(timer.StepsDue(15) == 0);
  CHECK(timer.StepsDue(16) == 1);
  CHECK(timer.base_ms() == 16);
  CHECK(timer.StepsDue(40) == 1);  // 24 ms elapsed: one step, 8 ms left over
  CHECK(timer.base_ms() == 32);
  CHECK(timer.StepsDue(100) == 4);  // a late frame catches up
  CHECK(timer.base_ms() == 96);
}

TEST_CASE("Sixty seconds of frames run 3,750 steps, 62.5 a second") {
  FrameTimer timer;
  timer.SetFrameRate(60, 0);
  std::uint32_t total = 0;
  for (std::uint32_t t = 1; t <= 60000; t += 7) total += timer.StepsDue(t);
  total += timer.StepsDue(60000);
  CHECK(total == 3750);
}

TEST_CASE("With catch-up off every frame runs one step, and the base still advances") {
  FrameTimer timer;
  timer.SetFrameRate(60, 0);
  timer.SetCatchUp(false);
  CHECK(timer.StepsDue(5) == 1);
  CHECK(timer.base_ms() == 0);
  CHECK(timer.StepsDue(100) == 1);
  CHECK(timer.base_ms() == 96);
}

TEST_CASE("The 32-bit millisecond clock wraps without losing steps") {
  FrameTimer timer;
  timer.SetFrameRate(60, 0xFFFFFFF0u);
  CHECK(timer.StepsDue(0x00000010u) == 2);  // 32 ms across the wrap
}

TEST_CASE("Reset drops the time owed") {
  FrameTimer timer;
  timer.SetFrameRate(60, 0);
  timer.Reset(5000);
  CHECK(timer.StepsDue(5010) == 0);
}
