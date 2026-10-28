#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "res/data_files.h"
#include "res/hashed_file_name.h"

namespace ragexe::res {

// A loaded resource (CRes; docs/formats/resources.md). Each resource class implements
// Clone and Load; the manager owns every resource it returns.
class Resource {
 public:
  virtual ~Resource() = default;

  // Slot 2: a new, empty object of the same class.
  virtual std::unique_ptr<Resource> Clone() const = 0;
  // Slot 3: loads the resource from a path such as "texture\x.bmp".
  virtual bool Load(const DataFiles& files, std::string_view path) = 0;
  // Slot 1: decodes the resource from bytes already in memory; not every class can.
  virtual bool LoadFromMemory(std::string_view /*name*/, std::span<const std::uint8_t> /*data*/) {
    return false;
  }
  // Slot 4: frees what Load allocated; the manager calls it before destroying a resource.
  virtual void Reset() {}
  // Why the last Load failed, for the classes that keep a reason; not the client's.
  virtual const std::string& error() const;

  void AddRef() { ++references_; }
  // Release (004df960) also marks the resource as used.
  void Release(std::uint32_t now) {
    last_use_ = now;
    --references_;
  }
  void Touch(std::uint32_t now) { last_use_ = now; }

  int references() const { return references_; }
  std::uint32_t last_use() const { return last_use_; }
  int type() const { return type_; }
  const HashedFileName& key() const { return key_; }

 private:
  friend class ResourceManager;

  int references_ = 0;
  std::uint32_t last_use_ = 0;
  int type_ = -1;
  HashedFileName key_;
};

// The resource cache (ResourceManager, 004dfa60): types by extension, one map of loaded
// resources per type, and ResNameTable.txt for names that are stored under another one.
// Thread-safe; a resource's Load may itself call Get, as the original's critical section
// allows.
class ResourceManager {
 public:
  using Clock = std::function<std::uint32_t()>;
  using ErrorHandler = std::function<void(std::string_view message)>;

  // Resources unused this long, with no references, go in UnloadExpired (00612710).
  static constexpr std::uint32_t kExpiryMs = 15000;

  ResourceManager(const DataFiles& files, Clock clock);
  ~ResourceManager();

  // RegisterType (004e06c0): extension without the dot, the folder its names are under
  // (empty for none), and an object to clone. Returns the type's index.
  int RegisterType(std::string extension, std::string folder, std::unique_ptr<Resource> prototype);

  // GetResource (004dff60); nullptr for an unknown extension or a file that does not load.
  Resource* Get(std::string_view name);
  // A new, empty resource of the type the name's extension selects, as GetResource clones
  // it before loading; nullptr for an unknown extension. The manager does not keep it.
  std::unique_ptr<Resource> Create(std::string_view name) const;
  // Exists (004e0190): loaded, or present in the archive or on disk.
  bool Exists(std::string_view name);

  // LoadResNameTable (004e0940) from ResNameTable.txt; false when the file is missing.
  bool LoadNameTable();
  void SetNameTable(std::map<std::string, std::string> table);
  // MapResourceName (004e0f30): the name ResNameTable.txt gives, or the name itself.
  std::string MapName(const std::string& lowered) const;

  void UnloadExpired();                                 // 004e0450
  void UnloadUnused();                                  // 004e0540
  void UnloadUnusedOfType(std::string_view extension);  // 004e0620
  void Unload(Resource* resource);                      // 004e0280
  void Clear();                                         // 004e0310

  std::size_t loaded_count() const;
  void set_error_handler(ErrorHandler handler) { on_error_ = std::move(handler); }

 private:
  struct Type {
    std::string folder;
    std::unique_ptr<Resource> prototype;
    std::map<HashedFileName, std::unique_ptr<Resource>> loaded;
  };

  int TypeIndex(std::string_view lowered_name) const;
  std::string PathFor(const Type& type, std::string_view name) const;
  void Destroy(std::unique_ptr<Resource>& resource);

  const DataFiles& files_;
  Clock clock_;
  ErrorHandler on_error_;
  std::map<std::string, int, std::less<>> extensions_;
  std::vector<Type> types_;
  std::map<std::string, std::string> name_table_;
  mutable std::recursive_mutex mutex_;
};

// The pairs of a ResNameTable.txt, parsed as LoadResNameTable does: '#' ends a key, then
// a value; line breaks only end tokens; the first value of a key wins.
std::map<std::string, std::string> ParseResNameTable(std::string_view text);

// LowerCaseName (004e1040): A-Z to a-z, every other byte unchanged.
std::string LowerCaseName(std::string_view name);

}  // namespace ragexe::res
