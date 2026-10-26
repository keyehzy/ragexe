#pragma once

#include <string_view>

namespace ragexe::game {

class ModeSwitcher;

// The mode numbers ModeSwitcher::Run creates (docs/architecture.md §3).
enum class ModeId : int { Login = 0, Game = 1 };

// Messages every mode handles through SendMsg (CMode::SendMsg).
inline constexpr int kMsgQuit = 2;
inline constexpr int kMsgScreenshot = 0x4b;

// CMode: the slots ModeSwitcher calls, in order OnInit, Run, OnExit, then destruction.
// Run is the same loop in both modes; the per-frame work is OnFrame.
class Mode {
 public:
  explicit Mode(ModeSwitcher& switcher) : switcher_(switcher) {}
  virtual ~Mode() = default;
  Mode(const Mode&) = delete;
  Mode& operator=(const Mode&) = delete;

  virtual void OnInit(std::string_view map) = 0;
  // Repeats while the run flag is set and the client is not quitting: a pending state
  // becomes current (OnChangeState), then one OnFrame.
  void Run();
  virtual void OnExit() = 0;
  virtual void OnFrame() = 0;
  virtual int SendMsg(int msg, int arg1 = 0, int arg2 = 0, int arg3 = 0);
  virtual void OnChangeState(int state);

  // The state changes at the top of the next Run iteration, not at once.
  void RequestState(int state) { pending_state_ = state; }
  void Stop() { run_ = false; }

  bool running() const { return run_; }
  int state() const { return state_; }
  int frames_in_state() const { return frames_in_state_; }

 protected:
  ModeSwitcher& switcher_;

 private:
  int state_ = 0;
  int frames_in_state_ = 0;
  int pending_state_ = -1;
  bool run_ = true;
};

}  // namespace ragexe::game
