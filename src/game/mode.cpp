#include "game/mode.h"

#include "game/mode_switcher.h"

namespace ragexe::game {

void Mode::Run() {
  while (run_ && !switcher_.quitting()) {
    if (pending_state_ != -1) {
      state_ = pending_state_;
      pending_state_ = -1;
      frames_in_state_ = 0;
      OnChangeState(state_);
    }
    OnFrame();
    ++frames_in_state_;
  }
}

int Mode::SendMsg(int msg, int, int, int) {
  if (msg == kMsgQuit) switcher_.Quit();
  return 0;
}

void Mode::OnChangeState(int) {}

}  // namespace ragexe::game
