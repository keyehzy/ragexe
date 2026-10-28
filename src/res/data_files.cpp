#include "res/data_files.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>
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

// The entries of dir named like component, ignoring A-Z case, the exact name first and the
// others in sorted order.
std::vector<std::filesystem::path> FindEntries(const std::filesystem::path& dir,
                                               std::string_view component) {
  std::error_code ec;
  std::vector<std::filesystem::path> matches;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (SameIgnoringAsciiCase(entry.path().filename().string(), component)) {
      matches.push_back(entry.path());
    }
  }
  std::sort(matches.begin(), matches.end());
  const auto exact = std::find_if(matches.begin(), matches.end(), [&](const auto& p) {
    return p.filename().string() == component;
  });
  if (exact != matches.end()) std::rotate(matches.begin(), exact, exact + 1);
  return matches;
}

// Depth-first through every case-insensitive match, so that of "Texture/" and "texture/"
// the one holding the file is found.
std::optional<std::filesystem::path> Resolve(const std::filesystem::path& dir,
                                             std::span<const std::string> parts) {
  if (parts.empty()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(dir, ec)) return dir;
    return std::nullopt;
  }
  for (const auto& entry : FindEntries(dir, parts.front())) {
    if (auto found = Resolve(entry, parts.subspan(1))) return found;
  }
  return std::nullopt;
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
  return Resolve(install_dir_, parts);
}

std::optional<std::vector<std::uint8_t>> DataFiles::Load(std::string_view path) const {
  const auto read = [&](const GrfArchive* archive) -> std::optional<std::vector<std::uint8_t>> {
    if (archive == nullptr) return std::nullopt;
    const GrfEntry* entry = archive->Find(path);
    if (entry == nullptr) return std::nullopt;
    std::string error;
    return archive->Read(*entry, &error);
  };
  if (auto data = read(archive_.get())) return data;
  for (const GrfArchive* layer : layers_) {
    if (auto data = read(layer)) return data;
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
  for (const GrfArchive* layer : layers_) {
    if (layer->Find(path) != nullptr) return true;
  }
  return ResolveLoose(path).has_value();
}

}  // namespace ragexe::res
