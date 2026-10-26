// Runs a fuzz target over files and directories of inputs, for compilers without
// libFuzzer: the same command line as a libFuzzer binary run with -runs=0. Arguments
// starting with '-' are libFuzzer options and are ignored.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

void RunFile(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                        std::istreambuf_iterator<char>());
  LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t inputs = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.starts_with("-")) continue;
    const std::filesystem::path path(arg);
    if (std::filesystem::is_directory(path)) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (!entry.is_regular_file()) continue;
        RunFile(entry.path());
        ++inputs;
      }
    } else {
      RunFile(path);
      ++inputs;
    }
  }
  std::printf("ran %zu inputs\n", inputs);
  return 0;
}
