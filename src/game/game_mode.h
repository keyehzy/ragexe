#pragma once

#include <cstdint>

#include "game/client.h"
#include "game/mode.h"

namespace ragexe::game {

// CGameMode, stubbed: no world, network or UI yet. Each frame keeps the original's order
// (CGameMode::OnFrame 0x50d400): events, then the fixed world steps the frame timer says
// are due, then drawing.
class GameMode : public Mode {
 public:
  GameMode(ModeSwitcher& switcher, Client& client) : Mode(switcher), client_(client) {}

  void OnInit(std::string_view map) override;
  void OnExit() override;
  void OnFrame() override;

  std::uint64_t world_steps() const { return world_steps_; }

 private:
  Client& client_;
  std::uint64_t world_steps_ = 0;
};

}  // namespace ragexe::game
