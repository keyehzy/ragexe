# Third-party libraries, pinned by release archive and SHA-256. Each one is taken from an
# installed package when find_package finds it (Homebrew's SDL3, for example); otherwise
# FetchContent downloads the pinned release and builds it with the project. Configure
# with -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER to always build the pinned versions,
# as CI does.
#
# A library is added here by the milestone that first uses it (docs/PLAN.md section 5:
# miniaudio, FreeType and stb come later).

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
