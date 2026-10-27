#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/xml.h"

namespace ragexe::res {

class DataFiles;

// The language object SelectLanguage (004ca940) points g_pCLanguage at.
enum class LanguageClass {
  kDefault,             // g_CLanguage
  kTraditionalChinese,  // g_CLanguageTC
  kThai,                // g_CLanguageTH
};

// What LoadClientInfo (004ca090) leaves in the client's globals (docs/formats/clientinfo.md).
struct ClientInfo {
  // 0067c9d4: "servicetype" (korea 0, america 1, japan 2, china 3, taiwan 4, thai 5,
  // indonesia 6, philippine 7, malaysia 8, singapore 9), then a connection's "langtype".
  std::int32_t service_type = 0;
  std::int32_t server_type = 0;    // 0067c9dc: "servertype": primary 0, sakray 1, local 2
  bool hide_account_list = false;  // 0067c9cc
  bool password_encrypt = false;   // 0067c9cd
  bool password_encrypt2 = false;  // 0067c9ce

  // The connection: from clientinfo, or the built-in values of the Korean service.
  std::string address;           // 0060bbb8
  std::string port;              // 0060bbbc
  std::string registration_web;  // 0060bbc0
  std::int32_t version = 0;      // 0067c9c8
  // The account ids of the "admin" entries, sorted unsigned, or the built-in tables as they are.
  std::vector<std::int32_t> admins_a;  // 0067c988, IsInAdminListA
  std::vector<std::int32_t> admins_b;  // 0067c978, IsInAdminListB

  // SelectLanguage.
  std::int32_t charset = 0;     // 0067c9d8
  std::uint32_t code_page = 0;  // 0067c998
  LanguageClass language = LanguageClass::kDefault;

  // The rest of LoadClientInfo, by service and server type.
  bool flag_67c9cf = false;
  bool flag_67c9d0 = false;
  std::int32_t value_60bbd8 = 0xc;
  std::string licence;        // 0060bbb4: the licence file GetLicenceText reads
  std::string registry_path;  // 0060bbd4: under HKEY_LOCAL_MACHINE
  std::string game_name;      // 0060bbd0
  std::string executable;     // 0060bbc4
  std::string patcher;        // 0060bbc8
  std::string run_message;    // 0060bbcc

  // What ShowErrorMessage showed, in order.
  std::vector<std::string> messages;
};

// LoadClientInfo after its parse, on the document. Fails where the client dereferences a
// null element: an "aid" with no "admin" in it.
bool ReadClientInfo(const XmlElement& document, ClientInfo& info, std::string* error);

// LoadConnectionSettings (004ca260): the index-th "connection" of clientinfo into info, as
// the login screen does when a server is chosen; nothing without one.
bool SelectConnection(const XmlElement& document, int index, ClientInfo& info, std::string* error);

// LoadClientInfo (004ca090) on a file: "clientinfo.xml", or what /account: names. A file
// that does not open leaves the document empty.
inline constexpr std::string_view kClientInfoFile = "clientinfo.xml";
bool LoadClientInfo(const DataFiles& files, std::string_view name, XmlElement& document,
                    ClientInfo& info, std::string* error);

// CLoginMode::LoadConnectionList (0053ecd0): each connection's "display" and "desc".
struct ConnectionEntry {
  std::string display;
  std::string desc;
};
std::vector<ConnectionEntry> ListConnections(const XmlElement& document);

}  // namespace ragexe::res
