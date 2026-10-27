add_executable(ragexe_tests
  act_test.cpp
  bitmap_test.cpp
  client_tables_test.cpp
  ebm_test.cpp
  frame_timer_test.cpp
  gat_test.cpp
  gnd_test.cpp
  grf_test.cpp
  imf_test.cpp
  item_tables_test.cpp
  resource_test.cpp
  rma_test.cpp
  rsm_test.cpp
  rsw_test.cpp
  rsx_test.cpp
  mode_switcher_test.cpp
  palette_test.cpp
  settings_test.cpp
  sprite_test.cpp
  str_test.cpp
  wav_test.cpp
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
    # Every ground, against the original loader (tools/gnd_oracle.py).
    add_test(NAME gnd_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/gnd_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # Every world, against the original loader (tools/rsw_oracle.py).
    add_test(NAME rsw_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/rsw_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # Every model, against the original loader (tools/rsm_oracle.py).
    add_test(NAME rsm_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/rsm_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # The animated model and synthetic ones, against the original loader
    # (tools/rsx_oracle.py).
    add_test(NAME rsx_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/rsx_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # Every .imf, against the original loader (tools/imf_oracle.py).
    add_test(NAME imf_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/imf_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # Synthetic .rma files, against the original loader (tools/rma_oracle.py).
    add_test(NAME rma_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/rma_oracle.py"
        --ragres $<TARGET_FILE:ragres>)
    # Every effect, against the original loader (tools/str_oracle.py).
    add_test(NAME str_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/str_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # Synthetic emblems and mutated streams, against the original loader and cps.dll
    # (tools/ebm_oracle.py).
    add_test(NAME ebm_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/ebm_oracle.py"
        --ragres $<TARGET_FILE:ragres>)
    # Every wave and synthetic ones, against the original loader and Mss32.dll
    # (tools/wav_oracle.py).
    add_test(NAME wav_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/wav_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
    # The item and card tables of data.grf and its patches, and synthetic ones, against
    # the original loader (tools/item_tables_oracle.py).
    add_test(NAME item_tables_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/item_tables_oracle.py"
        --ragres $<TARGET_FILE:ragres> --install "${RAGEXE_INSTALL_DIR}")
    # ClientState's name-pair tables, real and synthetic, against its loaders
    # (tools/name_pairs_oracle.py).
    add_test(NAME name_pairs_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/name_pairs_oracle.py"
        --ragres $<TARGET_FILE:ragres> --install "${RAGEXE_INSTALL_DIR}")
    # Every palette, against the original conversion (tools/palette_oracle.py).
    add_test(NAME palette_oracle
      COMMAND "${RAGEXE_PYTHON}" -I "${PROJECT_SOURCE_DIR}/tools/palette_oracle.py"
        --ragres $<TARGET_FILE:ragres> --archive "${RAGEXE_INSTALL_DIR}/data.grf")
  endif()
endif()
