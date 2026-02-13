//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Shared.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Renderer/api_export.h>
#include <Oxygen/content/EngineTag.h>
#include <Oxygen/content/ResourceKey.h>
#include <Oxygen/data/AssetKey.h>
#include <Oxygen/data/GeometryAsset.h>
#include <Oxygen/data/MaterialAsset.h>
#include <Oxygen/data/SceneAsset.h>

namespace oxygen::content {

namespace internal {
  struct ResourceRef;
  struct DependencyCollector;
} // namespace internal

//! Configuration and tuning parameters for AssetLoader.
/*!
 Provides construction-time configuration for AssetLoader. This keeps the
 runtime API surface minimal while allowing the engine to inject platform
 services (e.g. an async thread pool) and tune loader behavior.

 @note All fields are optional; unspecified fields use default behavior.
*/
struct AssetLoaderConfig final {
  //! Thread pool used for coroutine-based async load tasks.
  /*!
   When set, AssetLoader will schedule blocking IO and CPU decode work on
   this pool.

     @note Async load APIs require a thread pool. If unset, async loads fail
       fast rather than falling back to synchronous work.
  */
  observer_ptr<co::ThreadPool> thread_pool {};

  //! Enforce that offline loads must not perform GPU work.
  /*!
   When true, AssetLoader propagates a strict offline policy to loader
   functions via LoaderContext. Loaders must honor this by avoiding any GPU
    side effects when `context.work_offline` is true.

   @note Defaults to false (online).
  */
  bool work_offline { false };

  //! Enable hash-based integrity verification when mounting content sources.
  /*!
   When true, AssetLoader will ask mounted content sources to verify their
   integrity hashes (e.g. PAK CRC32, loose cooked SHA-256) during mount.

   @note Defaults to false to avoid the potentially expensive full-file hashing
     of large resource blobs during development.
  */
  bool verify_content_hashes { false };
};

class AssetLoader : public oxygen::co::LiveObject, public IAssetLoader {
public:
  using IAssetLoader::BufferCallback;
  using IAssetLoader::EvictionHandler;
  using IAssetLoader::EvictionSubscription;
  using IAssetLoader::GeometryCallback;
  using IAssetLoader::MaterialCallback;
  using IAssetLoader::SceneCallback;
  using IAssetLoader::TextureCallback;

  //! LiveObject contract
  OXGN_CNTT_NDAPI auto ActivateAsync(co::TaskStarted<> started = {})
    -> co::Co<> override;

  OXGN_CNTT_API void Run() override;

  OXGN_CNTT_API void Stop() override;

  OXGN_CNTT_NDAPI auto IsRunning() const -> bool override;

  //! Engine-only capability token is required for construction.
  OXGN_CNTT_API explicit AssetLoader(
    EngineTag tag, AssetLoaderConfig config = {});
  OXGN_CNTT_API virtual ~AssetLoader();

  OXYGEN_MAKE_NON_COPYABLE(AssetLoader)
  OXYGEN_DEFAULT_MOVABLE(AssetLoader)

  OXGN_CNTT_API auto AddPakFile(const std::filesystem::path& path) -> void;

  OXGN_CNTT_API auto AddLooseCookedRoot(const std::filesystem::path& path)
    -> void;

  //! Enable/disable hash verification for future mounts.
  /*!
   Controls whether newly mounted content sources verify integrity hashes.

   @note This does not retroactively re-verify already mounted sources.
    @note This is primarily intended for tooling and examples; production code
      should typically configure this via engine/service config.
  */
  OXGN_CNTT_API auto SetVerifyContentHashes(bool enable) -> void;

  OXGN_CNTT_NDAPI auto VerifyContentHashesEnabled() const noexcept -> bool;

  //! Clear all mounted roots and pak files.
  OXGN_CNTT_API auto ClearMounts() -> void;
  //! Clear cached assets/resources without unmounting sources.
  OXGN_CNTT_API auto TrimCache() -> void override;
  OXGN_CNTT_API auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) noexcept -> void override;
  OXGN_CNTT_API auto ApplyConsoleCVars(
    const console::Console& console) -> void override;

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
   @param resource_key The resource key that is depended upon

