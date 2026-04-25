//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Data/PhysicsResource.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/EnumIndexedArray.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/RefCountedEviction.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/LiveObject.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Shared.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace oxygen::content {

namespace internal {
  class AssetIdentityIndex;
  class DependencyGraphStore;
  class DependencyReleaseEngine;
  class EvictionRegistry;
  class InFlightOperationTable;
  class PhysicsQueryService;
  class ResourceKeyRegistry;
  class ResourceLoadPipeline;
  class SceneCatalogQueryService;
  class ScriptQueryService;
  class ScriptHotReloadService;
  class IContentSource;
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

  //! Path resolution service used for mapping disk changes to script assets.
  std::optional<PathFinder> path_finder;

  //! Runtime residency policy (budget/trim/default-priority).
  ResidencyPolicy residency_policy {};
};

class AssetLoader : public oxygen::co::LiveObject, public IAssetLoader {
public:
  using IAssetLoader::BufferCallback;
  using IAssetLoader::EvictionHandler;
  using IAssetLoader::EvictionSubscription;
  using IAssetLoader::GeometryCallback;
  using IAssetLoader::MaterialCallback;
  using IAssetLoader::PhysicsSceneCallback;
  using IAssetLoader::SceneCallback;
  using IAssetLoader::ScriptCallback;
  using IAssetLoader::TextureCallback;

  enum class TypedLoadMetric : uint8_t {
    kFirst = 0,
    kRequests = kFirst,
    kCacheHits,
    kCacheMisses,
    kTasksDeduped,
    kTasksSpawned,
    kErrDecode,
    kErrTypeMismatch,
    kErrRetryFailed,
    kErrCanceled,
    kCount,
  };
  enum class LoadTelemetryEvent : uint8_t {
    kFirst = 0,
    kRequest = kFirst,
    kCacheHit,
    kCacheMiss,
    kTasksDeduped,
    kTasksSpawned,
    kDecodeFailure,
    kTypeMismatch,
    kStoreRetryFailure,
    kCancellation,
    kCount,
  };
  using TypedLoadTelemetry
    = oxygen::EnumIndexedArray<TypedLoadMetric, uint64_t>;

  struct InFlightTelemetry final {
    uint64_t find_calls { 0 };
    uint64_t find_hits { 0 };
    uint64_t insert_calls { 0 };
    uint64_t erase_calls { 0 };
    uint64_t clear_calls { 0 };
    std::size_t active_type_buckets { 0 };
    std::size_t active_operations { 0 };
  };

  struct TelemetryStats final {
    bool telemetry_enabled { true };

    struct PressureTelemetry final {
      uint64_t events_total { 0 };
      uint64_t events_forced { 0 };
      uint64_t events_soft { 0 };
      uint64_t resource_store_failed { 0 };
      uint64_t resource_store_over_budget { 0 };
      uint64_t asset_store_failed { 0 };
      uint64_t asset_store_succeeded { 0 };
    };

    struct TrimTelemetry final {
      uint64_t manual_attempts { 0 };
      uint64_t auto_attempts { 0 };
      uint64_t reclaimed_items { 0 };
      uint64_t reclaimed_bytes { 0 };
      uint64_t blocked_total { 0 };
      uint64_t pruned_live_branches { 0 };
      uint64_t blocked_priority_roots { 0 };
      uint64_t orphan_resources { 0 };
    };

    struct EvictionTelemetry final {
      uint64_t on_refcount_zero { 0 };
      uint64_t on_trim { 0 };
      uint64_t on_clear { 0 };
      uint64_t on_shutdown { 0 };
    };

    struct CacheTelemetry final {
      std::size_t entries { 0 };
      uint64_t consumed_budget { 0 };
      std::size_t checked_out_items { 0 };
      bool over_budget { false };
    };

    TypedLoadTelemetry material_assets {};
    TypedLoadTelemetry geometry_assets {};
    TypedLoadTelemetry scene_assets {};
    TypedLoadTelemetry physics_scene_assets {};
    TypedLoadTelemetry script_assets {};
    TypedLoadTelemetry input_action_assets {};
    TypedLoadTelemetry input_mapping_context_assets {};

    TypedLoadTelemetry texture_resources {};
    TypedLoadTelemetry buffer_resources {};
    TypedLoadTelemetry script_resources {};
    TypedLoadTelemetry physics_resources {};

    PressureTelemetry pressure {};
    TrimTelemetry trim {};
    EvictionTelemetry eviction {};
    CacheTelemetry cache {};

    InFlightTelemetry in_flight {};
  };

  //! LiveObject contract
  OXGN_CNTT_NDAPI auto ActivateAsync(co::TaskStarted<> started = {})
    -> co::Co<> override;

  OXGN_CNTT_API void Run() override;

  OXGN_CNTT_API void Stop() override;

  OXGN_CNTT_NDAPI auto IsRunning() const -> bool override;

  //! Engine-only capability token is required for construction.
  OXGN_CNTT_API explicit AssetLoader(
    engine::EngineTag tag, const AssetLoaderConfig& config = {});
  OXGN_CNTT_API virtual ~AssetLoader();

