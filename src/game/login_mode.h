#pragma once

#include "game/client.h"
#include "game/mode.h"

namespace ragexe::game {

// CLoginMode, stubbed: no network or UI yet. Each frame is the login mode's frame
// without content: events, then a cleared window. With Client::auto_login_frames set it
// moves to the game mode the way login packet 0x73 does, RequestMode(Game, map).
class LoginMode : public Mode {
 public:
  LoginMode(ModeSwitcher& switcher, Client& client) : Mode(switcher), client_(client) {}

  void OnInit(std::string_view map) override;
  void OnExit() override;
  void OnFrame() override;

 private:
  Client& client_;
};

}  // namespace ragexe::game
