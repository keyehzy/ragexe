#include "res/resource.h"

#include <iterator>
#include <utility>

#include "res/data_file.h"
#include "res/data_path.h"

namespace ragexe::res {

std::string LowerCaseName(std::string_view name) {
  std::string out(name);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

std::map<std::string, std::string> ParseResNameTable(std::string_view text) {
  std::map<std::string, std::string> table;
  std::string key;
  std::size_t token = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\n' || c == '\r') {
      token = i + 1;
    } else if (c == '#') {
      const std::string_view field = text.substr(token, i - token);
      if (key.empty()) {
        key = field;
      } else {
        table.emplace(std::move(key), std::string(field));
        key.clear();
      }
      token = i + 1;
    }
  }
  return table;
}

ResourceManager::ResourceManager(const DataFiles& files, Clock clock)
    : files_(files), clock_(std::move(clock)) {}

ResourceManager::~ResourceManager() { Clear(); }

int ResourceManager::RegisterType(std::string extension, std::string folder,
                                  std::unique_ptr<Resource> prototype) {
  std::scoped_lock lock(mutex_);
  const int index = static_cast<int>(types_.size());
  extensions_.emplace(std::move(extension), index);
  types_.push_back(Type{std::move(folder), std::move(prototype), {}});
  return index;
}

int ResourceManager::TypeIndex(std::string_view lowered_name) const {
  // GetExtension (004e0e70): after the last '.', or no type when there is none.
  const std::size_t dot = lowered_name.rfind('.');
  if (dot == std::string_view::npos) return -1;
  const auto it = extensions_.find(lowered_name.substr(dot + 1));
  return it == extensions_.end() ? -1 : it->second;
}

// AddTypeFolder (004e0df0): the folder, then the name, unless the name starts with it.
std::string ResourceManager::PathFor(const Type& type, std::string_view name) const {
  if (name.starts_with(type.folder)) return std::string(name);
  return type.folder + std::string(name);
}

Resource* ResourceManager::Get(std::string_view name) {
  std::scoped_lock lock(mutex_);
  const std::string lowered = LowerCaseName(name);
  const int index = TypeIndex(lowered);
  if (index < 0) return nullptr;
  Type& type = types_[static_cast<std::size_t>(index)];

  std::string path = PathFor(type, lowered);
  if (auto it = type.loaded.find(HashedFileName(path)); it != type.loaded.end()) {
    it->second->Touch(clock_());
    return it->second.get();
  }
  std::unique_ptr<Resource> resource = type.prototype->Clone();
  if (!resource->Load(files_, path)) {
    path = PathFor(type, MapName(lowered));
    if (!resource->Load(files_, path)) {
      // OnLoadError (004df9d0) gets the lower-cased name (004e0096); the clone is then
      // deleted without Reset (004e00ac).
      if (on_error_) on_error_("ResourceError : Can't find file " + lowered);
      return nullptr;
    }
  }
  resource->key_ = HashedFileName(path);
  resource->type_ = index;
  resource->Touch(clock_());
  // The type's map keeps the first resource stored under a name.
  auto [it, inserted] = type.loaded.emplace(resource->key_, std::move(resource));
  return it->second.get();
}

bool ResourceManager::Exists(std::string_view name) {
  std::scoped_lock lock(mutex_);
  const std::string lowered = LowerCaseName(name);
  const int index = TypeIndex(lowered);
  if (index < 0) return false;
  Type& type = types_[static_cast<std::size_t>(index)];
  const std::string path = PathFor(type, lowered);
  if (auto it = type.loaded.find(HashedFileName(path)); it != type.loaded.end()) {
    it->second->Touch(clock_());
    return true;
  }
  return files_.Exists(MakeDataPath(path));
}

bool ResourceManager::LoadNameTable() {
  DataFile file;
  if (!file.Open(files_, "ResNameTable.txt")) {
    SetNameTable({});
    return false;
  }
  const auto bytes = file.data();
  SetNameTable(ParseResNameTable(
      std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
  return true;
}

void ResourceManager::SetNameTable(std::map<std::string, std::string> table) {
  std::scoped_lock lock(mutex_);
  name_table_ = std::move(table);
}

std::string ResourceManager::MapName(const std::string& lowered) const {
  std::scoped_lock lock(mutex_);
  const auto it = name_table_.find(lowered);
  return it == name_table_.end() ? lowered : it->second;
}

void ResourceManager::Destroy(std::unique_ptr<Resource>& resource) {
  resource->Reset();
  resource.reset();
}

void ResourceManager::UnloadExpired() {
  std::scoped_lock lock(mutex_);
  // Unsigned, as the original compares: right after start-up the limit wraps around.
  const std::uint32_t limit = clock_() - kExpiryMs;
  for (Type& type : types_) {
    for (auto it = type.loaded.begin(); it != type.loaded.end();) {
      if (it->second->references() == 0 && it->second->last_use() < limit) {
        Destroy(it->second);
        it = type.loaded.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void ResourceManager::UnloadUnused() {
  std::scoped_lock lock(mutex_);
  for (Type& type : types_) {
    for (auto it = type.loaded.begin(); it != type.loaded.end();) {
      if (it->second->references() == 0) {
        Destroy(it->second);
        it = type.loaded.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void ResourceManager::UnloadUnusedOfType(std::string_view extension) {
  std::scoped_lock lock(mutex_);
  const auto found = extensions_.find(extension);
  if (found == extensions_.end()) return;
  Type& type = types_[static_cast<std::size_t>(found->second)];
  for (auto it = type.loaded.begin(); it != type.loaded.end();) {
    if (it->second->references() == 0) {
      Destroy(it->second);
      it = type.loaded.erase(it);
    } else {
      ++it;
    }
  }
}

void ResourceManager::Unload(Resource* resource) {
  std::scoped_lock lock(mutex_);
  if (resource == nullptr || resource->type_ < 0) return;
  Type& type = types_[static_cast<std::size_t>(resource->type_)];
  const auto it = type.loaded.find(resource->key_);
  if (it == type.loaded.end() || it->second.get() != resource) return;
  Destroy(it->second);
  type.loaded.erase(it);
}

void ResourceManager::Clear() {
  std::scoped_lock lock(mutex_);
  for (Type& type : types_) {
    for (auto& [key, resource] : type.loaded) Destroy(resource);
    type.loaded.clear();
  }
}

std::size_t ResourceManager::loaded_count() const {
  std::scoped_lock lock(mutex_);
  std::size_t n = 0;
  for (const Type& type : types_) n += type.loaded.size();
  return n;
}

}  // namespace ragexe::res
