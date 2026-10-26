#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "game/mode.h"
#include "game/mode_switcher.h"

using ragexe::game::kMsgQuit;
using ragexe::game::Mode;
using ragexe::game::ModeId;
using ragexe::game::ModeSwitcher;

namespace {

// A mode that records its slot calls and runs a script: on frame N of its life it does
// whatever the script says.
struct Script {
  std::vector<std::string> log;
  int login_frames_before_game = -1;
  int game_frames_before_map_change = -1;
  int game_frames_before_quit = -1;
  int login_state_request_at = -1;
};

class ScriptedMode : public Mode {
 public:
  ScriptedMode(ModeSwitcher& s, ModeId id, Script& script) : Mode(s), id_(id), script_(script) {}
  ~ScriptedMode() override { script_.log.push_back(Name() + " destroyed"); }

  void OnInit(std::string_view map) override { script_.log.push_back(Name() + " init " + std::string(map)); }
  void OnExit() override { script_.log.push_back(Name() + " exit"); }
  void OnChangeState(int state) override {
    script_.log.push_back(Name() + " state " + std::to_string(state) + " after frame " + std::to_string(frame_));
  }
  void OnFrame() override {
    ++frame_;
    if (id_ == ModeId::Login) {
      if (frame_ == script_.login_state_request_at) RequestState(3);
      if (frame_ == script_.login_frames_before_game) switcher_.RequestMode(ModeId::Game, "prontera.rsw");
    } else {
      if (frame_ == script_.game_frames_before_map_change) switcher_.RequestMode(ModeId::Game, "yuno.rsw");
      if (frame_ == script_.game_frames_before_quit) SendMsg(kMsgQuit);
    }
  }

 private:
  std::string Name() const { return id_ == ModeId::Login ? "login" : "game"; }
  ModeId id_;
  Script& script_;
  int frame_ = 0;
};

ModeSwitcher MakeSwitcher(Script& script) {
  return ModeSwitcher([&script](ModeId id, ModeSwitcher& s) -> std::unique_ptr<Mode> {
    return std::make_unique<ScriptedMode>(s, id, script);
  });
}

}  // namespace

TEST_CASE("Login, game, a map change that rebuilds the game mode, then quit") {
  Script script;
  script.login_frames_before_game = 3;
  script.game_frames_before_map_change = 2;
  script.game_frames_before_quit = -1;
  int game_modes = 0;
  ModeSwitcher switcher([&](ModeId id, ModeSwitcher& s) -> std::unique_ptr<Mode> {
    if (id == ModeId::Game && ++game_modes == 2) script.game_frames_before_quit = 1;
    return std::make_unique<ScriptedMode>(s, id, script);
  });
  switcher.Run(ModeId::Login, "login.rsw");

  const std::vector<std::string> expected = {
      "login init login.rsw", "login exit", "login destroyed",
      "game init prontera.rsw", "game exit", "game destroyed",
      "game init yuno.rsw", "game exit", "game destroyed",
  };
  CHECK(script.log == expected);
  CHECK(switcher.quitting());
  CHECK(switcher.current() == nullptr);
}

TEST_CASE("A requested state becomes current at the top of the next frame") {
  Script script;
  script.login_state_request_at = 2;
  script.login_frames_before_game = 5;
  script.game_frames_before_quit = 1;
  ModeSwitcher switcher = MakeSwitcher(script);
  switcher.Run(ModeId::Login, "login.rsw");
  CHECK(script.log.at(1) == "login state 3 after frame 2");
}

TEST_CASE("RequestMode ends the current mode after the frame that asked") {
  Script script;
  script.login_frames_before_game = 1;
  script.game_frames_before_quit = 1;
  ModeSwitcher switcher = MakeSwitcher(script);
  switcher.Run(ModeId::Login, "login.rsw");
  CHECK(script.log.front() == "login init login.rsw");
  CHECK(script.log.at(1) == "login exit");
}

TEST_CASE("Quit before Run leaves no mode created") {
  Script script;
  ModeSwitcher switcher = MakeSwitcher(script);
  switcher.Quit();
  switcher.Run(ModeId::Login, "login.rsw");
  CHECK(script.log.empty());
}
