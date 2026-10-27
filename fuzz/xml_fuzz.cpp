// The XML parser on arbitrary documents.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "fuzz_check.h"
#include "res/xml.h"

namespace {

void CheckNames(const ragexe::res::XmlElement& element) {
  for (const auto& child : element.children) {
    for (const char c : child.name) FUZZ_CHECK(c != ' ' && c != '\t' && c != '>' && c != '/');
    CheckNames(child);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  ragexe::res::XmlElement document;
  std::string error;
  if (!ragexe::res::ParseXml(std::span<const std::uint8_t>(data, size), document, &error)) {
    FUZZ_CHECK(!error.empty());
    return 0;
  }
  FUZZ_CHECK(document.name == ragexe::res::kXmlDocumentName);
  CheckNames(document);
  return 0;
}
