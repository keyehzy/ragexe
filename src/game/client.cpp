#include "game/client.h"

#include "game/mode_switcher.h"
#include "platform/platform.h"

namespace ragexe::game {

void Client::BeginFrame(ModeSwitcher& switcher) {
  platform::PumpEvents([&](const platform::Event& e) {
    switch (e.type) {
      case platform::Event::Type::Quit: switcher.Quit(); break;
      case platform::Event::Type::FocusGained: active = true; break;
      case platform::Event::Type::FocusLost: active = false; break;
      default: break;
    }
  });
  ++frames;
  if (max_frames != 0 && frames >= max_frames) switcher.Quit();
}

}  // namespace ragexe::game