   @note The resource's reference count is incremented in the cache.
   @see AddAssetDependency, ReleaseResource
   */
  OXGN_CNTT_API virtual auto AddResourceDependency(
    const data::AssetKey& dependent, ResourceKey resource_key) -> void;

  //=== Asset Loading ===-----------------------------------------------------//

  //! Coroutine-based asset load by AssetKey.
  /*!
   Async-only API for assets.

   @tparam T The asset type (must satisfy IsTyped)
   @param key The asset key to load
   @return Shared pointer to the asset, or nullptr if not found

  @warning Not all asset types are migrated to async yet. Currently,
  `data::MaterialAsset` and `data::GeometryAsset` are supported.
  */
  template <IsTyped T>
  auto LoadAssetAsync(const data::AssetKey& key) -> co::Co<std::shared_ptr<T>>
  {
    if constexpr (std::is_same_v<T, data::MaterialAsset>) {
      co_return co_await LoadMaterialAssetAsyncImpl(key);
    } else if constexpr (std::is_same_v<T, data::GeometryAsset>) {
      co_return co_await LoadGeometryAssetAsyncImpl(key);
    } else if constexpr (std::is_same_v<T, data::SceneAsset>) {
      co_return co_await LoadSceneAssetAsyncImpl(key);
    } else {
      throw std::runtime_error(
        "LoadAssetAsync<T> is not implemented for this asset type yet");
    }
  }

  //! Start an async asset load and invoke a callback on completion.
  /*!
   This is the callback bridge for non-coroutine callers.

   @tparam T The asset type (must satisfy IsTyped)
   @param key The asset key to load
   @param on_complete Callback invoked on the owning thread with the loaded
     asset (or nullptr on failure).
  */
  template <IsTyped T>
  void StartLoadAsset(const data::AssetKey& key,
    std::function<void(std::shared_ptr<T>)> on_complete)
  {
    AssertOwningThread();
    if (!nursery_) {
      throw std::runtime_error(
        "AssetLoader must be activated before StartLoadAsset");
    }
    if (!thread_pool_) {
      throw std::runtime_error(
        "AssetLoader requires a thread pool for StartLoadAsset");
    }

    nursery_->Start(
      [this, key, on_complete = std::move(on_complete)]() mutable -> co::Co<> {
        try {
          auto res = co_await LoadAssetAsync<T>(key);
          on_complete(std::move(res));
        } catch (const std::exception& e) {
          LOG_F(ERROR, "StartLoadAsset failed: {}", e.what());
          on_complete(nullptr);
        }
        co_return;
      });
  }

  //! Get cached asset without loading
  /*!
   Returns an asset if it's already loaded in the cache, without triggering
   a load operation.

   @tparam T The asset type (must satisfy IsTyped)
   @param key The asset key to check
   @return Shared pointer to cached asset, or nullptr if not cached

   @note Does not increment reference count
  @see LoadAssetAsync, HasAsset
  */
  template <IsTyped T>
  auto GetAsset(const data::AssetKey& key) const -> std::shared_ptr<T>
  {
    return content_cache_.Peek<T>(HashAssetKey(key));
  }

