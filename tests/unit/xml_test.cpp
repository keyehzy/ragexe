#include "res/xml.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "res/data_files.h"

using namespace ragexe::res;

namespace {

std::vector<std::uint8_t> Bytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

}  // namespace

TEST_CASE("XML: elements, text and the root the client parses into") {
  XmlElement document;
  std::string error;
  REQUIRE(ParseXml(Bytes("<?xml version=\"1.0\"?>\r\n<a k=\"v\">\r\n\t<b>x</b>\r\n\t<c/>tail</a>"),
                   document, &error));
  CHECK(document.name == "XMLDocument");
  REQUIRE(document.children.size() == 1);
  const XmlElement& a = document.children[0];
  CHECK(a.name == "a");
  // Line breaks are dropped; the tabs before the children stay in the parent's text.
  CHECK(a.text == "\t\ttail");
  REQUIRE(a.FindChild("b") != nullptr);
  CHECK(a.FindChild("b")->text == "x");
  CHECK(a.FindChild("c")->children.empty());
  CHECK(a.FindChild("d") == nullptr);
}

TEST_CASE("XML: entities, stray closing tags and the document's own") {
  XmlElement document;
  std::string error;
  REQUIRE(ParseXml(Bytes("<t>&lt;&gt;&amp;&quot;&ampx;</t>"), document, &error));
  CHECK(document.children[0].text == "<>&&");
  // A closing tag of another element is skipped.
  REQUIRE(ParseXml(Bytes("<a><b>1</c>2</b>3</a>"), document, &error));
  CHECK(document.children[0].children[0].text == "12");
  CHECK(document.children[0].text == "3");
  // The document's own closing tag ends the parse.
  REQUIRE(ParseXml(Bytes("<a/></XMLDocument><b/>"), document, &error));
  CHECK(document.children.size() == 1);
  // A comment ends at its first '>'.
  REQUIRE(ParseXml(Bytes("<!-- a > b -->c"), document, &error));
  CHECK(document.text == " b -->c");
}

TEST_CASE("XML: what makes the client read past the data fails") {
  XmlElement document;
  std::string error;
  for (const char* text : {"<a", "<a>x<", "<a>&lt", "<a>&", "<a>&am", "<a></a", "<!-- x"}) {
    INFO(text);
    CHECK_FALSE(ParseXml(Bytes(text), document, &error));
  }
  CHECK(ParseXml(Bytes("<a><b>never closed"), document, &error));
  CHECK(ParseXml(Bytes(""), document, &error));
  CHECK(document.children.empty());
}

TEST_CASE("the pet talk table loads from data, and parses as empty when missing") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_xml_pet_talk";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data");
  XmlElement document;
  std::string error;
  {
    const DataFiles files(dir);
    REQUIRE(LoadPetTalkTable(files, document, &error));
    CHECK(document.children.empty());
  }
  {
    std::ofstream f(dir / "data" / "PetTalkTable.xml", std::ios::binary);
    f << "<monster_talk_table><poring><hungry><feeding>Yum</feeding></hungry></poring>"
         "</monster_talk_table>";
  }
  const DataFiles files(dir);
  REQUIRE(LoadPetTalkTable(files, document, &error));
  const XmlElement* poring = document.FindChild("monster_talk_table")->FindChild("poring");
  REQUIRE(poring != nullptr);
  CHECK(poring->FindChild("hungry")->FindChild("feeding")->text == "Yum");
  std::filesystem::remove_all(dir);
}
