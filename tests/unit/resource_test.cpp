#include "res/resource.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/data_file.h"
#include "res/data_files.h"
#include "res/data_path.h"
#include "res/grf.h"
#include "support/grf_builder.h"

using namespace ragexe::res;
using ragexe::testing::BuildArchive;

namespace {

struct PathVector {
  const char* name;
  const char* path;
};
#include "resource_vectors.inc"

std::vector<std::uint8_t> Bytes(std::string_view s) { return {s.begin(), s.end()}; }

// An install directory under the system's temporary directory, removed afterwards.
class TempInstall {
 public:
  explicit TempInstall(const char* name) : dir_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
  }
  ~TempInstall() {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  TempInstall(const TempInstall&) = delete;
  TempInstall& operator=(const TempInstall&) = delete;

  void Write(const std::filesystem::path& relative, std::string_view text) const {
    std::filesystem::create_directories((dir_ / relative).parent_path());
    std::ofstream f(dir_ / relative, std::ios::binary);
    f << text;
  }
  const std::filesystem::path& dir() const { return dir_; }

 private:
  std::filesystem::path dir_;
};

// A resource that keeps the file's text; counts its loads and resets.
class TextResource : public Resource {
 public:
  explicit TextResource(int* resets = nullptr) : resets_(resets) {}
  std::unique_ptr<Resource> Clone() const override {
    return std::make_unique<TextResource>(resets_);
  }
  bool Load(const DataFiles& files, std::string_view path) override {
    DataFile file;
    if (!file.Open(files, path)) return false;
    text.assign(reinterpret_cast<const char*>(file.data().data()), file.size());
    return true;
  }
  void Reset() override {
    if (resets_ != nullptr) ++*resets_;
  }

  std::string text;

 private:
  int* resets_;
};

}  // namespace

TEST_CASE("MakeDataPath matches the original") {
  for (const PathVector& v : kPathVectors) {
    INFO(v.name);
    CHECK(MakeDataPath(v.name) == v.path);
  }
}

TEST_CASE("the archive comes first, then loose files in any case") {
  TempInstall install("ragexe_res_lookup");
  install.Write("data/Texture/Both.BMP", "loose both");
  install.Write("data/texture/only_loose.bmp", "loose only");
  install.Write("_tmpEmblem/e.ebm", "emblem");
  auto archive = GrfArchive::FromBytes(
      BuildArchive({{"data\\texture\\both.bmp", Bytes("archived both"), 1}}), nullptr);
  REQUIRE(archive != nullptr);
  const DataFiles files(install.dir(), std::move(archive));

  CHECK(files.Load("data\\texture\\both.bmp") == Bytes("archived both"));
  CHECK(files.Load("DATA\\TEXTURE\\ONLY_LOOSE.BMP") == Bytes("loose only"));
  CHECK(files.Load("data/texture/only_loose.bmp") == Bytes("loose only"));
  CHECK(files.Load("data\\..\\_tmpEmblem\\e.ebm") == Bytes("emblem"));
  CHECK_FALSE(files.Load("data\\texture\\missing.bmp").has_value());
  CHECK_FALSE(files.Load("..\\outside.bmp").has_value());
  CHECK_FALSE(files.Load("C:\\data\\texture\\only_loose.bmp").has_value());
  CHECK_FALSE(files.Load("data\\texture").has_value());
  CHECK(files.Exists("data\\texture\\both.bmp"));
  CHECK(files.Exists("data\\TEXTURE\\only_loose.bmp"));
  CHECK_FALSE(files.Exists("data\\texture\\missing.bmp"));
}

