cmake_minimum_required(VERSION 3.25)

project(ragexe-recomp
  VERSION 0.0.0
  DESCRIPTION "A clean-room reimplementation of the 2003 Ragnarok Online client"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RAGEXE_BUILD_TESTS "Build the Catch2 unit tests" ON)
option(RAGEXE_SANITIZE "Build with AddressSanitizer (and UndefinedBehaviorSanitizer where the compiler has it)" OFF)

# Warnings and sanitizers for our own targets only, not for the third-party libraries.
add_library(ragexe_options INTERFACE)
if(MSVC)
  target_compile_options(ragexe_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus)
  if(RAGEXE_SANITIZE)
    # MSVC has AddressSanitizer only. It cannot be combined with the /RTC1 runtime
    # checks of Debug builds, and the container annotations must be off because the
    # third-party libraries (static SDL3 and Catch2) are built without it.
    target_compile_options(ragexe_options INTERFACE /fsanitize=address)
    target_compile_definitions(ragexe_options INTERFACE _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
  endif()
else()
  target_compile_options(ragexe_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
  if(RAGEXE_SANITIZE)
    target_compile_options(ragexe_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(ragexe_options INTERFACE -fsanitize=address,undefined)
  endif()
endif()

include(cmake/dependencies.cmake)

add_subdirectory(src)

if(RAGEXE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
endif()
