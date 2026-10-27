#include "res/act.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "res/data_files.h"
#include "res/resource.h"
#include "res/sprite.h"

using namespace ragexe::res;

namespace {

void Put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  Put16(out, v & 0xffff);
  Put16(out, v >> 16);
}

void PutFloat(std::vector<std::uint8_t>& out, float f) {
  std::uint32_t u = 0;
  std::memcpy(&u, &f, 4);
  Put32(out, u);
}

// One action of one frame with one item, centred at (cx, cy) on image 0 of type 0, in
// the layout of the given version.
std::vector<std::uint8_t> Act1(std::uint16_t version, std::int32_t cx, std::int32_t cy,
                               float scale = 1.0f) {
  std::vector<std::uint8_t> out = {'A', 'C'};
  Put16(out, version);
  Put16(out, 1);
  out.resize(16, 0);
  Put32(out, 1);  // frames
  for (int i = 0; i < 8; ++i) Put32(out, static_cast<std::uint32_t>(i));
  Put32(out, 1);  // items
  Put32(out, static_cast<std::uint32_t>(cx));
  Put32(out, static_cast<std::uint32_t>(cy));
  Put32(out, 0);  // image
  Put32(out, 1);  // flags: mirrored
  if (version >= 0x200) {
    out.insert(out.end(), {10, 20, 30, 40});
    PutFloat(out, scale);
    if (version >= 0x204) PutFloat(out, scale * 2);
    Put32(out, 90);  // angle
    Put32(out, 0);   // image type
    if (version >= 0x205) {
      Put32(out, 7);  // width
      Put32(out, 4);  // height
    }
    Put32(out, 3);  // event
  }
  if (version >= 0x203) {
    Put32(out, 1);  // anchors
    for (std::uint32_t v : {99u, 5u, 6u, 2u}) Put32(out, v);
  }
  if (version >= 0x201) {
    Put32(out, 1);
    std::vector<std::uint8_t> name(40, 0);
    std::memcpy(name.data(), "atk", 3);
    out.insert(out.end(), name.begin(), name.end());
  }
  if (version >= 0x202) PutFloat(out, 2.5f);
  return out;
}

ActImageLookup Images(std::int32_t width, std::int32_t height, bool halved = false) {
  return [=](std::int32_t type, std::int32_t index) -> std::optional<ActImageSize> {
    if (type != 0 || index != 0) return std::nullopt;
    return ActImageSize{width, height, halved, halved};
  };
}

}  // namespace

TEST_CASE("items move from their centre to their top-left corner") {
  ActItem item;
  item.x = 10;
  item.y = -3;
  AdjustActItem(item, {7, 4, false, false});  // c(7) = 4, c(4) = 2
  CHECK(item.x == 6);
  CHECK(item.y == -5);

  ActItem scaled;
  scaled.x = 10;
  scaled.y = 10;
  scaled.scale_x = 2.0f;
  scaled.scale_y = 0.5f;
  AdjustActItem(scaled, {7, 4, false, false});
  CHECK(scaled.x == 1);   // (10 - 4 * 2) / 2
  CHECK(scaled.y == 18);  // (10 - 2 * 0.5) / 0.5

  ActItem zero_scale;
  zero_scale.x = 5;
  zero_scale.y = 0;
  zero_scale.scale_x = 0.0f;
  zero_scale.scale_y = 0.0f;
  AdjustActItem(zero_scale, {7, 4, false, false});  // 5 / 0 and 0 / 0: __ftol gives 0
  CHECK(zero_scale.x == 0);
  CHECK(zero_scale.y == 0);

  ActItem halved;
  halved.x = 0;
  halved.y = 0;
  AdjustActItem(halved, {3, 3, true, true});  // doubled to 6: c(6) = 3
  CHECK(halved.x == -3);
  CHECK(halved.y == -3);
}