TEST_CASE("layers are read after the archive and before loose files") {
  TempInstall install("ragexe_res_layers");
  install.Write("data/loose.bmp", "loose");
  install.Write("data/layered.bmp", "loose layered");
  auto patch =
      GrfArchive::FromBytes(BuildArchive({{"data\\both.bmp", Bytes("patch"), 1}}), nullptr);
  auto earlier =
      GrfArchive::FromBytes(BuildArchive({{"data\\both.bmp", Bytes("earlier"), 1},
                                          {"data\\layered.bmp", Bytes("earlier layered"), 1}}),
                            nullptr);
  auto base = GrfArchive::FromBytes(BuildArchive({{"data\\layered.bmp", Bytes("base layered"), 1},
                                                  {"data\\base.bmp", Bytes("base"), 1}}),
                                    nullptr);
  REQUIRE((patch && earlier && base));
  DataFiles files(install.dir(), std::move(patch));
  files.set_layers({earlier.get(), base.get()});

  CHECK(files.Load("data\\both.bmp") == Bytes("patch"));
  CHECK(files.Load("data\\layered.bmp") == Bytes("earlier layered"));
  CHECK(files.Load("data\\base.bmp") == Bytes("base"));
  CHECK(files.Load("data\\loose.bmp") == Bytes("loose"));
  CHECK(files.Exists("data\\base.bmp"));
  CHECK_FALSE(files.Exists("data\\missing.bmp"));
}

TEST_CASE("DataFile reads all or nothing and clamps seeks to the end") {
  TempInstall install("ragexe_res_datafile");
  install.Write("data/a.bin", "0123456789");
  const DataFiles files(install.dir());
  DataFile file;
  REQUIRE(file.Open(files, "a.bin"));
  CHECK(file.path() == "data\\a.bin");
  CHECK(file.size() == 10);

  char buf[8] = {};
  CHECK(file.Read(buf, 4));
  CHECK(std::string(buf, 4) == "0123");
  CHECK_FALSE(file.Read(buf, 7));
  CHECK(file.position() == 4);
  CHECK(file.Read(buf, 6));
  CHECK(file.Read(buf, 0));

  CHECK(file.Seek(2, DataFile::Origin::kBegin));
  CHECK(file.Seek(3, DataFile::Origin::kCurrent));
  CHECK(file.position() == 5);
  CHECK(file.Seek(static_cast<std::uint32_t>(-4), DataFile::Origin::kEnd));
  CHECK(file.position() == 6);
  CHECK_FALSE(file.Seek(20, DataFile::Origin::kBegin));
  CHECK(file.position() == 10);

  CHECK_FALSE(file.Open(files, "missing.bin"));
  CHECK_FALSE(file.is_open());
  CHECK_FALSE(file.Read(buf, 1));
}

TEST_CASE("ResNameTable.txt parses as the original scans it") {
  const auto table = ParseResNameTable(
      "a.gnd#b.gnd#\r\n"
      "a.gnd#ignored.gnd#\r\n"
      "split#\r\nacross.gnd#\r\n"
      "c.gnd#d.gnd# trailing text\n"
      "no hash at all\n");
  const std::map<std::string, std::string> want = {
      {"a.gnd", "b.gnd"}, {"split", "across.gnd"}, {"c.gnd", "d.gnd"}};
  CHECK(table == want);
  CHECK(ParseResNameTable("").empty());
  // An empty token leaves the key empty, so the next token becomes the key.
  CHECK(ParseResNameTable("##x#").empty());
  CHECK(ParseResNameTable("##x#y#").at("x") == "y");
}

