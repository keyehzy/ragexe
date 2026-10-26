#include "game/game_mode.h"

#include <SDL3/SDL_log.h>

#include "game/mode_switcher.h"
#include "platform/platform.h"

namespace ragexe::game {

void GameMode::OnInit(std::string_view map) {
  SDL_Log("game mode: init (%.*s)", static_cast<int>(map.size()), map.data());
  // CGameMode::OnInit ends with ResetFrameTimer, so loading time is not caught up.
  client_.timer.Reset(platform::TimeMs());
}

void GameMode::OnExit() {
  SDL_Log("game mode: exit after %d frames, %llu world steps", frames_in_state(),
          static_cast<unsigned long long>(world_steps_));
}

void GameMode::OnFrame() {
  client_.BeginFrame(switcher_);
  if (running()) world_steps_ += client_.timer.StepsDue(platform::TimeMs());
  if (client_.window != nullptr && client_.active) {
    client_.window->Clear(0, 0, 0);
    client_.window->Present();
  }
}

}  // namespace ragexe::game
