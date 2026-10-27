#include "res/xml.h"

#include <optional>

#include "res/data_file.h"

namespace ragexe::res {

namespace {

// Each element is a call deeper in the client's parser; the port stops well before the
// stack would.
constexpr int kMaxDepth = 10000;

class Parser {
 public:
  explicit Parser(std::span<const std::uint8_t> data) : data_(data) {}

  // The content of an element (004e65c0), from pos: nullopt where the client reads past
  // the data, else where it stops.
  std::optional<std::size_t> Content(XmlElement& parent, std::size_t pos, int depth) {
    if (depth > kMaxDepth) return Fail("the elements nest deeper than the port allows");
    while (pos != data_.size()) {
      const std::uint8_t c = data_[pos];
      if (c == '<') {
        if (!Has(pos + 1)) return Fail("a '<' ends the data");
        if (data_[pos + 1] == '/') {
          // A closing tag: its own element's ends the content, any other is skipped.
          const std::string name = TagName(pos + 2);
          const auto after = PastNext('>', pos);
          if (!after) return std::nullopt;
          if (name == parent.name) return after;
          pos = *after;
        } else {
          const auto after = Element(parent, pos, depth);
          if (!after) return std::nullopt;
          pos = *after;
        }
      } else if (c == '\r' || c == '\n') {
        ++pos;
      } else if (c == '&') {
        const auto entity = Entity(pos + 1);
        if (!entity) return std::nullopt;
        parent.text += *entity;
        const auto after = PastNext(';', pos);
        if (!after) return std::nullopt;
        pos = *after;
      } else {
        parent.text.push_back(static_cast<char>(c));
        ++pos;
      }
    }
    return pos;
  }

  const std::string& error() const { return error_; }

 private:
  bool Has(std::size_t pos) const { return pos < data_.size(); }

  std::nullopt_t Fail(const char* message) {
    error_ = message;
    return std::nullopt;
  }

  // Past the next c from pos. With none, the client steps one past the end and reads on.
  std::optional<std::size_t> PastNext(std::uint8_t c, std::size_t pos) {
    while (pos != data_.size() && data_[pos] != c) ++pos;
    if (pos == data_.size()) return Fail("a tag, comment or entity is left open");
    return pos + 1;
  }

  // ReadXmlTagName (004e69f0): up to a space, a tab, '>', '/' or the end.
  std::string TagName(std::size_t pos) const {
    std::size_t end = pos;
    while (end < data_.size()) {
      const std::uint8_t c = data_[end];
      if (c == ' ' || c == '\t' || c == '>' || c == '/') break;
      ++end;
    }
    return std::string(reinterpret_cast<const char*>(data_.data()) + pos, end - pos);
  }

  // DecodeXmlEntity (004e6b20): the two bytes after '&' against "lt" and "gt", then the
  // three against "amp" up to the first that differs; anything else decodes to nothing.
  std::optional<std::string> Entity(std::size_t pos) {
    if (!Has(pos + 1)) return Fail("an '&' is too near the end");
    if (data_[pos] == 'l' && data_[pos + 1] == 't') return std::string("<");
    if (data_[pos] == 'g' && data_[pos + 1] == 't') return std::string(">");
    if (data_[pos] != 'a' || data_[pos + 1] != 'm') return std::string();
    if (!Has(pos + 2)) return Fail("an '&' is too near the end");
    return std::string(data_[pos + 2] == 'p' ? "&" : "");
  }

  // ParseElement (004e6870), at a '<' that does not start a closing tag.
  std::optional<std::size_t> Element(XmlElement& parent, std::size_t pos, int depth) {
    const std::uint8_t kind = data_[pos + 1];
    if (kind == '?' || kind == '!') return PastNext('>', pos);
    parent.children.push_back(XmlElement{TagName(pos + 1), {}, {}});
    const auto after = PastNext('>', pos);
    if (!after) return std::nullopt;
    // Self-closing when the byte before its '>' is '/'.
    if (data_[*after - 2] == '/') return after;
    return Content(parent.children.back(), *after, depth + 1);
  }

  std::span<const std::uint8_t> data_;
  std::string error_;
};

}  // namespace

const XmlElement* XmlElement::FindChild(std::string_view child) const {
  for (const XmlElement& element : children) {
    if (element.name == child) return &element;
  }
  return nullptr;
}

bool ParseXml(std::span<const std::uint8_t> data, XmlElement& document, std::string* error) {
  document = XmlElement{std::string(kXmlDocumentName), {}, {}};
  Parser parser(data);
  if (parser.Content(document, 0, 0)) return true;
  if (error != nullptr) *error = parser.error();
  return false;
}

bool LoadPetTalkTable(const DataFiles& files, XmlElement& document, std::string* error) {
  DataFile file;
  if (!file.Open(files, kPetTalkTable)) {
    document = XmlElement{std::string(kXmlDocumentName), {}, {}};
    return true;
  }
  return ParseXml(file.data(), document, error);
}

}  // namespace ragexe::res
