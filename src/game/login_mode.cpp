#include "game/login_mode.h"

#include <SDL3/SDL_log.h>

#include "game/mode_switcher.h"
#include "platform/platform.h"

namespace ragexe::game {

void LoginMode::OnInit(std::string_view map) {
  SDL_Log("login mode: init (%.*s)", static_cast<int>(map.size()), map.data());
}

void LoginMode::OnExit() { SDL_Log("login mode: exit after %d frames", frames_in_state()); }

void LoginMode::OnFrame() {
  client_.BeginFrame(switcher_);
  if (client_.auto_login_frames >= 0 && frames_in_state() >= client_.auto_login_frames) {
    switcher_.RequestMode(ModeId::Game, client_.auto_login_map);
  }
  if (client_.window != nullptr) {
    client_.window->Clear(0, 0, 0);
    client_.window->Present();
  }
}

}  // namespace ragexe::game
