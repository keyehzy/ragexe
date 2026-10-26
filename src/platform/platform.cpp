#include "platform/platform.h"

#include <SDL3/SDL.h>

namespace ragexe::platform {

Platform::Platform() {
  ok_ = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  if (!ok_) error_ = SDL_GetError();
}

Platform::~Platform() {
  if (ok_) SDL_Quit();
}

std::uint32_t TimeMs() { return static_cast<std::uint32_t>(SDL_GetTicks()); }

std::filesystem::path UserDir() {
  char* dir = SDL_GetPrefPath("ragexe-recomp", "client");
  if (dir == nullptr) return {};
  std::filesystem::path path(reinterpret_cast<const char8_t*>(dir));
  SDL_free(dir);
  return path;
}

void PumpEvents(const std::function<void(const Event&)>& handle) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        handle({Event::Type::Quit, 0, {}});
        break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        handle({Event::Type::FocusGained, 0, {}});
        break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        handle({Event::Type::FocusLost, 0, {}});
        break;
      case SDL_EVENT_KEY_DOWN:
        handle({Event::Type::KeyDown, static_cast<std::int32_t>(e.key.key), {}});
        break;
      case SDL_EVENT_KEY_UP:
        handle({Event::Type::KeyUp, static_cast<std::int32_t>(e.key.key), {}});
        break;
      case SDL_EVENT_TEXT_INPUT:
        handle({Event::Type::TextInput, 0, e.text.text});
        break;
      default:
        break;
    }
  }
}

std::unique_ptr<Window> Window::Create(const std::string& title, const Settings& settings) {
  SDL_WindowFlags flags = settings.fullscreen ? SDL_WINDOW_FULLSCREEN : 0;
  SDL_Window* window = SDL_CreateWindow(title.c_str(), settings.width, settings.height, flags);
  if (window == nullptr) return nullptr;
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer != nullptr) SDL_SetRenderVSync(renderer, 1);
  return std::unique_ptr<Window>(new Window(window, renderer));
}

Window::~Window() {
  if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
  SDL_DestroyWindow(window_);
}

void Window::Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  if (renderer_ == nullptr) return;
  SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
  SDL_RenderClear(renderer_);
}

void Window::Present() {
  if (renderer_ != nullptr) SDL_RenderPresent(renderer_);
}

}  // namespace ragexe::platform
