//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/TextureResource.h>

using oxygen::content::AssetLoader;
using oxygen::content::LoaderContext;
using oxygen::content::LoadFunction;
using oxygen::content::PakFile;
using oxygen::content::PakResource;

//=== Helpers to get Resource TypeId by index in ResourceTypeList ============//

namespace {
template <typename... Ts>
auto MakeTypeIdArray(oxygen::TypeList<Ts...> /*unused*/)
  -> std::array<oxygen::TypeId, sizeof...(Ts)>
{
  return { Ts::ClassTypeId()... };
}

inline auto GetResourceTypeIdByIndex(std::size_t type_index)
{
  static const auto ids = MakeTypeIdArray(oxygen::content::ResourceTypeList {});
  return ids.at(type_index);
}
} // namespace

//=== Sanity Checking Helper =================================================//

namespace {
// Helper validates eviction callback arguments. Parameter ordering chosen to
// minimize misuse. expected_key_hash: hash originally computed for resource
// actual_key_hash: hash received from eviction callback
// type_id: type id reference for validation
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto SanityCheckResourceEviction(const uint64_t expected_key_hash,
  uint64_t& actual_key_hash, oxygen::TypeId& type_id) -> bool
{
  CHECK_EQ_F(expected_key_hash, actual_key_hash);
  oxygen::content::internal::InternalResourceKey internal_key(actual_key_hash);
  const uint16_t resource_type_index = internal_key.GetResourceTypeIndex();
  const auto class_type_id = GetResourceTypeIdByIndex(resource_type_index);
  CHECK_EQ_F(type_id, class_type_id);
  return true;
}
} // namespace

//=== Basic methods ==========================================================//

AssetLoader::AssetLoader()
{
  using serio::FileStream;

  LOG_SCOPE_FUNCTION(INFO);

  owning_thread_id_ = std::this_thread::get_id();

  // Register asset loaders
  RegisterLoader(loaders::LoadGeometryAsset, loaders::UnloadGeometryAsset);
  RegisterLoader(loaders::LoadMaterialAsset, loaders::UnloadMaterialAsset);

  // Register resource loaders
  RegisterLoader(loaders::LoadBufferResource, loaders::UnloadBufferResource);
  RegisterLoader(loaders::LoadTextureResource, loaders::UnloadTextureResource);
}

auto AssetLoader::AddPakFile(const std::filesystem::path& path) -> void
{
  // Normalize the path to ensure consistent handling
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path);
  paks_.push_back(std::make_unique<PakFile>(normalized));
}

auto AssetLoader::AddTypeErasedAssetLoader(const TypeId type_id,
  const std::string_view type_name, LoadAssetFnErased&& loader) -> void
{
  auto [it, inserted] = asset_loaders_.insert_or_assign(
    type_id, std::forward<LoadAssetFnErased>(loader));
  if (!inserted) {
    LOG_F(WARNING, "Replacing loader for type: {}/{}", type_id, type_name);
  } else {
    LOG_F(INFO, "Registered loader for type: {}/{}", type_id, type_name);
  }
}

auto AssetLoader::AddTypeErasedResourceLoader(const TypeId type_id,
  const std::string_view type_name, LoadResourceFnErased&& loader) -> void
{
  auto [it, inserted] = resource_loaders_.insert_or_assign(
    type_id, std::forward<LoadResourceFnErased>(loader));
  if (!inserted) {
    LOG_F(
      WARNING, "Replacing resource loader for type: {}/{}", type_id, type_name);
  } else {
    LOG_F(
      INFO, "Registered resource loader for type: {}/{}", type_id, type_name);
  }
}

auto AssetLoader::AddTypeErasedUnloader(const TypeId type_id,
  const std::string_view type_name, UnloadFnErased&& unloader) -> void
{
  auto [it, inserted] = unloaders_.insert_or_assign(
    type_id, std::forward<UnloadFnErased>(unloader));
  if (!inserted) {
    LOG_F(WARNING, "Replacing unloader for type: {}/{}", type_id, type_name);
  } else {
    LOG_F(INFO, "Registered unloader for type: {}/{}", type_id, type_name);
  }
}

//=== Dependency management ==================================================//

