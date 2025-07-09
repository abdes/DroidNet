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
   and reference counting. This will increment the reference count of the
   dependency in the cache, ensuring it remains loaded until the dependency
   is removed.

   @param dependent The asset that has the dependency
   @param dependency The asset that is depended upon

   @note The dependency's reference count is incremented in the cache.
   @see AddResourceDependency, ReleaseAsset
   */
  OXGN_CNTT_API virtual auto AddAssetDependency(
    const data::AssetKey& dependent, const data::AssetKey& dependency) -> void;

  //! Register an asset-to-resource dependency
  /*!
   Records that an asset depends on a resource for proper loading order
   and reference counting. This will increment the reference count of the
   resource in the cache, ensuring it remains loaded until the dependency
   is removed.

   @param dependent The asset that has the dependency
   @param resource_key The ResourceKey that is depended upon

   @note The resource's reference count is incremented in the cache.
   @see AddAssetDependency, ReleaseResource
   */
  OXGN_CNTT_API virtual auto AddResourceDependency(
    const data::AssetKey& dependent, ResourceKey resource_key) -> void;

  //=== Asset Loading ===-----------------------------------------------------//

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

  //! Release an asset, indicating it is no longer in use by the caller.
  /*!
   Assets are centrally cached and shared throughout the engine, with automatic
   reference counting and dependency tracking. When an asset is no longer in
   use, it should be explicitly released. Releasing an asset checks it back into
   the cache, releases all of its resource dependencies first, and then
   recursively releases all of its asset dependencies. This ensures that
   transitive dependencies—whether resources or other assets—are properly
   released in a safe and efficient order.

   ### Deferred Unloading Behavior

   Releasing an asset and its dependencies does not guarantee their immediate
   unloading. Unloading (as a result of eviction from the cache) occurs only
   when an asset or resource is no longer checked out for use, either directly
   or as a dependency, by any part of the system.

   @param key The asset key to release
   @return True if this call caused the asset to be fully evicted from the
   cache, false if the asset is still present or was not present.

   @note There is no atomic, all-or-nothing eviction of dependency trees;
   eviction is determined individually by reference counts and dependents.
   @see LoadAsset, HasAsset, ReleaseResource
  */
  OXGN_CNTT_API auto ReleaseAsset(const data::AssetKey& key) -> bool;

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

  //! Releases (checks in) a resource usage.
  /*!
   This method decrements the usage count for the resource. The resource will
   only be evicted from the cache when all users (including all asset
   dependents) have released (checked in) their usage.

   If this call returns false, you have released your usage, but the resource is
   still in use elsewhere or not present. The resource will only be fully
   evicted (and return true) when all users and dependents have released it.

   @param key The ResourceKey identifying the resource.
   @return True if this call caused the resource to be fully evicted from the
   cache, false if the resource is still present or was not present.

   @note This method is idempotent: repeated calls after eviction return false.
   @see LoadResource, HasResource, ReleaseAsset, AnyCache
  */
  OXGN_CNTT_API auto ReleaseResource(ResourceKey key) -> bool;

  //! Register a load and unload functions for assets or resources (unified
  //! interface)
  /*!
   The load function is invoked when an asset or resource is requested and is
   not already cached.

   The unload function is invoked when the asset or resource is evicted from the
   cache (i.e. no longer used directly or as a dependency).

   The target type (specific asset or resource) is automatically deduced from
   the load function's return type. The unload function must be callable with a
   `std::shared_ptr<T>`, an `AssetLoader&`, and a `bool` (offline flag), where
   `T` is the `element_type` type of the smart pointer returned by the load
   function.

   @tparam LF The load function type (must satisfy LoadFunction)
   @tparam UF The unload function type; must be callable as
     `void(std::shared_ptr<T>, AssetLoader&, bool)` where T matches the load
     function's return type.
   @param load_fn The load function to register
   @param unload_fn The unload function to register

   ### Usage Examples

   ```cpp
   // Register asset loaders - type inferred from function signature
   asset_loader.RegisterLoader(LoadGeometryAsset, UnloadGeometryAsset);
   asset_loader.RegisterLoader(LoadMaterialAsset, UnloadMaterialAsset);

   // Register resource loaders - type inferred from function signature
   asset_loader.RegisterLoader(LoadBufferResource, UnloadBufferResource);
   asset_loader.RegisterLoader(LoadTextureResource, UnloadTextureResource);
   ```

   @note Only one load/unload function pair per type is supported.
   @see LoadAsset, LoadResource
  */
  template <LoadFunction LF, typename UF>
  auto RegisterLoader(LF&& load_fn, UF&& unload_fn) -> void
  {
    // Infer the type from the loader function signature
    using LoaderPtr = decltype(load_fn(std::declval<LoaderContext>()));
    using T = std::remove_pointer_t<typename LoaderPtr::element_type>;
    static_assert(IsTyped<T>, "T must satisfy IsTyped concept");
    static_assert(
      std::is_invocable_r_v<void, UF, std::shared_ptr<T>, AssetLoader&, bool>,
      "Unload function must be callable with (std::shared_ptr<T>, "
      "AssetLoader&, bool) and return void");

    auto type_id = T::ClassTypeId();
    auto type_name = T::ClassTypeNamePretty();

    // Store type-erased unload function
    unloaders_[type_id]
      = [unload_fn = std::forward<UF>(unload_fn)](
          std::shared_ptr<void> asset, AssetLoader& loader, bool offline) {
          auto typed = std::static_pointer_cast<T>(asset);
          unload_fn(typed, loader, offline);
        };

    if constexpr (PakResource<T>) {
      // Resource loader path
      LoadResourceFnErased erased
        = [load_fn = std::forward<LF>(load_fn)](AssetLoader& loader,
            const PakFile& pak, data::pak::ResourceIndexT resource_index,
            bool offline) -> std::shared_ptr<void> {
        return MakeResourceLoaderCall<T>(
          load_fn, loader, pak, resource_index, offline);
      };
      AddTypeErasedResourceLoader(type_id, type_name, std::move(erased));

    } else {
      // Asset loader path
      LoadAssetFnErased erased
        = [load_fn = std::forward<LF>(load_fn)](AssetLoader& loader,
            const PakFile& pak, const data::pak::AssetDirectoryEntry& entry,
            bool offline) -> std::shared_ptr<void> {
        return MakeAssetLoaderCall(load_fn, loader, pak, entry, offline);
      };
      AddTypeErasedAssetLoader(type_id, type_name, std::move(erased));
    }
  }

private:
  // Type-erased unload function signature
  using UnloadFnErased
    = std::function<void(std::shared_ptr<void>, AssetLoader&, bool)>;

  // Map from type id to type-erased unload function
  std::unordered_map<TypeId, UnloadFnErased> unloaders_;
  std::vector<std::unique_ptr<PakFile>> paks_;

  //! Helper method for the recursive descent of asset dependencies when
  //! releasing assets.
  auto ReleaseAssetTree(const data::AssetKey& key) -> void;

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

  //=== Unified Content Cache ===---------------------------------------------//

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

  //=== Type-erased Loading/Unloading ===-------------------------------------//

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

  OXGN_CNTT_API auto AddTypeErasedAssetLoader(TypeId type_id,
    std::string_view type_name, LoadAssetFnErased loader) -> void;

  OXGN_CNTT_API auto AddTypeErasedResourceLoader(TypeId type_id,
    std::string_view type_name, LoadResourceFnErased loader) -> void;
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
