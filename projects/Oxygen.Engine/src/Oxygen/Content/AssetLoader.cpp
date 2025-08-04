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
#include <Oxygen/Content/Internal/ResourceKey.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/TextureResource.h>

using oxygen::content::AssetLoader;
using oxygen::content::LoadFunction;
using oxygen::content::PakFile;
using oxygen::content::PakResource;

//=== Helpers to get Resource TypeId by index in ResourceTypeList ============//

namespace {
template <typename... Ts>
std::array<oxygen::TypeId, sizeof...(Ts)> MakeTypeIdArray(
  oxygen::TypeList<Ts...>)
{
  return { Ts::ClassTypeId()... };
}

inline oxygen::TypeId GetResourceTypeIdByIndex(std::size_t type_index)
{
  static const auto ids = MakeTypeIdArray(oxygen::content::ResourceTypeList {});
  return ids.at(type_index);
}
} // namespace

//=== Sanity Checking Helper =================================================//

namespace {
auto SanityCheckResourceEviction(const uint64_t key_hash, uint64_t& cacche_key,
  oxygen::TypeId& type_id) -> bool
{
  CHECK_EQ_F(key_hash, cacche_key);
  // Get the resource type index from the key
  oxygen::content::internal::ResourceKey internal_key(cacche_key);
  const uint16_t resource_type_index = internal_key.GetResourceTypeIndex();
  // Get the class type id for the resource type
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
  LOG_SCOPE_F(2, "Add Asset Dependency");
  LOG_F(2, "dependent: {} -> dependency: {}", nostd::to_string(dependent),
    nostd::to_string(dependency));

  // Add forward dependency
  asset_dependencies_[dependent].insert(dependency);

  // Add reverse dependency for reference counting
  reverse_asset_dependencies_[dependency].insert(dependent);

  // Touch the dependency asset in the cache to increment its reference count
  content_cache_.Touch(HashAssetKey(dependency));
}

auto AssetLoader::AddResourceDependency(
  const data::AssetKey& dependent, const ResourceKey resource_key) -> void
{
  LOG_SCOPE_F(2, "Add Resource Dependency");

  // Decode ResourceKey for logging
  internal::ResourceKey internal_key(resource_key);
  LOG_F(2, "dependent: {} -> resource: {}", nostd::to_string(dependent),
    nostd::to_string(internal_key));

  // Add forward dependency
  resource_dependencies_[dependent].insert(resource_key);

  // Add reverse dependency for reference counting
  reverse_resource_dependencies_[resource_key].insert(dependent);

  // Touch the dependency resource in the cache to increment its reference count
  content_cache_.Touch(resource_key);
}

//=== Asset Loading Implementations ==========================================//

// ReSharper disable CppRedundantQualifier
template <LoadFunction F>
auto AssetLoader::InvokeAssetLoaderFunction(const F& fn, AssetLoader& loader,
  const PakFile& pak, const oxygen::data::pak::AssetDirectoryEntry& entry,
  const bool offline) -> std::shared_ptr<void>
// ReSharper enable CppRedundantQualifier
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
  auto result = fn(context);
  return result ? std::shared_ptr<void>(std::move(result)) : nullptr;
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
        } else {
          LOG_F(ERROR, "Loaded asset type mismatch: expected {}, got {}",
            T::ClassTypeNamePretty(), typed ? typed->GetTypeName() : "nullptr");
        }
      }
      return nullptr;
    }
  }
  return nullptr;
}

auto AssetLoader::ReleaseAsset(const data::AssetKey& key, bool offline) -> bool
{
  // Recursively release (check in) the asset and all its dependencies.
  ReleaseAssetTree(key, offline);
  // Return true if the asset is no longer present in the cache
  return !content_cache_.Contains(HashAssetKey(key));
}

auto AssetLoader::ReleaseAssetTree(const data::AssetKey& key, bool offline)
  -> void
{
  auto guard = content_cache_.OnEviction(
    [&](uint64_t cacche_key, std::shared_ptr<void> value, TypeId type_id) {
      DCHECK_EQ_F(HashAssetKey(key), cacche_key);
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

template <PakResource T, oxygen::content::LoadFunction F>
auto AssetLoader::InvokeResourceLoaderFunction(const F& fn, AssetLoader& loader,
  const PakFile& pak, data::pak::ResourceIndexT resource_index,
  const bool offline) -> std::shared_ptr<void>
{
  // Get ResourceTable and offset for this resource type
  auto* resource_table = pak.GetResourceTable<T>();
  if (!resource_table) {
    LOG_F(ERROR, "PAK file '{}' does not contain resource table for {}",
      pak.FilePath().string(), T::ClassTypeNamePretty());
    return nullptr;
  }
  auto offset = resource_table->GetResourceOffset(resource_index);
  if (!offset) {
    LOG_F(ERROR, "Resource({}) index {} not found in PAK file '{}'",
      T::ClassTypeNamePretty(), resource_index, pak.FilePath().string());
    return nullptr;
  }

  // Create a new FileStream from the original file path, and position it at
  // the correct offset for the Reader that will be used for loading
  const auto table_stream
    = std::make_unique<serio::FileStream<>>(pak.FilePath(), std::ios::in);
  if (!table_stream->Seek(*offset)) {
    LOG_F(ERROR, "Failed to seek to resource offset {} in PAK file '{}'",
      *offset, pak.FilePath().string());
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

  auto result = fn(context);
  return result ? std::shared_ptr<void>(std::move(result)) : nullptr;
}

template <PakResource T>
auto AssetLoader::LoadResource(const PakFile& pak,
  data::pak::ResourceIndexT resource_index, bool offline) -> std::shared_ptr<T>
{
  const uint16_t pak_index = GetPakIndex(pak);
  const uint16_t resource_type_index = IndexOf<T, ResourceTypeList>::value;
  const internal::ResourceKey internal_key(
    pak_index, resource_type_index, resource_index);
  auto key_hash = std::hash<internal::ResourceKey> {}(internal_key);

  // Check cache first using the ResourceKey directly
  if (auto cached = content_cache_.CheckOut<T>(key_hash)) {
    return cached;
  }

  // Validate PAK index
  if (pak_index >= paks_.size()) {
    return nullptr;
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
  const auto key_hash = HashResourceKey(key);

  // The resource should always be checked in on release. Whether it remains in
  // the cache or gets evicted is dependent on the eviction policy.
  auto guard = content_cache_.OnEviction(
    [&](uint64_t cacche_key, std::shared_ptr<void> value, TypeId type_id) {
      DCHECK_F(SanityCheckResourceEviction(key_hash, cacche_key, type_id));
      UnloadObject(type_id, value, offline);
    });
  content_cache_.CheckIn(HashResourceKey(key));
  return (content_cache_.Contains(key_hash)) ? false : true;
}

auto AssetLoader::GetPakIndex(const PakFile& pak) const -> uint16_t
{
  // Normalize the path of the input pak
  const auto& pak_path = std::filesystem::weakly_canonical(pak.FilePath());

  for (uint16_t i = 0U; i < paks_.size(); ++i) {
    // Compare normalized paths
    if (std::filesystem::weakly_canonical(paks_[i]->FilePath()) == pak_path) {
      return i;
    }
  }

  LOG_F(ERROR, "PAK file not found in AssetLoader collection (by path)");
  throw std::runtime_error("PAK file not found in AssetLoader collection");
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
  const internal::ResourceKey internal_key(key);
  return std::hash<internal::ResourceKey> {}(internal_key);
}
