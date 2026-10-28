#include "res/rma.h"

#include <algorithm>

#include "res/c_runtime.h"
#include "res/data_file.h"

namespace ragexe::res {

namespace {

constexpr std::size_t kLineBuffer = 0x400;  // CTokenizer::ReadLine's getline buffer

bool Fail(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
  return false;
}

}  // namespace

std::vector<std::string> TokenizeLine(std::string_view line) {
  std::vector<std::string> tokens;
  std::string token;
  enum { kQuoted, kToken, kSpaces } state = kToken;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (state == kQuoted) {
      if (c == '"') {
        tokens.push_back(token);
        token.clear();
        state = kSpaces;
      } else {
        token += c;
      }
    } else if (state == kToken) {
      // "//" ends the line, and drops a token it cuts short.
      if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return tokens;
      if (IsCSpace(c)) {
        // The test is on the position in the line, not the token's length.
        if (i != 0) {
          tokens.push_back(token);
          token.clear();
        }
        state = kSpaces;
      } else if (c == '"') {
        token.clear();  // a quote drops what the token held
        state = kQuoted;
      } else {
        token += c;
      }
    } else if (!IsCSpace(c)) {
      state = kToken;
      --i;  // read it again as part of a token
    }
  }
  if (state != kSpaces && !line.empty()) tokens.push_back(token);
  return tokens;
}

bool DecodeRma(std::span<const std::uint8_t> data, Rma& out, std::string* error) {
  out = Rma{};
  // The stream reads the text up to its first NUL.
  const auto end = std::find(data.begin(), data.end(), std::uint8_t{0});
  const std::string text(data.begin(), end);
  const char* missing = "Error in CTokenizer :: operator []";  // 00605dc4
  std::int32_t rsx = -1;
  std::size_t pos = 0;
  bool failed = false;  // the stream's failbit: the loop stops before the next line
  while (!failed) {
    // getline(buffer, 0x400, '\n'): the delimiter is taken first, then a full buffer
    // fails the stream, and so does an end of text with nothing read.
    std::string line;
    if (pos >= text.size()) break;
    while (true) {
      if (pos >= text.size()) break;
      if (text[pos] == '\n') {
        ++pos;
        break;
      }
      if (line.size() >= kLineBuffer - 1) {
        failed = true;
        break;
      }
      line += text[pos++];
    }
    const std::vector<std::string> tokens = TokenizeLine(line);
    if (tokens.empty() || tokens[0].empty()) continue;
    const std::string& keyword = tokens[0];
    const auto need = [&](std::size_t n) { return tokens.size() >= n; };
    if (keyword == "#rsxname") {
      if (!need(2)) return Fail(error, missing);
      ++rsx;
      out.rsx_names.resize(static_cast<std::size_t>(rsx) + 1);
      out.rsx_names[static_cast<std::size_t>(rsx)] = tokens[1];
    } else if (keyword == "#action") {
      if (!need(2)) return Fail(error, missing);
      const std::int32_t number = AtoI(tokens[1]);
      // The original takes it as unsigned and grows its vectors to it.
      if (number < 0 || number > kRmaMaxAction) return Fail(error, "an action number too large");
      const auto n = static_cast<std::size_t>(number);
      if (out.actions.size() <= n) out.actions.resize(n + 1);
      RmaAction& action = out.actions[n];
      if (action.value_0 != -1) out.messages.emplace_back("a duplicate action");  // 00605d98
      if (!need(5)) return Fail(error, missing);
      action.value_8 = AtoI(tokens[2]);
      action.value_0 = AtoI(tokens[3]);
      action.value_4 = AtoI(tokens[4]);
      // Six or more tokens replace the list; fewer leave it as it was.
      if (tokens.size() > 5) {
        action.list.clear();
        for (std::size_t k = 5; k < tokens.size(); ++k) action.list.push_back(AtoI(tokens[k]));
      }
      action.rsx = rsx;
    } else if (keyword == "#interiorzoom") {
      if (!need(2)) return Fail(error, missing);
      out.interior_zoom = static_cast<float>(AtoF(tokens[1]));
    } else if (keyword == "#interioroffsetx") {
      if (!need(2)) return Fail(error, missing);
      out.interior_offset_x = AtoI(tokens[1]);
    } else if (keyword == "#interioroffsety") {
      if (!need(2)) return Fail(error, missing);
      out.interior_offset_y = AtoI(tokens[1]);
    } else if (keyword == "#interiorframes") {
      if (!need(3)) return Fail(error, missing);
      out.interior_frames = {AtoI(tokens[1]), AtoI(tokens[2])};
    } else if (keyword == "#shadowzoom") {
      if (!need(2)) return Fail(error, missing);
      out.shadow_zoom = static_cast<float>(AtoF(tokens[1]));
    } else {
      out.messages.emplace_back("Syntax Error in Rma");  // 00605d30
    }
  }
  return true;
}

std::unique_ptr<Resource> RmaRes::Clone() const { return std::make_unique<RmaRes>(); }

bool RmaRes::Load(const DataFiles& files, std::string_view path) {
  DataFile file;
  error_.clear();
  // The original does not check the open: with no file it reads nothing and succeeds.
  if (!file.Open(files, path)) {
    rma_ = Rma{};
    return true;
  }
  return DecodeRma(file.data(), rma_, &error_);
}

void RmaRes::Reset() {
  // Slot 4 (00421e40) empties the actions, the names and the action-to-name indices.
  rma_.actions.clear();
  rma_.rsx_names.clear();
}

void RegisterRmaTypes(ResourceManager& manager) {
  manager.RegisterType("rma", "model\\", std::make_unique<RmaRes>());
}

}  // namespace ragexe::res
