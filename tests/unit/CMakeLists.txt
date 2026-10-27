add_executable(ragexe_tests
  act_test.cpp
  bitmap_test.cpp
  frame_timer_test.cpp
  gat_test.cpp
  grf_test.cpp
  resource_test.cpp
  mode_switcher_test.cpp
  palette_test.cpp
  settings_test.cpp
  sprite_test.cpp
  windows_no_dialogs.cpp)
target_include_directories(ragexe_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(ragexe_tests PRIVATE ragexe_game ragexe_res ragexe_stb_image ZLIB::ZLIB Catch2::Catch2WithMain ragexe_options)

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
  # 30 images of each kind against the original decoders in the emulation oracle
  # (tools/bitmap_oracle.py, which needs the analysis virtual environment).
  set(RAGEXE_PYTHON "${PROJECT_SOURCE_DIR}/.venv/bin/python")
  if(EXISTS "${RAGEXE_PYTHON}" AND EXISTS "${PROJECT_SOURCE_DIR}/original/client/Ragexe.exe")
    add_test(NAME bitmap_oracle_sample
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/bitmap_oracle.py"
        --sample 30 --ragres $<TARGET_FILE:ragres>
        --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # 20 sprites at every detail level, against the original loader (tools/sprite_oracle.py).
    add_test(NAME sprite_oracle_sample
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/sprite_oracle.py"
        --sample 20 --scales 00,11,10,01 --ragres $<TARGET_FILE:ragres>
        --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # 40 actions at every detail level, against the original loader (tools/act_oracle.py).
    add_test(NAME act_oracle_sample
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/act_oracle.py"
        --sample 40 --scales 00,11,10,01 --ragres $<TARGET_FILE:ragres>
        --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # Every attribute map, against the original loader (tools/gat_oracle.py).
    add_test(NAME gat_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/gat_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # Every palette, against the original conversion (tools/palette_oracle.py).
    add_test(NAME palette_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/palette_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
  endif()
endif()