TEST_CASE("each version's layout") {
  for (int version : {0x101, 0x200, 0x201, 0x202, 0x203, 0x204, 0x205}) {
    INFO("version " << std::hex << version);
    Act act;
    std::string error;
    REQUIRE(
        DecodeAct(Act1(static_cast<std::uint16_t>(version), 10, 20), Images(7, 4), act, &error));
    REQUIRE(act.actions.size() == 1);
    REQUIRE(act.actions[0].size() == 1);
    const ActFrame& frame = act.actions[0][0];
    CHECK(frame.header[7] == 7);
    REQUIRE(frame.items.size() == 1);
    const ActItem& item = frame.items[0];
    CHECK(item.flags == 1);
    if (version < 0x200) {
      CHECK(item.x == 10);  // not adjusted
      CHECK(item.color == std::array<std::uint8_t, 4>{0xff, 0xff, 0xff, 0xff});
      CHECK(frame.event == -1);
    } else {
      CHECK(item.x == 6);
      CHECK(item.color == std::array<std::uint8_t, 4>{10, 20, 30, 40});
      CHECK(item.angle == 90);
      CHECK(frame.event == (version == 0x200 ? -1 : 3));
    }
    CHECK(item.scale_y == (version >= 0x204 ? 2.0f : 1.0f));
    CHECK(frame.anchors.size() == (version >= 0x203 ? 1u : 0u));
    if (version >= 0x203) {
      CHECK(frame.anchors[0].x == 5);
      CHECK(frame.anchors[0].y == 6);
      CHECK(frame.anchors[0].kind == 2);
    }
    CHECK(act.events ==
          (version >= 0x201 ? std::vector<std::string>{"atk"} : std::vector<std::string>{}));
    CHECK(act.delays == std::vector<float>{version >= 0x202 ? 2.5f : 4.0f});
    CHECK(act.max_items == 1);
  }
}

TEST_CASE("version 0x205 uses the stored image size, not the sprite") {
  Act act;
  std::string error;
  REQUIRE(DecodeAct(Act1(0x205, 10, 20), nullptr, act, &error));
  CHECK(act.actions[0][0].items[0].x == 6);
  CHECK(act.actions[0][0].items[0].y == 8);  // (20 - 2 * 2) / 2, the y scale being 2
}

TEST_CASE("actions the original refuses or cannot load") {
  Act act;
  std::string error;
  auto bad = Act1(0x204, 0, 0);
  bad[0] = 'X';
  CHECK_FALSE(DecodeAct(bad, Images(1, 1), act, &error));
  CHECK(error == "Illegal file format");
  CHECK_FALSE(DecodeAct(Act1(0x206, 0, 0), Images(1, 1), act, &error));
  CHECK(error == "Unsupported Version. Content may be corrupted");
  CHECK_FALSE(DecodeAct(Act1(0x204, 0, 0), nullptr, act, &error));
  CHECK(error == "Error While Load Act");
  auto truncated = Act1(0x204, 0, 0);
  truncated.resize(60);
  CHECK_FALSE(DecodeAct(truncated, Images(1, 1), act, &error));
}

TEST_CASE("ActRes takes its sprite from the resource manager") {
  const auto dir = std::filesystem::temp_directory_path() / "ragexe_act_res";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir / "data" / "sprite");
  auto write = [&](const char* name, const std::vector<std::uint8_t>& bytes) {
    std::ofstream f(dir / "data" / "sprite" / name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  };
  // A 0x201 sprite with one 7 x 4 image.
  std::vector<std::uint8_t> spr = {'S', 'P', 0x01, 0x02, 1, 0, 0, 0, 7, 0, 4, 0};
  const auto rle = EncodeSpriteRle(std::vector<std::uint8_t>(28, 1), 7, 4);
  Put16(spr, static_cast<std::uint32_t>(rle.size()));
  spr.insert(spr.end(), rle.begin(), rle.end());
  spr.resize(spr.size() + 1024, 0);
  write("poring.spr", spr);
  write("poring.act", Act1(0x204, 10, 20));
  write("lonely.act", Act1(0x204, 10, 20));

  const DataFiles files(dir);
  ResourceManager manager(files, [] { return 0u; });
  RegisterSpriteTypes(manager);
  RegisterActTypes(manager);
  auto* poring = static_cast<ActRes*>(manager.Get("Poring.act"));
  REQUIRE(poring != nullptr);
  CHECK(poring->act().actions[0][0].items[0].x == 6);
  CHECK(manager.loaded_count() == 2);  // the action and its sprite
  CHECK(manager.Get("lonely.act") == nullptr);
  manager.Clear();
  std::filesystem::remove_all(dir);
}
