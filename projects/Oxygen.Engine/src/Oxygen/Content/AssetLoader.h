//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/data/AssetKey.h>
#include <Oxygen/data/GeometryAsset.h>
#include <Oxygen/data/MaterialAsset.h>

namespace oxygen::content {

// Type aliases for cleaner code
using ResourceKey = uint64_t;

class AssetLoader {
public:
  OXGN_CNTT_API AssetLoader();
  virtual ~AssetLoader() = default;

  OXYGEN_MAKE_NON_COPYABLE(AssetLoader)
  OXYGEN_DEFAULT_MOVABLE(AssetLoader)

  OXGN_CNTT_API auto AddPakFile(const std::filesystem::path& path) -> void;

  //=== Dependency Management ===---------------------------------------------//

  //! Register an asset-to-asset dependency
  /*!
   Records that an asset depends on another asset for proper loading order
   and reference counting.

   @param dependent The asset that has the dependency
   @param dependency The asset that is depended upon
   */
  OXGN_CNTT_API virtual auto AddAssetDependency(
    const data::AssetKey& dependent, const data::AssetKey& dependency) -> void;

  //! Register an asset-to-resource dependency
  /*!
   Records that an asset depends on a resource for proper loading order
   and reference counting.

   @param dependent The asset that has the dependency
   @param resource_key The ResourceKey that is depended upon
   */
  OXGN_CNTT_API virtual auto AddResourceDependency(
    const data::AssetKey& dependent, ResourceKey resource_key) -> void;

  //=== Asset Loading
  //===-----------------------------------------------------------//

  //! Load or get cached asset
  /*!
   Loads an asset by key, using the unified content cache. Assets are cached
   globally across all PAK files.

   @tparam T The asset type (must satisfy IsTyped)
   @param key The asset key to load
   @param offline Whether to load in offline mode (no GPU side effects)
   @return Shared pointer to the asset, or nullptr if not found

   ### Performance Characteristics
   - Time Complexity: O(1) if cached, O(log n) + load time if uncached
   - Memory: Shared assets cached with reference counting
   - Optimization: Multiple requests return same cached instance

   @note Automatically increments reference count for shared assets
   @see GetAsset, HasAsset, ReleaseAsset
  */
  template <IsTyped T>
  auto LoadAsset(const data::AssetKey& key, bool offline = false)
    -> std::shared_ptr<T>;

  //! Get cached asset without loading
  /*!
   Returns an asset if it's already loaded in the cache, without triggering
   a load operation.

   @tparam T The asset type (must satisfy IsTyped)
   @param key The asset key to check
   @return Shared pointer to cached asset, or nullptr if not cached

   @note Does not increment reference count
   @see LoadAsset, HasAsset
  */
  template <IsTyped T>
  auto GetAsset(const data::AssetKey& key) const -> std::shared_ptr<T>
  {
    return content_cache_.CheckOut<T>(HashAssetKey(key));
  }

  //! Check if asset is loaded in cache
  /*!
   Checks whether an asset is currently loaded in the cache.

   @tparam T The asset type (must satisfy IsTyped)
   @param key The asset key to check
   @return True if asset is cached, false otherwise

   @note Does not trigger loading or affect reference count
   @see Load, GetAsset
  */
  template <IsTyped T> auto HasAsset(const data::AssetKey& key) const -> bool
  {
    return content_cache_.Contains(HashAssetKey(key));
  }

  //! Release asset reference (decrements ref count)
  /*!
   Decrements the reference count for an asset. When count reaches zero,
   the asset is removed from the cache.

   @param key The asset key to release
   @param asset_type TypeId of the asset type

   @note Automatically removes asset when ref count reaches zero
   @see LoadAsset, HasAsset
  */
  OXGN_CNTT_API auto ReleaseAsset(const data::AssetKey& key, TypeId asset_type)
    -> void;

  //=== Resource Loading ===================================================//

