// clientinfo.xml on arbitrary documents: the settings, each of the first connections, and
// the connection list.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "fuzz_check.h"
#include "res/client_info.h"
#include "res/xml.h"

namespace {

// Either the built-in tables, as they are, or the "aid" list twice, sorted.
void CheckAdmins(const ragexe::res::ClientInfo& info) {
  if (info.admins_a.size() == 32 && info.admins_b.size() == 39) return;
  FUZZ_CHECK(info.admins_a == info.admins_b);
  for (std::size_t k = 1; k < info.admins_a.size(); ++k) {
    FUZZ_CHECK(static_cast<std::uint32_t>(info.admins_a[k - 1]) <=
               static_cast<std::uint32_t>(info.admins_a[k]));
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::XmlElement document;
  std::string error;
  if (!ragexe::res::ParseXml(std::span<const std::uint8_t>(data, size), document, &error)) return 0;
  ragexe::res::ClientInfo info;
  if (!ragexe::res::ReadClientInfo(document, info, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(!info.executable.empty());
  for (int index = 0; index < 4; ++index) {
    if (!ragexe::res::SelectConnection(document, index, info, &error)) break;
    CheckAdmins(info);
  }
  ragexe::res::ListConnections(document);
  return 0;
}
