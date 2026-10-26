#pragma once

#include <functional>
#include <memory>
#include <string>

#include "game/mode.h"

namespace ragexe::game {

// ModeSwitcher (the global at 0x67c9e8): runs one mode at a time until the client quits.
// A mode ends when something requests another mode or quits; every map change builds a
// new game mode, so state that must survive it lives outside the mode.
class ModeSwitcher {
 public:
  using Factory = std::function<std::unique_ptr<Mode>(ModeId, ModeSwitcher&)>;

  explicit ModeSwitcher(Factory factory) : factory_(std::move(factory)) {}

  // Creates the requested mode, calls OnInit(map), Run and OnExit, destroys it, and
  // repeats with the next request until Quit.
  void Run(ModeId mode, std::string map);

  // Stores the next mode and map and stops the current mode.
  void RequestMode(ModeId mode, std::string map);
  // Stops the current mode and the switcher (the original's running flag and the global
  // quit flag at 0x688a68).
  void Quit();

  bool quitting() const { return quit_; }
  Mode* current() const { return current_.get(); }
  ModeId current_id() const { return current_id_; }
  const std::string& current_map() const { return current_map_; }

 private:
  Factory factory_;
  std::unique_ptr<Mode> current_;
  ModeId current_id_ = ModeId::Login;
  ModeId next_id_ = ModeId::Login;
  std::string current_map_;
  std::string next_map_;
  bool quit_ = false;
};

}  // namespace ragexe::game
