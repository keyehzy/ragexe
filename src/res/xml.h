#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ragexe::res {

class DataFiles;

// An element as the client's XmlElement holds it (docs/formats/xml.md): its name, the
// character data directly inside it, all of it run together, and its children in order.
struct XmlElement {
  std::string name;
  std::string text;
  std::vector<XmlElement> children;

  // FindChild (004e6480): the first child with this name, or null.
  const XmlElement* FindChild(std::string_view child) const;
};

// The name XmlDocument's constructor (004c95b0) gives the root it parses into.
inline constexpr std::string_view kXmlDocumentName = "XMLDocument";

// XmlDocument::Parse (004e6590) into document, a root named kXmlDocumentName. The client's
// Parse succeeds when the root then has a first child. Fails where the client reads past
// the data: a tag, comment or entity left open, or a '<' or '&' too near the end.
bool ParseXml(std::span<const std::uint8_t> data, XmlElement& document, std::string* error);

// ClientState::method_005a8aa0: "PetTalkTable.xml" into the XmlDocument at +0xc60. The open
// is not checked, so a missing file parses as empty.
inline constexpr std::string_view kPetTalkTable = "PetTalkTable.xml";
bool LoadPetTalkTable(const DataFiles& files, XmlElement& document, std::string* error);

}  // namespace ragexe::res