  //! Check if asset is loaded in cache
  /*!
   Checks whether an asset is currently loaded in the cache.

   @tparam T The asset type (must satisfy IsTyped)
   @param key The asset key to check
   @return True if asset is cached, false otherwise

   @note Does not trigger loading or affect reference count
   @see LoadAssetAsync, GetAsset
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
  OXGN_CNTT_API auto ReleaseAsset(const data::AssetKey& key) -> bool override;

  //! Subscribe to resource eviction notifications for a resource type.
  OXGN_CNTT_API auto SubscribeResourceEvictions(TypeId resource_type,
    EvictionHandler handler) -> EvictionSubscription override;

  //=== Resource Loading =====================================================//

  //! Create a resource key for a specific resource type, PAK file, and index
  /*!
   Constructs a ResourceKey that uniquely identifies a resource of type T
   within a specific PAK file. The resource key combines the PAK file index,
   resource type information, and resource index into a single 64-bit key
   for efficient lookups and dependency tracking.

   @tparam T The resource type (must satisfy PakResource concept)
   @param pak_file The PAK file containing the resource
   @param resource_index The index of the resource within the PAK file
   @return A ResourceKey that uniquely identifies the resource

   ### Usage Examples

   ```cpp
   // In a material loader registering texture dependencies:
   auto texture_key = loader.MakeResourceKey<TextureResource>(pak,
   texture_index); loader.AddResourceDependency(material_key, texture_key);

   // In a geometry loader registering buffer dependencies:
   auto vertex_buffer_key = loader.MakeResourceKey<BufferResource>(pak,
   vb_index); auto index_buffer_key =
   loader.MakeResourceKey<BufferResource>(pak, ib_index);
   ```

   @note The resource key is deterministic and repeatable for the same inputs
   @note Resource type T must be registered in ResourceTypeList
   @see LoadResourceAsync, AddResourceDependency, ResourceKey
  */
  template <typename T>
  inline auto MakeResourceKey(const PakFile& pak_file, uint32_t resource_index)
    -> ResourceKey
  {
    auto pak_index = GetPakIndex(pak_file);
    auto resource_type_index
      = static_cast<uint16_t>(IndexOf<T, ResourceTypeList>::value);
    return PackResourceKey(pak_index, resource_type_index, resource_index);
  }

  //! Create a resource key for the current source and resource index.
  /*!
   This overload is intended for use inside loader functions. The current
   source is established by AssetLoader when invoking a loader.

   @tparam T The resource type (must satisfy PakResource concept)
   @param resource_index The index of the resource within the current source
   @return A ResourceKey that uniquely identifies the resource

   @warning Calling this outside of a load operation is invalid.
  */
  template <typename T>
  inline auto MakeResourceKey(uint32_t resource_index) -> ResourceKey
  {
    const auto source_index = GetCurrentSourceId();
    auto resource_type_index
      = static_cast<uint16_t>(IndexOf<T, ResourceTypeList>::value);
    return PackResourceKey(source_index, resource_type_index, resource_index);
  }

  //! Coroutine-based resource load by source-aware ResourceKey.
  template <PakResource T>
  auto LoadResourceAsync(ResourceKey key) -> co::Co<std::shared_ptr<T>>;

  //! Coroutine-based resource decode from caller-provided cooked bytes.
  template <PakResource T>
  auto LoadResourceAsync(CookedResourceData<T> cooked)
    -> co::Co<std::shared_ptr<T>>
  {
    auto decoded = co_await LoadResourceAsyncFromCookedErased(
      T::ClassTypeId(), cooked.key, cooked.bytes);
    co_return std::static_pointer_cast<T>(std::move(decoded));
  }

  //! Coroutine-based convenience for texture loads.
  /*!
    Async-only API: returns the CPU-side decoded `TextureResource` on
    completion (or `nullptr` on failure). Implementations must not perform
    GPU work; GPU creation and upload belongs to the renderer.
  */
  OXGN_CNTT_API auto LoadTextureAsync(ResourceKey key)
    -> co::Co<std::shared_ptr<data::TextureResource>>;

  //! Coroutine-based convenience for decoding a texture from cooked bytes.
  OXGN_CNTT_API auto LoadTextureAsync(
    CookedResourceData<data::TextureResource> cooked)
    -> co::Co<std::shared_ptr<data::TextureResource>>;

  //! Convenience: schedule a texture load on the loader nursery and invoke
  //! `on_complete` on the engine thread when the CPU-side payload is ready.
  /*! Deprecated: prefer co_awaiting `LoadTextureAsync` directly when
      possible. This helper simply starts a coroutine inside the loader's
      nursery and will invoke the callback when complete. */
  OXGN_CNTT_API void StartLoadTexture(
    ResourceKey key, TextureCallback on_complete) override;