auto AssetLoader::AddAssetDependency(
  const data::AssetKey& dependent, const data::AssetKey& dependency) -> void
{
  AssertOwningThread();
  LOG_SCOPE_F(2, "Add Asset Dependency");
  LOG_F(2, "dependent: {} -> dependency: {}",
    nostd::to_string(dependent).c_str(), nostd::to_string(dependency).c_str());

  // Cycle detection: adding edge dependent -> dependency must not create a path
  // dependency -> ... -> dependent (checked via DetectCycle).
  if (DetectCycle(dependency, dependent)) {
    LOG_F(ERROR, "Rejecting asset dependency that introduces a cycle: {} -> {}",
      nostd::to_string(dependent).c_str(),
      nostd::to_string(dependency).c_str());
#if !defined(NDEBUG)
    DCHECK_F(false, "Cycle detected in asset dependency graph");
#endif
    return; // Do not insert
  }

  // Add forward dependency only (reference counting handled by cache Touch)
  asset_dependencies_[dependent].insert(dependency);

  // Touch the dependency asset in the cache to increment its reference count
  content_cache_.Touch(HashAssetKey(dependency));
}

auto AssetLoader::AddResourceDependency(
  const data::AssetKey& dependent, ResourceKey resource_key) -> void
{
  AssertOwningThread();
  LOG_SCOPE_F(2, "Add Resource Dependency");

  // Decode ResourceKey for logging
  AssertOwningThread();
  internal::InternalResourceKey internal_key(resource_key);
  LOG_F(2, "dependent: {} -> resource: {}", nostd::to_string(dependent).c_str(),
    nostd::to_string(internal_key).c_str());

  // Add forward dependency only (reference counting handled by cache Touch)
  resource_dependencies_[dependent].insert(resource_key);

  // Touch the dependency resource in the cache to increment its reference count
  content_cache_.Touch(resource_key);
}

//=== Asset Loading Implementations ==========================================//

// Common template implementation for asset loading
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto AssetLoader::InvokeAssetLoaderImpl(
  std::function<std::shared_ptr<void>(LoaderContext)> loader_fn,
  AssetLoader& loader, const PakFile& pak,
  const data::pak::AssetDirectoryEntry& entry, bool offline)
  -> std::shared_ptr<void>
{
  auto reader = pak.CreateReader(entry);
  // FIXME: for now we just get a reader on the PAK file itself - future mem map
  auto buf_reader = pak.CreateDataReader<data::BufferResource>();
  auto tex_reader = pak.CreateDataReader<data::TextureResource>();

  LoaderContext context {
    .asset_loader = &loader,
    .current_asset_key = entry.asset_key,
    .desc_reader = &reader,
    .data_readers = std::make_tuple(&buf_reader, &tex_reader),
    .offline = offline,
    .source_pak = &pak,
  };

  return loader_fn(context);
}

template <oxygen::IsTyped T>
auto AssetLoader::LoadAsset(const oxygen::data::AssetKey& key, bool offline)
  -> std::shared_ptr<T>
{
  // Check cache first
  auto hash_key = HashAssetKey(key);
  if (auto cached = content_cache_.CheckOut<T>(hash_key)) {
    return cached;
  }

  // Not cached - load from PAK files
  for (const auto& pak : paks_) {
    if (const auto entry_opt = pak->FindEntry(key)) {
      const auto& entry = *entry_opt;
      auto loader_it = asset_loaders_.find(T::ClassTypeId());
      if (loader_it != asset_loaders_.end()) {
        auto void_ptr = loader_it->second(*this, *pak, entry, offline);
        auto typed = std::static_pointer_cast<T>(void_ptr);
        if (typed && typed->GetTypeId() == T::ClassTypeId()) {
          // Cache the loaded asset
          content_cache_.Store(hash_key, typed);
          return typed;
        }
        LOG_F(ERROR, "Loaded asset type mismatch: expected {}, got {}",
          T::ClassTypeNamePretty(), typed ? typed->GetTypeName() : "nullptr");
      }
      return nullptr;
    }
  }
  return nullptr;
}

auto AssetLoader::ReleaseAsset(const data::AssetKey& key, bool offline) -> bool
{
  AssertOwningThread();
  // Recursively release (check in) the asset and all its dependencies.
  ReleaseAssetTree(key, offline);
  // Return true if the asset is no longer present in the cache
  return !content_cache_.Contains(HashAssetKey(key));
}

