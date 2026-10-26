#include "res/data_path.h"

namespace ragexe::res {

std::string MakeDataPath(std::string_view name) {
  std::string path = "data\\";
  path += name;
  // The prefix puts any ':' at index 5 or later, so the character before it exists.
  if (const std::size_t colon = path.find(':'); colon != std::string::npos) {
    path.erase(0, colon - 1);
  }
  for (std::size_t slashes = path.find("//"); slashes != std::string::npos;
       slashes = path.find("//")) {
    path.erase(slashes, 1);
  }
  return path;
}

}  // namespace ragexe::res
