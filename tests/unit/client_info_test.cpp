#include "res/client_info.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "res/xml.h"

using namespace ragexe::res;

namespace {

XmlElement Document(std::string_view text) {
  XmlElement document;
  std::string error;
  REQUIRE(ParseXml(std::vector<std::uint8_t>(text.begin(), text.end()), document, &error));
  return document;
}

constexpr std::string_view kIro =
    "<?xml version=\"1.0\" encoding=\"euc-kr\" ?>\r\n<clientinfo>\r\n"
    "\t<servicetype>america</servicetype>\r\n\t<servertype>primary</servertype>\r\n"
    "\t<hideaccountlist></hideaccountlist>\r\n"
    "\t<connection>\r\n\t\t<display>iRO Server</display>\r\n\t\t<desc>None</desc>\r\n"
    "\t\t<address>38.144.194.2</address>\r\n\t\t<port>6900</port>\r\n"
    "\t\t<version>18</version>\r\n\t\t<langtype>1</langtype>\r\n"
    "\t\t<registrationweb>http://enweb.gravity.co.kr/e_regist.htm</registrationweb>\r\n"
    "\t\t<aid><admin>650874</admin><admin>100001</admin></aid>\r\n"
    "\t</connection>\r\n</clientinfo>\r\n";

}  // namespace

TEST_CASE("clientinfo: iRO's settings, connection and names") {
  ClientInfo info;
  std::string error;
  REQUIRE(ReadClientInfo(Document(kIro), info, &error));
  CHECK(info.service_type == 1);
  CHECK(info.server_type == 0);
  CHECK(info.hide_account_list);
  CHECK(info.address == "38.144.194.2");
  CHECK(info.port == "6900");
  CHECK(info.version == 18);
  CHECK(info.admins_a == std::vector<std::int32_t>{100001, 650874});
  CHECK(info.code_page == 0);
  CHECK(info.charset == 1);
  CHECK(info.licence == "..\\licence.txt");
  CHECK(info.executable == "Ragnarok.exe");
  CHECK(info.messages.empty());
  const auto connections = ListConnections(Document(kIro));
  REQUIRE(connections.size() == 1);
  CHECK(connections[0].display == "iRO Server");
}

TEST_CASE("clientinfo: no file is Korea's defaults, and langtype sets the service") {
  ClientInfo info;
  std::string error;
  REQUIRE(ReadClientInfo(Document(""), info, &error));
  CHECK(info.messages == std::vector<std::string>{"No ClientInfo File !!"});
  CHECK(info.service_type == 0);
  CHECK(info.address == "211.172.247.192");
  CHECK(info.version == 0x13);
  CHECK(info.code_page == 949);
  CHECK(info.admins_b.size() == 39u);
  // An unknown langtype has no language: SelectLanguage complains, and runs twice.
  REQUIRE(ReadClientInfo(Document("<clientinfo><servicetype>japan</servicetype>"
                                  "<servertype>sakray</servertype><connection>"
                                  "<langtype>12</langtype></connection></clientinfo>"),
                         info, &error));
  CHECK(info.service_type == 12);
  CHECK(info.messages ==
        std::vector<std::string>{"no language setting !!!", "no language setting !!!"});
  CHECK(info.executable == "Sakray.exe");
}

TEST_CASE("clientinfo: admin ids sort unsigned, and an empty aid fails") {
  ClientInfo info;
  std::string error;
  REQUIRE(ReadClientInfo(Document("<clientinfo><servicetype>america</servicetype>"
                                  "<connection><aid><admin>5</admin><admin>-3</admin>"
                                  "<admin>2</admin></aid></connection></clientinfo>"),
                         info, &error));
  CHECK(info.admins_a == std::vector<std::int32_t>{2, 5, -3});
  CHECK_FALSE(ReadClientInfo(Document("<clientinfo><servicetype>america</servicetype>"
                                      "<connection><aid></aid></connection></clientinfo>"),
                             info, &error));
}

TEST_CASE("clientinfo: choosing another connection") {
  const XmlElement document = Document(
      "<clientinfo><servicetype>america</servicetype>"
      "<connection><address>1.1.1.1</address></connection>"
      "<connection><address>2.2.2.2</address><langtype>4</langtype></connection>"
      "</clientinfo>");
  ClientInfo info;
  std::string error;
  REQUIRE(ReadClientInfo(document, info, &error));
  CHECK(info.address == "1.1.1.1");
  REQUIRE(SelectConnection(document, 1, info, &error));
  CHECK(info.address == "2.2.2.2");
  CHECK(info.code_page == 950);
  CHECK(info.language == LanguageClass::kTraditionalChinese);
  REQUIRE(SelectConnection(document, 5, info, &error));
  CHECK(info.address == "2.2.2.2");
}
