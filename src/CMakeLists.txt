# One static library per layer, in dependency order, and the client executable.

add_library(ragexe_core STATIC
  core/frame_timer.cpp)
target_include_directories(ragexe_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ragexe_core PRIVATE ragexe_options)

add_library(ragexe_platform STATIC
  platform/platform.cpp
  platform/settings.cpp)
target_include_directories(ragexe_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ragexe_platform PUBLIC SDL3::SDL3 PRIVATE ragexe_options)

# stb_image, JPEG only, compiled once and without our warnings.
add_library(ragexe_stb_image STATIC res/stb_image.c)
target_include_directories(ragexe_stb_image SYSTEM PUBLIC ${stb_SOURCE_DIR})
if(NOT MSVC)
  # Its integer IDCT can overflow int on corrupt coefficients and then clamps the result;
  # -fwrapv makes that overflow defined (it wraps, as on MSVC) instead of undefined.
  target_compile_options(ragexe_stb_image PRIVATE -fwrapv)
endif()
if(RAGEXE_FUZZ)
  # Instrumented like our code, so that the bitmap fuzzer sees inside the decoder.
  target_compile_options(ragexe_stb_image PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
endif()

add_library(ragexe_res STATIC
  res/act.cpp
  res/bitmap.cpp
  res/data_file.cpp
  res/data_files.cpp
  res/data_path.cpp
  res/gat.cpp
  res/gnd.cpp
  res/imf.cpp
  res/rsm.cpp
  res/rsw.cpp
  res/rsx.cpp
  res/grf.cpp
  res/grf_cipher.cpp
  res/hashed_file_name.cpp
  res/palette.cpp
  res/resource.cpp
  res/rma.cpp
  res/sprite.cpp
  res/str.cpp)
target_include_directories(ragexe_res PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ragexe_res PRIVATE ZLIB::ZLIB ragexe_stb_image ragexe_options)

add_library(ragexe_game STATIC
  game/client.cpp
  game/game_mode.cpp
  game/login_mode.cpp
  game/mode.cpp
  game/mode_switcher.cpp)
target_include_directories(ragexe_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ragexe_game PUBLIC ragexe_core ragexe_platform PRIVATE ragexe_options)

add_executable(ragexe app/main.cpp)
target_link_libraries(ragexe PRIVATE ragexe_game ragexe_options)

add_executable(ragres ragres/main.cpp)
target_link_libraries(ragres PRIVATE ragexe_res ragexe_options)
