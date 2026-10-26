#include "game/mode_switcher.h"

#include <utility>

namespace ragexe::game {

void ModeSwitcher::Run(ModeId mode, std::string map) {
  next_id_ = mode;
  next_map_ = std::move(map);
  while (!quit_) {
    current_id_ = next_id_;
    current_map_ = next_map_;
    current_ = factory_(current_id_, *this);
    if (!current_) break;
    current_->OnInit(current_map_);
    current_->Run();
    current_->OnExit();
    current_.reset();
  }
}

void ModeSwitcher::RequestMode(ModeId mode, std::string map) {
  next_id_ = mode;
  next_map_ = std::move(map);
  if (current_) current_->Stop();
}

void ModeSwitcher::Quit() {
  quit_ = true;
  if (current_) current_->Stop();
}

}  // namespace ragexe::game
