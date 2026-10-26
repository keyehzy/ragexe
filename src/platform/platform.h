#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "platform/settings.h"

struct SDL_Window;
struct SDL_Renderer;

namespace ragexe::platform {

// SDL for the lifetime of the object: video and events. Check ok() after constructing;
// error() then says why it failed.
class Platform {
 public:
  Platform();
  ~Platform();
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }

 private:
  bool ok_ = false;
  std::string error_;
};

// Milliseconds since start-up as a 32-bit value that wraps, the way the original uses
// timeGetTime for every clock: the frame timer, animation, fades, timeouts.
std::uint32_t TimeMs();

// The per-user directory for settings and saved files (the registry and the files the
// original writes next to the executable). Created if missing; empty on failure.
std::filesystem::path UserDir();

// The window events the client acts on so far.
struct Event {
  enum class Type { Quit, FocusGained, FocusLost, KeyDown, KeyUp, TextInput };
  Type type;
  std::int32_t key = 0;  // an SDL_Keycode for KeyDown and KeyUp
  std::string text;      // UTF-8, for TextInput
};

// Takes every pending event off the queue, the way PumpMessages empties the Windows
// message queue at the start of each frame.
void PumpEvents(const std::function<void(const Event&)>& handle);

class Window {
 public:
  // nullptr when SDL cannot make the window; the renderer is optional (a window without
  // one still runs the loop, which is what the dummy video driver gives in CI).
  static std::unique_ptr<Window> Create(const std::string& title, const Settings& settings);
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  // A stand-in for the renderer until the SDL_GPU backend exists: clear and present.
  void Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b);
  void Present();
  bool has_renderer() const { return renderer_ != nullptr; }

 private:
  Window(SDL_Window* window, SDL_Renderer* renderer) : window_(window), renderer_(renderer) {}
  SDL_Window* window_;
  SDL_Renderer* renderer_;
};

}  // namespace ragexe::platform
