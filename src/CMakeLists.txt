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