  //! Load or get cached resource from a specific PAK file and resource index
  /*!
   Loads a resource of type T from the specified PAK file and resource index,
   or returns a cached instance if already loaded. This function is used when
   the caller knows both the PAK file and the resource's index within that file.

   @tparam T The resource type (must satisfy PakResource concept)
   @param pak The PAK file containing the resource
   @param resource_index The index of the resource within the PAK file
   @param offline Whether to load in offline mode (no GPU side effects)
   @return Shared pointer to the resource, or nullptr if not found

   ### Performance Characteristics

   - Time Complexity: O(1) if cached, O(log n) + load time if uncached
   - Memory: Shared resources cached with reference counting
   - Optimization: Multiple requests return the same cached instance

   ### Usage Examples

   ```cpp
   // In a loader (GeometryLoader example):
   auto vertex_buffer = context.asset_loader->LoadResource<BufferResource>(
     desc.vertex_buffer_key, context. offline);
   ```

   @note Automatically increments reference count for shared resources
   @warning Returns nullptr if ResourceKey is invalid or resource not found
   @see GetResource, HasResource, ReleaseResource
  */
  template <PakResource T>
  auto LoadResource(const PakFile& pak,
    data::pak::ResourceIndexT resource_index, bool offline = false)
    -> std::shared_ptr<T>;

  //! Get cached resource without loading
  /*!
   Returns a resource if it's already loaded in the cache, without triggering
   a load operation.

   @tparam T The resource type (must satisfy PakResource concept)
   @param key The ResourceKey identifying the resource
   @return Shared pointer to cached resource, or nullptr if not cached

   @note Does not increment reference count
   @see LoadResource, HasResource
  */
  template <PakResource T>
  auto GetResource(ResourceKey key) const noexcept -> std::shared_ptr<T>
  {
    return content_cache_.CheckOut<T>(HashResourceKey(key));
  }

  //! Check if resource is loaded
  /*!
   Checks whether a resource is currently loaded in the engine-wide cache.

   @tparam T The resource type (must satisfy PakResource concept)
   @param key The ResourceKey identifying the resource
   @return True if resource is cached, false otherwise

   @note Does not trigger loading or affect reference count
   @see LoadResource, GetResource
  */
  template <PakResource T>
  auto HasResource(ResourceKey key) const noexcept -> bool
  {
    return content_cache_.Contains(HashResourceKey(key));
  }

  //! Release resource reference (decrements ref count)
  /*!
   Decrements the reference count for a resource. When count reaches zero AND
   no assets depend on the resource, triggers cleanup and notifies the
   originating ResourceTable via OnResourceUnloaded().

   @param key The ResourceKey identifying the resource
   @param resource_type TypeId of the resource type (for type-erased storage)

   @note Automatically cascades to dependent resources if safe to unload
   @note Calls ResourceTable::OnResourceUnloaded() when actually unloaded
   @see LoadResource, HasResourceDependents
  */
  OXGN_CNTT_API auto ReleaseResource(ResourceKey key, TypeId resource_type)
    -> void;

protected:
  //! Register a loader for assets or resources (unified interface)
  /*!
   Registers a loader function for assets or resources. The system automatically
   determines the type from the loader function signature and uses the
   appropriate loading strategy.

   @tparam F The loader function type (must satisfy LoadFunction)
   @param fn The loader function to register

   ### Usage Examples
   ```cpp
   // Register asset loaders - type inferred from function signature
   asset_loader.RegisterLoader(LoadGeometryAsset);
   asset_loader.RegisterLoader(LoadMaterialAsset);

   // Register resource loaders - type inferred from function signature
   asset_loader.RegisterLoader(LoadBufferResource);
   asset_loader.RegisterLoader(LoadTextureResource);
   ```

   @note Uses same LoaderContext interface for both assets and resources
   @note Type is automatically inferred from loader function return type
   @note Only one loader per type is supported
   @see LoadAsset, LoadResource
  */
  template <LoadFunction F> auto RegisterLoader(F&& fn) -> void
  {
    // Infer the type from the loader function signature
    using LoaderPtr
      = decltype(fn(std::declval<LoaderContext<serio::FileStream<>>>()));
    using T = std::remove_pointer_t<typename LoaderPtr::element_type>;
    static_assert(IsTyped<T>, "T must satisfy IsTyped concept");

    auto type_id = T::ClassTypeId();
    auto type_name = T::ClassTypeNamePretty();

    if constexpr (PakResource<T>) {
      // Resource loader path
      LoadResourceFnErased erased
        = [fn = std::forward<F>(fn)](AssetLoader& loader, const PakFile& pak,
            data::pak::ResourceIndexT resource_index,
            bool offline) -> std::shared_ptr<void> {
        return MakeResourceLoaderCall<T>(
          fn, loader, pak, resource_index, offline);
      };
      AddTypeErasedResourceLoader(type_id, type_name, std::move(erased));

    } else {
      // Asset loader path
      LoadAssetFnErased erased
        = [fn = std::forward<F>(fn)](AssetLoader& loader, const PakFile& pak,
            const data::pak::AssetDirectoryEntry& entry,
            bool offline) -> std::shared_ptr<void> {
        return MakeAssetLoaderCall(fn, loader, pak, entry, offline);
      };
      AddTypeErasedLoader(type_id, type_name, std::move(erased));
    }
  }

private:
  // Use shared_ptr<void> for type erasure, loader functions get AssetLoader&
  // reference
  using LoadAssetFnErased = std::function<std::shared_ptr<void>(
    AssetLoader&, const PakFile&, const data::pak::AssetDirectoryEntry&, bool)>;

