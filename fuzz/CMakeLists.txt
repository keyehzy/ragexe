# Fuzz targets: libFuzzer binaries with RAGEXE_FUZZ, otherwise the same targets linked with
# replay_main.cpp, which runs them over input files. Either way ctest replays the seeds and
# the saved regressions (with -runs=0, which makes a libFuzzer binary only run its inputs).

add_executable(make_grf_seeds make_grf_seeds.cpp)
target_include_directories(make_grf_seeds PRIVATE ${PROJECT_SOURCE_DIR}/tests)
target_link_libraries(make_grf_seeds PRIVATE ragexe_res ragexe_stb_image ZLIB::ZLIB ragexe_options)

set(RAGEXE_FUZZ_TARGETS grf_table grf_archive res_name_table bitmap sprite act gat gnd rsw rsm rsx imf rma str ebm)
foreach(target IN LISTS RAGEXE_FUZZ_TARGETS)
  add_executable(${target}_fuzz ${target}_fuzz.cpp)
  target_link_libraries(${target}_fuzz PRIVATE ragexe_res ragexe_options)
  if(RAGEXE_FUZZ)
    target_link_options(${target}_fuzz PRIVATE -fsanitize=fuzzer)
  else()
    target_sources(${target}_fuzz PRIVATE replay_main.cpp)
  endif()
endforeach()

if(RAGEXE_BUILD_TESTS)
  set(seeds "${CMAKE_CURRENT_BINARY_DIR}/seeds")
  add_test(NAME fuzz_seeds COMMAND make_grf_seeds "${seeds}")
  set_tests_properties(fuzz_seeds PROPERTIES FIXTURES_SETUP fuzz_seeds)
  foreach(target IN LISTS RAGEXE_FUZZ_TARGETS)
    add_test(NAME ${target}_fuzz_replay
      COMMAND ${target}_fuzz -runs=0 "${seeds}/${target}"
        "${CMAKE_CURRENT_SOURCE_DIR}/regressions/${target}")
    set_tests_properties(${target}_fuzz_replay PROPERTIES FIXTURES_REQUIRED fuzz_seeds)
  endforeach()
endif()