auto AssetLoader::ReleaseAssetTree(const data::AssetKey& key, bool offline)
  -> void
{
  AssertOwningThread();

#if !defined(NDEBUG)
  static thread_local std::unordered_set<data::AssetKey> release_visit_set;
  const bool inserted = release_visit_set.emplace(key).second;
  DCHECK_F(inserted, "Cycle encountered during ReleaseAssetTree recursion");
  class VisitGuard final {
  public:
    VisitGuard(
      std::unordered_set<data::AssetKey>& set, data::AssetKey k) noexcept
      : set_(set)
      , key_(std::move(k))
    {
    }
    ~VisitGuard() { set_.erase(key_); }
    VisitGuard(const VisitGuard&) = delete;
    VisitGuard& operator=(const VisitGuard&) = delete;

  private:
    std::unordered_set<data::AssetKey>& set_;
    data::AssetKey key_;
  } visit_guard(release_visit_set, key);
#endif

  auto eviction_guard
    = content_cache_.OnEviction([&]([[maybe_unused]] uint64_t cache_key,
                                  std::shared_ptr<void> value, TypeId type_id) {
        DCHECK_EQ_F(HashAssetKey(key), cache_key);
        LOG_F(2, "Evict asset: key_hash={} type_id={}", cache_key, type_id);
        UnloadObject(type_id, value, offline);
      });

  // Release resource dependencies first
  auto res_dep_it = resource_dependencies_.find(key);
  if (res_dep_it != resource_dependencies_.end()) {
    for (const auto& res_key : res_dep_it->second) {
      content_cache_.CheckIn(res_key);
    }
    resource_dependencies_.erase(res_dep_it);
  }
  // Then release asset dependencies
  auto dep_it = asset_dependencies_.find(key);
  if (dep_it != asset_dependencies_.end()) {
    for (const auto& dep_key : dep_it->second) {
      ReleaseAssetTree(dep_key, offline);
    }
    asset_dependencies_.erase(dep_it);
  }
  // Release the asset itself
  content_cache_.CheckIn(HashAssetKey(key));
}

//=== Resource Loading  ======================================================//

// Common implementation for resource loading
template <PakResource T>
auto AssetLoader::InvokeResourceLoaderImpl(
  const std::function<std::shared_ptr<void>(LoaderContext)>& loader_fn,
  AssetLoader& loader, const PakFile& pak,
  data::pak::ResourceIndexT resource_index, bool offline)
  -> std::shared_ptr<void>
{
  // Get ResourceTable and offset for this resource type
  auto* resource_table = pak.GetResourceTable<T>();
  if (!resource_table) {
    LOG_F(ERROR, "PAK file '{}' does not contain resource table for {}",
      pak.FilePath().string().c_str(), T::ClassTypeNamePretty());
    return nullptr;
  }
  auto offset = resource_table->GetResourceOffset(resource_index);
  if (!offset) {
    LOG_F(ERROR, "Resource({}) index {} not found in PAK file '{}'",
      T::ClassTypeNamePretty(), resource_index,
      pak.FilePath().string().c_str());
    return nullptr;
  }

  // Create a new FileStream from the original file path, and position it at
  // the correct offset for the Reader that will be used for loading
  const auto table_stream
    = std::make_unique<serio::FileStream<>>(pak.FilePath(), std::ios::in);
  if (!table_stream->Seek(*offset)) {
    LOG_F(ERROR, "Failed to seek to resource offset {} in PAK file '{}'",
      *offset, pak.FilePath().string().c_str());
    return nullptr;
  }
  serio::Reader reader(*table_stream);
  // FIXME: for now we just get a reader on the PAK file itself - future mem map
  auto buf_reader = pak.CreateDataReader<data::BufferResource>();
  auto tex_reader = pak.CreateDataReader<data::TextureResource>();

  LoaderContext context {
    .asset_loader = &loader,
    .current_asset_key = {}, // No asset key for resources
    .desc_reader = &reader,
    .data_readers = std::make_tuple(&buf_reader, &tex_reader),
    .offline = offline,
    .source_pak = &pak,
  };

  return loader_fn(context);
}

// Concrete implementations for specific resource types
auto AssetLoader::InvokeBufferResourceLoader(
  std::function<std::shared_ptr<void>(LoaderContext)> loader_fn,
  AssetLoader& loader, const PakFile& pak,
  data::pak::ResourceIndexT resource_index, bool offline)
  -> std::shared_ptr<void>
{
  return InvokeResourceLoaderImpl<data::BufferResource>(
    loader_fn, loader, pak, resource_index, offline);
}

auto AssetLoader::InvokeTextureResourceLoader(
  std::function<std::shared_ptr<void>(LoaderContext)> loader_fn,
  AssetLoader& loader, const PakFile& pak,
  data::pak::ResourceIndexT resource_index, bool offline)
  -> std::shared_ptr<void>
{
  return InvokeResourceLoaderImpl<data::TextureResource>(
    loader_fn, loader, pak, resource_index, offline);
}

