#include "res/tips.h"

#include <fstream>
#include <iterator>

#include "res/data_files.h"

namespace ragexe::res {

namespace {

constexpr std::uint8_t kCtrlZ = 0x1a;

}  // namespace

std::string_view TipFileName(std::int32_t service_type) {
  if (service_type == 1) return "tipOfTheday_e.txt";
  if (service_type == 2) return "tipOfTheday_j.txt";
  return "tipOfTheday.txt";
}

std::vector<std::uint8_t> ReadAsText(std::span<const std::uint8_t> file) {
  std::vector<std::uint8_t> out;
  out.reserve(file.size());
  for (std::size_t i = 0; i < file.size(); ++i) {
    if (file[i] == kCtrlZ) break;
    if (file[i] == '\r' && i + 1 < file.size() && file[i + 1] == '\n') continue;
    out.push_back(file[i]);
  }
  return out;
}

std::vector<Tip> ParseTips(std::span<const std::uint8_t> text) {
  std::vector<Tip> tips;
  Tip lines;
  std::size_t start = 0;
  // Each line is read as a C string: a NUL in the file ends it.
  const auto line = [&](std::size_t end) {
    std::size_t stop = start;
    while (stop < end && text[stop] != 0) ++stop;
    return std::string(reinterpret_cast<const char*>(text.data()) + start, stop - start);
  };
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      lines.push_back(line(i));
      start = i + 1;
    } else if (text[i] == '#') {
      lines.push_back(line(i));
      tips.push_back(std::move(lines));
      lines.clear();
      start = i + 1;
    }
  }
  return tips;
}

bool LoadTips(const DataFiles& files, std::int32_t service_type, std::vector<Tip>& out) {
  const auto path = files.ResolveLoose(TipFileName(service_type));
  if (!path) return false;
  std::ifstream f(*path, std::ios::binary);
  if (!f) return false;
  const std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(f),
                                       std::istreambuf_iterator<char>()};
  // The client reads into a buffer of the file's size; what text mode leaves unread at its
  // end is uninitialized memory, taken here to hold neither '\n' nor '#'.
  const std::vector<Tip> tips = ParseTips(ReadAsText(file));
  out.insert(out.end(), tips.begin(), tips.end());
  return true;
}

const Tip* GetTip(const std::vector<Tip>& tips, int index) {
  if (tips.empty()) return nullptr;
  if (index < 0 || static_cast<std::size_t>(index) >= tips.size()) return &tips[0];
  return &tips[static_cast<std::size_t>(index)];
}

}  // namespace ragexe::res