  OXGN_CNTT_API void StartLoadTexture(
    CookedResourceData<data::TextureResource> cooked,
    TextureCallback on_complete) override;

  OXGN_CNTT_API void StartLoadBuffer(
    ResourceKey key, BufferCallback on_complete) override;

  OXGN_CNTT_API void StartLoadBuffer(
    CookedResourceData<data::BufferResource> cooked,
    BufferCallback on_complete) override;

  OXGN_CNTT_API void StartLoadMaterialAsset(
    const data::AssetKey& key, MaterialCallback on_complete) override
  {
    StartLoadAsset<data::MaterialAsset>(key, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadGeometryAsset(
    const data::AssetKey& key, GeometryCallback on_complete) override
  {
    StartLoadAsset<data::GeometryAsset>(key, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadScene(
    const data::AssetKey& key, SceneCallback on_complete) override
  {
    StartLoadAsset<data::SceneAsset>(key, std::move(on_complete));
  }

  //! Start an async resource load and invoke a callback on completion.
  /*!
   This is the callback bridge for non-coroutine callers.

   @tparam T The resource type (must satisfy PakResource)
   @param key The resource key to load
   @param on_complete Callback invoked on the owning thread with the loaded
     resource (or nullptr on failure).
  */
  template <PakResource T>
  void StartLoadResource(
    ResourceKey key, std::function<void(std::shared_ptr<T>)> on_complete)
  {
    AssertOwningThread();
    if (!nursery_) {
      throw std::runtime_error(
        "AssetLoader must be activated before StartLoadResource");
    }
    if (!thread_pool_) {
      throw std::runtime_error(
        "AssetLoader requires a thread pool for StartLoadResource");
    }

    nursery_->Start(
      [this, key, on_complete = std::move(on_complete)]() mutable -> co::Co<> {
        try {
          auto res = co_await LoadResourceAsync<T>(key);
          on_complete(std::move(res));
        } catch (const std::exception& e) {
          LOG_F(ERROR, "StartLoadResource failed: {}", e.what());
          on_complete(nullptr);
        }
        co_return;
      });
  }

  //! Start a cooked-bytes resource decode and invoke a callback.
  /*!
   This is the callback bridge for non-coroutine callers.

   @tparam T The resource type (must satisfy PakResource).
   @param cooked Cooked payload plus cache identity.
   @param on_complete Callback invoked on the owning thread with the decoded
     resource (or nullptr on failure).
  */
  template <PakResource T>
  void StartLoadResource(CookedResourceData<T> cooked,
    std::function<void(std::shared_ptr<T>)> on_complete)
  {
    AssertOwningThread();
    if (!nursery_) {
      throw std::runtime_error(
        "AssetLoader must be activated before StartLoadResource (cooked)");
    }
    if (!thread_pool_) {
      throw std::runtime_error(
        "AssetLoader requires a thread pool for StartLoadResource (cooked)");
    }

    nursery_->Start(
      [this, key = cooked.key,
        bytes = std::vector<uint8_t>(cooked.bytes.begin(), cooked.bytes.end()),
        on_complete = std::move(on_complete)]() mutable -> co::Co<> {
        try {
          std::span<const uint8_t> span(bytes.data(), bytes.size());
          auto res = co_await LoadResourceAsync<T>({
            .key = key,
            .bytes = span,
          });
          on_complete(std::move(res));
        } catch (const std::exception& e) {
          LOG_F(ERROR, "StartLoadResource (cooked) failed: {}", e.what());
          on_complete(nullptr);
        }
        co_return;
      });
  }

  [[nodiscard]] auto GetTexture(ResourceKey key) const noexcept
    -> std::shared_ptr<data::TextureResource> override
  {
    return GetResource<data::TextureResource>(key);
  }

  [[nodiscard]] auto GetBuffer(ResourceKey key) const noexcept
    -> std::shared_ptr<data::BufferResource> override
  {
    return GetResource<data::BufferResource>(key);
  }

  [[nodiscard]] auto GetMaterialAsset(const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::MaterialAsset> override
  {
    return GetAsset<data::MaterialAsset>(key);
  }

  [[nodiscard]] auto GetGeometryAsset(const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::GeometryAsset> override
  {
    return GetAsset<data::GeometryAsset>(key);
  }

  [[nodiscard]] auto HasTexture(ResourceKey key) const noexcept -> bool override
  {
    return HasResource<data::TextureResource>(key);
  }

  [[nodiscard]] auto HasBuffer(ResourceKey key) const noexcept -> bool override
  {
    return HasResource<data::BufferResource>(key);
  }

  [[nodiscard]] auto HasMaterialAsset(const data::AssetKey& key) const noexcept
    -> bool override
  {
    return HasAsset<data::MaterialAsset>(key);
  }

  [[nodiscard]] auto HasGeometryAsset(const data::AssetKey& key) const noexcept
    -> bool override
  {
    return HasAsset<data::GeometryAsset>(key);
  }

  //! Get cached resource without loading
  /*!
   Returns a resource if it's already loaded in the cache, without triggering
   a load operation.

   @tparam T The resource type (must satisfy PakResource concept)
   @param key The resource key identifying the resource
   @return Shared pointer to cached resource, or nullptr if not cached

   @note Does not increment reference count
   @see LoadResource, HasResource
  */
  template <PakResource T>
  auto GetResource(ResourceKey key) const noexcept -> std::shared_ptr<T>
  {
    return content_cache_.Peek<T>(HashResourceKey(key));
  }

  //! Check out a resource, incrementing its usage count.
  /*!
   Use this when you need to hold a resource beyond a transient query. Each
   checkout must be paired with a matching ReleaseResource call.

   @tparam T The resource type (must satisfy PakResource concept)
   @param key The resource key identifying the resource
   @return Shared pointer to cached resource, or nullptr if not cached

   @note This method increments the cache refcount.
   @see GetResource, ReleaseResource
  */
  template <PakResource T>
  auto CheckOutResource(ResourceKey key) const noexcept -> std::shared_ptr<T>
  {
    return content_cache_.CheckOut<T>(HashResourceKey(key));
  }

  //! Check if resource is loaded
  /*!
   Checks whether a resource is currently loaded in the engine-wide cache.

   @tparam T The resource type (must satisfy PakResource concept)
   @param key The resource key identifying the resource
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

   @param key The resource key identifying the resource.
   @return True if this call caused the resource to be fully evicted from the
   cache, false if the resource is still present or was not present.

   @note This method is idempotent: repeated calls after eviction return false.
   @see LoadResource, HasResource, ReleaseAsset, AnyCache
  */
  OXGN_CNTT_API auto ReleaseResource(ResourceKey key) -> bool override;

  //! Mint a synthetic, texture-typed ResourceKey suitable for buffer-driven
  //! loads.
  /*!
   The returned key is validly encoded for `TextureResource` but does not refer
   to any mounted content source.

  This enables workflows where the application provides cooked bytes directly
  and the renderer treats the identity as an opaque `ResourceKey`.

   @return A new unique ResourceKey for a texture.

   @note Only AssetLoader is permitted to construct ResourceKeys. Callers must
         treat the key as opaque.
  */
  OXGN_CNTT_NDAPI auto MintSyntheticTextureKey() -> ResourceKey override;

  OXGN_CNTT_NDAPI auto MintSyntheticBufferKey() -> ResourceKey override;

  //! Register a load function for assets or resources (unified interface)
  /*!
   The load function is invoked when an asset or resource is requested and is
   not already cached.

   The target type (specific asset or resource) is automatically deduced from
   the load function's return type.

   @tparam LF The load function type (must satisfy LoadFunction)
   the type deduced from the load function's return type).
   @param load_fn The load function to register

   ### Usage Examples

   ```cpp
   // Register asset loaders - type inferred from function signature
   asset_loader.RegisterLoader(LoadGeometryAsset);
   asset_loader.RegisterLoader(LoadMaterialAsset);

   // Register resource loaders - type inferred from function signature
   asset_loader.RegisterLoader(LoadBufferResource);
   asset_loader.RegisterLoader(LoadTextureResource);
   ```

   @note Only one load/unload function pair per type is supported.
   @see LoadAsset, LoadResource
  */
  template <LoadFunction LF> auto RegisterLoader(LF&& load_fn) -> void
  {
    // Infer the type from the loader function signature
    using LoaderPtr = decltype(load_fn(std::declval<LoaderContext>()));
    using T = std::remove_pointer_t<typename LoaderPtr::element_type>;
    static_assert(IsTyped<T>, "T must satisfy the `IsTyped` concept");

    auto type_id = T::ClassTypeId();
    auto type_name = T::ClassTypeNamePretty();

    if constexpr (PakResource<T>) {
      // Resource loader path
      LoadFnErased loader_erased
        = [load_fn = std::forward<LF>(load_fn)](
            LoaderContext context) -> std::shared_ptr<void> {
        auto result = load_fn(context);
        return result ? std::shared_ptr<void>(std::move(result)) : nullptr;
      };
      AddTypeErasedResourceLoader(type_id, type_name, std::move(loader_erased));
    } else {
      // Asset loader path
      LoadFnErased loader_erased
        = [load_fn = std::forward<LF>(load_fn)](
            LoaderContext context) -> std::shared_ptr<void> {
        auto result = load_fn(context);
        return result ? std::shared_ptr<void>(std::move(result)) : nullptr;
      };
      AddTypeErasedAssetLoader(type_id, type_name, std::move(loader_erased));
    }
  }

protected:
  //! Get PAK file index from pointer (for resource key creation)
  OXGN_CNTT_NDAPI virtual auto GetPakIndex(const PakFile& pak) const
    -> uint16_t;

private:
  struct DecodedAssetAsyncResult final {
    uint16_t source_id {};
    std::shared_ptr<void> asset;
    std::shared_ptr<internal::DependencyCollector> dependency_collector;
  };

  OXGN_CNTT_API auto DecodeAssetAsyncErasedImpl(TypeId type_id,
    const data::AssetKey& key) -> co::Co<DecodedAssetAsyncResult>;

  OXGN_CNTT_API auto LoadMaterialAssetAsyncImpl(const data::AssetKey& key)
    -> co::Co<std::shared_ptr<data::MaterialAsset>>;

  OXGN_CNTT_API auto LoadGeometryAssetAsyncImpl(const data::AssetKey& key)
    -> co::Co<std::shared_ptr<data::GeometryAsset>>;

  OXGN_CNTT_API auto LoadSceneAssetAsyncImpl(const data::AssetKey& key)
    -> co::Co<std::shared_ptr<data::SceneAsset>>;

  OXGN_CNTT_API auto LoadResourceAsyncFromCookedErased(
    TypeId type_id, ResourceKey key, std::span<const uint8_t> bytes)
    -> co::Co<std::shared_ptr<void>>;

  struct Impl;
  std::unique_ptr<Impl> impl_;

  // Nursery pointer for LiveObject activation. Set by
  // ActivateAsync/OpenNursery.
  co::Nursery* nursery_ {};

  //=== Dependency Tracking ===-----------------------------------------------//

  // Dependency graph storage is identity-only by design.
  //
  // This graph MUST NOT store access state such as locators, paths, streams, or
  // readers. Resolution from identity to access is a separate concern.

  // Asset-to-asset dependencies: dependent_asset -> set of dependency_assets
  std::unordered_map<data::AssetKey, std::unordered_set<data::AssetKey>>
    asset_dependencies_;

  // Asset-to-resource dependencies: dependent_asset -> set of resource_keys
  std::unordered_map<data::AssetKey, std::unordered_set<ResourceKey>>
    resource_dependencies_;

  //! Helper method for the recursive descent of asset dependencies when
  //! releasing assets.
  auto ReleaseAssetTree(const data::AssetKey& key) -> void;

  //=== Unified Content Cache ===---------------------------------------------//

  //! Unified content cache for both assets and resources
  mutable AnyCache<uint64_t, RefCountedEviction<uint64_t>> content_cache_;

  //! Hash an AssetKey for cache storage
  OXGN_CNTT_API static auto HashAssetKey(const data::AssetKey& key) -> uint64_t;

  //! Hash a ResourceKey for cache storage (requires instance for SourceKey
  //! lookup)
  OXGN_CNTT_API auto HashResourceKey(const ResourceKey& key) const -> uint64_t;

  //=== Type-erased Loading/Unloading ===-------------------------------------//

  using LoadFnErased = std::function<std::shared_ptr<void>(LoaderContext)>;

  // Type-erased unload function signature
  using UnloadFnErased
    = std::function<void(std::shared_ptr<void>, AssetLoader&)>;

  std::unordered_map<TypeId, LoadFnErased> asset_loaders_;
  std::unordered_map<TypeId, LoadFnErased> resource_loaders_;

  void UnloadObject(
    uint64_t cache_key, const oxygen::TypeId& type_id, EvictionReason reason);

  OXGN_CNTT_API auto AddTypeErasedAssetLoader(
    TypeId type_id, std::string_view type_name, LoadFnErased&& loader) -> void;

  OXGN_CNTT_API auto AddTypeErasedResourceLoader(
    TypeId type_id, std::string_view type_name, LoadFnErased&& loader) -> void;

  // Thread ownership for single-thread phase 1 policy.
  std::thread::id owning_thread_id_ {};
  inline void AssertOwningThread() const
  {
    if (owning_thread_id_ != std::this_thread::get_id()) {
      throw std::runtime_error(
        "AssetLoader used from non-owning thread (owning-thread invariant)");
    }
  }

  auto DetectCycle(const data::AssetKey& start, const data::AssetKey& target)
    -> bool; // returns true if adding edge start->target introduces cycle

  // Debug visited guard for ReleaseAssetTree recursion protection.
  struct ReleaseVisitGuard;

  OXGN_CNTT_NDAPI auto GetCurrentSourceId() const -> uint16_t;

  template <typename ResourceT>
  auto PublishResourceDependenciesAsync(
    const data::AssetKey& dependent_asset_key,
    const internal::DependencyCollector& collector) -> co::Co<>;

  struct LoadedGeometryBuffer final {
    ResourceKey key {};
    std::shared_ptr<data::BufferResource> resource;
  };

  using LoadedGeometryBuffersByIndex
    = std::unordered_map<uint32_t, LoadedGeometryBuffer>;

  using LoadedGeometryMaterialsByKey
    = std::unordered_map<data::AssetKey, std::shared_ptr<data::MaterialAsset>>;

  auto LoadGeometryBufferDependenciesAsync(
    const internal::DependencyCollector& collector)
    -> co::Co<LoadedGeometryBuffersByIndex>;

  auto LoadGeometryMaterialDependenciesAsync(
    const internal::DependencyCollector& collector)
    -> co::Co<LoadedGeometryMaterialsByKey>;

  auto BindGeometryRuntimePointers(data::GeometryAsset& asset,
    const LoadedGeometryBuffersByIndex& buffers_by_index,
    const LoadedGeometryMaterialsByKey& materials_by_key) -> void;

  auto PublishGeometryDependencyEdges(
    const data::AssetKey& dependent_asset_key,
    const LoadedGeometryBuffersByIndex& buffers_by_index,
    const LoadedGeometryMaterialsByKey& materials_by_key) -> void;

  // Private helper to pack resource key without exposing internal type in the
  // public header. Implemented in the .cpp which includes InternalResourceKey.
  OXGN_CNTT_API static auto PackResourceKey(uint16_t pak_index,
    uint16_t resource_type_index, uint32_t resource_index) -> ResourceKey;

  // Bind an internal container-relative resource reference into an opaque
  // ResourceKey. Owning-thread only.
  OXGN_CNTT_NDAPI auto BindResourceRefToKey(const internal::ResourceRef& ref)
    -> ResourceKey;

  observer_ptr<co::ThreadPool> thread_pool_ {};

  bool work_offline_ { false };

  bool verify_content_hashes_ { false };

  //=== Eviction Notifications ===-----------------------------------------//

  struct EvictionSubscriber final {
    uint64_t id { 0 };
    EvictionHandler handler {};
  };

  std::unordered_map<TypeId, std::vector<EvictionSubscriber>>
    eviction_subscribers_;
  std::unordered_map<uint64_t, ResourceKey> resource_key_by_hash_;
  std::unordered_map<uint64_t, data::AssetKey> asset_key_by_hash_;
  uint64_t next_eviction_subscriber_id_ { 1 };
  std::shared_ptr<int> eviction_alive_token_ {};

  // Eviction in-progress guard to prevent re-entrant notifications for the
  // same cache key (safely handles subscribers calling back into the loader).
  std::unordered_set<uint64_t> eviction_in_progress_;

  //=== In-flight Deduplication (Phase 2) ===--------------------------------//

  // Maps are owning-thread only.
  std::unordered_map<uint64_t,
    co::Shared<co::Co<std::shared_ptr<data::MaterialAsset>>>>
    in_flight_material_assets_;

  std::unordered_map<uint64_t,
    co::Shared<co::Co<std::shared_ptr<data::GeometryAsset>>>>
    in_flight_geometry_assets_;

  std::unordered_map<uint64_t,
    co::Shared<co::Co<std::shared_ptr<data::SceneAsset>>>>
    in_flight_scene_assets_;

  std::unordered_map<uint64_t,
    co::Shared<co::Co<std::shared_ptr<data::TextureResource>>>>
    in_flight_textures_;

  std::unordered_map<uint64_t,
    co::Shared<co::Co<std::shared_ptr<data::BufferResource>>>>
    in_flight_buffers_;

  std::atomic<uint32_t> next_synthetic_texture_index_ { 1 };
  std::atomic<uint32_t> next_synthetic_buffer_index_ { 1 };

public:
  // Debug-only dependent enumeration helper (implemented via forward scan).
  // Provided as public debug API below when OXYGEN_DEBUG is defined.

#if !defined(NDEBUG)
  // Enumerate direct dependents (debug only) by scanning forward map.
  template <typename Fn>
  auto ForEachDependent(const data::AssetKey& dependency, Fn&& fn) const -> void
  {
    for (const auto& [dependent, deps] : asset_dependencies_) {
      if (deps.contains(dependency)) {
        fn(dependent);
      }
    }
  }
#endif

private:
  void UnsubscribeResourceEvictions(
    TypeId resource_type, uint64_t id) noexcept override;
};

//=== Explicit Template Declarations for DLL Export ==========================//

//-- Known Asset Types --

template OXGN_CNTT_API auto AssetLoader::LoadAssetAsync<data::MaterialAsset>(
  const data::AssetKey& key) -> co::Co<std::shared_ptr<data::MaterialAsset>>;

template OXGN_CNTT_API auto AssetLoader::LoadAssetAsync<data::GeometryAsset>(
  const data::AssetKey& key) -> co::Co<std::shared_ptr<data::GeometryAsset>>;

template OXGN_CNTT_API auto AssetLoader::LoadAssetAsync<data::SceneAsset>(
  const data::AssetKey& key) -> co::Co<std::shared_ptr<data::SceneAsset>>;

//-- Known Resource Types --

template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::BufferResource>(ResourceKey)
    -> co::Co<std::shared_ptr<data::BufferResource>>;

template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::TextureResource>(ResourceKey)
    -> co::Co<std::shared_ptr<data::TextureResource>>;

} // namespace oxygen::content