  OXYGEN_MAKE_NON_COPYABLE(AssetLoader)
  OXYGEN_DEFAULT_MOVABLE(AssetLoader)

  OXGN_CNTT_API auto AddPakFile(const std::filesystem::path& path)
    -> void override;

  //! Mount a patch pak and register manifest tombstones.
  /*!
   Validates the patch manifest compatibility envelope against the mounted
   * base
   catalogs, then mounts the patch pak at highest precedence and
   * registers
   deleted-key tombstones on that mount layer.

   @param path
   * Path to the patch `.pak`.
   @param manifest Patch manifest emitted by the
   * cooker.
   @param mounted_base_catalogs Catalog snapshot for the currently
   * mounted base
     set.
   @throw std::runtime_error on compatibility
   * validation failures.
  */
  OXGN_CNTT_API auto AddPatchPakFile(const std::filesystem::path& path,
    const data::PatchManifest& manifest,
    std::span<const data::PakCatalog> mounted_base_catalogs) -> void;

  OXGN_CNTT_API auto AddLooseCookedRoot(const std::filesystem::path& path)
    -> void override;

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
  OXGN_CNTT_API auto ClearMounts() -> void override;
  //! Clear cached assets/resources without unmounting sources.
  OXGN_CNTT_API auto TrimCache() -> void override;
  OXGN_CNTT_API auto SetResidencyPolicy(const ResidencyPolicy& policy)
    -> void override;
  [[nodiscard]] OXGN_CNTT_NDAPI auto GetResidencyPolicy() const noexcept
    -> ResidencyPolicy override;
  [[nodiscard]] OXGN_CNTT_NDAPI auto QueryResidencyPolicyState() const
    -> ResidencyPolicyState override;
  [[nodiscard]] OXGN_CNTT_API auto EnumerateMountedScenes() const
    -> std::vector<IAssetLoader::MountedSceneEntry> override;
  [[nodiscard]] OXGN_CNTT_API auto EnumerateMountedInputContexts() const
    -> std::vector<IAssetLoader::MountedInputContextEntry> override;
  [[nodiscard]] OXGN_CNTT_API auto EnumerateMountedSources() const
    -> std::vector<IAssetLoader::MountedSourceEntry> override;
  OXGN_CNTT_API auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) noexcept -> void override;
  OXGN_CNTT_API auto ApplyConsoleCVars(const console::Console& console)
    -> void override;
  [[nodiscard]] OXGN_CNTT_NDAPI auto GetTelemetryStats() const
    -> TelemetryStats;
  OXGN_CNTT_API auto ResetTelemetryStats() noexcept -> void;
  OXGN_CNTT_API auto SetTelemetryEnabled(bool enabled) -> void;
  [[nodiscard]] OXGN_CNTT_NDAPI auto IsTelemetryEnabled() const noexcept
    -> bool;

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
    // Non-canonical convenience overload for call sites that do not classify
    // request intent/priority. Canonical execution uses the request-aware
    // overload.
    co_return co_await LoadAssetAsync<T>(key, LoadRequest {});
  }

  template <IsTyped T>
  auto LoadAssetAsync(const data::AssetKey& key, LoadRequest request)
    -> co::Co<std::shared_ptr<T>>
  {
    request = NormalizeLoadRequest(request);
    if constexpr (std::is_same_v<T, data::MaterialAsset>) {
      co_return co_await LoadMaterialAssetAsyncImpl(key, std::nullopt, request);
    } else if constexpr (std::is_same_v<T, data::GeometryAsset>) {
      co_return co_await LoadGeometryAssetAsyncImpl(key, std::nullopt, request);
    } else if constexpr (std::is_same_v<T, data::SceneAsset>) {
      co_return co_await LoadSceneAssetAsyncImpl(key, std::nullopt, request);
    } else if constexpr (std::is_same_v<T, data::PhysicsSceneAsset>) {
      co_return co_await LoadPhysicsSceneAssetAsyncImpl(
        key, std::nullopt, request);
    } else if constexpr (std::is_same_v<T, data::ScriptAsset>) {
      co_return co_await LoadScriptAssetAsyncImpl(key, std::nullopt, request);
    } else if constexpr (std::is_same_v<T, data::InputActionAsset>) {
      co_return co_await LoadInputActionAssetAsyncImpl(
        key, std::nullopt, request);
    } else if constexpr (std::is_same_v<T, data::InputMappingContextAsset>) {
      co_return co_await LoadInputMappingContextAssetAsyncImpl(
        key, std::nullopt, request);
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
    // Non-canonical convenience wrapper for callers that do not pass an
    // explicit request classification. Canonical path is request-aware.
    StartLoadAsset<T>(key, LoadRequest {}, std::move(on_complete));
  }

  template <IsTyped T>
  void StartLoadAsset(const data::AssetKey& key, LoadRequest request,
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
      [this, key, request,
        on_complete = std::move(on_complete)]() mutable -> co::Co<> {
        try {
          auto res = co_await LoadAssetAsync<T>(key, request);
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
    const auto identity = ResolveAssetIdentityForKey(key);
    if (!identity.has_value()) {
      return nullptr;
    }
    if (auto cached = content_cache_.Peek<T>(identity->hash_key)) {
      return cached;
    }
    return nullptr;
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
    const auto identity = ResolveAssetIdentityForKey(key);
    if (!identity.has_value()) {
      return false;
    }
    return content_cache_.Contains(identity->hash_key);
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
  OXGN_CNTT_API auto PinAsset(const data::AssetKey& key) -> bool override;
  OXGN_CNTT_API auto UnpinAsset(const data::AssetKey& key) -> bool override;

  //! Subscribe to resource eviction notifications for a resource type.
  OXGN_CNTT_API auto SubscribeResourceEvictions(TypeId resource_type,
    EvictionHandler handler) -> EvictionSubscription override;

  //=== Hot Reloading ===-----------------------------------------------------//

  //! Trigger a hot-reload of a script asset from a file path.
  OXGN_CNTT_API auto ReloadScript(const std::filesystem::path& path)
    -> void override;

  //! Trigger a full reload of all currently loaded script assets.
  OXGN_CNTT_API auto ReloadAllScripts() -> void override;

  using ScriptReloadCallback = IAssetLoader::ScriptReloadCallback;

  //! Subscribe to script reload events.
  OXGN_CNTT_API auto SubscribeScriptReload(ScriptReloadCallback callback)
    -> EvictionSubscription override;

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
  inline auto MakeResourceKey(const PakFile& pak_file,
    data::pak::core::ResourceIndexT resource_index) -> ResourceKey
  {
    auto pak_index = GetPakIndex(pak_file);
    auto resource_type_index
      = static_cast<uint16_t>(IndexOf<T, ResourceTypeList>::value);
    return PackResourceKey(pak_index, resource_type_index, resource_index);
  }

  //! Coroutine-based resource load by source-aware ResourceKey.
  //! Non-canonical convenience overload: for callers without explicit request
  //! classification. Canonical load path is request-aware.
  template <PakResource T>
  auto LoadResourceAsync(ResourceKey key) -> co::Co<std::shared_ptr<T>>;
  template <PakResource T>
  auto LoadResourceAsync(ResourceKey key, LoadRequest request)
    -> co::Co<std::shared_ptr<T>>;

  //! Coroutine-based resource decode from caller-provided cooked bytes.
  template <PakResource T>
  auto LoadResourceAsync(CookedResourceData<T> cooked)
    -> co::Co<std::shared_ptr<T>>
  {
    // Non-canonical convenience overload for cooked payload callers without
    // explicit request classification. Canonical load path is request-aware.
    co_return co_await LoadResourceAsync<T>(cooked, LoadRequest {});
  }

  template <PakResource T>
  auto LoadResourceAsync(CookedResourceData<T> cooked, LoadRequest request)
    -> co::Co<std::shared_ptr<T>>
  {
    auto decoded = co_await LoadResourceAsyncFromCookedErased(
      T::ClassTypeId(), cooked.key, cooked.bytes, request);
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
    ResourceKey key, TextureCallback on_complete) override
  {
    // Non-canonical convenience wrapper for callers that do not classify load
    // intent/priority. Canonical path is the request-aware overload below.
    StartLoadTexture(key, LoadRequest {}, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadTexture(
    ResourceKey key, LoadRequest request, TextureCallback on_complete) override
  {
    StartLoadResource<data::TextureResource>(
      key, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadTexture(
    CookedResourceData<data::TextureResource> cooked,
    TextureCallback on_complete) override
  {
    // Non-canonical convenience wrapper for callers that do not classify load
    // intent/priority. Canonical path is the request-aware overload below.
    StartLoadTexture(cooked, LoadRequest {}, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadTexture(
    CookedResourceData<data::TextureResource> cooked, LoadRequest request,
    TextureCallback on_complete) override
  {
    StartLoadResource<data::TextureResource>(
      cooked, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadBuffer(
    ResourceKey key, BufferCallback on_complete) override
  {
    // Non-canonical convenience wrapper for callers that do not classify load
    // intent/priority. Canonical path is the request-aware overload below.
    StartLoadBuffer(key, LoadRequest {}, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadBuffer(
    ResourceKey key, LoadRequest request, BufferCallback on_complete) override
  {
    StartLoadResource<data::BufferResource>(
      key, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadBuffer(
    CookedResourceData<data::BufferResource> cooked,
    BufferCallback on_complete) override
  {
    // Non-canonical convenience wrapper for callers that do not classify load
    // intent/priority. Canonical path is the request-aware overload below.
    StartLoadBuffer(cooked, LoadRequest {}, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadBuffer(
    CookedResourceData<data::BufferResource> cooked, LoadRequest request,
    BufferCallback on_complete) override
  {
    StartLoadResource<data::BufferResource>(
      cooked, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadPhysicsResource(
    ResourceKey key, PhysicsResourceCallback on_complete) override
  {
    // Non-canonical convenience wrapper for callers that do not classify load
    // intent/priority. Canonical path is the request-aware overload below.
    StartLoadPhysicsResource(key, LoadRequest {}, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadPhysicsResource(ResourceKey key,
    LoadRequest request, PhysicsResourceCallback on_complete) override
  {
    StartLoadResource<data::PhysicsResource>(
      key, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadMaterialAsset(
    const data::AssetKey& key, MaterialCallback on_complete) override
  {
    StartLoadAsset<data::MaterialAsset>(key, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadMaterialAsset(const data::AssetKey& key,
    LoadRequest request, MaterialCallback on_complete) override
  {
    StartLoadAsset<data::MaterialAsset>(key, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadGeometryAsset(
    const data::AssetKey& key, GeometryCallback on_complete) override
  {
    StartLoadAsset<data::GeometryAsset>(key, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadGeometryAsset(const data::AssetKey& key,
    LoadRequest request, GeometryCallback on_complete) override
  {
    StartLoadAsset<data::GeometryAsset>(key, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadScene(
    const data::AssetKey& key, SceneCallback on_complete) override
  {
    StartLoadAsset<data::SceneAsset>(key, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadScene(const data::AssetKey& key,
    LoadRequest request, SceneCallback on_complete) override
  {
    StartLoadAsset<data::SceneAsset>(key, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadPhysicsSceneAsset(
    const data::AssetKey& key, PhysicsSceneCallback on_complete) override
  {
    StartLoadAsset<data::PhysicsSceneAsset>(key, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadPhysicsSceneAsset(const data::AssetKey& key,
    LoadRequest request, PhysicsSceneCallback on_complete) override
  {
    StartLoadAsset<data::PhysicsSceneAsset>(
      key, request, std::move(on_complete));
  }

  OXGN_CNTT_API void StartLoadScriptAsset(
    const data::AssetKey& key, ScriptCallback on_complete) override
  {
    StartLoadAsset<data::ScriptAsset>(key, std::move(on_complete));
  }
  OXGN_CNTT_API void StartLoadScriptAsset(const data::AssetKey& key,
    LoadRequest request, ScriptCallback on_complete) override
  {
    StartLoadAsset<data::ScriptAsset>(key, request, std::move(on_complete));
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
    // Non-canonical convenience wrapper for callers that do not classify load
    // intent/priority. Canonical path is the request-aware overload below.
    StartLoadResource<T>(key, LoadRequest {}, std::move(on_complete));
  }

  template <PakResource T>
  void StartLoadResource(ResourceKey key, LoadRequest request,
    std::function<void(std::shared_ptr<T>)> on_complete)
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
      [this, key, request,
        on_complete = std::move(on_complete)]() mutable -> co::Co<> {
        try {
          auto res = co_await LoadResourceAsync<T>(key, request);
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
    // Non-canonical convenience wrapper for callers that do not classify load
    // intent/priority. Canonical path is the request-aware overload below.
    StartLoadResource<T>(cooked, LoadRequest {}, std::move(on_complete));
  }

  template <PakResource T>
  void StartLoadResource(CookedResourceData<T> cooked, LoadRequest request,
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
        request, on_complete = std::move(on_complete)]() mutable -> co::Co<> {
        try {
          std::span<const uint8_t> span(bytes.data(), bytes.size());
          auto res = co_await LoadResourceAsync<T>(
            {
              .key = key,
              .bytes = span,
            },
            request);
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

  [[nodiscard]] auto GetScriptAsset(const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::ScriptAsset> override
  {
    return GetAsset<data::ScriptAsset>(key);
  }
  [[nodiscard]] auto GetScriptResource(ResourceKey key) const noexcept
    -> std::shared_ptr<data::ScriptResource> override
  {
    return GetResource<data::ScriptResource>(key);
  }
  auto LoadScriptResourceAsync(ResourceKey key)
    -> co::Co<std::shared_ptr<data::ScriptResource>> override
  {
    co_return co_await LoadResourceAsync<data::ScriptResource>(key);
  }
  [[nodiscard]] OXGN_CNTT_API auto MakeScriptResourceKeyForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index) const noexcept
    -> std::optional<ResourceKey> override;
  [[nodiscard]] OXGN_CNTT_API auto ReadScriptResourceForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index) const
    -> std::shared_ptr<const data::ScriptResource> override;

  [[nodiscard]] auto GetPhysicsSceneAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::PhysicsSceneAsset> override
  {
    return GetAsset<data::PhysicsSceneAsset>(key);
  }

  [[nodiscard]] auto GetPhysicsResource(ResourceKey key) const noexcept
    -> std::shared_ptr<data::PhysicsResource> override
  {
    return GetResource<data::PhysicsResource>(key);
  }

  auto LoadPhysicsResourceAsync(ResourceKey key)
    -> co::Co<std::shared_ptr<data::PhysicsResource>> override
  {
    co_return co_await LoadResourceAsync<data::PhysicsResource>(key);
  }

  [[nodiscard]] OXGN_CNTT_API auto MakePhysicsResourceKey(
    data::SourceKey source_key,
    data::pak::core::ResourceIndexT resource_index) const noexcept
    -> std::optional<ResourceKey> override;
  [[nodiscard]] OXGN_CNTT_API auto MakePhysicsResourceKeyForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index) const noexcept
    -> std::optional<ResourceKey> override;
  [[nodiscard]] OXGN_CNTT_API auto MakePhysicsResourceKeyForAsset(
    const data::AssetKey& context_asset_key,
    const data::AssetKey& resource_asset_key) const noexcept
    -> std::optional<ResourceKey> override;
  [[nodiscard]] OXGN_CNTT_API auto ReadCollisionShapeAssetDescForAsset(
    const data::AssetKey& context_asset_key,
    const data::AssetKey& shape_asset_key) const
    -> std::optional<data::pak::physics::CollisionShapeAssetDesc> override;
  [[nodiscard]] OXGN_CNTT_API auto ReadPhysicsMaterialAssetDescForAsset(
    const data::AssetKey& context_asset_key,
    const data::AssetKey& material_asset_key) const
    -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc> override;
  [[nodiscard]] OXGN_CNTT_API auto FindPhysicsSidecarAssetKeyForScene(
    const data::AssetKey& scene_key) const
    -> std::optional<data::AssetKey> override;

  [[nodiscard]] auto GetInputActionAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::InputActionAsset> override
  {
    return GetAsset<data::InputActionAsset>(key);
  }

  [[nodiscard]] auto GetInputMappingContextAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::InputMappingContextAsset> override
  {
    return GetAsset<data::InputMappingContextAsset>(key);
  }

  [[nodiscard]] OXGN_CNTT_API auto GetHydratedScriptSlots(
    const data::SceneAsset& scene_asset,
    const data::pak::scripting::ScriptingComponentRecord& component) const
    -> std::vector<IAssetLoader::HydratedScriptSlot> override;

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

  [[nodiscard]] auto HasScriptAsset(const data::AssetKey& key) const noexcept
    -> bool override
  {
    return HasAsset<data::ScriptAsset>(key);
  }

  [[nodiscard]] auto HasPhysicsSceneAsset(
    const data::AssetKey& key) const noexcept -> bool override
  {
    return HasAsset<data::PhysicsSceneAsset>(key);
  }

  [[nodiscard]] auto HasInputActionAsset(
    const data::AssetKey& key) const noexcept -> bool override
  {
    return HasAsset<data::InputActionAsset>(key);
  }

  [[nodiscard]] auto HasInputMappingContextAsset(
    const data::AssetKey& key) const noexcept -> bool override
  {
    return HasAsset<data::InputMappingContextAsset>(key);
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
    const auto key_hash = HashResourceKey(key);
    return content_cache_.CheckOut<T>(
      key_hash, oxygen::CheckoutOwner::kExternal);
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
  OXGN_CNTT_API auto PinResource(ResourceKey key) -> bool override;
  OXGN_CNTT_API auto UnpinResource(ResourceKey key) -> bool override;

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
    const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt)
    -> co::Co<DecodedAssetAsyncResult>;

  OXGN_CNTT_API auto LoadMaterialAssetAsyncImpl(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt,
    LoadRequest request = {}) -> co::Co<std::shared_ptr<data::MaterialAsset>>;

  OXGN_CNTT_API auto LoadGeometryAssetAsyncImpl(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt,
    LoadRequest request = {}) -> co::Co<std::shared_ptr<data::GeometryAsset>>;

  OXGN_CNTT_API auto LoadSceneAssetAsyncImpl(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt,
    LoadRequest request = {}) -> co::Co<std::shared_ptr<data::SceneAsset>>;

  OXGN_CNTT_API auto LoadPhysicsSceneAssetAsyncImpl(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt,
    LoadRequest request = {})
    -> co::Co<std::shared_ptr<data::PhysicsSceneAsset>>;

  OXGN_CNTT_API auto LoadScriptAssetAsyncImpl(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt,
    LoadRequest request = {}) -> co::Co<std::shared_ptr<data::ScriptAsset>>;
  OXGN_CNTT_API auto LoadInputActionAssetAsyncImpl(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt,
    LoadRequest request = {})
    -> co::Co<std::shared_ptr<data::InputActionAsset>>;
  OXGN_CNTT_API auto LoadInputMappingContextAssetAsyncImpl(
    const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt,
    LoadRequest request = {})
    -> co::Co<std::shared_ptr<data::InputMappingContextAsset>>;

  OXGN_CNTT_API auto LoadResourceAsyncFromCookedErased(TypeId type_id,
    ResourceKey key, std::span<const uint8_t> bytes, LoadRequest request = {})
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

  std::unique_ptr<internal::DependencyGraphStore> dependency_graph_;
  std::unique_ptr<internal::DependencyReleaseEngine> dependency_release_engine_;

  //! Helper method for the recursive descent of asset dependencies when
  //! releasing assets.
  auto ReleaseAssetTree(const data::AssetKey& key) -> void;

  //=== Unified Content Cache ===---------------------------------------------//

  //! Unified content cache for both assets and resources
  mutable AnyCache<uint64_t, RefCountedEviction<uint64_t>> content_cache_;

  //! Hash an AssetKey for cache storage
  OXGN_CNTT_API static auto HashAssetKey(const data::AssetKey& key) -> uint64_t;
  OXGN_CNTT_API auto HashAssetKey(
    const data::AssetKey& key, uint16_t source_id) const -> uint64_t;
  struct AssetLoadRequest final {
    uint16_t source_id = 0;
    uint64_t hash_key = 0;
  };
  OXGN_CNTT_API auto PrepareAssetLoadRequest(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt) const
    -> std::optional<AssetLoadRequest>;
  struct ResolvedAssetIdentity final {
    uint64_t hash_key = 0;
    uint16_t source_id = 0;
  };
  OXGN_CNTT_API auto ResolveAssetIdentityForKey(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt) const
    -> std::optional<ResolvedAssetIdentity>;
  OXGN_CNTT_API auto IndexAssetHashMapping(
    uint64_t hash_key, const data::AssetKey& key, uint16_t source_id) -> void;
  OXGN_CNTT_API auto UnindexAssetHashMapping(uint64_t hash_key) -> void;
  OXGN_CNTT_API auto AssertSourceKeyConsistency(std::string_view context) const
    -> void;
  OXGN_CNTT_API auto AssertDependencyEdgeRefcountSymmetry(
    std::string_view context) const -> void;
  OXGN_CNTT_API auto AssertMountStateResetCompleteness(std::string_view context,
    bool expect_dependency_graphs_empty) const -> void;
  OXGN_CNTT_API auto AssertResourceMappingConsistency(
    std::string_view context) const -> void;
  OXGN_CNTT_API auto ResolveSourceIdForAsset(
    const data::AssetKey& context_asset_key) const -> std::optional<uint16_t>;
  OXGN_CNTT_API auto ResolveLoadSourceId(const data::AssetKey& key,
    std::optional<uint16_t> preferred_source_id = std::nullopt) const
    -> std::optional<uint16_t>;
  OXGN_CNTT_API auto MountPakFile(const std::filesystem::path& path)
    -> uint16_t;
  OXGN_CNTT_API auto ResolveSourceForId(uint16_t source_id) const
    -> const internal::IContentSource*;

  //! Hash a ResourceKey for cache storage (requires instance for SourceKey
  //! lookup)
  OXGN_CNTT_API auto HashResourceKey(const ResourceKey& key) const -> uint64_t;

  //! Recursively invalidates an asset and all its cached resource dependencies.
  OXGN_CNTT_API auto InvalidateAssetTree(const data::AssetKey& key) -> void;
  OXGN_CNTT_API auto ExecuteTrimPass(std::string_view trigger, bool automatic)
    -> void;
  OXGN_CNTT_API auto MaybeAutoTrimOnBudgetPressure(
    std::string_view trigger, bool force = false) -> void;

  //=== Type-erased Loading/Unloading ===-------------------------------------//

  using LoadFnErased = std::function<std::shared_ptr<void>(LoaderContext)>;

  // Type-erased unload function signature
  using UnloadFnErased
    = std::function<void(std::shared_ptr<void>, AssetLoader&)>;

  std::unordered_map<TypeId, LoadFnErased> asset_loaders_;
  std::unordered_map<TypeId, LoadFnErased> resource_loaders_;

  void UnloadObject(
    uint64_t cache_key, const oxygen::TypeId& type_id, EvictionReason reason);
  auto FlushResourceEvictionsForUncachedMappings(
    EvictionReason reason, bool force_emit_all) -> void;

  OXGN_CNTT_API auto AddTypeErasedAssetLoader(
    TypeId type_id, std::string_view type_name, LoadFnErased&& loader) -> void;

  OXGN_CNTT_API auto AddTypeErasedResourceLoader(
    TypeId type_id, std::string_view type_name, LoadFnErased&& loader) -> void;

  // Thread ownership for single-thread phase 1 policy.
  std::thread::id owning_thread_id_;
  void AssertOwningThread() const
  {
    if (owning_thread_id_ != std::this_thread::get_id()) {
      throw std::runtime_error(
        "AssetLoader used from non-owning thread (owning-thread invariant)");
    }
  }

  // Debug-only structural guard. In release builds this returns false and
  // runtime relies on upstream import/authoring/CI acyclicity validation.
  auto DetectCycle(const data::AssetKey& start, const data::AssetKey& target)
    -> bool; // returns true if adding edge start->target introduces cycle

  // Debug-only visited guard for ReleaseAssetTree recursion diagnostics.
  struct ReleaseVisitGuard;

  template <typename ResourceT>
  auto PublishResourceDependenciesAsync(
    const data::AssetKey& dependent_asset_key,
    const internal::DependencyCollector& collector, LoadRequest request = {})
    -> co::Co<>;

  struct LoadedGeometryBuffer final {
    ResourceKey key {};
    std::shared_ptr<data::BufferResource> resource;
  };

  using LoadedGeometryBuffersByIndex
    = std::unordered_map<uint32_t, LoadedGeometryBuffer>;

  using LoadedGeometryMaterialsByKey
    = std::unordered_map<data::AssetKey, std::shared_ptr<data::MaterialAsset>>;

  auto LoadGeometryBufferDependenciesAsync(
    const internal::DependencyCollector& collector, LoadRequest request = {})
    -> co::Co<LoadedGeometryBuffersByIndex>;

  auto LoadGeometryMaterialDependenciesAsync(
    const internal::DependencyCollector& collector,
    std::optional<uint16_t> preferred_source_id = std::nullopt,
    LoadRequest request = {}) -> co::Co<LoadedGeometryMaterialsByKey>;

  auto BindGeometryRuntimePointers(data::GeometryAsset& asset,
    const LoadedGeometryBuffersByIndex& buffers_by_index,
    const LoadedGeometryMaterialsByKey& materials_by_key) -> void;

  auto PublishGeometryDependencyEdges(const data::AssetKey& dependent_asset_key,
    const LoadedGeometryBuffersByIndex& buffers_by_index,
    const LoadedGeometryMaterialsByKey& materials_by_key,
    std::optional<uint16_t> preferred_source_id = std::nullopt) -> void;

  // Private helper to pack resource key without exposing internal type in the
  // public header. Implemented in the .cpp which includes InternalResourceKey.
  OXGN_CNTT_API static auto PackResourceKey(uint16_t pak_index,
    uint16_t resource_type_index,
    data::pak::core::ResourceIndexT resource_index) -> ResourceKey;

  // Bind an internal container-relative resource reference into an opaque
  // ResourceKey. Owning-thread only.
  OXGN_CNTT_NDAPI auto BindResourceRefToKey(const internal::ResourceRef& ref)
    -> ResourceKey;

  observer_ptr<co::ThreadPool> thread_pool_;

  bool work_offline_ { false };

  bool verify_content_hashes_ { false };
  ResidencyPolicy residency_policy_ {};

  std::unique_ptr<internal::ScriptHotReloadService> script_hot_reload_service_;

  //=== Eviction Notifications ===-----------------------------------------//

  std::unique_ptr<internal::EvictionRegistry> eviction_registry_;
  std::unique_ptr<internal::ResourceKeyRegistry> resource_key_registry_;
  std::unique_ptr<internal::AssetIdentityIndex> asset_identity_index_;
  uint64_t next_eviction_subscriber_id_ { 1 };
  std::shared_ptr<int> eviction_alive_token_;

  //=== In-flight Deduplication ===------------------------------------------//
  std::unique_ptr<internal::InFlightOperationTable> in_flight_ops_;
  std::unique_ptr<internal::ResourceLoadPipeline> resource_load_pipeline_;
  std::unique_ptr<internal::SceneCatalogQueryService>
    scene_catalog_query_service_;
  std::unique_ptr<internal::ScriptQueryService> script_query_service_;
  std::unique_ptr<internal::PhysicsQueryService> physics_query_service_;

  std::atomic<uint32_t> next_synthetic_texture_index_ { 1 };
  std::atomic<uint32_t> next_synthetic_buffer_index_ { 1 };
  std::atomic<uint64_t> next_load_request_sequence_ { 1 };
  std::unordered_map<uint64_t, uint32_t> pinned_resource_counts_ {};
  std::unordered_map<uint64_t, uint32_t> pinned_asset_counts_ {};
  observer_ptr<console::Console> console_ { nullptr };
  struct TrimTelemetry final {
    uint64_t attempts { 0 };
    uint64_t reclaimed_items { 0 };
    uint64_t reclaimed_bytes { 0 };
    uint64_t blocked_roots { 0 };
    uint64_t pruned_live_branches { 0 };
    uint64_t blocked_priority_roots { 0 };
    uint64_t orphan_resources { 0 };
  } trim_telemetry_ {};
  TelemetryStats telemetry_stats_ {};

  [[nodiscard]] auto NormalizeLoadRequest(LoadRequest request) const noexcept
    -> LoadRequest
  {
    if (request.priority == LoadPriority::kDefault) {
      switch (residency_policy_.default_priority_class) {
      case LoadPriorityClass::kBackground:
        request.priority = LoadPriority::kBackground;
        break;
      case LoadPriorityClass::kDefault:
        request.priority = LoadPriority::kDefault;
        break;
      case LoadPriorityClass::kCritical:
        request.priority = LoadPriority::kCritical;
        break;
      }
    }
    return request;
  }

  auto RecordAssetTelemetry(
    data::AssetType type, LoadTelemetryEvent event) noexcept -> void;
  auto RecordResourceTelemetry(
    TypeId type_id, LoadTelemetryEvent event) noexcept -> void;
  static auto ApplyLoadTelemetryEvent(
    TypedLoadTelemetry& counters, LoadTelemetryEvent event) noexcept -> void;
  auto RecordStorePressureEvent(std::string_view trigger, bool forced) -> void;
  auto RecordTrimAttempt(std::string_view trigger, bool automatic) -> void;
  auto RecordEviction(EvictionReason reason) noexcept -> void;
  auto UpdateTelemetrySummaryCVar() -> void;
  [[nodiscard]] auto MutableAssetTelemetry(data::AssetType type) noexcept
    -> TypedLoadTelemetry*;
  [[nodiscard]] auto MutableResourceTelemetry(TypeId type_id) noexcept
    -> TypedLoadTelemetry*;

public:
  // Debug-only dependent enumeration helper (implemented via forward scan).
  // Provided as public debug API below when OXYGEN_DEBUG is defined.
#if !defined(NDEBUG)
  using DebugAssetDependencyMap
    = std::unordered_map<data::AssetKey, std::unordered_set<data::AssetKey>>;
  OXGN_CNTT_NDAPI auto GetDebugAssetDependencyMap() const
    -> const DebugAssetDependencyMap&;

  // Enumerate direct dependents (debug only) by scanning forward map.
  template <typename Fn>
  auto ForEachDependent(const data::AssetKey& dependency, Fn&& fn) const -> void
  {
    const auto& asset_deps = GetDebugAssetDependencyMap();
    for (const auto& [dependent, deps] : asset_deps) {
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

auto to_string(AssetLoader::LoadTelemetryEvent event) noexcept
  -> std::string_view;
auto to_string(AssetLoader::TypedLoadMetric metric) noexcept
  -> std::string_view;

//=== Static asserts for CookedResourceData ==================================//

static_assert(
  std::is_trivially_copyable_v<CookedResourceData<data::TextureResource>>
    && std::is_trivially_destructible_v<
      CookedResourceData<data::TextureResource>>,
  "CookedResourceData must remain a trivial value carrier (ResourceKey + "
  "span) so it is safe and cheap to pass by value.");

static_assert(
  std::is_trivially_copyable_v<CookedResourceData<data::BufferResource>>
    && std::is_trivially_destructible_v<
      CookedResourceData<data::BufferResource>>,
  "CookedResourceData must remain a trivial value carrier (ResourceKey + "
  "span) so it is safe and cheap to pass by value.");

static_assert(
  std::is_trivially_copyable_v<CookedResourceData<data::ScriptResource>>
    && std::is_trivially_destructible_v<
      CookedResourceData<data::ScriptResource>>,
  "CookedResourceData must remain a trivial value carrier (ResourceKey + "
  "span) so it is safe and cheap to pass by value.");

static_assert(
  std::is_trivially_copyable_v<CookedResourceData<data::PhysicsResource>>
    && std::is_trivially_destructible_v<
      CookedResourceData<data::PhysicsResource>>,
  "CookedResourceData must remain a trivial value carrier (ResourceKey + "
  "span) so it is safe and cheap to pass by value.");

//=== Explicit Template Declarations for DLL Export ==========================//

//-- Known Asset Types --

template OXGN_CNTT_API auto AssetLoader::LoadAssetAsync<data::MaterialAsset>(
  const data::AssetKey& key) -> co::Co<std::shared_ptr<data::MaterialAsset>>;

template OXGN_CNTT_API auto AssetLoader::LoadAssetAsync<data::GeometryAsset>(
  const data::AssetKey& key) -> co::Co<std::shared_ptr<data::GeometryAsset>>;

template OXGN_CNTT_API auto AssetLoader::LoadAssetAsync<data::SceneAsset>(
  const data::AssetKey& key) -> co::Co<std::shared_ptr<data::SceneAsset>>;

template OXGN_CNTT_API auto AssetLoader::LoadAssetAsync<data::ScriptAsset>(
  const data::AssetKey& key) -> co::Co<std::shared_ptr<data::ScriptAsset>>;
template OXGN_CNTT_API auto AssetLoader::LoadAssetAsync<data::InputActionAsset>(
  const data::AssetKey& key) -> co::Co<std::shared_ptr<data::InputActionAsset>>;
template OXGN_CNTT_API auto
AssetLoader::LoadAssetAsync<data::InputMappingContextAsset>(
  const data::AssetKey& key)
  -> co::Co<std::shared_ptr<data::InputMappingContextAsset>>;

//-- Known Resource Types --

extern template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::BufferResource>(ResourceKey)
    -> co::Co<std::shared_ptr<data::BufferResource>>;
extern template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::BufferResource>(ResourceKey, LoadRequest)
    -> co::Co<std::shared_ptr<data::BufferResource>>;

extern template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::TextureResource>(ResourceKey)
    -> co::Co<std::shared_ptr<data::TextureResource>>;
extern template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::TextureResource>(
    ResourceKey, LoadRequest) -> co::Co<std::shared_ptr<data::TextureResource>>;

extern template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::ScriptResource>(ResourceKey)
    -> co::Co<std::shared_ptr<data::ScriptResource>>;
extern template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::ScriptResource>(ResourceKey, LoadRequest)
    -> co::Co<std::shared_ptr<data::ScriptResource>>;

extern template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::PhysicsResource>(ResourceKey)
    -> co::Co<std::shared_ptr<data::PhysicsResource>>;
extern template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<data::PhysicsResource>(
    ResourceKey, LoadRequest) -> co::Co<std::shared_ptr<data::PhysicsResource>>;

} // namespace oxygen::content
