// The client's entry point, in WinMain's order (docs/architecture.md §2) for what exists
// so far: settings, the platform, the window, the frame rate, then the mode switcher from
// the login mode until the client quits.
//
// Options for unattended runs:
//   --frames N        quit after N frames
//   --auto-login N    leave the login mode for the game mode after N frames
//   --map NAME        the map --auto-login enters (default prontera.rsw)

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>

#include <charconv>
#include <cstdlib>
#include <memory>
#include <string_view>

#include "game/client.h"
#include "game/game_mode.h"
#include "game/login_mode.h"
#include "game/mode_switcher.h"
#include "platform/platform.h"
#include "platform/settings.h"

namespace {

bool ParseCount(std::string_view s, long long& out) {
  const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), out);
  return error == std::errc() && end == s.data() + s.size() && out >= 0;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace ragexe;

  game::Client client;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool has_value = i + 1 < argc;
    long long n = 0;
    if (arg == "--frames" && has_value && ParseCount(argv[i + 1], n)) {
      client.max_frames = static_cast<std::uint64_t>(n);
      ++i;
    } else if (arg == "--auto-login" && has_value && ParseCount(argv[i + 1], n)) {
      client.auto_login_frames = static_cast<int>(n);
      ++i;
    } else if (arg == "--map" && has_value) {
      client.auto_login_map = argv[++i];
    } else {
      SDL_Log("unknown option %s", argv[i]);
      return EXIT_FAILURE;
    }
  }

  platform::Platform sdl;
  if (!sdl.ok()) {
    SDL_Log("SDL could not start: %s", sdl.error().c_str());
    return EXIT_FAILURE;
  }

  const std::filesystem::path settings_path = platform::UserDir() / "settings.txt";
  const platform::Settings settings = platform::LoadSettings(settings_path);
  if (!std::filesystem::exists(settings_path)) platform::SaveSettings(settings_path, settings);

  std::unique_ptr<platform::Window> window = platform::Window::Create("Ragnarok", settings);
  if (!window) {
    SDL_Log("no window: %s", SDL_GetError());
    return EXIT_FAILURE;
  }
  if (!window->has_renderer())
    SDL_Log("no renderer: %s; the loop runs without drawing", SDL_GetError());
  client.window = window.get();
  client.timer.SetFrameRate(60, platform::TimeMs());

  game::ModeSwitcher switcher(
      [&client](game::ModeId id, game::ModeSwitcher& s) -> std::unique_ptr<game::Mode> {
        switch (id) {
          case game::ModeId::Login: return std::make_unique<game::LoginMode>(s, client);
          case game::ModeId::Game: return std::make_unique<game::GameMode>(s, client);
        }
        return nullptr;
      });
  switcher.Run(game::ModeId::Login, "login.rsw");

  SDL_Log("quit after %llu frames", static_cast<unsigned long long>(client.frames));
  return EXIT_SUCCESS;
}
