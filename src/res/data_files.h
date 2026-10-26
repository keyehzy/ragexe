#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "res/grf.h"

namespace ragexe::res {

// Where DataFile reads from (docs/formats/resources.md): the archive, then a loose file
// under the install directory, as CGPak::LoadFile (004dd9a0) does. Paths are archive
// paths such as "data\texture\x.bmp"; loose files are matched without regard to A-Z case
// on every system.
class DataFiles {
 public:
  explicit DataFiles(std::filesystem::path install_dir,
                     std::unique_ptr<GrfArchive> archive = nullptr);

  // The bytes of the path; nullopt when neither the archive nor the disk has it.
  std::optional<std::vector<std::uint8_t>> Load(std::string_view path) const;
  bool Exists(std::string_view path) const;

  // The loose file a path names, if it exists.
  std::optional<std::filesystem::path> ResolveLoose(std::string_view path) const;

  const GrfArchive* archive() const { return archive_.get(); }
  const std::filesystem::path& install_dir() const { return install_dir_; }

 private:
  std::filesystem::path install_dir_;
  std::unique_ptr<GrfArchive> archive_;
};

}  // namespace ragexe::res