  // Resource loader type erasure - similar to asset loaders but with
  // ResourceIndexT
  using LoadResourceFnErased = std::function<std::shared_ptr<void>(
    AssetLoader&, const PakFile&, data::pak::ResourceIndexT, bool)>;

  std::unordered_map<TypeId, LoadAssetFnErased> loaders_;
  std::unordered_map<TypeId, LoadResourceFnErased> resource_loaders_;

  //=== Dependency Tracking ===-----------------------------------------------//

  // Asset-to-asset dependencies: dependent_asset -> set of dependency_assets
  std::unordered_map<data::AssetKey, std::unordered_set<data::AssetKey>>
    asset_dependencies_;

  // Asset-to-resource dependencies: dependent_asset -> set of resource_keys
  std::unordered_map<data::AssetKey, std::unordered_set<ResourceKey>>
    resource_dependencies_;

  // Reverse mapping: dependency_asset -> set of dependent_assets (for reference
  // counting)
  std::unordered_map<data::AssetKey, std::unordered_set<data::AssetKey>>
    reverse_asset_dependencies_;

  // Reverse mapping: resource_key -> set of dependent_assets (for reference
  // counting)
  std::unordered_map<ResourceKey, std::unordered_set<data::AssetKey>>
    reverse_resource_dependencies_;

  //=== Unified Content Cache ================================================//

  //! Unified content cache for both assets and resources
  AnyCache<uint64_t, RefCountedEviction<uint64_t>> content_cache_;

  //! Hash an AssetKey for cache storage
  OXGN_CNTT_API static auto HashAssetKey(const data::AssetKey& key) -> uint64_t;

  //! Hash an AssetKey for cache storage
  OXGN_CNTT_API static auto HashResourceKey(const ResourceKey& key) -> uint64_t;

  //! Get PAK file index from pointer (for resource key creation)
  auto GetPakIndex(const PakFile& pak) const -> uint32_t;

  //! Helper: Create asset loader call with unified error handling
  template <LoadFunction F>
  static auto MakeAssetLoaderCall(const F& fn, AssetLoader& loader,
    const PakFile& pak, const data::pak::AssetDirectoryEntry& entry,
    bool offline) -> std::shared_ptr<void>;

  //! Helper: Create resource loader call with unified error handling
  template <PakResource T, LoadFunction F>
  static auto MakeResourceLoaderCall(const F& fn, AssetLoader& loader,
    const PakFile& pak, data::pak::ResourceIndexT resource_index, bool offline)
    -> std::shared_ptr<void>;

  OXGN_CNTT_API auto AddTypeErasedLoader(TypeId type_id,
    std::string_view type_name, LoadAssetFnErased loader) -> void;

  OXGN_CNTT_API auto AddTypeErasedResourceLoader(TypeId type_id,
    std::string_view type_name, LoadResourceFnErased loader) -> void;

  std::vector<std::unique_ptr<PakFile>> paks_;
};

//=== Explicit Template Declarations for DLL Export ==========================//

//-- Known Asset Types --

template OXGN_CNTT_API auto AssetLoader::LoadAsset<data::GeometryAsset>(
  const data::AssetKey& key, bool offline)
  -> std::shared_ptr<data::GeometryAsset>;

template OXGN_CNTT_API auto AssetLoader::LoadAsset<data::MaterialAsset>(
  const data::AssetKey& key, bool offline)
  -> std::shared_ptr<data::MaterialAsset>;

//-- Known Resource Types --

template OXGN_CNTT_API auto AssetLoader::LoadResource<data::TextureResource>(
  const PakFile& pak, data::pak::ResourceIndexT resource_index, bool)
  -> std::shared_ptr<data::TextureResource>;

template OXGN_CNTT_API auto AssetLoader::LoadResource<data::BufferResource>(
  const PakFile& pak, data::pak::ResourceIndexT resource_index, bool)
  -> std::shared_ptr<data::BufferResource>;

} // namespace oxygen::content
