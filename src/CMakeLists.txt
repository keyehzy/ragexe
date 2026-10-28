# One static library per layer, in dependency order, and the client executable.

add_library(ragexe_core STATIC
  core/angles.cpp
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
  res/c_runtime.cpp
  res/client_info.cpp
  res/client_tables.cpp
  res/data_file.cpp
  res/data_files.cpp
  res/data_path.cpp
  res/ebm.cpp
  res/gat.cpp
  res/gnd.cpp
  res/imf.cpp
  res/item_tables.cpp
  res/manner.cpp
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
  res/str.cpp
  res/tips.cpp
  res/wav.cpp
  res/xml.cpp)
target_include_directories(ragexe_res PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ragexe_res PRIVATE ZLIB::ZLIB ragexe_stb_image ragexe_options)

# The renderer: the original's Direct3D 7 state, and in time its SDL_GPU replacement.
add_library(ragexe_render STATIC
  render/actor.cpp
  render/camera.cpp
  render/device.cpp
  render/effect.cpp
  render/fixed_function.cpp
  render/fog.cpp
  render/ground.cpp
  render/matrix.cpp
  render/model.cpp
  render/player.cpp
  render/player_names.cpp
  render/scene.cpp
  render/scene_graph.cpp
  render/texture.cpp)
target_include_directories(ragexe_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ragexe_render PUBLIC ragexe_core ragexe_res PRIVATE ragexe_options)

# The renderer's shaders: GLSL compiled to SPIR-V by glslang and on to MSL by SPIRV-Cross
# during the build, then embedded in a generated header (cmake/embed_files.cmake).
set(RAGEXE_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(RAGEXE_SHADER_FILES "")
set(RAGEXE_SHADER_OUTPUTS "")
foreach(stage IN ITEMS vert frag)
  set(source "${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/scene.${stage}")
  set(spirv "${RAGEXE_SHADER_DIR}/scene.${stage}.spv")
  set(metal "${RAGEXE_SHADER_DIR}/scene.${stage}.msl")
  add_custom_command(OUTPUT "${spirv}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${RAGEXE_SHADER_DIR}"
    COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.0 --quiet -o "${spirv}" "${source}"
    DEPENDS "${source}" glslang-standalone
    VERBATIM)
  add_custom_command(OUTPUT "${metal}"
    COMMAND $<TARGET_FILE:spirv-cross> "${spirv}" --msl --msl-version 20100 --msl-decoration-binding
      --output "${metal}"
    DEPENDS "${spirv}" spirv-cross
    VERBATIM)
  if(stage STREQUAL "vert")
    set(name "kSceneVert")
  else()
    set(name "kSceneFrag")
  endif()
  list(APPEND RAGEXE_SHADER_FILES "${name}Spirv=${spirv}" "${name}Msl=${metal}")
  list(APPEND RAGEXE_SHADER_OUTPUTS "${spirv}" "${metal}")
endforeach()
string(REPLACE ";" "|" RAGEXE_SHADER_FILES "${RAGEXE_SHADER_FILES}")
add_custom_command(OUTPUT "${RAGEXE_SHADER_DIR}/scene_shaders.h"
  COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${RAGEXE_SHADER_DIR}/scene_shaders.h"
    "-DFILES=${RAGEXE_SHADER_FILES}" -P "${PROJECT_SOURCE_DIR}/cmake/embed_files.cmake"
  DEPENDS ${RAGEXE_SHADER_OUTPUTS} "${PROJECT_SOURCE_DIR}/cmake/embed_files.cmake"
  VERBATIM)

# The SDL_GPU device that replays the scene's Direct3D 7 calls.
add_library(ragexe_gpu STATIC
  render/gpu_device.cpp
  "${RAGEXE_SHADER_DIR}/scene_shaders.h")
target_include_directories(ragexe_gpu PRIVATE "${RAGEXE_SHADER_DIR}")
target_link_libraries(ragexe_gpu PUBLIC ragexe_render SDL3::SDL3 PRIVATE ragexe_options)

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
target_link_libraries(ragres PRIVATE ragexe_render ragexe_res ragexe_options)

add_executable(ragview ragview/main.cpp)
target_include_directories(ragview SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(ragview PRIVATE ragexe_gpu ragexe_render ragexe_res ragexe_options)
