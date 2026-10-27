#include "res/client_info.h"

#include <array>
#include <cstring>

#include "res/data_file.h"
#include "res/item_tables.h"
#include "res/vc6_sort.h"

namespace ragexe::res {

namespace {

// The built-in account ids (0060bbdc, 32, and 0060bc5c, 39), used unless a connection has
// an "aid" list.
constexpr std::array<std::int32_t, 32> kDefaultAdminsA = {
    100118,  2750446, 2750961, 2750963, 2750964, 2750966, 2750967, 2750969,
    2750971, 2750972, 2750973, 3143679, 3143680, 3143681, 3143682, 3143684,
    3143685, 3143686, 3143687, 3143688, 3143689, 3143690, 3143691, 3143692,
    3143693, 3143695, 3143696, 3143699, 3143700, 3143701, 3143702, 2919557};
constexpr std::array<std::int32_t, 39> kDefaultAdminsB = {
    100118,  2750446, 2750961, 2750963, 2750964, 2750966, 2750967, 2750969, 2750971, 2750972,
    2750973, 3143679, 3143680, 3143681, 3143682, 3143684, 3143685, 3143686, 3143687, 3143688,
    3143689, 3143690, 3143691, 3143692, 3143693, 3143695, 3143696, 3143699, 3143700, 3143701,
    3143702, 2919557, 3304372, 3304363, 3304378, 3107142, 3107143, 3107143, 3107145};

// ReadClientInfoSettings compares an element's whole text with these.
constexpr std::array<std::string_view, 10> kServiceTypes = {
    "korea", "america",   "japan",      "china",    "taiwan",
    "thai",  "indonesia", "philippine", "malaysia", "singapore"};
constexpr std::array<std::string_view, 3> kServerTypes = {"primary", "sakray", "local"};

// The globals point at an element's text buffer, so they read it as a C string.
std::string CString(const std::string& text) { return text.substr(0, text.find('\0')); }

std::int32_t Atoi(const std::string& text) { return ParseLeadingInt(CString(text)); }

const XmlElement* ClientInfoElement(const XmlElement& document) {
  return document.FindChild("clientinfo");
}

// FindNextSibling (004e6500): the next element after from, among parent's children, with
// that name.
const XmlElement* NextSibling(const XmlElement& parent, const XmlElement* from,
                              std::string_view name) {
  bool past = false;
  for (const XmlElement& child : parent.children) {
    if (past && child.name == name) return &child;
    if (&child == from) past = true;
  }
  return nullptr;
}

void SortUnsigned(std::vector<std::int32_t>& ids) {
  Vc6Sort(ids.data(), ids.data() + ids.size(), [](std::int32_t a, std::int32_t b) {
    return static_cast<std::uint32_t>(a) < static_cast<std::uint32_t>(b);
  });
}

void ShowError(ClientInfo& info, std::string_view message) { info.messages.emplace_back(message); }

// SelectLanguage (004ca940).
void SelectLanguage(ClientInfo& info) {
  info.language = LanguageClass::kDefault;
  switch (info.service_type) {
    case 0:
      info.charset = 0;
      info.code_page = 949;
      return;
    case 2:
      info.charset = 2;
      info.code_page = 932;
      return;
    case 3:
      info.charset = 3;
      info.code_page = 936;
      info.language = LanguageClass::kTraditionalChinese;
      return;
    case 4:
      info.charset = 4;
      info.code_page = 950;
      info.language = LanguageClass::kTraditionalChinese;
      return;
    case 5:
      info.charset = 5;
      info.code_page = 874;
      info.language = LanguageClass::kThai;
      return;
    case 6: info.charset = 6; break;
    case 1:
    case 7:
    case 8:
    case 9: info.charset = 1; break;
    default:
      ShowError(info, "no language setting !!!");  // 0060c088
      info.charset = 1;
      break;
  }
  info.code_page = 0;
}

// ReadClientInfoSettings (004c9b20).
void ReadSettings(const XmlElement& document, ClientInfo& info) {
  const XmlElement* clientinfo = ClientInfoElement(document);
  if (clientinfo == nullptr) {
    ShowError(info, "No ClientInfo File !!");  // 0060bd94
    return;
  }
  if (const XmlElement* service = clientinfo->FindChild("servicetype")) {
    bool known = false;
    for (std::size_t k = 0; k < kServiceTypes.size(); ++k) {
      if (service->text == kServiceTypes[k]) {
        info.service_type = static_cast<std::int32_t>(k);
        known = true;
        break;
      }
    }
    if (!known) ShowError(info, "ServiceType Error !!");  // 0060be40
  } else {
    ShowError(info, "No ServiceType !!");  // 0060be2c
  }
  if (const XmlElement* server = clientinfo->FindChild("servertype")) {
    bool known = false;
    for (std::size_t k = 0; k < kServerTypes.size(); ++k) {
      if (server->text == kServerTypes[k]) {
        info.server_type = static_cast<std::int32_t>(k);
        known = true;
        break;
      }
    }
    if (!known) ShowError(info, "ServerType Error !!");  // 0060bdf4
  } else {
    ShowError(info, "No ServerType !!");  // 0060bde0
  }
  if (clientinfo->FindChild("hideaccountlist") != nullptr) info.hide_account_list = true;
  if (clientinfo->FindChild("passwordencrypt") != nullptr) info.password_encrypt = true;
  if (clientinfo->FindChild("passwordencrypt2") != nullptr) {
    info.password_encrypt = true;
    info.password_encrypt2 = true;
  }
}

// InitDefaultClientInfo (004c97c0).
bool InitDefaults(const XmlElement& document, ClientInfo& info, std::string* error) {
  info.admins_a.assign(kDefaultAdminsA.begin(), kDefaultAdminsA.end());
  info.admins_b.assign(kDefaultAdminsB.begin(), kDefaultAdminsB.end());
  if (info.service_type != 0) {
    if (info.service_type > 0 && info.service_type < 10) {
      if (!SelectConnection(document, 0, info, error)) return false;
    } else {
      ShowError(info, "InitDefaultClientInfo() - Unknown ServerType !!!");  // 0060bd60
    }
    SelectLanguage(info);
    return true;
  }
  // The Korean service's servers, by server type.
  info.port = "6900";
  switch (info.server_type) {
    case 0:
      info.address = "211.172.247.192";
      info.version = 0x13;
      break;
    case 1:
      info.address = "211.172.247.192";
      info.version = 0x1d;
      break;
    case 2:
      info.version = 0xe;
      info.address = "192.168.1.241";
      break;
    case 3:
      info.version = 0x13;
      info.address = "211.172.247.192";
      info.port = "9676";
      break;
    case 4:
      info.address = "211.172.247.110";
      info.version = 0x1d;
      info.port = "6900";
      break;
    default: info.port = "6900"; break;
  }
  info.registration_web = "http://www.ragnarok.co.kr";
  SelectLanguage(info);
  return true;
}

// The rest of LoadClientInfo.
void SetNames(ClientInfo& info) {
  const auto ragnarok = [&] {
    info.registry_path = "Software\\Gravity Soft\\Ragnarok";
    info.game_name = "Ragnarok";
    if (info.server_type == 1) {
      info.executable = "Sakray.exe";
      info.patcher = "Sakup.exe";
      info.run_message = "Please run Sakray.exe";
    } else if (info.server_type == 2) {
      info.executable = "RagIndoor.exe";
      info.patcher = "Indoorup.exe";
      info.run_message = "Please run RagIndoor.exe";
    } else {
      info.executable = "Ragnarok.exe";
      info.patcher = "Patchup.exe";
      info.run_message = "Please run Ragnarok.exe";
    }
  };
  const bool korea = info.service_type == 0;
  info.flag_67c9cf = korea;
  info.flag_67c9d0 = korea;
  info.value_60bbd8 = korea ? 0xd : 0xc;
  if (!korea && info.service_type == 1 && info.server_type == 1) {
    info.licence = "..\\slicence.txt";
    ragnarok();
    return;
  }
  info.licence = "..\\licence.txt";
  if (info.service_type != 3) {
    ragnarok();
    return;
  }
  info.registry_path = "Software\\Gravity Soft\\RoClient";
  info.game_name = "\xcf\xc9\xbe\xb3\xb4\xab\xcb\xb5";  // 0060bf18, the name in code page 936
  if (info.server_type == 1) {
    info.executable = "SakClient.exe";
    info.patcher = "Sakup.exe";
    info.run_message = "Please run SakClient.exe";
  } else {
    info.executable = "RoClient.exe";
    info.patcher = "Patchup.exe";
    info.run_message = "Please run RoClient.exe";
  }
}

}  // namespace

bool SelectConnection(const XmlElement& document, int index, ClientInfo& info, std::string* error) {
  const XmlElement* clientinfo = ClientInfoElement(document);
  if (clientinfo == nullptr) return true;
  const XmlElement* connection = clientinfo->FindChild("connection");
  if (connection == nullptr) return true;
  for (; index != 0; --index) {
    if (connection == nullptr) return true;
    connection = NextSibling(*clientinfo, connection, "connection");
  }
  if (connection == nullptr) return true;
  if (const XmlElement* e = connection->FindChild("address")) info.address = CString(e->text);
  if (const XmlElement* e = connection->FindChild("port")) info.port = CString(e->text);
  if (const XmlElement* e = connection->FindChild("version")) info.version = Atoi(e->text);
  if (const XmlElement* e = connection->FindChild("langtype")) info.service_type = Atoi(e->text);
  if (const XmlElement* e = connection->FindChild("registrationweb")) {
    info.registration_web = CString(e->text);
  }
  if (const XmlElement* aid = connection->FindChild("aid")) {
    info.admins_a.clear();
    info.admins_b.clear();
    // The client reads the first "admin" without checking that there is one.
    const XmlElement* admin = aid->FindChild("admin");
    if (admin == nullptr) {
      if (error != nullptr)
        *error = "an \"aid\" with no \"admin\": the client reads a null element";
      return false;
    }
    for (; admin != nullptr; admin = NextSibling(*aid, admin, "admin")) {
      info.admins_a.push_back(Atoi(admin->text));
      info.admins_b.push_back(Atoi(admin->text));
    }
    SortUnsigned(info.admins_a);
    SortUnsigned(info.admins_b);
  } else {
    info.admins_a.assign(kDefaultAdminsA.begin(), kDefaultAdminsA.end());
    info.admins_b.assign(kDefaultAdminsB.begin(), kDefaultAdminsB.end());
  }
  SelectLanguage(info);
  return true;
}

bool ReadClientInfo(const XmlElement& document, ClientInfo& info, std::string* error) {
  info = ClientInfo{};
  ReadSettings(document, info);
  if (!InitDefaults(document, info, error)) return false;
  SetNames(info);
  return true;
}

bool LoadClientInfo(const DataFiles& files, std::string_view name, XmlElement& document,
                    ClientInfo& info, std::string* error) {
  document = XmlElement{std::string(kXmlDocumentName), {}, {}};
  DataFile file;
  if (file.Open(files, name) && !ParseXml(file.data(), document, error)) return false;
  return ReadClientInfo(document, info, error);
}

std::vector<ConnectionEntry> ListConnections(const XmlElement& document) {
  std::vector<ConnectionEntry> out;
  const XmlElement* clientinfo = ClientInfoElement(document);
  if (clientinfo == nullptr) return out;
  for (const XmlElement* connection = clientinfo->FindChild("connection"); connection != nullptr;
       connection = NextSibling(*clientinfo, connection, "connection")) {
    ConnectionEntry entry;
    if (const XmlElement* e = connection->FindChild("display")) entry.display = e->text;
    if (const XmlElement* e = connection->FindChild("desc")) entry.desc = e->text;
    out.push_back(std::move(entry));
  }
  return out;
}

}  // namespace ragexe::res
