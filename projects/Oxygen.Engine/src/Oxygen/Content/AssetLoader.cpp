//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Detail/ResourceKey.h>
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
  RegisterLoader(loaders::LoadGeometryAsset<FileStream<>>);
  RegisterLoader(loaders::LoadMaterialAsset<FileStream<>>);

  // Register resource loaders
  RegisterLoader(loaders::LoadBufferResource<FileStream<>>);
  RegisterLoader(loaders::LoadTextureResource<FileStream<>>);
}

auto AssetLoader::AddPakFile(const std::filesystem::path& path) -> void
{
  paks_.push_back(std::make_unique<PakFile>(path));
}

auto AssetLoader::AddTypeErasedLoader(const TypeId type_id,
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
}

auto AssetLoader::AddResourceDependency(
  const data::AssetKey& dependent, const ResourceKey resource_key) -> void
{
  LOG_SCOPE_F(2, "Add Resource Dependency");

  // Decode ResourceKey for logging
  detail::ResourceKey internal_key(resource_key);
  LOG_F(2, "dependent: {} -> resource: {}", nostd::to_string(dependent),
    nostd::to_string(internal_key));

  // Add forward dependency
  resource_dependencies_[dependent].insert(resource_key);

  // Add reverse dependency for reference counting
  reverse_resource_dependencies_[resource_key].insert(dependent);
}

auto AssetLoader::ReleaseResource(
  const ResourceKey key, const TypeId resource_type) -> void
{
  // Note: resource_type parameter is reserved for future type validation
  // but not currently used since hash key uniquely identifies the resource
  [[maybe_unused]] const auto& type = resource_type;

  detail::ResourceKey internal_key(key);
  const auto key_hash = std::hash<detail::ResourceKey> {}(internal_key);

  // Check for asset dependencies before allowing removal
  const auto it = reverse_resource_dependencies_.find(key);
  const bool has_dependents
    = (it != reverse_resource_dependencies_.end() && !it->second.empty());

  if (!has_dependents) {
    // Safe to decrement - ContentCache will remove when ref count reaches zero
    content_cache_.DecrementRefCount(key_hash);
  } else {
    LOG_F(INFO, "Resource {} has asset dependents, not releasing",
      nostd::to_string(internal_key));
  }
}

//=== Resource Loading Template Implementations ==============================//

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
  if (auto cached = content_cache_.Get<T>(hash_key)) {
    content_cache_.IncrementRefCount(hash_key);
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
          content_cache_.Store<T>(hash_key, typed, 1);
          return typed;
        }
      }
      return nullptr;
    }
  }
  return nullptr;
}

//=== Resource Loading Template Implementations ==============================//

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
  const detail::ResourceKey internal_key(pak_index, resource_index);
  auto key_hash = std::hash<detail::ResourceKey> {}(internal_key);

  // Check cache first using the ResourceKey directly
  if (auto cached = content_cache_.Get<T>(key_hash)) {
    content_cache_.IncrementRefCount(key_hash);
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
      content_cache_.Store<T>(key_hash, typed, 1);
      return typed;
    }
  }
  return nullptr;
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
  const detail::ResourceKey internal_key(key);
  return std::hash<detail::ResourceKey> {}(internal_key);
}

auto AssetLoader::GetPakIndex(const PakFile& pak) const -> uint32_t
{
  // Find the PAK file in our collection and return its index
  for (auto i = 0U; i < paks_.size(); ++i) {
    if (paks_[i].get() == &pak) {
      return i;
    }
  }

  // This should never happen if the PAK is properly managed
  LOG_F(ERROR, "PAK file not found in AssetLoader collection");
  throw std::runtime_error("PAK file not found in AssetLoader collection");
}

auto AssetLoader::ReleaseAsset(
  const data::AssetKey& key, const TypeId asset_type) -> void
{
  // Note: asset_type parameter is reserved for future type validation
  // but not currently used since AssetKey uniquely identifies the asset
  [[maybe_unused]] const auto& type = asset_type;

  const auto hash_key = HashAssetKey(key);
  content_cache_.DecrementRefCount(hash_key);
}

//=== Resource Cache Implementation (Updated for ContentCache) ==============//
