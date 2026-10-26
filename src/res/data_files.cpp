#include "res/data_files.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace ragexe::res {

namespace {

bool SameIgnoringAsciiCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    char x = a[i];
    char y = b[i];
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return true;
}

// The entry of dir named like component, ignoring A-Z case; the first in sorted order
// when several match.
std::optional<std::filesystem::path> FindEntry(const std::filesystem::path& dir,
                                               std::string_view component) {
  std::error_code ec;
  std::filesystem::path exact = dir / std::string(component);
  if (std::filesystem::exists(exact, ec)) return exact;
  std::vector<std::filesystem::path> matches;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (SameIgnoringAsciiCase(entry.path().filename().string(), component)) {
      matches.push_back(entry.path());
    }
  }
  if (matches.empty()) return std::nullopt;
  return *std::min_element(matches.begin(), matches.end());
}

}  // namespace

DataFiles::DataFiles(std::filesystem::path install_dir, std::unique_ptr<GrfArchive> archive)
    : install_dir_(std::move(install_dir)), archive_(std::move(archive)) {}

std::optional<std::filesystem::path> DataFiles::ResolveLoose(std::string_view path) const {
  if (path.find(':') != std::string_view::npos) return std::nullopt;
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find_first_of("\\/", start);
    if (end == std::string_view::npos) end = path.size();
    const std::string_view part = path.substr(start, end - start);
    if (part == "..") {
      if (parts.empty()) return std::nullopt;
      parts.pop_back();
    } else if (!part.empty() && part != ".") {
      parts.emplace_back(part);
    }
    start = end + 1;
  }
  if (parts.empty()) return std::nullopt;
  std::filesystem::path current = install_dir_;
  for (const std::string& part : parts) {
    auto next = FindEntry(current, part);
    if (!next) return std::nullopt;
    current = std::move(*next);
  }
  std::error_code ec;
  if (!std::filesystem::is_regular_file(current, ec)) return std::nullopt;
  return current;
}

std::optional<std::vector<std::uint8_t>> DataFiles::Load(std::string_view path) const {
  if (archive_ != nullptr) {
    if (const GrfEntry* entry = archive_->Find(path)) {
      std::string error;
      if (auto data = archive_->Read(*entry, &error)) return data;
    }
  }
  const auto loose = ResolveLoose(path);
  if (!loose) return std::nullopt;
  std::ifstream f(*loose, std::ios::binary);
  if (!f) return std::nullopt;
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
}

bool DataFiles::Exists(std::string_view path) const {
  if (archive_ != nullptr && archive_->Find(path) != nullptr) return true;
  return ResolveLoose(path).has_value();
}

}  // namespace ragexe::res
