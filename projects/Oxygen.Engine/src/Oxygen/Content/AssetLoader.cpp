//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
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

AssetLoader::AssetLoader()
{
  using serio::FileStream;

  LOG_SCOPE_FUNCTION(INFO);

  // Register asset loaders
  RegisterLoader(
    loaders::LoadGeometryAsset<FileStream<>>, loaders::UnloadGeometryAsset);
  RegisterLoader(
    loaders::LoadMaterialAsset<FileStream<>>, loaders::UnloadMaterialAsset);

  // Register resource loaders
  RegisterLoader(
    loaders::LoadBufferResource<FileStream<>>, loaders::UnloadBufferResource);
  RegisterLoader(
    loaders::LoadTextureResource<FileStream<>>, loaders::UnloadTextureResource);
}

auto AssetLoader::AddPakFile(const std::filesystem::path& path) -> void
{
  // Normalize the path to ensure consistent handling
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path);
  paks_.push_back(std::make_unique<PakFile>(normalized));
}

auto AssetLoader::AddTypeErasedAssetLoader(const TypeId type_id,
  const std::string_view type_name, LoadAssetFnErased loader) -> void
{
  auto [it, inserted] = loaders_.insert_or_assign(type_id, std::move(loader));
  if (!inserted) {
    LOG_F(WARNING, "Replacing loader for type: {}/{}", type_id, type_name);
  } else {
    LOG_F(INFO, "Registered loader for type: {}/{}", type_id, type_name);
  }
}

auto AssetLoader::AddTypeErasedResourceLoader(const TypeId type_id,
  const std::string_view type_name, LoadResourceFnErased loader) -> void
{
  auto [it, inserted]
    = resource_loaders_.insert_or_assign(type_id, std::move(loader));
  if (!inserted) {
    LOG_F(
      WARNING, "Replacing resource loader for type: {}/{}", type_id, type_name);
  } else {
    LOG_F(
      INFO, "Registered resource loader for type: {}/{}", type_id, type_name);
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
auto AssetLoader::MakeAssetLoaderCall(const F& fn, AssetLoader& loader,
  const PakFile& pak, const oxygen::data::pak::AssetDirectoryEntry& entry,
  const bool offline) -> std::shared_ptr<void>
// ReSharper enable CppRedundantQualifier
{
  auto reader = pak.CreateReader(entry);
  LoaderContext<serio::FileStream<>> context {
    .asset_loader = &loader,
    .current_asset_key = entry.asset_key,
    .reader = std::ref(reader),
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
      auto loader_it = loaders_.find(T::ClassTypeId());
      if (loader_it != loaders_.end()) {
        auto void_ptr = loader_it->second(*this, *pak, entry, offline);
        auto typed = std::static_pointer_cast<T>(void_ptr);
        if (typed && typed->GetTypeId() == T::ClassTypeId()) {
          // Cache the loaded asset
          content_cache_.Store(hash_key, typed);
          return typed;
        }
      }
      return nullptr;
    }
  }
  return nullptr;
}

auto AssetLoader::ReleaseAsset(const data::AssetKey& key) -> bool
{
  // Recursively release (check in) the asset and all its dependencies.
  ReleaseAssetTree(key);
  // Return true if the asset is no longer present in the cache
  return !content_cache_.Contains(HashAssetKey(key));
}

auto AssetLoader::ReleaseAssetTree(const data::AssetKey& key) -> void
{
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
      ReleaseAssetTree(dep_key);
    }
    asset_dependencies_.erase(dep_it);
  }
  // Release the asset itself
  content_cache_.CheckIn(HashAssetKey(key));
}

//=== Resource Loading  ======================================================//

template <PakResource T, oxygen::content::LoadFunction F>
auto AssetLoader::MakeResourceLoaderCall(const F& fn, AssetLoader& loader,
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
  if (!table_stream->seek(*offset)) {
    LOG_F(ERROR, "Failed to seek to resource offset {} in PAK file '{}'",
      *offset, pak.FilePath().string());
    return nullptr;
  }
  serio::Reader reader(*table_stream);

  LoaderContext context {
    .asset_loader = &loader,
    .current_asset_key = {}, // No asset key for resources
    .reader = std::ref(reader),
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
  const auto pak_index = GetPakIndex(pak);
  const internal::ResourceKey internal_key(pak_index, resource_index);
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

auto AssetLoader::ReleaseResource(const ResourceKey key) -> bool
{
  const auto key_hash = HashResourceKey(key);

  // The resource should always be checked in on release. Whether it remains in
  // the cache or gets evicted is dependent on the eviction policy.
  content_cache_.CheckIn(HashResourceKey(key));
  if (content_cache_.Contains(key_hash)) {
    return false;
  }

  // If the resource is no longer in the cache, we can, and should, safely
  // unload it.

  // TODO: Implement unloading logic if needed
  LOG_F(WARNING, "TODO: resource must be unloaded at this point", key);
  return true; // Successfully released
}

//=== Explicit Template Instantiations ======================================//

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

// TODO: When adding new resource types, remember to:
// 1. Add explicit template instantiation for LoadResource here
// Example for a future SomeNewResource:
//   template auto
//   AssetLoader::LoadResource<oxygen::data::SomeNewResource>(...);

//=== Hash Key Generation ===================================================//

auto AssetLoader::HashAssetKey(const data::AssetKey& key) -> uint64_t
{
  return std::hash<data::AssetKey> {}(key);
}

auto AssetLoader::HashResourceKey(const ResourceKey& key) -> uint64_t
{
  const internal::ResourceKey internal_key(key);
  return std::hash<internal::ResourceKey> {}(internal_key);
}

auto AssetLoader::GetPakIndex(const PakFile& pak) const -> uint32_t
{
  // Normalize the path of the input pak
  const auto& pak_path = std::filesystem::weakly_canonical(pak.FilePath());

  for (auto i = 0U; i < paks_.size(); ++i) {
    // Compare normalized paths
    if (std::filesystem::weakly_canonical(paks_[i]->FilePath()) == pak_path) {
      return i;
    }
  }

  LOG_F(ERROR, "PAK file not found in AssetLoader collection (by path)");
  throw std::runtime_error("PAK file not found in AssetLoader collection");
}
