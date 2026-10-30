# Third-party libraries, pinned by release archive and SHA-256. Each one is taken from an
# installed package when find_package finds it (Homebrew's SDL3, for example); otherwise
# FetchContent downloads the pinned release and builds it with the project. Configure
# with -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER to always build the pinned versions,
# as CI does.
#
# A library is added here by the milestone that first uses it (docs/PLAN.md section 5:
# miniaudio comes later). glslang and SPIRV-Cross are build tools only.

include(FetchContent)

# SDL3: window, events, timing, input; later SDL_GPU and audio devices. Built static, so
# the executables need no SDL3 library next to them.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
  URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  FIND_PACKAGE_ARGS 3.2 CONFIG)
FetchContent_MakeAvailable(SDL3)

# zlib: GRF entries and emblems, in place of the original's cps.dll (zlib 1.0.4, see
# docs/formats/grf.md). An installed zlib comes through CMake's FindZLIB module as
# ZLIB::ZLIB; a fetched one is built static and given the same name.
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ZLIB
  URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
  URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  FIND_PACKAGE_ARGS 1.2)
FetchContent_MakeAvailable(ZLIB)
if(NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif()

# stb_image: JPEG decoding, in place of the original's ijl15.dll (docs/formats/bitmap.md).
# A single header with no releases, pinned to the last commit that changed it (v2.30);
# src/res/stb_image.c compiles it.
FetchContent_Declare(stb
  URL https://github.com/nothings/stb/archive/013ac3beddff3dbffafd5177e7972067cd2b5083.tar.gz
  URL_HASH SHA256=b01aa93e1a968aed55f43e072c98ee401d2f20e897aabdb1a166c7166886ed11
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(stb)

# FreeType: the text's glyphs, in place of GDI (docs/render/text.md#the-glyphs). Always
# built from the pinned release, as the shader tools are, so every machine draws the same
# pixels; without the optional libraries it can use.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
  URL https://downloads.sourceforge.net/project/freetype/freetype2/2.15.0/freetype-2.15.0.tar.xz
  URL_HASH SHA256=93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  EXCLUDE_FROM_ALL
  SYSTEM)
FetchContent_MakeAvailable(freetype)

# Liberation Sans, metric-compatible with Arial, the face the iRO client asks for. Its
# regular and bold are embedded in the glyph library (SIL Open Font License 1.1).
FetchContent_Declare(liberation_fonts
  URL https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz
  URL_HASH SHA256=7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0
  DOWNLOAD_EXTRACT_TIMESTAMP ON)
FetchContent_MakeAvailable(liberation_fonts)

# glslang and SPIRV-Cross: the renderer's shaders are written once in GLSL and compiled
# during the build, to SPIR-V for SDL_GPU's Vulkan backend and on to MSL for its Metal
# one (src/render/shaders). Only their command-line tools are used; both are always
# built from the pinned releases, so every machine compiles the same shaders.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
  URL https://github.com/KhronosGroup/glslang/archive/refs/tags/16.6.0.tar.gz
  URL_HASH SHA256=9c09b901149c729df745057dafa815278aaa101b84d2b6e14f16a42de52f97f2
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(glslang)

set(SPIRV_CROSS_CLI ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(spirv_cross
  URL https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
  URL_HASH SHA256=f708982e88b763ef5b0394ed468fc2c3628f68cdab89f2e7a946cd628c04e721
  DOWNLOAD_EXTRACT_TIMESTAMP ON
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(spirv_cross)

if(RAGEXE_BUILD_TESTS)
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
    URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    FIND_PACKAGE_ARGS 3 CONFIG)
  FetchContent_MakeAvailable(Catch2)
  # catch_discover_tests: an installed package puts its module on the path; a fetched
  # one keeps it in its source tree.
  if(catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
  endif()
endif()
