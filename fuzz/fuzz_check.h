#pragma once

#include <cstdio>
#include <cstdlib>

namespace ragexe::fuzz {

// A violated invariant aborts, which the fuzzer reports as a crash with its input.
inline void Check(bool ok, const char* condition, const char* file, int line) {
  if (ok) return;
  std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, condition);
  std::abort();
}

}  // namespace ragexe::fuzz

#define FUZZ_CHECK(condition) ::ragexe::fuzz::Check((condition), #condition, __FILE__, __LINE__)
