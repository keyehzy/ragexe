#pragma once

#include <cstdint>
#include <string>

#include "core/frame_timer.h"

namespace ragexe::platform {
class Window;
}

namespace ragexe::game {

class ModeSwitcher;

// What the modes reach that the original keeps in globals: the window and device, the
// frame timer, the active flag (0x61ec54). One object, passed by reference.
struct Client {
  platform::Window* window = nullptr;
  core::FrameTimer timer;
  bool active = true;

  // Run limits for unattended runs (CI, smoke tests). max_frames 0 means no limit;
  // auto_login_frames below 0 leaves the login mode waiting.
  std::uint64_t frames = 0;
  std::uint64_t max_frames = 0;
  int auto_login_frames = -1;
  std::string auto_login_map = "prontera.rsw";

  // The start of every frame (PumpMessages): drains the event queue, turns a close
  // request into ModeSwitcher::Quit, tracks focus, and counts the frame against
  // max_frames.
  void BeginFrame(ModeSwitcher& switcher);
};

}  // namespace ragexe::game
