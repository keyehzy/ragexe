#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "res/hashed_file_name.h"

namespace ragexe::res {

// One entry of a GRF table, with the sizes decoded (docs/formats/grf.md).
struct GrfEntry {
  std::string name;  // as stored: CP949, '\'-separated, original case
  HashedFileName key;
  std::int32_t compressed_size = 0;
  std::uint32_t stored_size = 0;
  std::uint32_t size = 0;
  std::uint32_t offset = 0;  // from the end of the header
  std::uint8_t flags = 0;

  // The only flag value the client tests: zlib-compressed and encoded.
  bool compressed() const { return flags == 1; }
};

// A "Master of Magic" archive, versions 0x100 to 0x103: CGPak (004dd820) without the
// loose-file fallback, which belongs to the resource lookup.
class GrfArchive {
 public:
  static constexpr std::uint32_t kHeaderSize = 0x2e;

  static std::unique_ptr<GrfArchive> Open(const std::filesystem::path& path, std::string* error);

  std::uint32_t version() const { return version_; }
  // In table order.
  const std::vector<GrfEntry>& entries() const { return entries_; }

  // The entry stored under this name, compared as HashedFileName does; nullptr if none.
  const GrfEntry* Find(std::string_view name) const;
  // Names that occur more than once in the table, which the client cannot tell apart.
  std::vector<std::string> DuplicateNames() const;

  // The entry's bytes as 004de830 produces them: decoded and, when compressed, inflated.
  // A compressed stream that breaks off still gives the recorded size, zero-filled past
  // the break, as in the client; *damaged tells the caller.
  std::optional<std::vector<std::uint8_t>> Read(const GrfEntry& entry, std::string* error,
                                                bool* damaged = nullptr) const;

 private:
  GrfArchive() = default;

  bool ReadAt(std::uint64_t offset, std::uint8_t* out, std::size_t n) const;

  mutable std::mutex mutex_;
  mutable std::ifstream file_;
  std::uint64_t file_size_ = 0;
  std::uint32_t version_ = 0;
  std::vector<GrfEntry> entries_;
  std::vector<std::uint32_t> sorted_;  // indexes into entries_, ordered by key
};

// Parses a table (the bytes from the table offset to the end of the file) into entries.
// Exposed for the tests and the fuzzer.
std::optional<std::vector<GrfEntry>> ParseGrfTable(std::uint32_t version, std::uint32_t count,
                                                   std::vector<std::uint8_t> table,
                                                   std::string* error);

}  // namespace ragexe::res
