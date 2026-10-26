add_executable(ragexe_tests
  frame_timer_test.cpp
  grf_test.cpp
  mode_switcher_test.cpp
  settings_test.cpp)
target_link_libraries(ragexe_tests PRIVATE ragexe_game ragexe_res ZLIB::ZLIB Catch2::Catch2WithMain ragexe_options)

include(Catch)
catch_discover_tests(ragexe_tests DISCOVERY_MODE PRE_TEST)

# The client itself, unattended: login mode, the move to the game mode, then quit. The
# test environment selects SDL's dummy video driver (CMakePresets.json), so it runs
# without a display.
add_test(NAME client_smoke
  COMMAND ragexe --frames 180 --auto-login 60 --map prontera.rsw)
set_tests_properties(client_smoke PROPERTIES
  ENVIRONMENT "SDL_VIDEO_DRIVER=dummy"
  PASS_REGULAR_EXPRESSION "game mode: init \\(prontera.rsw\\).*quit after 180 frames")

# Every entry of the original archives, when they are present (original/ is not in the
# repository, so CI skips this).
set(RAGEXE_INSTALL_DIR "${PROJECT_SOURCE_DIR}/original/install")
file(GLOB RAGEXE_ARCHIVES "${RAGEXE_INSTALL_DIR}/data.grf" "${RAGEXE_INSTALL_DIR}/*.gpf")
if(RAGEXE_ARCHIVES)
  add_test(NAME grf_check_original COMMAND ragres check ${RAGEXE_ARCHIVES})
endif()