template <PakResource T>
auto AssetLoader::LoadResource(const PakFile& pak,
  data::pak::ResourceIndexT resource_index, bool offline) -> std::shared_ptr<T>
{
  const uint16_t pak_index = GetPakIndex(pak);
  const uint16_t resource_type_index = IndexOf<T, ResourceTypeList>::value;
  const internal::InternalResourceKey internal_key(
    pak_index, resource_type_index, resource_index);
  auto key_hash = std::hash<internal::InternalResourceKey> {}(internal_key);

  // Check cache first using the ResourceKey directly
  if (auto cached = content_cache_.CheckOut<T>(key_hash)) {
    return cached;
  }

  auto loader_it = resource_loaders_.find(T::ClassTypeId());
  if (loader_it != resource_loaders_.end()) {
    auto void_ptr = loader_it->second(*this, pak, resource_index, offline);
    auto typed = std::static_pointer_cast<T>(void_ptr);
    if (typed && typed->GetTypeId() == T::ClassTypeId()) {
      // Cache the loaded resource
      content_cache_.Store(key_hash, typed);
      return typed;
    }
  }
  return nullptr;
}

void oxygen::content::AssetLoader::UnloadObject(
  oxygen::TypeId& type_id, std::shared_ptr<void>& value, bool offline)
{
  // Invoke the resource unload function
  auto unload_it = unloaders_.find(type_id);
  if (unload_it != unloaders_.end()) {
    unload_it->second(value, *this, offline);
  } else {
    LOG_F(WARNING, "No unload function registered for type: {}", type_id);
  }
}

auto AssetLoader::ReleaseResource(const ResourceKey key, bool offline) -> bool
{
  AssertOwningThread();
  const auto key_hash = HashResourceKey(key);

  // The resource should always be checked in on release. Whether it remains in
  // the cache or gets evicted is dependent on the eviction policy.
  auto guard
    = content_cache_.OnEviction([&]([[maybe_unused]] uint64_t cache_key,
                                  std::shared_ptr<void> value, TypeId type_id) {
        DCHECK_F(SanityCheckResourceEviction(key_hash, cache_key, type_id));
        LOG_F(2, "Evict resource: key_hash={} type_id={}", cache_key, type_id);
        UnloadObject(type_id, value, offline);
      });
  content_cache_.CheckIn(HashResourceKey(key));
  return !content_cache_.Contains(key_hash);
}

auto AssetLoader::GetPakIndex(const PakFile& pak) const -> uint16_t
{
  // Normalize the path of the input pak
  const auto& pak_path = std::filesystem::weakly_canonical(pak.FilePath());

  for (size_t i = 0; i < paks_.size(); ++i) {
    // Compare normalized paths
    if (std::filesystem::weakly_canonical(paks_[i]->FilePath()) == pak_path) {
      return static_cast<uint16_t>(i);
    }
  }

  LOG_F(ERROR, "PAK file not found in AssetLoader collection (by path)");
  throw std::runtime_error("PAK file not found in AssetLoader collection");
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto AssetLoader::DetectCycle(
  const data::AssetKey& start, const data::AssetKey& target) -> bool
{
#if !defined(NDEBUG)
  // DFS stack
  std::vector<data::AssetKey> stack;
  std::unordered_set<data::AssetKey> visited;
  stack.push_back(start);
  while (!stack.empty()) {
    auto current = stack.back();
    stack.pop_back();
    if (current == target) {
      return true; // path start -> ... -> target exists
    }
    if (!visited.insert(current).second) {
      continue;
    }
    auto it = asset_dependencies_.find(current);
    if (it != asset_dependencies_.end()) {
      for (const auto& dep : it->second) {
        stack.push_back(dep);
      }
    }
  }
#else
  (void)start;
  (void)target;
#endif
  return false;
}

//=== Explicit Template Instantiations =======================================//

// Instantiate for all supported asset types
template auto AssetLoader::LoadAsset<oxygen::data::GeometryAsset>(
  const data::AssetKey& key, bool offline)
  -> std::shared_ptr<data::GeometryAsset>;
template auto AssetLoader::LoadAsset<oxygen::data::MaterialAsset>(
  const data::AssetKey& key, bool offline)
  -> std::shared_ptr<data::MaterialAsset>;

// Instantiate for all supported resource types
template auto AssetLoader::LoadResource<oxygen::data::BufferResource>(
  const PakFile& pak, data::pak::ResourceIndexT resource_index, bool)
  -> std::shared_ptr<data::BufferResource>;
template auto AssetLoader::LoadResource<oxygen::data::TextureResource>(
  const PakFile& pak, data::pak::ResourceIndexT resource_index, bool)
  -> std::shared_ptr<data::TextureResource>;

//=== Hash Key Generation ====================================================//

auto AssetLoader::HashAssetKey(const data::AssetKey& key) -> uint64_t
{
  return std::hash<data::AssetKey> {}(key);
}

auto AssetLoader::HashResourceKey(const ResourceKey& key) -> uint64_t
{
  const internal::InternalResourceKey internal_key(key);
  return std::hash<internal::InternalResourceKey> {}(internal_key);
}
