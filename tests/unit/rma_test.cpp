#include "res/rma.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "res/data_files.h"
#include "res/resource.h"

using namespace ragexe::res;

namespace {

std::vector<std::uint8_t> Bytes(std::string_view text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

using Tokens = std::vector<std::string>;

}  // namespace

TEST_CASE("the tokenizer: spaces, quotes and comments as the client splits them") {
  CHECK(TokenizeLine("#action 1  2\t3\r") == Tokens{"#action", "1", "2", "3"});
  CHECK(TokenizeLine("  lead") == Tokens{"lead"});
  CHECK(TokenizeLine("a \"b c\" d") == Tokens{"a", "b c", "d"});
  CHECK(TokenizeLine("ab\"cd\"") == Tokens{"cd"});         // a quote drops the token so far
  CHECK(TokenizeLine("a \"open") == Tokens{"a", "open"});  // an unclosed quote is kept
  CHECK(TokenizeLine("a \"\"") == Tokens{"a", ""});
  CHECK(TokenizeLine("a b//c") == Tokens{"a"});  // "b" is cut short and dropped
  CHECK(TokenizeLine("a b //c") == Tokens{"a", "b"});
  CHECK(TokenizeLine("\"a // b\"") == Tokens{"a // b"});
  CHECK(TokenizeLine("\xb9\xae \xb9\xae") == Tokens{"\xb9\xae", "\xb9\xae"});
  CHECK(TokenizeLine("").empty());
}

TEST_CASE("names, actions and the interior values") {
  Rma rma;
  std::string error;
  REQUIRE(DecodeRma(Bytes("#action 1 4 1 2\n"
                          "#rsxname door.rsx\n"
                          "#rsxname \"gate b.rsx\"\n"
                          "#action 3 9 7 8 1 2 3\n"
                          "#action 3 5 6 7\n"  // a duplicate: the list stays
                          "#interiorzoom 1.5\n#shadowzoom 3d-1\n"
                          "#interioroffsetx -4\n#interioroffsety 4294967297\n"
                          "#interiorframes 2 6\n#what\n"),
                    rma, &error));
  CHECK(rma.rsx_names == Tokens{"door.rsx", "gate b.rsx"});
  REQUIRE(rma.actions.size() == 4);
  CHECK(rma.actions[0].value_0 == -1);  // never defined
  CHECK(rma.actions[0].value_8 == 4);
  CHECK(rma.actions[1].rsx == -1);  // before any name
  const RmaAction& three = rma.actions[3];
  CHECK(three.value_8 == 5);
  CHECK(three.value_0 == 6);
  CHECK(three.value_4 == 7);
  CHECK(three.list == std::vector<std::int32_t>{1, 2, 3});
  CHECK(three.rsx == 1);
  CHECK(rma.interior_zoom == 1.5f);
  CHECK(rma.shadow_zoom == 0.3f);
  CHECK(rma.interior_offset_x == -4);
  CHECK(rma.interior_offset_y == 1);  // atoi wraps
  CHECK(rma.interior_frames == std::array<std::int32_t, 2>{2, 6});
  CHECK(rma.messages == Tokens{"a duplicate action", "Syntax Error in Rma"});
}

TEST_CASE("the text ends at a NUL, and a line that fills the buffer is the last") {
  Rma rma;
  std::string error;
  REQUIRE(DecodeRma(Bytes(std::string_view("#action 0 1 2 3\0#action 1 1 2 3", 31)), rma, &error));
  CHECK(rma.actions.size() == 1);
  std::string exact = "#action 0 1 2 3 ";
  exact += std::string(1023 - exact.size(), '7');
  REQUIRE(DecodeRma(Bytes(exact + "\n#action 1 1 2 3\n"), rma, &error));
  CHECK(rma.actions.size() == 2);
  REQUIRE(DecodeRma(Bytes(exact + "7\n#action 1 1 2 3\n"), rma, &error));
  CHECK(rma.actions.size() == 1);
}

TEST_CASE("lines the original reads past, and action numbers it cannot hold") {
  Rma rma;
  std::string error;
  CHECK_FALSE(DecodeRma(Bytes("#action 0 1 2\n"), rma, &error));
  CHECK(error == "Error in CTokenizer :: operator []");
  CHECK_FALSE(DecodeRma(Bytes("#rsxname\n"), rma, &error));
  CHECK_FALSE(DecodeRma(Bytes("#interiorframes 1\n"), rma, &error));
  CHECK_FALSE(DecodeRma(Bytes("#action -1 1 2 3\n"), rma, &error));
  CHECK_FALSE(DecodeRma(Bytes("#action 70000 1 2 3\n"), rma, &error));
}

TEST_CASE("RmaRes succeeds empty when the file is missing, as the original does") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_rma_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "model");
  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterRmaTypes(manager);
  auto* missing = static_cast<RmaRes*>(manager.Get("\xc4\xae.rma"));
  REQUIRE(missing != nullptr);
  CHECK(missing->rma().actions.empty());
  CHECK(missing->rma().shadow_zoom == 1.0f);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