TEST_CASE("the resource manager loads once, by type folder, case-insensitively") {
  TempInstall install("ragexe_res_manager");
  install.Write("data/texture/a.bmp", "texture a");
  install.Write("data/texture/b.bmp", "texture b");
  install.Write("data/prontera.rsw", "world");
  install.Write("data/ResNameTable.txt", "alias.bmp#b.bmp#\r\n");
  const DataFiles files(install.dir());
  std::uint32_t now = 100000;
  int resets = 0;
  ResourceManager manager(files, [&] { return now; });
  CHECK(manager.RegisterType("bmp", "texture\\", std::make_unique<TextResource>(&resets)) == 0);
  CHECK(manager.RegisterType("rsw", "", std::make_unique<TextResource>(&resets)) == 1);
  std::vector<std::string> errors;
  manager.set_error_handler([&](std::string_view m) { errors.emplace_back(m); });

  Resource* a = manager.Get("A.BMP");
  REQUIRE(a != nullptr);
  CHECK(static_cast<TextResource*>(a)->text == "texture a");
  CHECK(a->key().name == "texture\\a.bmp");
  CHECK(a->type() == 0);
  CHECK(manager.Get("texture\\a.bmp") == a);
  CHECK(manager.Get("a.bmp") == a);
  CHECK(manager.loaded_count() == 1);

  CHECK(static_cast<TextResource*>(manager.Get("prontera.rsw"))->text == "world");
  CHECK(manager.Get("noextension") == nullptr);
  CHECK(manager.Get("a.xyz") == nullptr);
  CHECK(errors.empty());

  CHECK(manager.Get("Missing.bmp") == nullptr);
  REQUIRE(errors.size() == 1);
  CHECK(errors[0] == "ResourceError : Can't find file missing.bmp");

  SECTION("Create clones the type's prototype without keeping it") {
    const std::size_t loaded = manager.loaded_count();
    auto made = manager.Create("sprite\\X.BMP");
    REQUIRE(made != nullptr);
    CHECK(dynamic_cast<TextResource*>(made.get()) != nullptr);
    CHECK(made->Load(files, "texture\\b.bmp"));
    CHECK(static_cast<TextResource*>(made.get())->text == "texture b");
    CHECK(made->error().empty());
    CHECK(manager.Create("a.xyz") == nullptr);
    CHECK(manager.Create("noextension") == nullptr);
    CHECK(manager.loaded_count() == loaded);
  }

  SECTION("ResNameTable names a stand-in, stored under its own path") {
    REQUIRE(manager.LoadNameTable());
    Resource* alias = manager.Get("alias.bmp");
    REQUIRE(alias != nullptr);
    CHECK(static_cast<TextResource*>(alias)->text == "texture b");
    CHECK(alias->key().name == "texture\\b.bmp");
    CHECK(manager.Get("b.bmp") == alias);
  }
  SECTION("Exists sees loaded resources and files, not ResNameTable") {
    REQUIRE(manager.LoadNameTable());
    CHECK(manager.Exists("a.bmp"));
    CHECK(manager.Exists("B.BMP"));
    CHECK_FALSE(manager.Exists("alias.bmp"));
    CHECK_FALSE(manager.Exists("a.xyz"));
  }
  SECTION("unused resources expire after 15 s; referenced ones stay") {
    Resource* world = manager.Get("prontera.rsw");
    world->AddRef();
    now += ResourceManager::kExpiryMs;
    manager.UnloadExpired();
    CHECK(manager.loaded_count() == 2);
    now += 1;
    manager.UnloadExpired();
    CHECK(manager.loaded_count() == 1);
    CHECK(resets == 1);
    world->Release(now);
    CHECK(world->last_use() == now);
    manager.UnloadUnusedOfType("bmp");
    CHECK(manager.loaded_count() == 1);
    manager.UnloadUnused();
    CHECK(manager.loaded_count() == 0);
    CHECK(resets == 2);
  }
  SECTION("Unload and Clear reset what they delete") {
    manager.Unload(a);
    CHECK(manager.loaded_count() == 1);
    manager.Clear();
    CHECK(manager.loaded_count() == 0);
    CHECK(resets == 2);
  }
}

TEST_CASE("a resource may load others while it loads") {
  TempInstall install("ragexe_res_nested");
  install.Write("data/model/house.rsm", "uses wall.bmp");
  install.Write("data/texture/wall.bmp", "bricks");
  const DataFiles files(install.dir());
  ResourceManager manager(files, [] { return 0u; });

  class Model : public Resource {
   public:
    explicit Model(ResourceManager* manager) : manager_(manager) {}
    std::unique_ptr<Resource> Clone() const override { return std::make_unique<Model>(manager_); }
    bool Load(const DataFiles& /*files*/, std::string_view /*path*/) override {
      texture = manager_->Get("wall.bmp");
      return texture != nullptr;
    }
    Resource* texture = nullptr;

   private:
    ResourceManager* manager_;
  };
  manager.RegisterType("bmp", "texture\\", std::make_unique<TextResource>());
  manager.RegisterType("rsm", "model\\", std::make_unique<Model>(&manager));
  auto* house = static_cast<Model*>(manager.Get("house.rsm"));
  REQUIRE(house != nullptr);
  REQUIRE(house->texture != nullptr);
  CHECK(static_cast<TextResource*>(house->texture)->text == "bricks");
  CHECK(manager.loaded_count() == 2);
}
