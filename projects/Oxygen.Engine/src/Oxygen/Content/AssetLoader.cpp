//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/EnumIndexedArray.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Constants.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Internal/AssetIdentityIndex.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/DependencyGraphStore.h>
#include <Oxygen/Content/Internal/DependencyReleaseEngine.h>
#include <Oxygen/Content/Internal/EvictionRegistry.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/InFlightOperationTable.h>
#include <Oxygen/Content/Internal/InternalResourceKey.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/PakFileSource.h>
#include <Oxygen/Content/Internal/PhysicsQueryService.h>
#include <Oxygen/Content/Internal/ResourceKeyRegistry.h>
#include <Oxygen/Content/Internal/ResourceLoadPipeline.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/Internal/SceneCatalogQueryService.h>
#include <Oxygen/Content/Internal/ScriptHotReloadService.h>
#include <Oxygen/Content/Internal/ScriptQueryService.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/InputActionLoader.h>
#include <Oxygen/Content/Loaders/InputMappingContextLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/PhysicsResourceLoader.h>
#include <Oxygen/Content/Loaders/PhysicsSceneLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Serio/Reader.h>

using oxygen::content::AssetLoader;
using oxygen::content::LoaderContext;
using oxygen::content::LoadFunction;
using oxygen::content::PakFile;
using oxygen::content::PakResource;
namespace pak = oxygen::data::pak;
namespace internal = oxygen::content::internal;

namespace {
constexpr std::string_view kCVarVerifyContentHashes
  = "cntt.verify_content_hashes";
constexpr std::string_view kCVarTelemetryEnabled = "cntt.telemetry_enabled";
constexpr std::string_view kCVarLastStats = "cntt.last_stats";
constexpr std::string_view kCommandTrimCache = "cntt.trim_cache";
constexpr std::string_view kCommandDumpStats = "cntt.dump_stats";
constexpr std::string_view kCommandResetStats = "cntt.reset_stats";

// Debug-only hash collision tracking for AssetKey hashes.
#if !defined(NDEBUG)
std::mutex g_asset_hash_mutex;
std::unordered_map<uint64_t, oxygen::data::AssetKey> g_asset_hash_to_key;
#endif

struct ResourceCompositeKey final {
  oxygen::data::SourceKey source_key;
  uint16_t resource_type_index = 0;
  pak::core::ResourceIndexT resource_index = pak::core::kNoResourceIndex;

  auto operator==(const ResourceCompositeKey&) const -> bool = default;
};

} // namespace

namespace oxygen::content {

using oxygen::content::constants::kLooseCookedSourceIdBase;
using oxygen::content::constants::kSyntheticSourceId;

// Implement the private helper declared in the header to avoid exposing the
// internal header in the public API.
auto AssetLoader::PackResourceKey(uint16_t pak_index,
  uint16_t resource_type_index, pak::core::ResourceIndexT resource_index)
  -> ResourceKey
{
  internal::InternalResourceKey key(
    pak_index, resource_type_index, resource_index);
  return key.GetRawKey();
}

struct AssetLoader::Impl final {
  internal::ContentSourceRegistry source_registry {};

#if !defined(NDEBUG)
  std::mutex hash_collision_mutex;
  std::unordered_map<uint64_t, ResourceCompositeKey> resource_hash_to_key;
#endif
};
} // namespace oxygen::content

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

inline auto GetResourceTypeIndexByTypeId(const oxygen::TypeId type_id)
  -> uint16_t
{
  static const auto ids = MakeTypeIdArray(oxygen::content::ResourceTypeList {});
  for (uint16_t i = 0; i < ids.size(); ++i) {
    if (ids[i] == type_id) {
      return i;
    }
  }
  throw std::runtime_error("Unknown resource type id for ResourceRef binding");
}

inline auto IsResourceTypeId(const oxygen::TypeId type_id) -> bool
{
  static const auto ids = MakeTypeIdArray(oxygen::content::ResourceTypeList {});
  return std::ranges::any_of(
    ids, [type_id](const auto id) { return id == type_id; });
}
} // namespace

namespace {
// Helper validates eviction callback arguments. Parameter ordering chosen to
// minimize misuse. expected_key_hash: hash originally computed for resource
// actual_key_hash: hash received from eviction callback
// type_id: type id reference for validation
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto SanityCheckResourceEviction(const uint64_t expected_key_hash,
  const uint64_t actual_key_hash, const oxygen::TypeId expected_type_id,
  const oxygen::TypeId actual_type_id) -> bool
{
  CHECK_EQ_F(expected_key_hash, actual_key_hash);
  CHECK_EQ_F(expected_type_id, actual_type_id);
  return true;
}
} // namespace

namespace {
struct AssetLoadTelemetryCallbacks final {
  std::function<void()> on_request;
  std::function<void()> on_cache_hit;
  std::function<void()> on_cache_miss;
  std::function<void()> on_joined_inflight;
  std::function<void()> on_started_new_inflight;
};

template <typename AssetT, typename CacheHitFn, typename DecodeAndPublishFn>
auto RunAssetLoadPipeline(const oxygen::TypeId type_id, const uint64_t hash_key,
  internal::InFlightOperationTable& in_flight_ops,
  const oxygen::content::LoadRequest& request, const uint64_t request_sequence,
  CacheHitFn&& cache_hit_fn, DecodeAndPublishFn&& decode_and_publish_fn,
  const AssetLoadTelemetryCallbacks& telemetry_callbacks = {})
  -> oxygen::co::Co<std::shared_ptr<AssetT>>
{
  if (telemetry_callbacks.on_request) {
    telemetry_callbacks.on_request();
  }
  if (auto cached = co_await cache_hit_fn()) {
    if (telemetry_callbacks.on_cache_hit) {
      telemetry_callbacks.on_cache_hit();
    }
    co_return cached;
  }
  if (telemetry_callbacks.on_cache_miss) {
    telemetry_callbacks.on_cache_miss();
  }

  if (auto shared_join = in_flight_ops.Find(type_id, hash_key,
        internal::InFlightOperationTable::RequestMeta {
          .priority = request.priority,
          .intent = request.intent,
          .sequence = request_sequence,
        });
    shared_join.has_value()) {
    if (telemetry_callbacks.on_joined_inflight) {
      telemetry_callbacks.on_joined_inflight();
    }
    auto joined = co_await *shared_join;
    co_return std::static_pointer_cast<AssetT>(std::move(joined));
  }
  if (telemetry_callbacks.on_started_new_inflight) {
    telemetry_callbacks.on_started_new_inflight();
  }

  auto op = [type_id, hash_key, &in_flight_ops,
              cache_hit_fn = std::forward<CacheHitFn>(cache_hit_fn),
              decode_and_publish_fn = std::forward<DecodeAndPublishFn>(
                decode_and_publish_fn)]() mutable
    -> oxygen::co::Co<std::shared_ptr<void>> {
    oxygen::ScopeGuard erase_guard(
      [&in_flight_ops, type_id, hash_key]() noexcept {
        in_flight_ops.Erase(type_id, hash_key);
      });

    if (auto cached = co_await cache_hit_fn()) {
      co_return std::static_pointer_cast<void>(std::move(cached));
    }

    auto decoded = co_await decode_and_publish_fn();
    co_return std::static_pointer_cast<void>(std::move(decoded));
  }();

  oxygen::co::Shared shared(std::move(op));
  in_flight_ops.InsertOrAssign(type_id, hash_key, shared,
    internal::InFlightOperationTable::RequestMeta {
      .priority = request.priority,
      .intent = request.intent,
      .sequence = request_sequence,
    });
  auto decoded = co_await shared;
  co_return std::static_pointer_cast<AssetT>(std::move(decoded));
}
} // namespace

//=== Basic methods ==========================================================//

AssetLoader::AssetLoader(
  engine::EngineTag /*tag*/, const AssetLoaderConfig& config)
  : impl_(std::make_unique<Impl>())
  , dependency_graph_(std::make_unique<internal::DependencyGraphStore>())
  , dependency_release_engine_(
      std::make_unique<internal::DependencyReleaseEngine>())
  , thread_pool_(config.thread_pool)
  , work_offline_(config.work_offline)
  , verify_content_hashes_(config.verify_content_hashes)
  , residency_policy_(config.residency_policy)
  , eviction_registry_(std::make_unique<internal::EvictionRegistry>())
  , resource_key_registry_(std::make_unique<internal::ResourceKeyRegistry>())
  , asset_identity_index_(std::make_unique<internal::AssetIdentityIndex>())
  , in_flight_ops_(std::make_unique<internal::InFlightOperationTable>())
{
  using serio::FileStream;

  LOG_SCOPE_FUNCTION(INFO);

  owning_thread_id_ = std::this_thread::get_id();
  eviction_alive_token_ = std::make_shared<int>(0);
  script_hot_reload_service_
    = std::make_unique<internal::ScriptHotReloadService>(config.path_finder);
  scene_catalog_query_service_
    = std::make_unique<internal::SceneCatalogQueryService>();
  script_query_service_ = std::make_unique<internal::ScriptQueryService>();
  physics_query_service_ = std::make_unique<internal::PhysicsQueryService>();

  resource_load_pipeline_ = std::make_unique<internal::ResourceLoadPipeline>(
    impl_->source_registry, resource_loaders_, content_cache_, *in_flight_ops_,
    thread_pool_, work_offline_,
    internal::ResourceLoadPipeline::Callbacks {
      .assert_owning_thread = [this]() { AssertOwningThread(); },
      .hash_resource_key
      = [this](const ResourceKey& key) { return HashResourceKey(key); },
      .map_resource_key =
        [this](const uint64_t hash, const ResourceKey key) {
          resource_key_registry_->InsertOrAssign(hash, key);
        },
      .default_priority_class
      = [this]() { return residency_policy_.default_priority_class; },
      .next_request_sequence
      = [this]() { return next_load_request_sequence_.fetch_add(1); },
      .on_resource_request =
        [this](const TypeId type_id) {
          RecordResourceTelemetry(type_id, LoadTelemetryEvent::kRequest);
        },
      .on_resource_cache_hit =
        [this](const TypeId type_id) {
          RecordResourceTelemetry(type_id, LoadTelemetryEvent::kCacheHit);
        },
      .on_resource_cache_miss =
        [this](const TypeId type_id) {
          RecordResourceTelemetry(type_id, LoadTelemetryEvent::kCacheMiss);
        },
      .on_resource_joined_inflight =
        [this](const TypeId type_id) {
          RecordResourceTelemetry(type_id, LoadTelemetryEvent::kTasksDeduped);
        },
      .on_resource_started_inflight =
        [this](const TypeId type_id) {
          RecordResourceTelemetry(type_id, LoadTelemetryEvent::kTasksSpawned);
        },
      .on_resource_decode_failure =
        [this](const TypeId type_id) {
          RecordResourceTelemetry(type_id, LoadTelemetryEvent::kDecodeFailure);
        },
      .on_resource_type_mismatch =
        [this](const TypeId type_id) {
          RecordResourceTelemetry(type_id, LoadTelemetryEvent::kTypeMismatch);
        },
      .on_resource_store_retry_failed =
        [this](const TypeId type_id) {
          RecordResourceTelemetry(
            type_id, LoadTelemetryEvent::kStoreRetryFailure);
        },
      .on_store_pressure
      = [this](const std::string_view trigger,
          const bool force) { MaybeAutoTrimOnBudgetPressure(trigger, force); },
    });

  if (residency_policy_.cache_budget_bytes == 0) {
    throw std::invalid_argument("AssetLoader residency budget must be > 0");
  }
  const auto set_budget_status = content_cache_.SetBudget(
    static_cast<AnyCache<uint64_t, RefCountedEviction<uint64_t>>::CostType>(
      residency_policy_.cache_budget_bytes));
  LOG_F(INFO,
    "residency policy initialized (budget={} trim_mode={} "
    "default_priority={} status={})",
    residency_policy_.cache_budget_bytes, residency_policy_.trim_mode,
    residency_policy_.default_priority_class,
    oxygen::to_string(set_budget_status));

  // Register asset loaders
  RegisterLoader(loaders::LoadGeometryAsset);
  RegisterLoader(loaders::LoadMaterialAsset);
  RegisterLoader(loaders::LoadSceneAsset);
  RegisterLoader(loaders::LoadPhysicsSceneAsset);
  RegisterLoader(loaders::LoadScriptAsset);
  RegisterLoader(loaders::LoadInputActionAsset);
  RegisterLoader(loaders::LoadInputMappingContextAsset);

  // Register resource loaders
  RegisterLoader(loaders::LoadBufferResource);
  RegisterLoader(loaders::LoadScriptResource);
  RegisterLoader(loaders::LoadTextureResource);
  RegisterLoader(loaders::LoadPhysicsResource);
}

auto AssetLoader::SetVerifyContentHashes(const bool enable) -> void
{
  AssertOwningThread();
  if (verify_content_hashes_ == enable) {
    return;
  }
  verify_content_hashes_ = enable;
  LOG_F(INFO, "verify_content_hashes={}",
    verify_content_hashes_ ? "enabled" : "disabled");
}

auto AssetLoader::VerifyContentHashesEnabled() const noexcept -> bool
{
  return verify_content_hashes_;
}

auto AssetLoader::SetResidencyPolicy(const ResidencyPolicy& policy) -> void
{
  AssertOwningThread();
  if (policy.cache_budget_bytes == 0) {
    throw std::invalid_argument("AssetLoader residency budget must be > 0");
  }

  const auto set_budget_status = content_cache_.SetBudget(
    static_cast<AnyCache<uint64_t, RefCountedEviction<uint64_t>>::CostType>(
      policy.cache_budget_bytes));
  residency_policy_ = policy;

  LOG_F(INFO,
    "residency policy updated (budget={} trim_mode={} "
    "default_priority={} status={})",
    residency_policy_.cache_budget_bytes, residency_policy_.trim_mode,
    residency_policy_.default_priority_class,
    oxygen::to_string(set_budget_status));

  MaybeAutoTrimOnBudgetPressure("policy_updated");
}

auto AssetLoader::GetResidencyPolicy() const noexcept -> ResidencyPolicy
{
  return residency_policy_;
}

auto AssetLoader::QueryResidencyPolicyState() const -> ResidencyPolicyState
{
  AssertOwningThread();
  const auto stats = content_cache_.SnapshotStats();
  return ResidencyPolicyState {
    .policy = residency_policy_,
    .cache_entries = stats.size,
    .consumed_bytes = static_cast<uint64_t>(stats.consumed),
    .checked_out_items = stats.checked_out_items,
    .over_budget = stats.over_budget,
    .trim_attempts = trim_telemetry_.attempts,
    .reclaimed_items = trim_telemetry_.reclaimed_items,
    .reclaimed_bytes = trim_telemetry_.reclaimed_bytes,
    .blocked_roots = trim_telemetry_.blocked_roots,
  };
}

auto AssetLoader::MutableAssetTelemetry(const data::AssetType type) noexcept
  -> TypedLoadTelemetry*
{
  switch (type) {
  case data::AssetType::kMaterial:
    return &telemetry_stats_.material_assets;
  case data::AssetType::kGeometry:
    return &telemetry_stats_.geometry_assets;
  case data::AssetType::kScene:
    return &telemetry_stats_.scene_assets;
  case data::AssetType::kPhysicsScene:
    return &telemetry_stats_.physics_scene_assets;
  case data::AssetType::kScript:
    return &telemetry_stats_.script_assets;
  case data::AssetType::kInputAction:
    return &telemetry_stats_.input_action_assets;
  case data::AssetType::kInputMappingContext:
    return &telemetry_stats_.input_mapping_context_assets;
  case data::AssetType::kUnknown:
  case data::AssetType::kPhysicsMaterial:
  case data::AssetType::kCollisionShape:
    return nullptr;
  }
  return nullptr;
}

auto AssetLoader::MutableResourceTelemetry(const TypeId type_id) noexcept
  -> TypedLoadTelemetry*
{
  if (type_id == data::TextureResource::ClassTypeId()) {
    return &telemetry_stats_.texture_resources;
  }
  if (type_id == data::BufferResource::ClassTypeId()) {
    return &telemetry_stats_.buffer_resources;
  }
  if (type_id == data::ScriptResource::ClassTypeId()) {
    return &telemetry_stats_.script_resources;
  }
  if (type_id == data::PhysicsResource::ClassTypeId()) {
    return &telemetry_stats_.physics_resources;
  }
  return nullptr;
}

auto AssetLoader::RecordAssetTelemetry(
  const data::AssetType type, const LoadTelemetryEvent event) noexcept -> void
{
  if (!telemetry_stats_.telemetry_enabled) {
    return;
  }
  auto* counters = MutableAssetTelemetry(type);
  if (counters == nullptr) {
    return;
  }
  ApplyLoadTelemetryEvent(*counters, event);
}

auto AssetLoader::RecordResourceTelemetry(
  const TypeId type_id, const LoadTelemetryEvent event) noexcept -> void
{
  if (!telemetry_stats_.telemetry_enabled) {
    return;
  }
  auto* counters = MutableResourceTelemetry(type_id);
  if (counters == nullptr) {
    return;
  }
  ApplyLoadTelemetryEvent(*counters, event);
}

auto oxygen::content::to_string(
  const AssetLoader::LoadTelemetryEvent event) noexcept -> std::string_view
{
  using LoadTelemetryEvent = AssetLoader::LoadTelemetryEvent;
  static constexpr oxygen::EnumIndexedArray<LoadTelemetryEvent, std::string_view>
    kEventNames {
      .data = {
        "request",
        "cache_hit",
        "cache_miss",
        "tasks_deduped",
        "tasks_spawned",
        "err_decode",
        "err_type_mismatch",
        "err_retry_failed",
        "err_canceled",
      },
    };
  return kEventNames[event];
}

auto oxygen::content::to_string(
  const AssetLoader::TypedLoadMetric metric) noexcept -> std::string_view
{
  using TypedLoadMetric = AssetLoader::TypedLoadMetric;
  static constexpr oxygen::EnumIndexedArray<TypedLoadMetric, std::string_view>
    kMetricNames {
      .data = {
        "requests",
        "cache_hits",
        "cache_misses",
        "tasks_deduped",
        "tasks_spawned",
        "err_decode",
        "err_type_mismatch",
        "err_retry_failed",
        "err_canceled",
      },
    };
  return kMetricNames[metric];
}

auto AssetLoader::ApplyLoadTelemetryEvent(
  TypedLoadTelemetry& counters, const LoadTelemetryEvent event) noexcept -> void
{
  static constexpr oxygen::EnumIndexedArray<LoadTelemetryEvent, std::optional<TypedLoadMetric>>
    kEventCounters {
      .data = {
        TypedLoadMetric::kRequests,
        TypedLoadMetric::kCacheHits,
        TypedLoadMetric::kCacheMisses,
        TypedLoadMetric::kTasksDeduped,
        TypedLoadMetric::kTasksSpawned,
        TypedLoadMetric::kErrDecode,
        TypedLoadMetric::kErrTypeMismatch,
        TypedLoadMetric::kErrRetryFailed,
        TypedLoadMetric::kErrCanceled,
      },
    };
  if (const auto metric = kEventCounters[event]; metric.has_value()) {
    ++counters[*metric];
  }
}

auto AssetLoader::RecordStorePressureEvent(
  const std::string_view trigger, const bool forced) -> void
{
  if (!telemetry_stats_.telemetry_enabled) {
    return;
  }
  ++telemetry_stats_.pressure.events_total;
  if (forced) {
    ++telemetry_stats_.pressure.events_forced;
  } else {
    ++telemetry_stats_.pressure.events_soft;
  }
  if (trigger == "resource_store_failed") {
    ++telemetry_stats_.pressure.resource_store_failed;
  } else if (trigger == "resource_store_over_budget") {
    ++telemetry_stats_.pressure.resource_store_over_budget;
  } else if (trigger.find("_store_failed") != std::string_view::npos) {
    ++telemetry_stats_.pressure.asset_store_failed;
  } else if (trigger.find("_store_succeeded") != std::string_view::npos) {
    ++telemetry_stats_.pressure.asset_store_succeeded;
  }
}

auto AssetLoader::RecordTrimAttempt(
  const std::string_view trigger, const bool automatic) -> void
{
  static_cast<void>(trigger);
  if (!telemetry_stats_.telemetry_enabled) {
    return;
  }
  if (automatic) {
    ++telemetry_stats_.trim.auto_attempts;
  } else {
    ++telemetry_stats_.trim.manual_attempts;
  }
}

auto AssetLoader::RecordEviction(const EvictionReason reason) noexcept -> void
{
  if (!telemetry_stats_.telemetry_enabled) {
    return;
  }
  if (reason == EvictionReason::kRefCountZero) {
    ++telemetry_stats_.eviction.on_refcount_zero;
    return;
  }
  if (reason == EvictionReason::kTrim) {
    ++telemetry_stats_.eviction.on_trim;
    return;
  }
  if (reason == EvictionReason::kClear) {
    ++telemetry_stats_.eviction.on_clear;
    return;
  }
  if (reason == EvictionReason::kShutdown) {
    ++telemetry_stats_.eviction.on_shutdown;
  }
}

auto AssetLoader::GetTelemetryStats() const -> TelemetryStats
{
  AssertOwningThread();
  auto snapshot = telemetry_stats_;
  snapshot.telemetry_enabled = telemetry_stats_.telemetry_enabled;
  snapshot.trim.reclaimed_items = trim_telemetry_.reclaimed_items;
  snapshot.trim.reclaimed_bytes = trim_telemetry_.reclaimed_bytes;
  snapshot.trim.blocked_total = trim_telemetry_.blocked_roots;
  snapshot.trim.pruned_live_branches = trim_telemetry_.pruned_live_branches;
  snapshot.trim.blocked_priority_roots = trim_telemetry_.blocked_priority_roots;
  snapshot.trim.orphan_resources = trim_telemetry_.orphan_resources;
  const auto cache_stats = content_cache_.SnapshotStats();
  snapshot.cache.entries = cache_stats.size;
  snapshot.cache.consumed_budget = static_cast<uint64_t>(cache_stats.consumed);
  // Fast telemetry path: report entries with checkout count above baseline
  // loader retain (refcount > 1).
  snapshot.cache.checked_out_items = cache_stats.checked_out_external;
  snapshot.cache.over_budget = cache_stats.over_budget;
  const auto in_flight_stats = in_flight_ops_->GetStats();
  snapshot.in_flight = InFlightTelemetry {
    .find_calls = in_flight_stats.find_calls,
    .find_hits = in_flight_stats.find_hits,
    .insert_calls = in_flight_stats.insert_calls,
    .erase_calls = in_flight_stats.erase_calls,
    .clear_calls = in_flight_stats.clear_calls,
    .active_type_buckets = in_flight_stats.active_type_buckets,
    .active_operations = in_flight_stats.active_operations,
  };
  return snapshot;
}

auto AssetLoader::ResetTelemetryStats() noexcept -> void
{
  AssertOwningThread();
  const bool was_enabled = telemetry_stats_.telemetry_enabled;
  telemetry_stats_ = {};
  telemetry_stats_.telemetry_enabled = was_enabled;
  trim_telemetry_ = {};
  if (in_flight_ops_) {
    in_flight_ops_->ResetStats();
  }
}

auto AssetLoader::SetTelemetryEnabled(const bool enabled) -> void
{
  AssertOwningThread();
  telemetry_stats_.telemetry_enabled = enabled;
}

auto AssetLoader::IsTelemetryEnabled() const noexcept -> bool
{
  return telemetry_stats_.telemetry_enabled;
}

auto AssetLoader::UpdateTelemetrySummaryCVar() -> void
{
  if (console_ == nullptr) {
    return;
  }
  const auto stats = GetTelemetryStats();
  const auto requests = TypedLoadMetric::kRequests;
  const auto cache_hits = TypedLoadMetric::kCacheHits;
  const auto summary = fmt::format(
    "cache_entries={},consumed_budget={},over_budget={},"
    "asset_requests={},resource_requests={},asset_hits={},resource_hits={}",
    stats.cache.entries, stats.cache.consumed_budget,
    stats.cache.over_budget ? 1 : 0,
    stats.material_assets[requests] + stats.geometry_assets[requests]
      + stats.scene_assets[requests] + stats.physics_scene_assets[requests]
      + stats.script_assets[requests] + stats.input_action_assets[requests]
      + stats.input_mapping_context_assets[requests],
    stats.texture_resources[requests] + stats.buffer_resources[requests]
      + stats.script_resources[requests] + stats.physics_resources[requests],
    stats.material_assets[cache_hits] + stats.geometry_assets[cache_hits]
      + stats.scene_assets[cache_hits] + stats.physics_scene_assets[cache_hits]
      + stats.script_assets[cache_hits] + stats.input_action_assets[cache_hits]
      + stats.input_mapping_context_assets[cache_hits],
    stats.texture_resources[cache_hits] + stats.buffer_resources[cache_hits]
      + stats.script_resources[cache_hits]
      + stats.physics_resources[cache_hits]);
  (void)console_->SetCVarFromText(
    { .name = std::string(kCVarLastStats), .text = summary },
    { .source = console::CommandSource::kAutomation,
      .shipping_build = false,
      .record_history = false });
}

AssetLoader::~AssetLoader() = default;

// LiveObject activation: open the nursery used by the AssetLoader
auto AssetLoader::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
  // AssetLoader enforces a single-thread (owning-thread) policy for its
  // public API. The engine may construct the AssetLoader on a different thread
  // than the one that runs the engine loop (e.g., editor creates the engine on
  // the UI thread). Bind ownership to the activation thread, which is the
  // engine thread in normal operation.
  LOG_F(INFO, "AssetLoader::ActivateAsync thread={} previous_owner={}",
    std::hash<std::thread::id> {}(std::this_thread::get_id()),
    std::hash<std::thread::id> {}(owning_thread_id_));
  owning_thread_id_ = std::this_thread::get_id();
  LOG_F(INFO, "AssetLoader::ActivateAsync bound owner={}",
    std::hash<std::thread::id> {}(owning_thread_id_));
  return co::OpenNursery(nursery_, std::move(started));
}

void AssetLoader::Run()
{
  // Optional: start background supervision tasks here via nursery_->Start(...)
}

void AssetLoader::Stop()
{
  if (nursery_) {
    nursery_->Cancel();
  }

  // Prevent new joiners from attaching to canceled shared operations.
  // Per-operation erase guards tolerate missing entries.
  in_flight_ops_->Clear();

  {
    auto eviction_guard = content_cache_.OnEviction(
      [&](const uint64_t cache_key, std::shared_ptr<void> value,
        const TypeId type_id) {
        static_cast<void>(value);
        UnloadObject(cache_key, type_id, EvictionReason::kShutdown);
      });
    content_cache_.Clear();
  }
  FlushResourceEvictionsForUncachedMappings(EvictionReason::kShutdown, true);

  resource_key_registry_->Clear();
  asset_identity_index_->Clear();
  eviction_registry_->Clear();
  pinned_resource_counts_.clear();
  pinned_asset_counts_.clear();
  eviction_alive_token_.reset();
}

auto AssetLoader::IsRunning() const -> bool { return nursery_ != nullptr; }

auto AssetLoader::AddPakFile(const std::filesystem::path& path) -> void
{
  AssertOwningThread();
  // Normalize the path to ensure consistent handling
  std::error_code ec {};
  auto normalized = std::filesystem::weakly_canonical(path, ec);
  if (ec) {
    normalized = path.lexically_normal();
  }

  auto new_source = std::make_unique<internal::PakFileSource>(
    normalized, verify_content_hashes_);
  const bool was_already_mounted
    = std::ranges::find(impl_->source_registry.PakPaths(), normalized)
    != impl_->source_registry.PakPaths().end();
#if !defined(NDEBUG)
  {
    const auto source_key = new_source->GetSourceKey();
    if (source_key.IsNil()) {
      LOG_F(WARNING,
        "Mounted PAK has zero SourceKey (PakHeader.guid); cache aliasing risk: "
        "path={}",
        normalized.string());
    }
    for (const auto& existing : impl_->source_registry.Sources()) {
      if (existing && existing->GetSourceKey() == source_key) {
        LOG_F(WARNING,
          "Mounted PAK shares SourceKey with an existing source; cache "
          "aliasing "
          "risk: source_key={} new_path={}",
          source_key, normalized.string());
        break;
      }
    }
  }
#endif

  const auto mount_result
    = impl_->source_registry.MountPak(normalized, std::move(new_source));

  if (mount_result.action
      == internal::ContentSourceRegistry::MountAction::kRefreshed
    || was_already_mounted) {
    LOG_F(INFO,
      "Refreshing mounted PAK content source: id={} path={} (reloading pak)",
      mount_result.source_id, normalized.string());

    {
      auto eviction_guard = content_cache_.OnEviction(
        [&](const uint64_t cache_key, std::shared_ptr<void> value,
          const TypeId type_id) {
          static_cast<void>(value);
          UnloadObject(cache_key, type_id, EvictionReason::kClear);
        });
      content_cache_.Clear();
    }
    FlushResourceEvictionsForUncachedMappings(EvictionReason::kClear, true);

    resource_key_registry_->Clear();
    asset_identity_index_->Clear();
    pinned_resource_counts_.clear();
    pinned_asset_counts_.clear();
    dependency_graph_->Clear();
    AssertSourceKeyConsistency("AddPakFile.refresh");
    AssertMountStateResetCompleteness(
      "AddPakFile.refresh", /*expect_dependency_graphs_empty=*/true);
    return;
  }

  LOG_F(INFO, "Mounted PAK content source: id={} path={}",
    mount_result.source_id, normalized.string());
  AssertSourceKeyConsistency("AddPakFile.mount");
}

auto AssetLoader::AddLooseCookedRoot(const std::filesystem::path& path) -> void
{
  AssertOwningThread();
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path);
  const auto normalized_s = normalized.string();

  auto new_source = std::make_unique<internal::LooseCookedSource>(
    normalized, verify_content_hashes_);

  auto clear_content_caches = [this]() {
    // Refreshing a mounted loose root is a destructive cache operation.
    // Callers should only trigger this when they explicitly want to invalidate
    // cached assets/resources for that source (e.g. reimport/update workflow),
    // not during regular scene swap/reload.
    auto eviction_guard = content_cache_.OnEviction(
      [&](const uint64_t cache_key, std::shared_ptr<void> value,
        const TypeId type_id) {
        static_cast<void>(value);
        UnloadObject(cache_key, type_id, EvictionReason::kClear);
      });
    content_cache_.Clear();
    FlushResourceEvictionsForUncachedMappings(EvictionReason::kClear, true);
    resource_key_registry_->Clear();
    asset_identity_index_->Clear();
    pinned_resource_counts_.clear();
    pinned_asset_counts_.clear();
    dependency_graph_->Clear();
    AssertSourceKeyConsistency("AddLooseCookedRoot.clear_content_caches");
    AssertMountStateResetCompleteness("AddLooseCookedRoot.clear_content_caches",
      /*expect_dependency_graphs_empty=*/true);
  };

  const bool was_already_mounted
    = std::ranges::any_of(impl_->source_registry.Sources(),
      [&](const auto& s) { return s && s->DebugName() == normalized_s; });
#if !defined(NDEBUG)
  {
    const auto source_key = new_source->GetSourceKey();
    if (source_key.IsNil()) {
      LOG_F(WARNING,
        "Mounted loose cooked root has zero SourceKey (IndexHeader.guid); "
        "cache aliasing risk: root={}",
        normalized.string());
    }
    for (const auto& existing : impl_->source_registry.Sources()) {
      if (existing && existing->GetSourceKey() == source_key) {
        LOG_F(WARNING,
          "Mounted loose cooked root shares SourceKey with an existing source; "
          "cache aliasing risk: source_key={} new_root={}",
          source_key, normalized.string());
        break;
      }
    }
  }
#endif

  const auto mount_result
    = impl_->source_registry.MountLoose(normalized_s, std::move(new_source));
  if (mount_result.action
      == internal::ContentSourceRegistry::MountAction::kRefreshed
    || was_already_mounted) {
    LOG_F(INFO,
      "Refreshing loose cooked content source: root={} (reloading index)",
      normalized_s);
    clear_content_caches();
    return;
  }

  DCHECK_F(mount_result.source_id >= kLooseCookedSourceIdBase);
  LOG_F(INFO, "Mounted loose cooked content source: id={} root={}",
    mount_result.source_id, normalized.string());
  AssertSourceKeyConsistency("AddLooseCookedRoot.mount");
}

auto AssetLoader::ClearMounts() -> void
{
  LOG_F(INFO, "AssetLoader::ClearMounts thread={} owner={}",
    std::hash<std::thread::id> {}(std::this_thread::get_id()),
    std::hash<std::thread::id> {}(owning_thread_id_));
  AssertOwningThread(); // Ensure this method is called on the owning thread
  impl_->source_registry.Clear();

  // Clear the content cache to prevent stale assets from being returned
  // when switching content sources (e.g. scene swap).
  {
    auto eviction_guard = content_cache_.OnEviction(
      [&](const uint64_t cache_key, std::shared_ptr<void> value,
        const TypeId type_id) {
        static_cast<void>(value);
        UnloadObject(cache_key, type_id, EvictionReason::kClear);
      });
    content_cache_.Clear();
  }
  FlushResourceEvictionsForUncachedMappings(EvictionReason::kClear, true);

  resource_key_registry_->Clear();
  asset_identity_index_->Clear();
  pinned_resource_counts_.clear();
  pinned_asset_counts_.clear();
  // Dependency graphs are keyed by AssetKey/ResourceKey from mounted sources.
  // Clearing mounts invalidates those identities; drop all edges to avoid
  // stale dependencies leaking into subsequent loads.
  dependency_graph_->Clear();
  AssertSourceKeyConsistency("ClearMounts");
  AssertMountStateResetCompleteness(
    "ClearMounts", /*expect_dependency_graphs_empty=*/true);
}

auto AssetLoader::ExecuteTrimPass(
  const std::string_view trigger, const bool automatic) -> void
{
  LOG_F(INFO, "trim start trigger={} automatic={} thread={} owner={}", trigger,
    automatic, std::hash<std::thread::id> {}(std::this_thread::get_id()),
    std::hash<std::thread::id> {}(owning_thread_id_));
  AssertOwningThread();
  RecordTrimAttempt(trigger, automatic);
  ++trim_telemetry_.attempts;
  const auto before = content_cache_.SnapshotStats();

  auto eviction_guard = content_cache_.OnEviction(
    [&](const uint64_t cache_key, std::shared_ptr<void> value,
      const TypeId type_id) {
      static_cast<void>(value);
      UnloadObject(cache_key, type_id, EvictionReason::kTrim);
    });

  const internal::DependencyReleaseEngine::ReleaseCallbacks callbacks {
    .resolve_asset_hash
    = [this](const data::AssetKey& key) -> std::optional<uint64_t> {
      if (const auto identity = ResolveAssetIdentityForKey(key);
        identity.has_value()) {
        return identity->hash_key;
      }
      return std::nullopt;
    },
    .hash_asset_fallback = [this](const data::AssetKey& key) -> uint64_t {
      return HashAssetKey(key);
    },
    .hash_resource = [this](const ResourceKey key) -> uint64_t {
      return HashResourceKey(key);
    },
    .assert_refcount_symmetry =
      [this](std::string_view context) {
        AssertDependencyEdgeRefcountSymmetry(context);
      },
  };

  const auto trim_result = dependency_release_engine_->TrimCache(
    asset_identity_index_->AssetKeyByHash(), resource_key_registry_->Entries(),
    *dependency_graph_, content_cache_, callbacks);

  const auto after = content_cache_.SnapshotStats();
  const auto reclaimed_items = before.size > after.size
    ? static_cast<uint64_t>(before.size - after.size)
    : 0ULL;
  const auto reclaimed_bytes = before.consumed > after.consumed
    ? static_cast<uint64_t>(before.consumed - after.consumed)
    : 0ULL;
  trim_telemetry_.reclaimed_items += reclaimed_items;
  trim_telemetry_.reclaimed_bytes += reclaimed_bytes;
  trim_telemetry_.blocked_roots += static_cast<uint64_t>(
    trim_result.pruned_live_branches + trim_result.blocked_priority_roots);
  trim_telemetry_.pruned_live_branches
    += static_cast<uint64_t>(trim_result.pruned_live_branches);
  trim_telemetry_.blocked_priority_roots
    += static_cast<uint64_t>(trim_result.blocked_priority_roots);
  trim_telemetry_.orphan_resources
    += static_cast<uint64_t>(trim_result.orphan_resources);
  const auto blocked_total
    = static_cast<uint64_t>(trim_result.pruned_live_branches)
    + static_cast<uint64_t>(trim_result.blocked_priority_roots);

  LOG_F(INFO,
    "trim summary trigger={} automatic={} roots={} blocked_total={} "
    "blocked_priority_roots={} orphan_resources={} "
    "reclaimed_items={} reclaimed_bytes={}",
    trigger, automatic, trim_result.trim_roots, blocked_total,
    trim_result.blocked_priority_roots, trim_result.orphan_resources,
    reclaimed_items, reclaimed_bytes);
  if (trim_result.trim_roots == 0U && trim_result.orphan_resources > 0U) {
    LOG_F(INFO,
      "trim removed orphan resources without trim roots; "
      "this usually means resource edges were not published by current owners");
  }

  FlushResourceEvictionsForUncachedMappings(EvictionReason::kTrim, false);
}

auto AssetLoader::MaybeAutoTrimOnBudgetPressure(
  const std::string_view trigger, const bool force) -> void
{
  AssertOwningThread();
  RecordStorePressureEvent(trigger, force);
  if (residency_policy_.trim_mode != ResidencyTrimMode::kAutoOnOverBudget) {
    return;
  }
  if (!force && !content_cache_.IsOverBudget()) {
    return;
  }
  ExecuteTrimPass(trigger, true);
}

auto AssetLoader::TrimCache() -> void { ExecuteTrimPass("manual_trim", false); }

auto AssetLoader::EnumerateMountedScenes() const
  -> std::vector<IAssetLoader::MountedSceneEntry>
{
  AssertOwningThread();
  return scene_catalog_query_service_->EnumerateMountedScenes(
    impl_->source_registry);
}

auto AssetLoader::EnumerateMountedSources() const
  -> std::vector<IAssetLoader::MountedSourceEntry>
{
  AssertOwningThread();
  std::vector<IAssetLoader::MountedSourceEntry> mounted_sources;
  const auto& sources = impl_->source_registry.Sources();
  const auto& source_ids = impl_->source_registry.SourceIds();
  mounted_sources.reserve(sources.size());

  for (size_t i = 0; i < sources.size(); ++i) {
    const auto& source = sources[i];
    if (!source) {
      continue;
    }
    IAssetLoader::MountedSourceEntry entry {};
    entry.source_key = source->GetSourceKey();
    entry.source_id = source_ids[i];
    entry.source_kind
      = source->GetTypeId() == internal::LooseCookedSource::ClassTypeId()
      ? IAssetLoader::ContentSourceKind::kLooseCooked
      : IAssetLoader::ContentSourceKind::kPak;
    entry.source_path = source->SourcePath();
    mounted_sources.push_back(std::move(entry));
  }

  return mounted_sources;
}

auto AssetLoader::RegisterConsoleBindings(
  const observer_ptr<console::Console> console) noexcept -> void
{
  if (console == nullptr) {
    return;
  }
  console_ = console;

  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarVerifyContentHashes),
    .help = "Enable content hash verification for AssetLoader mounts",
    .default_value = verify_content_hashes_,
    .flags = console::CVarFlags::kArchive,
  });
  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarTelemetryEnabled),
    .help = "Enable AssetLoader telemetry accumulation",
    .default_value = telemetry_stats_.telemetry_enabled,
    .flags = console::CVarFlags::kDevOnly,
  });
  (void)console->RegisterCVar(console::CVarDefinition {
    .name = std::string(kCVarLastStats),
    .help = "AssetLoader telemetry summary snapshot",
    .default_value = std::string {},
    .flags = console::CVarFlags::kDevOnly,
    .min_value = std::nullopt,
    .max_value = std::nullopt,
  });

  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandTrimCache),
    .help = "Trim AssetLoader in-memory cache",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>&,
                 const console::CommandContext&) -> console::ExecutionResult {
      TrimCache();
      return console::ExecutionResult {
        .status = console::ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "AssetLoader cache trimmed",
        .error = {},
      };
    },
  });
  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandDumpStats),
    .help = "Dump AssetLoader telemetry [scope]",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (args.size() > 1) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: cntt.dump_stats [scope]",
        };
      }

      const auto scope = args.empty() ? std::string("all") : args[0];
      const auto stats = GetTelemetryStats();
      LOG_SCOPE_F(INFO, "AssetLoader Telemetry (%s)", scope.c_str());
      auto log_kv = [](const std::string_view key, const auto& value) -> void {
        LOG_F(INFO, "{:<30}: {}", key, value);
      };
      auto log_typed
        = [&](const char* label, const TypedLoadTelemetry& counters) -> void {
        LOG_SCOPE_F(INFO, "%s", label);
        for (const auto idx : ::enum_as_index<TypedLoadMetric>) {
          const auto metric = idx.to_enum();
          log_kv(nostd::to_string(metric), counters[metric]);
        }
      };

      if (scope == "all" || scope == "asset") {
        LOG_SCOPE_F(INFO, "Asset Telemetry");
        log_typed("material_assets", stats.material_assets);
        log_typed("geometry_assets", stats.geometry_assets);
        log_typed("scene_assets", stats.scene_assets);
        log_typed("physics_scene_assets", stats.physics_scene_assets);
        log_typed("script_assets", stats.script_assets);
        log_typed("input_action_assets", stats.input_action_assets);
        log_typed("input_mapping_context", stats.input_mapping_context_assets);
      }
      if (scope == "all" || scope == "resource") {
        LOG_SCOPE_F(INFO, "Resource Telemetry");
        log_typed("texture_resources", stats.texture_resources);
        log_typed("buffer_resources", stats.buffer_resources);
        log_typed("script_resources", stats.script_resources);
        log_typed("physics_resources", stats.physics_resources);
      }
      if (scope == "all" || scope == "pressure") {
        LOG_SCOPE_F(INFO, "Pressure Telemetry");
        log_kv("events_total", stats.pressure.events_total);
        log_kv("events_forced", stats.pressure.events_forced);
        log_kv("events_soft", stats.pressure.events_soft);
        log_kv("resource_store_failed", stats.pressure.resource_store_failed);
        log_kv("resource_store_over_budget",
          stats.pressure.resource_store_over_budget);
        log_kv("asset_store_failed", stats.pressure.asset_store_failed);
        log_kv("asset_store_succeeded", stats.pressure.asset_store_succeeded);
      }
      if (scope == "all" || scope == "trim") {
        LOG_SCOPE_F(INFO, "Trim Telemetry");
        log_kv("manual_attempts", stats.trim.manual_attempts);
        log_kv("auto_attempts", stats.trim.auto_attempts);
        log_kv("reclaimed_items", stats.trim.reclaimed_items);
        log_kv("reclaimed_bytes", stats.trim.reclaimed_bytes);
        log_kv("blocked_total", stats.trim.blocked_total);
        log_kv("pruned_live_branches", stats.trim.pruned_live_branches);
        log_kv("blocked_priority_roots", stats.trim.blocked_priority_roots);
        log_kv("orphan_resources", stats.trim.orphan_resources);
      }
      if (scope == "all" || scope == "eviction") {
        LOG_SCOPE_F(INFO, "Eviction Telemetry");
        log_kv("on_refcount_zero", stats.eviction.on_refcount_zero);
        log_kv("on_trim", stats.eviction.on_trim);
        log_kv("on_clear", stats.eviction.on_clear);
        log_kv("on_shutdown", stats.eviction.on_shutdown);
      }
      if (scope == "all" || scope == "cache") {
        LOG_SCOPE_F(INFO, "Cache Snapshot");
        log_kv("entries", stats.cache.entries);
        log_kv("consumed_budget", stats.cache.consumed_budget);
        log_kv("checked_out", stats.cache.checked_out_items);
        log_kv("over_budget", stats.cache.over_budget ? "true" : "false");
      }
      if (scope == "all" || scope == "inflight") {
        LOG_SCOPE_F(INFO, "InFlight Telemetry");
        log_kv("find_calls", stats.in_flight.find_calls);
        log_kv("find_hits", stats.in_flight.find_hits);
        log_kv("insert_calls", stats.in_flight.insert_calls);
        log_kv("erase_calls", stats.in_flight.erase_calls);
        log_kv("clear_calls", stats.in_flight.clear_calls);
        log_kv("active_type_buckets", stats.in_flight.active_type_buckets);
        log_kv("active_operations", stats.in_flight.active_operations);
      }

      const auto output = fmt::format(
        "entries={} consumed_budget={} over_budget={} asset_req={} "
        "resource_req={}",
        stats.cache.entries, stats.cache.consumed_budget,
        stats.cache.over_budget ? 1 : 0,
        stats.material_assets[TypedLoadMetric::kRequests]
          + stats.geometry_assets[TypedLoadMetric::kRequests]
          + stats.scene_assets[TypedLoadMetric::kRequests]
          + stats.physics_scene_assets[TypedLoadMetric::kRequests]
          + stats.script_assets[TypedLoadMetric::kRequests]
          + stats.input_action_assets[TypedLoadMetric::kRequests]
          + stats.input_mapping_context_assets[TypedLoadMetric::kRequests],
        stats.texture_resources[TypedLoadMetric::kRequests]
          + stats.buffer_resources[TypedLoadMetric::kRequests]
          + stats.script_resources[TypedLoadMetric::kRequests]
          + stats.physics_resources[TypedLoadMetric::kRequests]);
      return console::ExecutionResult {
        .status = console::ExecutionStatus::kOk,
        .exit_code = 0,
        .output = output,
        .error = {},
      };
    },
  });
  (void)console->RegisterCommand(console::CommandDefinition {
    .name = std::string(kCommandResetStats),
    .help = "Reset AssetLoader telemetry counters",
    .flags = console::CommandFlags::kDevOnly,
    .handler = [this](const std::vector<std::string>& args,
                 const console::CommandContext&) -> console::ExecutionResult {
      if (!args.empty()) {
        return console::ExecutionResult {
          .status = console::ExecutionStatus::kInvalidArguments,
          .exit_code = 2,
          .output = {},
          .error = "usage: cntt.reset_stats",
        };
      }
      ResetTelemetryStats();
      UpdateTelemetrySummaryCVar();
      return console::ExecutionResult {
        .status = console::ExecutionStatus::kOk,
        .exit_code = 0,
        .output = "AssetLoader telemetry reset",
        .error = {},
      };
    },
  });
}

auto AssetLoader::ApplyConsoleCVars(const console::Console& console) -> void
{
  bool verify_hashes = verify_content_hashes_;
  if (console.TryGetCVarValue<bool>(kCVarVerifyContentHashes, verify_hashes)
    && verify_hashes != verify_content_hashes_) {
    SetVerifyContentHashes(verify_hashes);
  }
  bool telemetry_enabled = telemetry_stats_.telemetry_enabled;
  if (console.TryGetCVarValue<bool>(kCVarTelemetryEnabled, telemetry_enabled)
    && telemetry_enabled != telemetry_stats_.telemetry_enabled) {
    SetTelemetryEnabled(telemetry_enabled);
  }
  UpdateTelemetrySummaryCVar();
}

auto AssetLoader::BindResourceRefToKey(const internal::ResourceRef& ref)
  -> ResourceKey
{
  AssertOwningThread();

  const auto source_id_opt
    = impl_->source_registry.FindSourceIdByToken(ref.source);
  if (!source_id_opt.has_value()) {
    throw std::runtime_error("Unknown SourceToken for ResourceRef binding");
  }

  const uint16_t source_id = *source_id_opt;
  const uint16_t resource_type_index
    = GetResourceTypeIndexByTypeId(ref.resource_type_id);

  return PackResourceKey(source_id, resource_type_index, ref.resource_index);
}

auto AssetLoader::GetHydratedScriptSlots(const data::SceneAsset& scene_asset,
  const data::pak::scripting::ScriptingComponentRecord& component) const
  -> std::vector<IAssetLoader::HydratedScriptSlot>
{
  AssertOwningThread();

  std::vector<IAssetLoader::HydratedScriptSlot> hydrated_slots;
  const auto scene_key = scene_asset.GetAssetKey();

  std::optional<uint64_t> scene_hash_key;
  const auto& asset_hash_by_key_and_source
    = asset_identity_index_->AssetHashByKeyAndSource();
  if (const auto by_key_it = asset_hash_by_key_and_source.find(scene_key);
    by_key_it != asset_hash_by_key_and_source.end()) {
    for (const auto& [source_id, candidate_hash] : by_key_it->second) {
      static_cast<void>(source_id);
      const auto cached_scene
        = content_cache_.Peek<data::SceneAsset>(candidate_hash);
      if (cached_scene && cached_scene.get() == &scene_asset) {
        scene_hash_key = candidate_hash;
        break;
      }
    }
  }

  if (!scene_hash_key.has_value()) {
    LOG_F(ERROR, "script slot hydration skipped: scene asset not cached");
    return hydrated_slots;
  }

  std::optional<uint16_t> source_id;
  if (const auto source_id_opt
    = asset_identity_index_->FindSourceId(*scene_hash_key);
    source_id_opt.has_value()) {
    source_id = *source_id_opt;
  }

  if (!source_id.has_value()) {
    for (const auto candidate_source_id : impl_->source_registry.SourceIds()) {
      if (HashAssetKey(scene_key, candidate_source_id) == *scene_hash_key) {
        source_id = candidate_source_id;
        break;
      }
    }
  }

  if (!source_id.has_value()) {
    LOG_F(ERROR, "script slot hydration skipped: source id not resolved");
    return hydrated_slots;
  }

  const auto source_it
    = impl_->source_registry.SourceIdToIndex().find(*source_id);
  if (source_it == impl_->source_registry.SourceIdToIndex().end()) {
    LOG_F(ERROR, "script slot hydration skipped: source index not found");
    return hydrated_slots;
  }

  const auto& source = *impl_->source_registry.Sources().at(source_it->second);
  std::vector<data::pak::scripting::ScriptSlotRecord> slot_records;
  auto read_params
    = [&](const data::pak::scripting::ScriptSlotRecord& slot_record)
    -> std::vector<data::pak::scripting::ScriptParamRecord> {
    if (slot_record.params_count == 0) {
      return {};
    }
    return source.ReadScriptParamRecords(
      slot_record.params_array_offset, slot_record.params_count);
  };

  try {
    slot_records = source.ReadScriptSlotRecords(
      component.slot_start_index, component.slot_count);
  } catch (const std::exception& ex) {
    LOG_F(ERROR, "failed to read script slots: {}", ex.what());
    return hydrated_slots;
  }

  hydrated_slots.reserve(slot_records.size());
  for (const auto& slot_record : slot_records) {
    IAssetLoader::HydratedScriptSlot hydrated {
      .script_asset_key = slot_record.script_asset_key,
      .flags = slot_record.flags,
    };
    try {
      hydrated.params = read_params(slot_record);
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "failed to read script parameters: {}", ex.what());
    }
    hydrated_slots.push_back(std::move(hydrated));
  }

  return hydrated_slots;
}

auto AssetLoader::LoadTextureAsync(ResourceKey key)
  -> co::Co<std::shared_ptr<data::TextureResource>>
{
  if (!nursery_) {
    throw std::runtime_error(
      "AssetLoader must be activated before async loads (LoadTextureAsync)");
  }
  if (!thread_pool_) {
    throw std::runtime_error(
      "AssetLoader requires a thread pool for async loads (LoadTextureAsync)");
  }

  co_return co_await LoadResourceAsync<data::TextureResource>(key);
}

auto AssetLoader::LoadTextureAsync(
  CookedResourceData<data::TextureResource> cooked)
  -> co::Co<std::shared_ptr<data::TextureResource>>
{
  auto decoded = co_await LoadResourceAsyncFromCookedErased(
    data::TextureResource::ClassTypeId(), cooked.key, cooked.bytes);
  co_return std::static_pointer_cast<data::TextureResource>(std::move(decoded));
}

auto AssetLoader::LoadResourceAsyncFromCookedErased(const TypeId type_id,
  const ResourceKey key, std::span<const uint8_t> bytes, LoadRequest request)
  -> co::Co<std::shared_ptr<void>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadResourceAsync (cooked)");
  DLOG_F(2, "type_id : {}", type_id);
  DLOG_F(2, "key     : {}", key);
  DLOG_F(2, "bytes   : {}", bytes.size());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();
  request = NormalizeLoadRequest(request);

  if (!nursery_) {
    throw std::runtime_error("AssetLoader must be activated before async loads "
                             "(LoadResourceAsyncFromCookedErased)");
  }
  if (!thread_pool_) {
    throw std::runtime_error(
      "AssetLoader requires a thread pool for async loads "
      "(LoadResourceAsyncFromCookedErased)");
  }

  try {
    co_return co_await resource_load_pipeline_->LoadErasedFromCooked(
      type_id, key, bytes, request);
  } catch (const co::TaskCancelledException& e) {
    throw OperationCancelledException(e.what());
  }
}

auto AssetLoader::AddTypeErasedAssetLoader(const TypeId type_id,
  const std::string_view type_name, LoadFnErased&& loader) -> void
{
  auto [it, inserted] = asset_loaders_.insert_or_assign(
    type_id, std::forward<LoadFnErased>(loader));
  if (!inserted) {
    LOG_F(WARNING, "Replacing loader for type: {}/{}", type_id, type_name);
  } else {
    LOG_F(INFO, "Registered loader for type: {}/{}", type_id, type_name);
  }
}

auto AssetLoader::AddTypeErasedResourceLoader(const TypeId type_id,
  const std::string_view type_name, LoadFnErased&& loader) -> void
{
  auto [it, inserted] = resource_loaders_.insert_or_assign(
    type_id, std::forward<LoadFnErased>(loader));
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
  AssertOwningThread();
  LOG_SCOPE_F(2, "Add Asset Dependency");
  LOG_F(2, "dependent: {} -> dependency: {}",
    nostd::to_string(dependent).c_str(), nostd::to_string(dependency).c_str());

  // Policy: structural cycle detection in runtime dependency insertion is a
  // debug diagnostic only. Release behavior assumes content pipeline/CI
  // acyclicity validation and does not enforce this guard at runtime.
  //
  // Adding edge dependent -> dependency must not create a path
  // dependency -> ... -> dependent (checked via DetectCycle in debug).
  if (DetectCycle(dependency, dependent)) {
    LOG_F(ERROR, "Rejecting asset dependency that introduces a cycle: {} -> {}",
      nostd::to_string(dependent).c_str(),
      nostd::to_string(dependency).c_str());
#if !defined(NDEBUG)
    DCHECK_F(false, "Cycle detected in asset dependency graph");
#endif
    return; // Do not insert
  }

  // Add forward dependency only (reference counting handled by cache Touch).
  // Only touch on first insertion to keep retain/release balanced.
  const bool inserted
    = dependency_graph_->AddAssetDependency(dependent, dependency);
  if (!inserted) {
    return;
  }

  // Touch the dependency asset in the cache to increment its reference count.
  const auto dependency_identity = ResolveAssetIdentityForKey(dependency);
  const auto dependency_hash = dependency_identity.has_value()
    ? std::optional<uint64_t> { dependency_identity->hash_key }
    : std::nullopt;
  content_cache_.Touch(
    dependency_hash.has_value() ? *dependency_hash : HashAssetKey(dependency),
    oxygen::CheckoutOwner::kInternal);
  AssertDependencyEdgeRefcountSymmetry("AddAssetDependency");
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

  // Add forward dependency only (reference counting handled by cache Touch).
  // Only touch on first insertion to keep retain/release balanced.
  const bool inserted
    = dependency_graph_->AddResourceDependency(dependent, resource_key);
  if (!inserted) {
    return;
  }

  // Touch the dependency resource in the cache to increment its reference
  // count.
  content_cache_.Touch(
    HashResourceKey(resource_key), oxygen::CheckoutOwner::kInternal);
  AssertDependencyEdgeRefcountSymmetry("AddResourceDependency");
}

//=== Asset Loading Implementations ==========================================//

auto AssetLoader::ReleaseAsset(const data::AssetKey& key) -> bool
{
  AssertOwningThread();
  const auto identity = ResolveAssetIdentityForKey(key);
  const auto key_hash
    = identity.has_value() ? identity->hash_key : HashAssetKey(key);

  if (const auto pin_it = pinned_asset_counts_.find(key_hash);
    pin_it != pinned_asset_counts_.end() && pin_it->second > 0U) {
    content_cache_.CheckIn(key_hash);
    const bool still_present = content_cache_.Contains(key_hash);
    LOG_F(INFO,
      "release asset with active pins: key={} pins={} still_present={}", key,
      pin_it->second, still_present ? "true" : "false");
    return !still_present;
  }

  // Enable eviction notifications for the whole release cascade, since
  // dependency check-ins may evict multiple entries (resources and/or assets).
  auto eviction_guard = content_cache_.OnEviction(
    [&]([[maybe_unused]] uint64_t cache_key,
      [[maybe_unused]] std::shared_ptr<void> value, TypeId type_id) {
      static_cast<void>(value);
      LOG_F(2, "Evict entry: key_hash={} type_id={} reason={}", cache_key,
        type_id, EvictionReason::kRefCountZero);
      UnloadObject(cache_key, type_id, EvictionReason::kRefCountZero);
    });

  // Recursively release (check in) the asset and all its dependencies.
  ReleaseAssetTree(key);
  // Return true if the asset is no longer present in the cache
  const bool still_present = content_cache_.Contains(key_hash);
  LOG_F(
    2, "ReleaseAsset key={} evicted={}", key, still_present ? "false" : "true");
  return !still_present;
}

auto AssetLoader::PinAsset(const data::AssetKey& key) -> bool
{
  AssertOwningThread();
  const auto identity = ResolveAssetIdentityForKey(key);
  if (!identity.has_value()) {
    LOG_F(WARNING, "pin asset failed: key={} not loaded", key);
    return false;
  }
  const auto key_hash = identity->hash_key;
  if (!content_cache_.Pin(key_hash, oxygen::CheckoutOwner::kExternal)) {
    LOG_F(WARNING, "pin asset failed: key={} missing in cache", key);
    return false;
  }
  ++pinned_asset_counts_[key_hash];
  return true;
}

auto AssetLoader::UnpinAsset(const data::AssetKey& key) -> bool
{
  AssertOwningThread();
  const auto identity = ResolveAssetIdentityForKey(key);
  if (!identity.has_value()) {
    LOG_F(WARNING, "unpin asset failed: key={} not loaded", key);
    return false;
  }
  const auto key_hash = identity->hash_key;
  auto pin_it = pinned_asset_counts_.find(key_hash);
  if (pin_it == pinned_asset_counts_.end() || pin_it->second == 0U) {
    LOG_F(ERROR, "unpin asset failed: key={} has no matching pin", key);
    return false;
  }
  if (!content_cache_.Unpin(key_hash)) {
    LOG_F(ERROR, "unpin asset failed: key={} cache refcount underflow", key);
    return false;
  }
  --pin_it->second;
  if (pin_it->second == 0U) {
    pinned_asset_counts_.erase(pin_it);
    if (content_cache_.Contains(key_hash)
      && content_cache_.GetCheckoutCount(key_hash) <= 1U) {
      // Final explicit pin was released and no transient checkouts remain:
      // collapse dependency edges now so release traversal semantics stay
      // consistent with explicit residency controls.
      ReleaseAssetTree(key);
    }
  }
  return true;
}

auto AssetLoader::SubscribeResourceEvictions(
  const TypeId resource_type, EvictionHandler handler) -> EvictionSubscription
{
  AssertOwningThread();
  const auto id = next_eviction_subscriber_id_++;
  eviction_registry_->AddSubscriber(resource_type, id, std::move(handler));

  return MakeEvictionSubscription(resource_type, id,
    observer_ptr<IAssetLoader> { this }, eviction_alive_token_);
}

void AssetLoader::UnsubscribeResourceEvictions(
  const TypeId resource_type, const uint64_t id) noexcept
{
  if (resource_type == data::ScriptAsset::ClassTypeId()) {
    script_hot_reload_service_->Unsubscribe(id);
    return;
  }
  eviction_registry_->RemoveSubscriber(resource_type, id);
}

auto AssetLoader::InvalidateAssetTree(const data::AssetKey& key) -> void
{
  AssertOwningThread();

  const auto identity = ResolveAssetIdentityForKey(key);
  if (!identity.has_value()) {
    return;
  }
  const auto hash = identity->hash_key;
  const auto source_id = identity->source_id;
  auto asset = content_cache_.Peek<data::ScriptAsset>(hash);

  if (asset) {
    // Invalidate script-specific resources if applicable
    const auto resource_type_index = static_cast<uint16_t>(
      IndexOf<data::ScriptResource, ResourceTypeList>::value);

    auto invalidate_resource = [&](const pak::core::ResourceIndexT index) {
      if (index != data::pak::core::kNoResourceIndex) {
        const auto rkey
          = PackResourceKey(source_id, resource_type_index, index);
        content_cache_.Remove(HashResourceKey(rkey));
      }
    };

    invalidate_resource(asset->GetBytecodeResourceIndex());
    invalidate_resource(asset->GetSourceResourceIndex());
  }

  // Finally remove the asset itself
  content_cache_.Remove(hash);
}

auto AssetLoader::ReloadScript(const std::filesystem::path& path) -> void
{
  AssertOwningThread();
  if ((nursery_ == nullptr) || !thread_pool_) {
    return;
  }

  const internal::ScriptHotReloadService::ReloadCallbacks callbacks {
    .enumerate_loaded_script_keys =
      [this]() {
        std::vector<data::AssetKey> script_keys;
        for (const auto& [hash, key] :
          asset_identity_index_->AssetKeyByHash()) {
          static_cast<void>(hash);
          if (HasScriptAsset(key)) {
            script_keys.push_back(key);
          }
        }
        return script_keys;
      },
    .get_script_asset
    = [this](
        const data::AssetKey& key) { return GetAsset<data::ScriptAsset>(key); },
    .invalidate_asset_tree
    = [this](const data::AssetKey& key) { InvalidateAssetTree(key); },
    .start_load_script_asset =
      [this](const data::AssetKey& key,
        std::function<void(std::shared_ptr<data::ScriptAsset>)> done) {
        StartLoadAsset<data::ScriptAsset>(key, std::move(done));
      },
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .make_script_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::ScriptResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
    .get_script_resource =
      [this](const ResourceKey key) {
        return GetResource<data::ScriptResource>(key);
      },
  };
  script_hot_reload_service_->ReloadScript(path, callbacks);
}

auto AssetLoader::ReloadAllScripts() -> void
{
  AssertOwningThread();
  const internal::ScriptHotReloadService::ReloadCallbacks callbacks {
    .enumerate_loaded_script_keys =
      [this]() {
        std::vector<data::AssetKey> script_keys;
        for (const auto& [hash, key] :
          asset_identity_index_->AssetKeyByHash()) {
          static_cast<void>(hash);
          if (HasScriptAsset(key)) {
            script_keys.push_back(key);
          }
        }
        return script_keys;
      },
    .get_script_asset
    = [this](
        const data::AssetKey& key) { return GetAsset<data::ScriptAsset>(key); },
    .invalidate_asset_tree
    = [this](const data::AssetKey& key) { InvalidateAssetTree(key); },
    .start_load_script_asset =
      [this](const data::AssetKey& key,
        std::function<void(std::shared_ptr<data::ScriptAsset>)> done) {
        StartLoadAsset<data::ScriptAsset>(key, std::move(done));
      },
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .make_script_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::ScriptResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
    .get_script_resource =
      [this](const ResourceKey key) {
        return GetResource<data::ScriptResource>(key);
      },
  };
  script_hot_reload_service_->ReloadAllScripts(callbacks);
}

auto AssetLoader::SubscribeScriptReload(ScriptReloadCallback callback)
  -> EvictionSubscription
{
  AssertOwningThread();
  const auto id = next_eviction_subscriber_id_++;
  script_hot_reload_service_->Subscribe(id, std::move(callback));

  return MakeEvictionSubscription(data::ScriptAsset::ClassTypeId(), id,
    observer_ptr<IAssetLoader> { this }, eviction_alive_token_);
}

auto AssetLoader::ReleaseAssetTree(const data::AssetKey& key) -> void
{
  AssertOwningThread();

  const internal::DependencyReleaseEngine::ReleaseCallbacks callbacks {
    .resolve_asset_hash
    = [this](const data::AssetKey& dep_key) -> std::optional<uint64_t> {
      if (const auto dep_identity = ResolveAssetIdentityForKey(dep_key);
        dep_identity.has_value()) {
        return dep_identity->hash_key;
      }
      return std::nullopt;
    },
    .hash_asset_fallback = [this](const data::AssetKey& dep_key) -> uint64_t {
      return HashAssetKey(dep_key);
    },
    .hash_resource = [this](const ResourceKey res_key) -> uint64_t {
      return HashResourceKey(res_key);
    },
    .assert_refcount_symmetry =
      [this](std::string_view context) {
        AssertDependencyEdgeRefcountSymmetry(context);
      },
  };
  dependency_release_engine_->ReleaseAssetTree(
    key, *dependency_graph_, content_cache_, callbacks);
}

/*!
Publish resource dependencies and update cache refcounts.

This enumerates the dependency collector, loads each referenced resource,
and registers it as a dependency of the provided asset. Registration
touches the cache to increment the dependency refcount.

@tparam ResourceT Resource type to publish (must satisfy PakResource).
@param dependent_asset_key Asset key that owns the dependencies.
@param collector Dependency collector populated during decode.

### Ref-count Contract

- This call increments the dependency refcount via `AddResourceDependency`.
- The caller remains responsible for its own resource references.
- Any explicit checkouts acquired by the caller must be released separately.

### Performance Characteristics

- Time Complexity: $O(n)$ over referenced dependencies.
- Memory: $O(n)$ for seen-key tracking.
- Optimization: Deduplicates dependencies by hashed key.
*/
template <typename ResourceT>
auto AssetLoader::PublishResourceDependenciesAsync(
  const data::AssetKey& dependent_asset_key,
  const internal::DependencyCollector& collector, LoadRequest request)
  -> co::Co<>
{
  AssertOwningThread();

  const auto expected_type_id = ResourceT::ClassTypeId();

  std::unordered_set<uint64_t> seen_key_hashes;
  seen_key_hashes.reserve(collector.ResourceRefDependencies().size()
    + collector.ResourceKeyDependencies().size());

  for (const auto& ref : collector.ResourceRefDependencies()) {
    if (ref.resource_type_id != expected_type_id) {
      continue;
    }
    const auto dep_key = BindResourceRefToKey(ref);
    const auto dep_key_hash = HashResourceKey(dep_key);
    if (!seen_key_hashes.insert(dep_key_hash).second) {
      continue;
    }

    auto res = co_await LoadResourceAsync<ResourceT>(dep_key, request);
    if (!res) {
      continue;
    }

    AddResourceDependency(dependent_asset_key, dep_key);
    (void)ReleaseResource(dep_key);
  }

  for (const auto& dep_key : collector.ResourceKeyDependencies()) {
    {
      const internal::InternalResourceKey internal_key(dep_key);
      const auto expected_type_index
        = static_cast<uint16_t>(IndexOf<ResourceT, ResourceTypeList>::value);
      if (internal_key.GetResourceTypeIndex() != expected_type_index) {
        continue;
      }
    }

    const auto dep_key_hash = HashResourceKey(dep_key);
    if (!seen_key_hashes.insert(dep_key_hash).second) {
      continue;
    }

    auto res = co_await LoadResourceAsync<ResourceT>(dep_key, request);
    if (!res) {
      continue;
    }

    AddResourceDependency(dependent_asset_key, dep_key);
    (void)ReleaseResource(dep_key);
  }

  co_return;
}

auto AssetLoader::DecodeAssetAsyncErasedImpl(const TypeId type_id,
  const data::AssetKey& key, std::optional<uint16_t> preferred_source_id)
  -> co::Co<DecodedAssetAsyncResult>
{
  AssertOwningThread();

  if (!nursery_) {
    throw std::runtime_error(
      "AssetLoader must be activated before async loads (LoadAssetAsync)");
  }
  if (!thread_pool_) {
    throw std::runtime_error(
      "AssetLoader requires a thread pool for async loads (LoadAssetAsync)");
  }

  // Resolve on owning thread: choose source and create independent readers.
  uint16_t source_id = 0;
  internal::SourceToken source_token {};
  std::unique_ptr<serio::AnyReader> desc_reader;
  std::unique_ptr<serio::AnyReader> buf_reader;
  std::unique_ptr<serio::AnyReader> tex_reader;
  std::unique_ptr<serio::AnyReader> script_reader;
  std::unique_ptr<serio::AnyReader> phys_reader;
  const PakFile* source_pak = nullptr;
  const internal::IContentSource* source_content = nullptr;

  auto try_prepare_from_source_index = [&](const size_t source_index) -> bool {
    const auto& source = *impl_->source_registry.Sources()[source_index];
    if (!source.HasAsset(key)) {
      return false;
    }

    source_id = impl_->source_registry.SourceIds().at(source_index);
    source_token = impl_->source_registry.SourceTokens().at(source_index);
    desc_reader = source.CreateAssetDescriptorReader(key);
    if (!desc_reader) {
      return false;
    }

    buf_reader = source.CreateBufferDataReader();
    tex_reader = source.CreateTextureDataReader();
    script_reader = source.CreateScriptDataReader();
    phys_reader = source.CreatePhysicsDataReader();

    if (source.GetTypeId() == internal::PakFileSource::ClassTypeId()) {
      const auto* pak_source
        = static_cast<const internal::PakFileSource*>(&source);
      source_pak = &pak_source->Pak();
    }
    source_content = &source;
    return true;
  };

  bool found = false;
  if (preferred_source_id.has_value()) {
    const auto source_it
      = impl_->source_registry.SourceIdToIndex().find(*preferred_source_id);
    if (source_it != impl_->source_registry.SourceIdToIndex().end()) {
      found = try_prepare_from_source_index(source_it->second);
    }
  }

  if (!found) {
    for (size_t source_index = impl_->source_registry.Sources().size();
      source_index-- > 0;) {
      if (try_prepare_from_source_index(source_index)) {
        found = true;
        break;
      }
    }
  }

  if (!found) {
    LOG_F(WARNING, "Asset not found (async): key={} type_id={}",
      nostd::to_string(key).c_str(), type_id);
    co_return {
      .source_id = 0, .asset = nullptr, .dependency_collector = nullptr
    };
  }

  auto collector = std::make_shared<internal::DependencyCollector>();

  LOG_F(2, "scheduling asset decode on thread pool: type_id={}", type_id);
  auto decoded = co_await thread_pool_->Run(
    [this, key, type_id, source_id, source_pak, source_content, collector,
      desc_reader = std::move(desc_reader), buf_reader = std::move(buf_reader),
      tex_reader = std::move(tex_reader),
      script_reader = std::move(script_reader),
      phys_reader = std::move(phys_reader),
      source_token]() mutable -> std::shared_ptr<void> {
      LoaderContext context {
        .current_asset_key = key,
        .source_token = source_token,
        .desc_reader = desc_reader.get(),
        .data_readers = std::make_tuple(buf_reader.get(), tex_reader.get(),
          script_reader.get(), phys_reader.get()),
        .work_offline = work_offline_,
        .dependency_collector = collector,
        .source_pak = source_pak,
        .source_content = source_content,
        .parse_only = false,
      };

      auto loader_it = asset_loaders_.find(type_id);
      if (loader_it == asset_loaders_.end()) {
        LOG_F(ERROR, "No loader registered for asset type id: {}", type_id);
        return nullptr;
      }

      return loader_it->second(context);
    });

  AssertOwningThread();
  co_return { .source_id = source_id,
    .asset = std::move(decoded),
    .dependency_collector = std::move(collector) };
}

auto AssetLoader::PrepareAssetLoadRequest(
  const data::AssetKey& key, std::optional<uint16_t> preferred_source_id) const
  -> std::optional<AssetLoadRequest>
{
  const auto source_id_opt = ResolveLoadSourceId(key, preferred_source_id);
  if (!source_id_opt.has_value()) {
    return std::nullopt;
  }

  return AssetLoadRequest {
    .source_id = *source_id_opt,
    .hash_key = HashAssetKey(key, *source_id_opt),
  };
}

auto AssetLoader::LoadMaterialAssetAsyncImpl(const data::AssetKey& key,
  std::optional<uint16_t> preferred_source_id, LoadRequest request)
  -> co::Co<std::shared_ptr<data::MaterialAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadMaterialAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  request = NormalizeLoadRequest(request);
  const auto request_sequence = next_load_request_sequence_.fetch_add(1);
  const auto load_target = PrepareAssetLoadRequest(key, preferred_source_id);
  if (!load_target.has_value()) {
    co_return nullptr;
  }
  const auto source_id = load_target->source_id;
  const auto hash_key = load_target->hash_key;
  const AssetLoadTelemetryCallbacks telemetry_callbacks {
    .on_request =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kMaterial, LoadTelemetryEvent::kRequest);
      },
    .on_cache_hit =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kMaterial, LoadTelemetryEvent::kCacheHit);
      },
    .on_cache_miss =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kMaterial, LoadTelemetryEvent::kCacheMiss);
      },
    .on_joined_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kMaterial, LoadTelemetryEvent::kTasksDeduped);
      },
    .on_started_new_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kMaterial, LoadTelemetryEvent::kTasksSpawned);
      },
  };
  const auto publish_material_texture_dependencies
    = [this, key, source_id, request](
        const std::shared_ptr<data::MaterialAsset>& material) -> co::Co<> {
    if (!material) {
      co_return;
    }

    using data::pak::core::kNoResourceIndex;
    using data::pak::core::ResourceIndexT;

    const auto texture_type_index = static_cast<uint16_t>(
      IndexOf<data::TextureResource, ResourceTypeList>::value);

    std::unordered_set<uint64_t> seen_texture_hashes;
    const auto try_publish_texture_index
      = [&, this](const ResourceIndexT texture_index) -> co::Co<> {
      if (texture_index == kNoResourceIndex) {
        co_return;
      }

      const auto texture_key
        = PackResourceKey(source_id, texture_type_index, texture_index);
      const auto dep_hash = HashResourceKey(texture_key);
      if (!seen_texture_hashes.insert(dep_hash).second) {
        co_return;
      }

      auto dep_res = co_await LoadResourceAsync<data::TextureResource>(
        texture_key, request);
      if (!dep_res) {
        co_return;
      }

      AddResourceDependency(key, texture_key);
      (void)ReleaseResource(texture_key);
    };

    co_await try_publish_texture_index(material->GetBaseColorTexture());
    co_await try_publish_texture_index(material->GetNormalTexture());
    co_await try_publish_texture_index(material->GetMetallicTexture());
    co_await try_publish_texture_index(material->GetRoughnessTexture());
    co_await try_publish_texture_index(material->GetAmbientOcclusionTexture());
    co_await try_publish_texture_index(material->GetEmissiveTexture());
    co_await try_publish_texture_index(material->GetSpecularTexture());
    co_await try_publish_texture_index(material->GetSheenColorTexture());
    co_await try_publish_texture_index(material->GetClearcoatTexture());
    co_await try_publish_texture_index(material->GetClearcoatNormalTexture());
    co_await try_publish_texture_index(material->GetTransmissionTexture());
    co_await try_publish_texture_index(material->GetThicknessTexture());
  };

  const auto cache_hit
    = [this, hash_key, publish_material_texture_dependencies]()
    -> co::Co<std::shared_ptr<data::MaterialAsset>> {
    if (auto cached = content_cache_.CheckOut<data::MaterialAsset>(
          hash_key, oxygen::CheckoutOwner::kInternal)) {
      // Cache-hit material loads must still rebuild dependency edges if they
      // were trimmed previously. Without this, live textures can become
      // standalone trim candidates and get evicted while still in use.
      co_await publish_material_texture_dependencies(cached);
      co_return cached;
    }
    co_return nullptr;
  };

  const auto decode_and_publish = [this, key, source_id, hash_key, request,
                                    publish_material_texture_dependencies]()
    -> co::Co<std::shared_ptr<data::MaterialAsset>> {
    try {
      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::MaterialAsset::ClassTypeId(), key, source_id);
      auto decoded
        = std::static_pointer_cast<data::MaterialAsset>(decoded_result.asset);
      if (!decoded
        || decoded->GetTypeId() != data::MaterialAsset::ClassTypeId()) {
        RecordAssetTelemetry(
          data::AssetType::kMaterial, LoadTelemetryEvent::kTypeMismatch);
        if (!decoded) {
          RecordAssetTelemetry(
            data::AssetType::kMaterial, LoadTelemetryEvent::kDecodeFailure);
        }
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::MaterialAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }
      if (!decoded_result.dependency_collector) {
        RecordAssetTelemetry(
          data::AssetType::kMaterial, LoadTelemetryEvent::kDecodeFailure);
        LOG_F(ERROR, "Missing dependency collector for decoded material asset");
        co_return nullptr;
      }

      // Publish (owning thread): store asset, then ensure resource dependencies
      // are loaded and held via dependency edges.
      {
        using data::pak::core::kNoResourceIndex;
        using data::pak::core::ResourceIndexT;

        const auto texture_type_index = static_cast<uint16_t>(
          IndexOf<data::TextureResource, ResourceTypeList>::value);

        auto make_texture_key = [&](const ResourceIndexT texture_index) {
          if (texture_index == kNoResourceIndex) {
            return ResourceKey { 0 };
          }
          return PackResourceKey(
            decoded_result.source_id, texture_type_index, texture_index);
        };

        std::vector<ResourceKey> texture_keys;
        texture_keys.reserve(12);
        texture_keys.push_back(
          make_texture_key(decoded->GetBaseColorTexture()));
        texture_keys.push_back(make_texture_key(decoded->GetNormalTexture()));
        texture_keys.push_back(make_texture_key(decoded->GetMetallicTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetRoughnessTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetAmbientOcclusionTexture()));
        texture_keys.push_back(make_texture_key(decoded->GetEmissiveTexture()));
        texture_keys.push_back(make_texture_key(decoded->GetSpecularTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetSheenColorTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetClearcoatTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetClearcoatNormalTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetTransmissionTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetThicknessTexture()));
        decoded->SetTextureResourceKeys(std::move(texture_keys));
      }

      auto stored = content_cache_.Store(hash_key, decoded);
      if (!stored) {
        MaybeAutoTrimOnBudgetPressure("material_store_failed", true);
        stored = content_cache_.Store(hash_key, decoded);
        if (!stored) {
          RecordAssetTelemetry(
            data::AssetType::kMaterial, LoadTelemetryEvent::kStoreRetryFailure);
        }
      }
      if (stored) {
        IndexAssetHashMapping(hash_key, key, source_id);
        // Keep one loader-owned cache retain; load caller gets its own retain.
        content_cache_.Touch(hash_key, oxygen::CheckoutOwner::kInternal);
        MaybeAutoTrimOnBudgetPressure("material_store_succeeded");
      }

      // Publish resolved texture slots for this material even on cache reuse.
      co_await publish_material_texture_dependencies(decoded);

      // Also publish collector-driven refs for loaders that provide additional
      // texture dependencies not represented in the resolved key list.
      co_await PublishResourceDependenciesAsync<data::TextureResource>(
        key, *decoded_result.dependency_collector, request);

      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      RecordAssetTelemetry(
        data::AssetType::kMaterial, LoadTelemetryEvent::kCancellation);
      throw OperationCancelledException(e.what());
    }
  };

  co_return co_await RunAssetLoadPipeline<data::MaterialAsset>(
    data::MaterialAsset::ClassTypeId(), hash_key, *in_flight_ops_, request,
    request_sequence, cache_hit, decode_and_publish, telemetry_callbacks);
}

auto AssetLoader::LoadGeometryBufferDependenciesAsync(
  const internal::DependencyCollector& collector, LoadRequest request)
  -> co::Co<LoadedGeometryBuffersByIndex>
{
  AssertOwningThread();

  using data::BufferResource;

  LoadedGeometryBuffersByIndex loaded_buffers_by_index;
  std::unordered_set<uint64_t> seen_resource_hashes;
  seen_resource_hashes.reserve(collector.ResourceRefDependencies().size()
    + collector.ResourceKeyDependencies().size());

  for (const auto& ref : collector.ResourceRefDependencies()) {
    if (ref.resource_type_id != BufferResource::ClassTypeId()) {
      continue;
    }

    const auto dep_key = BindResourceRefToKey(ref);
    const auto dep_hash = HashResourceKey(dep_key);
    if (!seen_resource_hashes.insert(dep_hash).second) {
      continue;
    }

    auto res = co_await LoadResourceAsync<BufferResource>(dep_key, request);
    if (!res) {
      continue;
    }

    loaded_buffers_by_index.insert_or_assign(
      static_cast<uint32_t>(ref.resource_index),
      LoadedGeometryBuffer { .key = dep_key, .resource = std::move(res) });
  }

  for (const auto& dep_key : collector.ResourceKeyDependencies()) {
    const internal::InternalResourceKey internal_key(dep_key);
    const auto expected_type_index
      = static_cast<uint16_t>(IndexOf<BufferResource, ResourceTypeList>::value);
    if (internal_key.GetResourceTypeIndex() != expected_type_index) {
      continue;
    }

    const auto dep_hash = HashResourceKey(dep_key);
    if (!seen_resource_hashes.insert(dep_hash).second) {
      continue;
    }

    auto res = co_await LoadResourceAsync<BufferResource>(dep_key);
    if (!res) {
      continue;
    }

    loaded_buffers_by_index.insert_or_assign(
      static_cast<uint32_t>(internal_key.GetResourceIndex()),
      LoadedGeometryBuffer { .key = dep_key, .resource = std::move(res) });
  }

  co_return loaded_buffers_by_index;
}

auto AssetLoader::LoadGeometryMaterialDependenciesAsync(
  const internal::DependencyCollector& collector,
  std::optional<uint16_t> preferred_source_id, LoadRequest request)
  -> co::Co<LoadedGeometryMaterialsByKey>
{
  AssertOwningThread();

  using data::MaterialAsset;

  LoadedGeometryMaterialsByKey loaded_materials;
  std::unordered_set<uint64_t> seen_asset_hashes;
  seen_asset_hashes.reserve(collector.AssetDependencies().size());

  for (const auto& dep_asset_key : collector.AssetDependencies()) {
    auto asset = co_await LoadMaterialAssetAsyncImpl(
      dep_asset_key, preferred_source_id, request);
    if (!asset) {
      continue;
    }

    const auto dep_identity
      = ResolveAssetIdentityForKey(dep_asset_key, preferred_source_id);
    const auto dep_hash = dep_identity.has_value()
      ? std::optional<uint64_t> { dep_identity->hash_key }
      : std::nullopt;
    if (!dep_hash.has_value() || !seen_asset_hashes.insert(*dep_hash).second) {
      continue;
    }

    loaded_materials.insert_or_assign(dep_asset_key, std::move(asset));
  }

  co_return loaded_materials;
}

auto AssetLoader::BindGeometryRuntimePointers(data::GeometryAsset& asset,
  const LoadedGeometryBuffersByIndex& buffers_by_index,
  const LoadedGeometryMaterialsByKey& materials_by_key) -> void
{
  AssertOwningThread();

  using data::BufferResource;
  using data::MaterialAsset;
  using data::MeshType;

  for (const auto& mesh_ptr : asset.Meshes()) {
    if (!mesh_ptr) {
      continue;
    }

    auto& mesh = *mesh_ptr;
    const auto& mesh_desc_opt = mesh.Descriptor();
    if (mesh_desc_opt
      && mesh_desc_opt->mesh_type
        == static_cast<uint8_t>(MeshType::kStandard)) {
      const auto& info = mesh_desc_opt->info.standard;

      const auto vb_it = buffers_by_index.find(info.vertex_buffer);
      const auto ib_it = buffers_by_index.find(info.index_buffer);

      std::shared_ptr<BufferResource> vb;
      if (vb_it != buffers_by_index.end()) {
        vb = vb_it->second.resource;
      }

      std::shared_ptr<BufferResource> ib;
      if (ib_it != buffers_by_index.end()) {
        ib = ib_it->second.resource;
      }

      mesh.SetBufferResources(std::move(vb), std::move(ib));
    } else if (mesh_desc_opt && mesh_desc_opt->IsSkinned()) {
      const auto& info = mesh_desc_opt->info.skinned;

      const auto vb_it = buffers_by_index.find(info.vertex_buffer);
      const auto ib_it = buffers_by_index.find(info.index_buffer);
      const auto joint_index_it
        = buffers_by_index.find(info.joint_index_buffer);
      const auto joint_weight_it
        = buffers_by_index.find(info.joint_weight_buffer);
      const auto inverse_bind_it
        = buffers_by_index.find(info.inverse_bind_buffer);
      const auto joint_remap_it
        = buffers_by_index.find(info.joint_remap_buffer);

      std::shared_ptr<BufferResource> vb;
      if (vb_it != buffers_by_index.end()) {
        vb = vb_it->second.resource;
      }

      std::shared_ptr<BufferResource> ib;
      if (ib_it != buffers_by_index.end()) {
        ib = ib_it->second.resource;
      }

      std::shared_ptr<BufferResource> joint_index;
      if (joint_index_it != buffers_by_index.end()) {
        joint_index = joint_index_it->second.resource;
      }

      std::shared_ptr<BufferResource> joint_weight;
      if (joint_weight_it != buffers_by_index.end()) {
        joint_weight = joint_weight_it->second.resource;
      }

      std::shared_ptr<BufferResource> inverse_bind;
      if (inverse_bind_it != buffers_by_index.end()) {
        inverse_bind = inverse_bind_it->second.resource;
      }

      std::shared_ptr<BufferResource> joint_remap;
      if (joint_remap_it != buffers_by_index.end()) {
        joint_remap = joint_remap_it->second.resource;
      }

      mesh.SetBufferResources(std::move(vb), std::move(ib));
      if (mesh.IsSkinned()) {
        mesh.SetSkiningBufferResources(std::move(joint_index),
          std::move(joint_weight), std::move(inverse_bind),
          std::move(joint_remap));
      }
    }

    const auto submeshes = mesh.SubMeshes();
    for (size_t i = 0; i < submeshes.size(); ++i) {
      const auto& sm_desc_opt = submeshes[i].Descriptor();
      if (!sm_desc_opt) {
        continue;
      }

      const auto mat_key = sm_desc_opt->material_asset_key;
      if (mat_key == data::AssetKey {}) {
        continue;
      }

      auto mat_it = materials_by_key.find(mat_key);
      if (mat_it == materials_by_key.end() || !mat_it->second) {
        LOG_F(WARNING,
          "AssetLoader: Material asset not found for submesh {} (key={}), "
          "using default material.",
          i, mat_key);
        mesh.SetSubMeshMaterial(i, MaterialAsset::CreateDefault());
        continue;
      }

      mesh.SetSubMeshMaterial(i, mat_it->second);
    }
  }
}

/*!
 Publish geometry dependency edges and release temporary checkouts.

 Registers resource and asset dependencies for a geometry asset that has
 already been decoded and bound. This updates cache refcounts via
 `AddResourceDependency` and `AddAssetDependency`, then releases any
 temporary asset checkouts acquired during loading.

 @param dependent_asset_key Geometry asset key that owns the dependencies.
 @param buffers_by_index Loaded buffer resources indexed by buffer slot.
 @param materials_by_key Loaded material assets indexed by asset key.

 ### Ref-count Contract

 - Resource dependencies are retained through cache Touch semantics.
 - Material dependencies are touched, then the temporary checkout held
   by the loader is released via `CheckIn`.
 - Callers that keep additional references must release them separately.

 ### Performance Characteristics

 - Time Complexity: $O(n)$ over buffers and materials.
 - Memory: No additional allocations.
 - Optimization: Skips null dependencies.
*/
auto AssetLoader::PublishGeometryDependencyEdges(
  const data::AssetKey& dependent_asset_key,
  const LoadedGeometryBuffersByIndex& buffers_by_index,
  const LoadedGeometryMaterialsByKey& materials_by_key,
  std::optional<uint16_t> preferred_source_id) -> void
{
  AssertOwningThread();

  for (const auto& [resource_index, loaded] : buffers_by_index) {
    (void)resource_index;
    if (!loaded.resource) {
      continue;
    }
    AddResourceDependency(dependent_asset_key, loaded.key);
    (void)ReleaseResource(loaded.key);
  }

  for (const auto& [dep_key, dep_asset] : materials_by_key) {
    if (!dep_asset) {
      continue;
    }
    AddAssetDependency(dependent_asset_key, dep_key);
    if (preferred_source_id.has_value()) {
      content_cache_.CheckIn(HashAssetKey(dep_key, *preferred_source_id));
    } else {
      content_cache_.CheckIn(HashAssetKey(dep_key));
    }
  }
}

auto AssetLoader::LoadGeometryAssetAsyncImpl(const data::AssetKey& key,
  std::optional<uint16_t> preferred_source_id, LoadRequest request)
  -> co::Co<std::shared_ptr<data::GeometryAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadGeometryAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  request = NormalizeLoadRequest(request);
  const auto request_sequence = next_load_request_sequence_.fetch_add(1);
  const auto load_target = PrepareAssetLoadRequest(key, preferred_source_id);
  if (!load_target.has_value()) {
    co_return nullptr;
  }
  const auto source_id = load_target->source_id;
  const auto hash_key = load_target->hash_key;
  const AssetLoadTelemetryCallbacks telemetry_callbacks {
    .on_request =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kGeometry, LoadTelemetryEvent::kRequest);
      },
    .on_cache_hit =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kGeometry, LoadTelemetryEvent::kCacheHit);
      },
    .on_cache_miss =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kGeometry, LoadTelemetryEvent::kCacheMiss);
      },
    .on_joined_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kGeometry, LoadTelemetryEvent::kTasksDeduped);
      },
    .on_started_new_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kGeometry, LoadTelemetryEvent::kTasksSpawned);
      },
  };
  const auto publish_geometry_material_dependencies
    = [this, key, source_id, request](
        const std::shared_ptr<data::GeometryAsset>& geometry) -> co::Co<> {
    if (!geometry) {
      co_return;
    }

    std::unordered_set<data::AssetKey> seen_material_keys;
    for (const auto& mesh_ptr : geometry->Meshes()) {
      if (!mesh_ptr) {
        continue;
      }

      for (const auto& submesh : mesh_ptr->SubMeshes()) {
        const auto& desc_opt = submesh.Descriptor();
        if (!desc_opt) {
          continue;
        }
        const auto material_key = desc_opt->material_asset_key;
        if (material_key == data::AssetKey {}) {
          continue;
        }
        if (!seen_material_keys.insert(material_key).second) {
          continue;
        }

        auto material = co_await LoadMaterialAssetAsyncImpl(
          material_key, source_id, request);
        if (!material) {
          continue;
        }

        AddAssetDependency(key, material_key);
        content_cache_.CheckIn(HashAssetKey(material_key, source_id));
      }
    }
  };

  const auto cache_hit
    = [this, hash_key, publish_geometry_material_dependencies]()
    -> co::Co<std::shared_ptr<data::GeometryAsset>> {
    if (auto cached = content_cache_.CheckOut<data::GeometryAsset>(
          hash_key, oxygen::CheckoutOwner::kInternal)) {
      // Cache-hit geometry loads must restore geometry->material edges after
      // trim.
      co_await publish_geometry_material_dependencies(cached);
      co_return cached;
    }
    co_return nullptr;
  };

  const auto decode_and_publish = [this, key, source_id, hash_key, request,
                                    publish_geometry_material_dependencies]()
    -> co::Co<std::shared_ptr<data::GeometryAsset>> {
    try {
      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::GeometryAsset::ClassTypeId(), key, source_id);
      auto decoded
        = std::static_pointer_cast<data::GeometryAsset>(decoded_result.asset);
      if (!decoded
        || decoded->GetTypeId() != data::GeometryAsset::ClassTypeId()) {
        RecordAssetTelemetry(
          data::AssetType::kGeometry, LoadTelemetryEvent::kTypeMismatch);
        if (!decoded) {
          RecordAssetTelemetry(
            data::AssetType::kGeometry, LoadTelemetryEvent::kDecodeFailure);
        }
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::GeometryAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }
      if (!decoded_result.dependency_collector) {
        RecordAssetTelemetry(
          data::AssetType::kGeometry, LoadTelemetryEvent::kDecodeFailure);
        LOG_F(ERROR, "Missing dependency collector for decoded geometry asset");
        co_return nullptr;
      }

      // Publish (owning thread), mirroring the Material pipeline:
      // 1) Load dependencies using DependencyCollector (single source).
      // 2) Bind runtime-only pointers into the decoded object graph.
      // 3) Store the fully published asset.
      // 4) Register dependency edges + release temporary checkouts.
      const auto& collector = *decoded_result.dependency_collector;

      const auto loaded_buffers_by_index
        = co_await LoadGeometryBufferDependenciesAsync(collector, request);
      const auto loaded_materials
        = co_await LoadGeometryMaterialDependenciesAsync(
          collector, source_id, request);

      BindGeometryRuntimePointers(
        *decoded, loaded_buffers_by_index, loaded_materials);

      // Store the fully published asset.
      auto stored = content_cache_.Store(hash_key, decoded);
      if (!stored) {
        MaybeAutoTrimOnBudgetPressure("geometry_store_failed", true);
        stored = content_cache_.Store(hash_key, decoded);
        if (!stored) {
          RecordAssetTelemetry(
            data::AssetType::kGeometry, LoadTelemetryEvent::kStoreRetryFailure);
        }
      }
      if (stored) {
        IndexAssetHashMapping(hash_key, key, source_id);
        // Keep one loader-owned cache retain; load caller gets its own retain.
        content_cache_.Touch(hash_key, oxygen::CheckoutOwner::kInternal);
        MaybeAutoTrimOnBudgetPressure("geometry_store_succeeded");
      }

      PublishGeometryDependencyEdges(
        key, loaded_buffers_by_index, loaded_materials, source_id);

      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      RecordAssetTelemetry(
        data::AssetType::kGeometry, LoadTelemetryEvent::kCancellation);
      throw OperationCancelledException(e.what());
    }
  };

  co_return co_await RunAssetLoadPipeline<data::GeometryAsset>(
    data::GeometryAsset::ClassTypeId(), hash_key, *in_flight_ops_, request,
    request_sequence, cache_hit, decode_and_publish, telemetry_callbacks);
}

auto AssetLoader::LoadSceneAssetAsyncImpl(const data::AssetKey& key,
  std::optional<uint16_t> preferred_source_id, LoadRequest request)
  -> co::Co<std::shared_ptr<data::SceneAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadSceneAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  request = NormalizeLoadRequest(request);
  const auto request_sequence = next_load_request_sequence_.fetch_add(1);
  const auto load_target = PrepareAssetLoadRequest(key, preferred_source_id);
  if (!load_target.has_value()) {
    co_return nullptr;
  }
  const auto source_id = load_target->source_id;
  const auto hash_key = load_target->hash_key;
  const AssetLoadTelemetryCallbacks telemetry_callbacks {
    .on_request =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScene, LoadTelemetryEvent::kRequest);
      },
    .on_cache_hit =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScene, LoadTelemetryEvent::kCacheHit);
      },
    .on_cache_miss =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScene, LoadTelemetryEvent::kCacheMiss);
      },
    .on_joined_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScene, LoadTelemetryEvent::kTasksDeduped);
      },
    .on_started_new_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScene, LoadTelemetryEvent::kTasksSpawned);
      },
  };
  const auto publish_scene_geometry_dependencies
    = [this, key, source_id, request](
        const std::shared_ptr<data::SceneAsset>& scene_asset) -> co::Co<> {
    if (!scene_asset) {
      co_return;
    }

    std::unordered_set<data::AssetKey> seen_geometry_keys;
    for (const auto& renderable :
      scene_asset->GetComponents<pak::world::RenderableRecord>()) {
      if (!seen_geometry_keys.insert(renderable.geometry_key).second) {
        continue;
      }

      auto geom = co_await LoadGeometryAssetAsyncImpl(
        renderable.geometry_key, source_id, request);
      if (!geom) {
        continue;
      }

      AddAssetDependency(key, renderable.geometry_key);
      content_cache_.CheckIn(HashAssetKey(renderable.geometry_key, source_id));
    }
  };

  const auto publish_scene_script_dependencies
    = [this, key, source_id, request](
        const std::shared_ptr<data::SceneAsset>& scene_asset) -> co::Co<> {
    if (!scene_asset) {
      co_return;
    }

    std::unordered_set<data::AssetKey> seen_script_keys;
    for (const auto& scripting_component :
      scene_asset->GetComponents<pak::scripting::ScriptingComponentRecord>()) {
      const auto hydrated_slots
        = GetHydratedScriptSlots(*scene_asset, scripting_component);
      for (const auto& slot : hydrated_slots) {
        if (slot.script_asset_key == data::AssetKey {}) {
          continue;
        }
        if (!seen_script_keys.insert(slot.script_asset_key).second) {
          continue;
        }

        auto script = co_await LoadScriptAssetAsyncImpl(
          slot.script_asset_key, source_id, request);
        if (!script) {
          continue;
        }

        AddAssetDependency(key, slot.script_asset_key);
        content_cache_.CheckIn(HashAssetKey(slot.script_asset_key, source_id));
      }
    }
  };

  const auto publish_scene_input_mapping_context_dependencies
    = [this, key, source_id, request](
        const std::shared_ptr<data::SceneAsset>& scene_asset) -> co::Co<> {
    if (!scene_asset) {
      co_return;
    }

    std::unordered_set<data::AssetKey> seen_context_keys;
    for (const auto& binding :
      scene_asset->GetComponents<pak::input::InputContextBindingRecord>()) {
      if (binding.context_asset_key == data::AssetKey {}) {
        continue;
      }
      if (!seen_context_keys.insert(binding.context_asset_key).second) {
        continue;
      }

      auto context = co_await LoadInputMappingContextAssetAsyncImpl(
        binding.context_asset_key, source_id, request);
      if (!context) {
        continue;
      }

      AddAssetDependency(key, binding.context_asset_key);
      content_cache_.CheckIn(
        HashAssetKey(binding.context_asset_key, source_id));
    }
  };

  const auto cache_hit = [this, hash_key, publish_scene_geometry_dependencies,
                           publish_scene_script_dependencies,
                           publish_scene_input_mapping_context_dependencies]()
    -> co::Co<std::shared_ptr<data::SceneAsset>> {
    if (auto cached = content_cache_.CheckOut<data::SceneAsset>(
          hash_key, oxygen::CheckoutOwner::kInternal)) {
      // Cache-hit scene loads must republish scene->geometry edges so live
      // scene content is protected from trim and can be rebuilt
      // deterministically.
      co_await publish_scene_geometry_dependencies(cached);
      co_await publish_scene_script_dependencies(cached);
      co_await publish_scene_input_mapping_context_dependencies(cached);
      co_return cached;
    }
    co_return nullptr;
  };

  const auto decode_and_publish
    = [this, key, source_id, hash_key, request,
        publish_scene_geometry_dependencies, publish_scene_script_dependencies,
        publish_scene_input_mapping_context_dependencies]()
    -> co::Co<std::shared_ptr<data::SceneAsset>> {
    try {
      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::SceneAsset::ClassTypeId(), key, source_id);
      auto decoded
        = std::static_pointer_cast<data::SceneAsset>(decoded_result.asset);
      if (!decoded || decoded->GetTypeId() != data::SceneAsset::ClassTypeId()) {
        RecordAssetTelemetry(
          data::AssetType::kScene, LoadTelemetryEvent::kTypeMismatch);
        if (!decoded) {
          RecordAssetTelemetry(
            data::AssetType::kScene, LoadTelemetryEvent::kDecodeFailure);
        }
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::SceneAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }
      if (!decoded_result.dependency_collector) {
        RecordAssetTelemetry(
          data::AssetType::kScene, LoadTelemetryEvent::kDecodeFailure);
        LOG_F(ERROR, "Missing dependency collector for decoded scene asset");
        co_return nullptr;
      }

      // Publish: store the scene asset, then load asset dependencies and
      // register dependency edges.
      auto stored = content_cache_.Store(hash_key, decoded);
      if (!stored) {
        MaybeAutoTrimOnBudgetPressure("scene_store_failed", true);
        stored = content_cache_.Store(hash_key, decoded);
        if (!stored) {
          RecordAssetTelemetry(
            data::AssetType::kScene, LoadTelemetryEvent::kStoreRetryFailure);
        }
      }
      if (stored) {
        IndexAssetHashMapping(hash_key, key, source_id);
        // Keep one loader-owned cache retain; load caller gets its own retain.
        content_cache_.Touch(hash_key, oxygen::CheckoutOwner::kInternal);
        MaybeAutoTrimOnBudgetPressure("scene_store_succeeded");
      }

      // Publish only what needs async residency management:
      // geometry assets referenced by renderable components.
      // Other scene node components (camera/light/etc.) are embedded records
      // and are not assets/resources.
      //
      // Scene assets can still carry direct resource references (for example
      // environment textures) in their dependency collector. Publish these
      // edges so trim does not treat currently used scene resources as
      // standalone and evict them.
      co_await PublishResourceDependenciesAsync<data::TextureResource>(
        key, *decoded_result.dependency_collector, request);
      co_await PublishResourceDependenciesAsync<data::BufferResource>(
        key, *decoded_result.dependency_collector, request);

      co_await publish_scene_geometry_dependencies(decoded);
      co_await publish_scene_script_dependencies(decoded);
      co_await publish_scene_input_mapping_context_dependencies(decoded);

      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      RecordAssetTelemetry(
        data::AssetType::kScene, LoadTelemetryEvent::kCancellation);
      throw OperationCancelledException(e.what());
    }
  };

  co_return co_await RunAssetLoadPipeline<data::SceneAsset>(
    data::SceneAsset::ClassTypeId(), hash_key, *in_flight_ops_, request,
    request_sequence, cache_hit, decode_and_publish, telemetry_callbacks);
}

auto AssetLoader::LoadPhysicsSceneAssetAsyncImpl(const data::AssetKey& key,
  std::optional<uint16_t> preferred_source_id, LoadRequest request)
  -> co::Co<std::shared_ptr<data::PhysicsSceneAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadPhysicsSceneAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  request = NormalizeLoadRequest(request);
  const auto request_sequence = next_load_request_sequence_.fetch_add(1);
  const auto load_target = PrepareAssetLoadRequest(key, preferred_source_id);
  if (!load_target.has_value()) {
    co_return nullptr;
  }
  const auto source_id = load_target->source_id;
  const auto hash_key = load_target->hash_key;
  const AssetLoadTelemetryCallbacks telemetry_callbacks {
    .on_request =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kPhysicsScene, LoadTelemetryEvent::kRequest);
      },
    .on_cache_hit =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kPhysicsScene, LoadTelemetryEvent::kCacheHit);
      },
    .on_cache_miss =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kPhysicsScene, LoadTelemetryEvent::kCacheMiss);
      },
    .on_joined_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kPhysicsScene, LoadTelemetryEvent::kTasksDeduped);
      },
    .on_started_new_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kPhysicsScene, LoadTelemetryEvent::kTasksSpawned);
      },
  };

  const auto cache_hit
    = [this, hash_key]() -> co::Co<std::shared_ptr<data::PhysicsSceneAsset>> {
    if (auto cached = content_cache_.CheckOut<data::PhysicsSceneAsset>(
          hash_key, oxygen::CheckoutOwner::kInternal)) {
      // Physics sidecar has no sub-asset dependencies to republish.
      co_return cached;
    }
    co_return nullptr;
  };

  const auto decode_and_publish
    = [this, key, source_id,
        hash_key]() -> co::Co<std::shared_ptr<data::PhysicsSceneAsset>> {
    try {
      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::PhysicsSceneAsset::ClassTypeId(), key, source_id);
      auto typed = std::static_pointer_cast<data::PhysicsSceneAsset>(
        decoded_result.asset);
      if (!typed
        || typed->GetTypeId() != data::PhysicsSceneAsset::ClassTypeId()) {
        RecordAssetTelemetry(
          data::AssetType::kPhysicsScene, LoadTelemetryEvent::kTypeMismatch);
        if (!typed) {
          RecordAssetTelemetry(
            data::AssetType::kPhysicsScene, LoadTelemetryEvent::kDecodeFailure);
        }
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::PhysicsSceneAsset::ClassTypeNamePretty(),
          typed ? typed->GetTypeName() : "nullptr");
        co_return nullptr;
      }

      // Publish: store the asset in cache and keep one loader-owned retain.
      auto stored = content_cache_.Store(hash_key, typed);
      if (!stored) {
        MaybeAutoTrimOnBudgetPressure("physics_scene_store_failed", true);
        stored = content_cache_.Store(hash_key, typed);
        if (!stored) {
          RecordAssetTelemetry(data::AssetType::kPhysicsScene,
            LoadTelemetryEvent::kStoreRetryFailure);
        }
      }
      if (stored) {
        IndexAssetHashMapping(hash_key, key, source_id);
        content_cache_.Touch(hash_key, oxygen::CheckoutOwner::kInternal);
        MaybeAutoTrimOnBudgetPressure("physics_scene_store_succeeded");
      }
      co_return typed;
    } catch (const co::TaskCancelledException& e) {
      RecordAssetTelemetry(
        data::AssetType::kPhysicsScene, LoadTelemetryEvent::kCancellation);
      throw OperationCancelledException(e.what());
    }
  };

  co_return co_await RunAssetLoadPipeline<data::PhysicsSceneAsset>(
    data::PhysicsSceneAsset::ClassTypeId(), hash_key, *in_flight_ops_, request,
    request_sequence, cache_hit, decode_and_publish, telemetry_callbacks);
}

auto AssetLoader::LoadScriptAssetAsyncImpl(const data::AssetKey& key,
  std::optional<uint16_t> preferred_source_id, LoadRequest request)
  -> co::Co<std::shared_ptr<data::ScriptAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadScriptAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  request = NormalizeLoadRequest(request);
  const auto request_sequence = next_load_request_sequence_.fetch_add(1);
  const auto load_target = PrepareAssetLoadRequest(key, preferred_source_id);
  if (!load_target.has_value()) {
    co_return nullptr;
  }
  const auto source_id = load_target->source_id;
  const auto hash_key = load_target->hash_key;
  const AssetLoadTelemetryCallbacks telemetry_callbacks {
    .on_request =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScript, LoadTelemetryEvent::kRequest);
      },
    .on_cache_hit =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScript, LoadTelemetryEvent::kCacheHit);
      },
    .on_cache_miss =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScript, LoadTelemetryEvent::kCacheMiss);
      },
    .on_joined_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScript, LoadTelemetryEvent::kTasksDeduped);
      },
    .on_started_new_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kScript, LoadTelemetryEvent::kTasksSpawned);
      },
  };
  const auto publish_script_resource_dependency
    = [this, key, source_id, request](
        const std::shared_ptr<data::ScriptAsset>& script_asset) -> co::Co<> {
    if (!script_asset) {
      co_return;
    }
    const auto script_type_index = static_cast<uint16_t>(
      IndexOf<data::ScriptResource, ResourceTypeList>::value);
    const auto indices = std::array { script_asset->GetBytecodeResourceIndex(),
      script_asset->GetSourceResourceIndex() };

    for (size_t i = 0; i < indices.size(); ++i) {
      const auto resource_index = indices[i];
      if (resource_index == data::pak::core::kNoResourceIndex) {
        continue;
      }
      if (i == 1 && resource_index == indices[0]) {
        continue;
      }
      const auto script_resource_key
        = PackResourceKey(source_id, script_type_index, resource_index);
      auto script_resource = co_await LoadResourceAsync<data::ScriptResource>(
        script_resource_key, request);
      if (!script_resource) {
        continue;
      }

      AddResourceDependency(key, script_resource_key);
      (void)ReleaseResource(script_resource_key);
    }
  };

  const auto cache_hit = [this, hash_key, publish_script_resource_dependency]()
    -> co::Co<std::shared_ptr<data::ScriptAsset>> {
    if (auto cached = content_cache_.CheckOut<data::ScriptAsset>(
          hash_key, oxygen::CheckoutOwner::kInternal)) {
      co_await publish_script_resource_dependency(cached);
      co_return cached;
    }
    co_return nullptr;
  };

  const auto decode_and_publish = [this, key, source_id, hash_key, request,
                                    publish_script_resource_dependency]()
    -> co::Co<std::shared_ptr<data::ScriptAsset>> {
    try {
      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::ScriptAsset::ClassTypeId(), key, source_id);
      auto decoded
        = std::static_pointer_cast<data::ScriptAsset>(decoded_result.asset);
      if (!decoded
        || decoded->GetTypeId() != data::ScriptAsset::ClassTypeId()) {
        RecordAssetTelemetry(
          data::AssetType::kScript, LoadTelemetryEvent::kTypeMismatch);
        if (!decoded) {
          RecordAssetTelemetry(
            data::AssetType::kScript, LoadTelemetryEvent::kDecodeFailure);
        }
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::ScriptAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }
      if (!decoded_result.dependency_collector) {
        RecordAssetTelemetry(
          data::AssetType::kScript, LoadTelemetryEvent::kDecodeFailure);
        LOG_F(ERROR, "Missing dependency collector for decoded script asset");
        co_return nullptr;
      }

      auto stored = content_cache_.Store(hash_key, decoded);
      if (!stored) {
        MaybeAutoTrimOnBudgetPressure("script_asset_store_failed", true);
        stored = content_cache_.Store(hash_key, decoded);
        if (!stored) {
          RecordAssetTelemetry(
            data::AssetType::kScript, LoadTelemetryEvent::kStoreRetryFailure);
        }
      }
      if (stored) {
        IndexAssetHashMapping(hash_key, key, source_id);
        // Baseline pinning: retain one loader-owned checkout.
        content_cache_.Touch(hash_key, oxygen::CheckoutOwner::kInternal);
        MaybeAutoTrimOnBudgetPressure("script_asset_store_succeeded");
      }

      co_await PublishResourceDependenciesAsync<data::ScriptResource>(
        key, *decoded_result.dependency_collector, request);
      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      RecordAssetTelemetry(
        data::AssetType::kScript, LoadTelemetryEvent::kCancellation);
      throw OperationCancelledException(e.what());
    }
  };

  co_return co_await RunAssetLoadPipeline<data::ScriptAsset>(
    data::ScriptAsset::ClassTypeId(), hash_key, *in_flight_ops_, request,
    request_sequence, cache_hit, decode_and_publish, telemetry_callbacks);
}

auto AssetLoader::LoadInputActionAssetAsyncImpl(const data::AssetKey& key,
  std::optional<uint16_t> preferred_source_id, LoadRequest request)
  -> co::Co<std::shared_ptr<data::InputActionAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadInputActionAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  request = NormalizeLoadRequest(request);
  const auto request_sequence = next_load_request_sequence_.fetch_add(1);
  const auto load_target = PrepareAssetLoadRequest(key, preferred_source_id);
  if (!load_target.has_value()) {
    co_return nullptr;
  }
  const auto source_id = load_target->source_id;
  const auto hash_key = load_target->hash_key;
  const AssetLoadTelemetryCallbacks telemetry_callbacks {
    .on_request =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kInputAction, LoadTelemetryEvent::kRequest);
      },
    .on_cache_hit =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kInputAction, LoadTelemetryEvent::kCacheHit);
      },
    .on_cache_miss =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kInputAction, LoadTelemetryEvent::kCacheMiss);
      },
    .on_joined_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kInputAction, LoadTelemetryEvent::kTasksDeduped);
      },
    .on_started_new_inflight =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kInputAction, LoadTelemetryEvent::kTasksSpawned);
      },
  };

  const auto cache_hit
    = [this, hash_key]() -> co::Co<std::shared_ptr<data::InputActionAsset>> {
    co_return content_cache_.CheckOut<data::InputActionAsset>(
      hash_key, oxygen::CheckoutOwner::kInternal);
  };

  const auto decode_and_publish
    = [this, key, source_id,
        hash_key]() -> co::Co<std::shared_ptr<data::InputActionAsset>> {
    try {
      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::InputActionAsset::ClassTypeId(), key, source_id);
      auto decoded = std::static_pointer_cast<data::InputActionAsset>(
        decoded_result.asset);
      if (!decoded
        || decoded->GetTypeId() != data::InputActionAsset::ClassTypeId()) {
        RecordAssetTelemetry(
          data::AssetType::kInputAction, LoadTelemetryEvent::kTypeMismatch);
        if (!decoded) {
          RecordAssetTelemetry(
            data::AssetType::kInputAction, LoadTelemetryEvent::kDecodeFailure);
        }
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::InputActionAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }

      auto stored = content_cache_.Store(hash_key, decoded);
      if (!stored) {
        MaybeAutoTrimOnBudgetPressure("input_action_store_failed", true);
        stored = content_cache_.Store(hash_key, decoded);
        if (!stored) {
          RecordAssetTelemetry(data::AssetType::kInputAction,
            LoadTelemetryEvent::kStoreRetryFailure);
        }
      }
      if (stored) {
        IndexAssetHashMapping(hash_key, key, source_id);
        // Baseline pinning: retain one loader-owned checkout.
        content_cache_.Touch(hash_key, oxygen::CheckoutOwner::kInternal);
        MaybeAutoTrimOnBudgetPressure("input_action_store_succeeded");
      }

      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      RecordAssetTelemetry(
        data::AssetType::kInputAction, LoadTelemetryEvent::kCancellation);
      throw OperationCancelledException(e.what());
    }
  };

  co_return co_await RunAssetLoadPipeline<data::InputActionAsset>(
    data::InputActionAsset::ClassTypeId(), hash_key, *in_flight_ops_, request,
    request_sequence, cache_hit, decode_and_publish, telemetry_callbacks);
}

auto AssetLoader::LoadInputMappingContextAssetAsyncImpl(
  const data::AssetKey& key, std::optional<uint16_t> preferred_source_id,
  LoadRequest request)
  -> co::Co<std::shared_ptr<data::InputMappingContextAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadInputMappingContextAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  request = NormalizeLoadRequest(request);
  const auto request_sequence = next_load_request_sequence_.fetch_add(1);
  const auto load_target = PrepareAssetLoadRequest(key, preferred_source_id);
  if (!load_target.has_value()) {
    co_return nullptr;
  }
  const auto source_id = load_target->source_id;
  const auto hash_key = load_target->hash_key;
  const AssetLoadTelemetryCallbacks telemetry_callbacks {
    .on_request =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kInputMappingContext, LoadTelemetryEvent::kRequest);
      },
    .on_cache_hit =
      [this]() {
        RecordAssetTelemetry(
          data::AssetType::kInputMappingContext, LoadTelemetryEvent::kCacheHit);
      },
    .on_cache_miss =
      [this]() {
        RecordAssetTelemetry(data::AssetType::kInputMappingContext,
          LoadTelemetryEvent::kCacheMiss);
      },
    .on_joined_inflight =
      [this]() {
        RecordAssetTelemetry(data::AssetType::kInputMappingContext,
          LoadTelemetryEvent::kTasksDeduped);
      },
    .on_started_new_inflight =
      [this]() {
        RecordAssetTelemetry(data::AssetType::kInputMappingContext,
          LoadTelemetryEvent::kTasksSpawned);
      },
  };
  const auto publish_input_action_dependencies
    = [this, key, source_id, request](
        const std::shared_ptr<data::InputMappingContextAsset>& context_asset)
    -> co::Co<> {
    if (!context_asset) {
      co_return;
    }

    std::unordered_set<data::AssetKey> seen_action_keys;
    for (const auto& mapping : context_asset->GetMappings()) {
      if (mapping.action_asset_key == data::AssetKey {}) {
        continue;
      }
      if (!seen_action_keys.insert(mapping.action_asset_key).second) {
        continue;
      }

      auto action = co_await LoadInputActionAssetAsyncImpl(
        mapping.action_asset_key, source_id, request);
      if (!action) {
        continue;
      }

      AddAssetDependency(key, mapping.action_asset_key);
      content_cache_.CheckIn(HashAssetKey(mapping.action_asset_key, source_id));
    }

    for (const auto& trigger : context_asset->GetTriggers()) {
      if (trigger.linked_action_asset_key == data::AssetKey {}) {
        continue;
      }
      if (!seen_action_keys.insert(trigger.linked_action_asset_key).second) {
        continue;
      }

      auto action = co_await LoadInputActionAssetAsyncImpl(
        trigger.linked_action_asset_key, source_id, request);
      if (!action) {
        continue;
      }

      AddAssetDependency(key, trigger.linked_action_asset_key);
      content_cache_.CheckIn(
        HashAssetKey(trigger.linked_action_asset_key, source_id));
    }

    for (const auto& aux : context_asset->GetTriggerAuxRecords()) {
      if (aux.action_asset_key == data::AssetKey {}) {
        continue;
      }
      if (!seen_action_keys.insert(aux.action_asset_key).second) {
        continue;
      }

      auto action = co_await LoadInputActionAssetAsyncImpl(
        aux.action_asset_key, source_id, request);
      if (!action) {
        continue;
      }

      AddAssetDependency(key, aux.action_asset_key);
      content_cache_.CheckIn(HashAssetKey(aux.action_asset_key, source_id));
    }
  };

  const auto cache_hit = [this, hash_key, publish_input_action_dependencies]()
    -> co::Co<std::shared_ptr<data::InputMappingContextAsset>> {
    if (auto cached = content_cache_.CheckOut<data::InputMappingContextAsset>(
          hash_key, oxygen::CheckoutOwner::kInternal)) {
      co_await publish_input_action_dependencies(cached);
      co_return cached;
    }
    co_return nullptr;
  };

  const auto decode_and_publish = [this, key, source_id, hash_key, request,
                                    publish_input_action_dependencies]()
    -> co::Co<std::shared_ptr<data::InputMappingContextAsset>> {
    try {
      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::InputMappingContextAsset::ClassTypeId(), key, source_id);
      auto decoded = std::static_pointer_cast<data::InputMappingContextAsset>(
        decoded_result.asset);
      if (!decoded
        || decoded->GetTypeId()
          != data::InputMappingContextAsset::ClassTypeId()) {
        RecordAssetTelemetry(data::AssetType::kInputMappingContext,
          LoadTelemetryEvent::kTypeMismatch);
        if (!decoded) {
          RecordAssetTelemetry(data::AssetType::kInputMappingContext,
            LoadTelemetryEvent::kDecodeFailure);
        }
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::InputMappingContextAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }

      auto stored = content_cache_.Store(hash_key, decoded);
      if (!stored) {
        MaybeAutoTrimOnBudgetPressure(
          "input_mapping_context_store_failed", true);
        stored = content_cache_.Store(hash_key, decoded);
        if (!stored) {
          RecordAssetTelemetry(data::AssetType::kInputMappingContext,
            LoadTelemetryEvent::kStoreRetryFailure);
        }
      }
      if (stored) {
        IndexAssetHashMapping(hash_key, key, source_id);
        // Baseline pinning: retain one loader-owned checkout.
        content_cache_.Touch(hash_key, oxygen::CheckoutOwner::kInternal);
        MaybeAutoTrimOnBudgetPressure("input_mapping_context_store_succeeded");
      }

      co_await publish_input_action_dependencies(decoded);
      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      RecordAssetTelemetry(data::AssetType::kInputMappingContext,
        LoadTelemetryEvent::kCancellation);
      throw OperationCancelledException(e.what());
    }
  };

  co_return co_await RunAssetLoadPipeline<data::InputMappingContextAsset>(
    data::InputMappingContextAsset::ClassTypeId(), hash_key, *in_flight_ops_,
    request, request_sequence, cache_hit, decode_and_publish,
    telemetry_callbacks);
}

template <PakResource T>
auto AssetLoader::LoadResourceAsync(const oxygen::content::ResourceKey key)
  -> co::Co<std::shared_ptr<T>>
{
  co_return co_await LoadResourceAsync<T>(key, LoadRequest {});
}

template <PakResource T>
auto AssetLoader::LoadResourceAsync(const oxygen::content::ResourceKey key,
  LoadRequest request) -> co::Co<std::shared_ptr<T>>
{
  static_assert(std::same_as<T, data::TextureResource>
      || std::same_as<T, data::BufferResource>
      || std::same_as<T, data::ScriptResource>
      || std::same_as<T, data::PhysicsResource>,
    "Unsupported resource type for LoadResourceAsync");

  DLOG_SCOPE_F(2, "AssetLoader LoadResourceAsync");
  DLOG_F(2, "type    : {}", T::ClassTypeNamePretty());
  DLOG_F(2, "key     : {}", key);
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();
  request = NormalizeLoadRequest(request);

  if (!nursery_) {
    throw std::runtime_error(
      "AssetLoader must be activated before async loads (LoadResourceAsync)");
  }
  if (!thread_pool_) {
    throw std::runtime_error(
      "AssetLoader requires a thread pool for async loads (LoadResourceAsync)");
  }

  const internal::InternalResourceKey internal_key(key);
  const auto expected_type_index
    = static_cast<uint16_t>(IndexOf<T, ResourceTypeList>::value);
  if (internal_key.GetResourceTypeIndex() != expected_type_index) {
    if constexpr (std::same_as<T, data::TextureResource>) {
      RecordResourceTelemetry(data::TextureResource::ClassTypeId(),
        LoadTelemetryEvent::kTypeMismatch);
    } else if constexpr (std::same_as<T, data::BufferResource>) {
      RecordResourceTelemetry(
        data::BufferResource::ClassTypeId(), LoadTelemetryEvent::kTypeMismatch);
    } else if constexpr (std::same_as<T, data::ScriptResource>) {
      RecordResourceTelemetry(
        data::ScriptResource::ClassTypeId(), LoadTelemetryEvent::kTypeMismatch);
    } else if constexpr (std::same_as<T, data::PhysicsResource>) {
      RecordResourceTelemetry(data::PhysicsResource::ClassTypeId(),
        LoadTelemetryEvent::kTypeMismatch);
    }
    LOG_F(ERROR,
      "ResourceKey type mismatch for {}: key_type={} expected_type={}",
      T::ClassTypeNamePretty(), internal_key.GetResourceTypeIndex(),
      expected_type_index);
    co_return nullptr;
  }

  if constexpr (std::same_as<T, data::TextureResource>) {
    LOG_F(INFO, "AssetLoader: Decode TextureResource {}", to_string(key));
  }

  try {
    const auto decoded = co_await resource_load_pipeline_->LoadErased(
      T::ClassTypeId(), key, request);
    auto typed = std::static_pointer_cast<T>(decoded);
    if (!typed || typed->GetTypeId() != T::ClassTypeId()) {
      if constexpr (std::same_as<T, data::TextureResource>) {
        RecordResourceTelemetry(data::TextureResource::ClassTypeId(),
          LoadTelemetryEvent::kTypeMismatch);
      } else if constexpr (std::same_as<T, data::BufferResource>) {
        RecordResourceTelemetry(data::BufferResource::ClassTypeId(),
          LoadTelemetryEvent::kTypeMismatch);
      } else if constexpr (std::same_as<T, data::ScriptResource>) {
        RecordResourceTelemetry(data::ScriptResource::ClassTypeId(),
          LoadTelemetryEvent::kTypeMismatch);
      } else if constexpr (std::same_as<T, data::PhysicsResource>) {
        RecordResourceTelemetry(data::PhysicsResource::ClassTypeId(),
          LoadTelemetryEvent::kTypeMismatch);
      }
      LOG_F(ERROR, "Loaded resource type mismatch: expected {}",
        T::ClassTypeNamePretty());
      co_return nullptr;
    }
    co_return typed;
  } catch (const co::TaskCancelledException& e) {
    if constexpr (std::same_as<T, data::TextureResource>) {
      RecordResourceTelemetry(data::TextureResource::ClassTypeId(),
        LoadTelemetryEvent::kCancellation);
    } else if constexpr (std::same_as<T, data::BufferResource>) {
      RecordResourceTelemetry(
        data::BufferResource::ClassTypeId(), LoadTelemetryEvent::kCancellation);
    } else if constexpr (std::same_as<T, data::ScriptResource>) {
      RecordResourceTelemetry(
        data::ScriptResource::ClassTypeId(), LoadTelemetryEvent::kCancellation);
    } else if constexpr (std::same_as<T, data::PhysicsResource>) {
      RecordResourceTelemetry(data::PhysicsResource::ClassTypeId(),
        LoadTelemetryEvent::kCancellation);
    }
    throw OperationCancelledException(e.what());
  }
}

void oxygen::content::AssetLoader::UnloadObject(const uint64_t cache_key,
  const oxygen::TypeId& type_id, const EvictionReason reason)
{
  RecordEviction(reason);
  EvictionEvent event {
    .key = ResourceKey {},
    .type_id = type_id,
    .reason = reason,
#if !defined(NDEBUG)
    .cache_key_hash = cache_key,
#endif
  };

  if (IsResourceTypeId(type_id)) {
    const auto key_opt = resource_key_registry_->Find(cache_key);
    if (!key_opt.has_value()) {
      LOG_F(WARNING,
        "Eviction without ResourceKey mapping: key_hash={} type_id={}",
        cache_key, type_id);
      return;
    }

    event.key = *key_opt;
    if (reason != EvictionReason::kRefCountZero) {
      resource_key_registry_->Erase(cache_key);
    }
    pinned_resource_counts_.erase(cache_key);
    LOG_F(2, "Evicted resource {} type_id={} reason={}", to_string(event.key),
      type_id, reason);
  } else {
    const auto* asset_key = asset_identity_index_->FindAssetKey(cache_key);
    if (asset_key == nullptr) {
      LOG_F(WARNING,
        "Eviction without AssetKey mapping: key_hash={} type_id={}", cache_key,
        type_id);
      return;
    }

    event.asset_key = *asset_key;
    UnindexAssetHashMapping(cache_key);
    pinned_asset_counts_.erase(cache_key);
    LOG_F(2, "Evicted asset {} type_id={} reason={}", *event.asset_key, type_id,
      reason);
  }

  const auto subscribers = eviction_registry_->SnapshotSubscribers(type_id);
  if (subscribers.empty()) {
    return;
  }

  // Prevent re-entrant eviction notifications for the same cache key.
  if (!eviction_registry_->TryEnterEviction(cache_key)) {
    LOG_F(
      2, "AssetLoader: nested eviction ignored for cache_key={}", cache_key);
    return;
  }
  // Ensure the guard is cleared on all exit paths.
  ScopeGuard clear_eviction_guard([this, cache_key]() noexcept {
    eviction_registry_->ExitEviction(cache_key);
  });

  for (const auto& subscriber : subscribers) {
    if (!subscriber.handler) {
      continue;
    }
    try {
      subscriber.handler(event);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "Eviction handler threw: {}", e.what());
    } catch (...) {
      LOG_F(ERROR, "Eviction handler threw unknown exception");
    }
  }
}

auto AssetLoader::FlushResourceEvictionsForUncachedMappings(
  const EvictionReason reason, const bool force_emit_all) -> void
{
  AssertOwningThread();

  std::vector<std::pair<uint64_t, TypeId>> pending;
  pending.reserve(resource_key_registry_->Size());
  std::vector<uint64_t> stale_hashes;
  stale_hashes.reserve(resource_key_registry_->Size());
  std::vector<std::pair<uint64_t, ResourceKey>> remap_entries;
  remap_entries.reserve(resource_key_registry_->Size());
  std::size_t uncached_candidates = 0U;

  for (const auto& [cache_key, resource_key] :
    resource_key_registry_->Entries()) {
    if (content_cache_.Contains(cache_key)) {
      continue;
    }
    ++uncached_candidates;

    if (!force_emit_all) {
      const auto canonical_hash = HashResourceKey(resource_key);
      if (canonical_hash != cache_key) {
        // This mapping is stale (typically from source remap/reload).
        // Do not emit an eviction from a stale hash entry.
        stale_hashes.push_back(cache_key);
        if (content_cache_.Contains(canonical_hash)) {
          remap_entries.emplace_back(canonical_hash, resource_key);
        }
        continue;
      }
    }

    const internal::InternalResourceKey internal_key(resource_key);
    pending.emplace_back(
      cache_key, GetResourceTypeIdByIndex(internal_key.GetResourceTypeIndex()));
  }

  for (const auto stale_hash : stale_hashes) {
    resource_key_registry_->Erase(stale_hash);
  }
  for (const auto& [canonical_hash, resource_key] : remap_entries) {
    resource_key_registry_->InsertOrAssign(canonical_hash, resource_key);
  }

  if (uncached_candidates > 0U || !stale_hashes.empty()
    || !remap_entries.empty() || !pending.empty()) {
    LOG_F(INFO,
      "AssetLoader: resource mapping flush inspected={} stale_removed={} "
      "remapped={} emitted={}",
      uncached_candidates, stale_hashes.size(), remap_entries.size(),
      pending.size());
  }

  if (!pending.empty()) {
    LOG_F(INFO,
      "AssetLoader: flushing {} uncached resource mappings as evictions "
      "(reason={})",
      pending.size(), reason);
  }

  for (const auto& [cache_key, type_id] : pending) {
    UnloadObject(cache_key, type_id, reason);
  }

  AssertResourceMappingConsistency("FlushResourceEvictionsForUncachedMappings");
}

auto AssetLoader::ReleaseResource(const ResourceKey key) -> bool
{
  AssertOwningThread();
  const auto key_hash = HashResourceKey(key);
  const internal::InternalResourceKey internal_key(key);
  [[maybe_unused]] const auto expected_type_id
    = GetResourceTypeIdByIndex(internal_key.GetResourceTypeIndex());

  // The resource should always be checked in on release. Whether it remains in
  // the cache or gets evicted is dependent on the eviction policy.
  auto guard = content_cache_.OnEviction(
    [&](const uint64_t cache_key, [[maybe_unused]] std::shared_ptr<void> value,
      const TypeId type_id) {
      static_cast<void>(value);
      DCHECK_F(SanityCheckResourceEviction(
        key_hash, cache_key, expected_type_id, type_id));
      LOG_F(2, "Evict resource: key_hash={} type_id={}", cache_key, type_id);

      UnloadObject(cache_key, type_id, EvictionReason::kRefCountZero);
    });
  content_cache_.CheckIn(key_hash);
  const bool still_present = content_cache_.Contains(key_hash);
  LOG_F(2, "AssetLoader: ReleaseResource key={} evicted={}", to_string(key),
    still_present ? "false" : "true");
  return !still_present;
}

auto AssetLoader::PinResource(const ResourceKey key) -> bool
{
  AssertOwningThread();
  const auto key_hash = HashResourceKey(key);
  if (!content_cache_.Pin(key_hash, oxygen::CheckoutOwner::kExternal)) {
    LOG_F(WARNING, "pin resource failed: key={} not loaded", to_string(key));
    return false;
  }
  ++pinned_resource_counts_[key_hash];
  return true;
}

auto AssetLoader::UnpinResource(const ResourceKey key) -> bool
{
  AssertOwningThread();
  const auto key_hash = HashResourceKey(key);
  auto pin_it = pinned_resource_counts_.find(key_hash);
  if (pin_it == pinned_resource_counts_.end() || pin_it->second == 0U) {
    LOG_F(ERROR, "unpin resource failed: key={} has no matching pin",
      to_string(key));
    return false;
  }
  if (!content_cache_.Unpin(key_hash)) {
    LOG_F(ERROR, "unpin resource failed: key={} cache refcount underflow",
      to_string(key));
    return false;
  }
  --pin_it->second;
  if (pin_it->second == 0U) {
    pinned_resource_counts_.erase(pin_it);
  }
  return true;
}

auto AssetLoader::GetPakIndex(const PakFile& pak) const -> uint16_t
{
  // Normalize the path of the input pak
  const auto& pak_path = std::filesystem::weakly_canonical(pak.FilePath());

  for (size_t i = 0; i < impl_->source_registry.PakPaths().size(); ++i) {
    if (impl_->source_registry.PakPaths()[i] == pak_path) {
      return static_cast<uint16_t>(i);
    }
  }

  LOG_F(ERROR, "PAK file not found in AssetLoader collection (by path)");
  throw std::runtime_error("PAK file not found in AssetLoader collection");
}

auto AssetLoader::MakePhysicsResourceKey(const data::SourceKey source_key,
  const data::pak::core::ResourceIndexT resource_index) const noexcept
  -> std::optional<ResourceKey>
{
  const internal::PhysicsQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .resolve_source_for_id
    = [this](
        const uint16_t source_id) { return ResolveSourceForId(source_id); },
    .resolve_source_id_for_source_key
    = [this](const data::SourceKey key) -> std::optional<uint16_t> {
      for (size_t i = 0; i < impl_->source_registry.Sources().size(); ++i) {
        const auto& source = impl_->source_registry.Sources()[i];
        if (source && source->GetSourceKey() == key) {
          return impl_->source_registry.SourceIds()[i];
        }
      }
      return std::nullopt;
    },
    .make_physics_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::PhysicsResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
  };
  return physics_query_service_->MakePhysicsResourceKey(
    source_key, resource_index, callbacks);
}

auto AssetLoader::MakeScriptResourceKeyForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::core::ResourceIndexT resource_index) const noexcept
  -> std::optional<ResourceKey>
{
  const internal::ScriptQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .resolve_source_for_id
    = [this](
        const uint16_t source_id) { return ResolveSourceForId(source_id); },
    .make_script_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::ScriptResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
  };
  return script_query_service_->MakeScriptResourceKeyForAsset(
    context_asset_key, resource_index, callbacks);
}

auto AssetLoader::ReadScriptResourceForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::core::ResourceIndexT resource_index) const
  -> std::shared_ptr<const data::ScriptResource>
{
  const internal::ScriptQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .resolve_source_for_id
    = [this](
        const uint16_t source_id) { return ResolveSourceForId(source_id); },
    .make_script_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::ScriptResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
  };
  return script_query_service_->ReadScriptResourceForAsset(
    context_asset_key, resource_index, callbacks);
}

auto AssetLoader::MakePhysicsResourceKeyForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::core::ResourceIndexT resource_index) const noexcept
  -> std::optional<ResourceKey>
{
  const internal::PhysicsQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .resolve_source_for_id
    = [this](
        const uint16_t source_id) { return ResolveSourceForId(source_id); },
    .resolve_source_id_for_source_key
    = [this](const data::SourceKey key) -> std::optional<uint16_t> {
      for (size_t i = 0; i < impl_->source_registry.Sources().size(); ++i) {
        const auto& source = impl_->source_registry.Sources()[i];
        if (source && source->GetSourceKey() == key) {
          return impl_->source_registry.SourceIds()[i];
        }
      }
      return std::nullopt;
    },
    .make_physics_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::PhysicsResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
  };
  return physics_query_service_->MakePhysicsResourceKeyForAsset(
    context_asset_key, resource_index, callbacks);
}

auto AssetLoader::ReadCollisionShapeAssetDescForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::core::ResourceIndexT shape_asset_index) const
  -> std::optional<data::pak::physics::CollisionShapeAssetDesc>
{
  const internal::PhysicsQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .resolve_source_for_id
    = [this](
        const uint16_t source_id) { return ResolveSourceForId(source_id); },
    .resolve_source_id_for_source_key
    = [this](const data::SourceKey key) -> std::optional<uint16_t> {
      for (size_t i = 0; i < impl_->source_registry.Sources().size(); ++i) {
        const auto& source = impl_->source_registry.Sources()[i];
        if (source && source->GetSourceKey() == key) {
          return impl_->source_registry.SourceIds()[i];
        }
      }
      return std::nullopt;
    },
    .make_physics_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::PhysicsResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
  };
  return physics_query_service_->ReadCollisionShapeAssetDescForAsset(
    context_asset_key, shape_asset_index, callbacks);
}

auto AssetLoader::ReadPhysicsMaterialAssetDescForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::core::ResourceIndexT material_asset_index) const
  -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc>
{
  const internal::PhysicsQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .resolve_source_for_id
    = [this](
        const uint16_t source_id) { return ResolveSourceForId(source_id); },
    .resolve_source_id_for_source_key
    = [this](const data::SourceKey key) -> std::optional<uint16_t> {
      for (size_t i = 0; i < impl_->source_registry.Sources().size(); ++i) {
        const auto& source = impl_->source_registry.Sources()[i];
        if (source && source->GetSourceKey() == key) {
          return impl_->source_registry.SourceIds()[i];
        }
      }
      return std::nullopt;
    },
    .make_physics_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::PhysicsResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
  };
  return physics_query_service_->ReadPhysicsMaterialAssetDescForAsset(
    context_asset_key, material_asset_index, callbacks);
}

auto AssetLoader::FindPhysicsSidecarAssetKeyForScene(
  const data::AssetKey& scene_key) const -> std::optional<data::AssetKey>
{
  const internal::PhysicsQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this](
        const data::AssetKey& key) { return ResolveSourceIdForAsset(key); },
    .resolve_source_for_id
    = [this](
        const uint16_t source_id) { return ResolveSourceForId(source_id); },
    .resolve_source_id_for_source_key
    = [this](const data::SourceKey key) -> std::optional<uint16_t> {
      for (size_t i = 0; i < impl_->source_registry.Sources().size(); ++i) {
        const auto& source = impl_->source_registry.Sources()[i];
        if (source && source->GetSourceKey() == key) {
          return impl_->source_registry.SourceIds()[i];
        }
      }
      return std::nullopt;
    },
    .make_physics_resource_key =
      [this](
        const uint16_t source_id, const data::pak::core::ResourceIndexT index) {
        const auto resource_type_index = static_cast<uint16_t>(
          IndexOf<data::PhysicsResource, ResourceTypeList>::value);
        return PackResourceKey(source_id, resource_type_index, index);
      },
  };
  return physics_query_service_->FindPhysicsSidecarAssetKeyForScene(
    scene_key, callbacks);
}

auto AssetLoader::ResolveAssetIdentityForKey(
  const data::AssetKey& key, std::optional<uint16_t> preferred_source_id) const
  -> std::optional<ResolvedAssetIdentity>
{
  if (const auto indexed = asset_identity_index_->ResolveIndexed(
        key, preferred_source_id, impl_->source_registry.SourceIdToIndex());
    indexed.has_value()) {
    return ResolvedAssetIdentity {
      .hash_key = indexed->hash_key,
      .source_id = indexed->source_id,
    };
  }

  if (preferred_source_id.has_value()) {
    const auto source_it
      = impl_->source_registry.SourceIdToIndex().find(*preferred_source_id);
    if (source_it != impl_->source_registry.SourceIdToIndex().end()) {
      const auto& source
        = impl_->source_registry.Sources().at(source_it->second);
      if (source && source->HasAsset(key)) {
        return ResolvedAssetIdentity {
          .hash_key = HashAssetKey(key, *preferred_source_id),
          .source_id = *preferred_source_id,
        };
      }
    }
    return std::nullopt;
  }

  for (size_t i = impl_->source_registry.Sources().size(); i-- > 0;) {
    const auto& source = impl_->source_registry.Sources()[i];
    if (source && source->HasAsset(key)) {
      const auto source_id = impl_->source_registry.SourceIds()[i];
      return ResolvedAssetIdentity {
        .hash_key = HashAssetKey(key, source_id),
        .source_id = source_id,
      };
    }
  }

  return std::nullopt;
}

auto AssetLoader::IndexAssetHashMapping(const uint64_t hash_key,
  const data::AssetKey& key, const uint16_t source_id) -> void
{
  asset_identity_index_->Index(hash_key, key, source_id);
}

auto AssetLoader::UnindexAssetHashMapping(const uint64_t hash_key) -> void
{
  asset_identity_index_->Unindex(hash_key);
}

auto AssetLoader::ResolveSourceIdForAsset(
  const data::AssetKey& context_asset_key) const -> std::optional<uint16_t>
{
  if (const auto identity = ResolveAssetIdentityForKey(context_asset_key);
    identity.has_value()) {
    return identity->source_id;
  }
  return std::nullopt;
}

auto AssetLoader::ResolveLoadSourceId(const data::AssetKey& key,
  std::optional<uint16_t> preferred_source_id) const -> std::optional<uint16_t>
{
  if (preferred_source_id.has_value()) {
    const auto source_it
      = impl_->source_registry.SourceIdToIndex().find(*preferred_source_id);
    if (source_it != impl_->source_registry.SourceIdToIndex().end()
      && impl_->source_registry.Sources()
        .at(source_it->second)
        ->HasAsset(key)) {
      return *preferred_source_id;
    }
  }

  for (size_t source_index = impl_->source_registry.Sources().size();
    source_index-- > 0;) {
    if (impl_->source_registry.Sources()[source_index]->HasAsset(key)) {
      return impl_->source_registry.SourceIds()[source_index];
    }
  }

  return std::nullopt;
}

auto AssetLoader::ResolveSourceForId(const uint16_t source_id) const
  -> const internal::IContentSource*
{
  const auto source_index_opt
    = impl_->source_registry.FindSourceIndexById(source_id);
  if (!source_index_opt.has_value()) {
    return nullptr;
  }
  const auto& source = impl_->source_registry.Sources().at(*source_index_opt);
  return source.get();
}

auto AssetLoader::MintSyntheticTextureKey() -> ResourceKey
{
  const uint32_t synthetic_index
    = next_synthetic_texture_index_.fetch_add(1, std::memory_order_relaxed);
  const auto resource_type_index = static_cast<uint16_t>(
    IndexOf<data::TextureResource, ResourceTypeList>::value);
  return PackResourceKey(kSyntheticSourceId, resource_type_index,
    pak::core::ResourceIndexT { synthetic_index });
}

auto AssetLoader::MintSyntheticBufferKey() -> ResourceKey
{
  const uint32_t synthetic_index
    = next_synthetic_buffer_index_.fetch_add(1, std::memory_order_relaxed);
  const auto resource_type_index = static_cast<uint16_t>(
    IndexOf<data::BufferResource, ResourceTypeList>::value);
  return PackResourceKey(kSyntheticSourceId, resource_type_index,
    pak::core::ResourceIndexT { synthetic_index });
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto AssetLoader::DetectCycle(
  const data::AssetKey& start, const data::AssetKey& target) -> bool
{
#if !defined(NDEBUG)
  // Policy: debug-only structural guard. Release returns false unconditionally
  // and relies on import/authoring/CI acyclicity guarantees.
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
    if (const auto* deps = dependency_graph_->FindAssetDependencies(current);
      deps != nullptr) {
      for (const auto& dep : *deps) {
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

#if !defined(NDEBUG)
auto AssetLoader::GetDebugAssetDependencyMap() const
  -> const DebugAssetDependencyMap&
{
  return dependency_graph_->AssetDependencies();
}
#endif

//=== Explicit Template Instantiations =======================================//

template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::BufferResource>(
    oxygen::content::ResourceKey)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::BufferResource>>;
template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::BufferResource>(
    oxygen::content::ResourceKey, oxygen::content::LoadRequest)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::BufferResource>>;

template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::TextureResource>(
    oxygen::content::ResourceKey)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::TextureResource>>;
template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::TextureResource>(
    oxygen::content::ResourceKey, oxygen::content::LoadRequest)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::TextureResource>>;

template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::ScriptResource>(
    oxygen::content::ResourceKey)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::ScriptResource>>;
template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::ScriptResource>(
    oxygen::content::ResourceKey, oxygen::content::LoadRequest)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::ScriptResource>>;

template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::PhysicsResource>(
    oxygen::content::ResourceKey)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::PhysicsResource>>;
template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::PhysicsResource>(
    oxygen::content::ResourceKey, oxygen::content::LoadRequest)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::PhysicsResource>>;

//=== Hash Key Generation ====================================================//

auto AssetLoader::HashAssetKey(const data::AssetKey& key) -> uint64_t
{
  const auto hash = std::hash<data::AssetKey> {}(key);

#if !defined(NDEBUG)
  {
    const std::scoped_lock lock(g_asset_hash_mutex);
    auto [collision_it, inserted] = g_asset_hash_to_key.emplace(hash, key);
    if (!inserted && collision_it->second != key) {
      LOG_F(WARNING,
        "AssetKey hash collision detected: hash=0x{:016x} existing={} new={} "
        "(cache aliasing risk)",
        hash, collision_it->second, key);
    }
  }
#endif

  return hash;
}

auto AssetLoader::HashAssetKey(
  const data::AssetKey& key, uint16_t source_id) const -> uint64_t
{
  const auto source_it
    = impl_->source_registry.SourceIdToIndex().find(source_id);
  if (source_it == impl_->source_registry.SourceIdToIndex().end()) {
    return HashAssetKey(key);
  }
  const auto& source = *impl_->source_registry.Sources()[source_it->second];
  size_t seed = 0;
  oxygen::HashCombine(seed, source.GetSourceKey());
  oxygen::HashCombine(seed, key);
  return static_cast<uint64_t>(seed);
}

auto AssetLoader::AssertSourceKeyConsistency(std::string_view context) const
  -> void
{
  impl_->source_registry.AssertStructuralConsistency(context);
  asset_identity_index_->AssertConsistency(context,
    impl_->source_registry.SourceIdToIndex(),
    [this](const data::AssetKey& key, const uint16_t source_id) {
      return HashAssetKey(key, source_id);
    });
}

auto AssetLoader::AssertDependencyEdgeRefcountSymmetry(
  std::string_view context) const -> void
{
  dependency_graph_->AssertEdgeRefcountSymmetry(
    context,
    [this](const data::AssetKey& dep_key) -> std::optional<uint64_t> {
      if (const auto dep_identity = ResolveAssetIdentityForKey(dep_key);
        dep_identity.has_value()) {
        return dep_identity->hash_key;
      }
      return std::nullopt;
    },
    [this](const data::AssetKey& dep_key) { return HashAssetKey(dep_key); },
    [this](const ResourceKey res_key) { return HashResourceKey(res_key); },
    [this](const uint64_t hash) -> uint32_t {
      const auto count = content_cache_.GetCheckoutCount(hash);
      if (count
        > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
        return (std::numeric_limits<uint32_t>::max)();
      }
      return static_cast<uint32_t>(count);
    });
}

auto AssetLoader::AssertMountStateResetCompleteness(std::string_view context,
  const bool expect_dependency_graphs_empty) const -> void
{
#if !defined(NDEBUG)
  const auto& asset_key_by_hash = asset_identity_index_->AssetKeyByHash();
  const auto& asset_source_id_by_hash
    = asset_identity_index_->AssetSourceIdByHash();
  const auto& asset_hash_by_key_and_source
    = asset_identity_index_->AssetHashByKeyAndSource();

  if (resource_key_registry_->Empty() != asset_key_by_hash.empty()) {
    LOG_F(ERROR,
      "[invariant:{}] cache key maps not reset symmetrically: "
      "resource_key_by_hash={} asset_key_by_hash={}",
      context, resource_key_registry_->Size(), asset_key_by_hash.size());
  }
  if (asset_key_by_hash.empty() != asset_source_id_by_hash.empty()) {
    LOG_F(ERROR,
      "[invariant:{}] asset key/source maps not reset symmetrically: "
      "asset_key_by_hash={} asset_source_id_by_hash={}",
      context, asset_key_by_hash.size(), asset_source_id_by_hash.size());
  }
  if (asset_key_by_hash.empty() != asset_hash_by_key_and_source.empty()) {
    LOG_F(ERROR,
      "[invariant:{}] reverse asset hash index not reset symmetrically: "
      "asset_key_by_hash={} asset_hash_by_key_and_source={}",
      context, asset_key_by_hash.size(), asset_hash_by_key_and_source.size());
  }
  if (expect_dependency_graphs_empty) {
    if (!dependency_graph_->AssetDependencies().empty()
      || !dependency_graph_->ResourceDependencies().empty()) {
      LOG_F(ERROR,
        "[invariant:{}] dependency graphs expected empty but are not: "
        "asset_edges={} resource_edges={}",
        context, dependency_graph_->AssetDependencies().size(),
        dependency_graph_->ResourceDependencies().size());
    }
  }
  AssertResourceMappingConsistency(context);
#else
  static_cast<void>(context);
  static_cast<void>(expect_dependency_graphs_empty);
#endif
}

auto AssetLoader::AssertResourceMappingConsistency(
  std::string_view context) const -> void
{
  resource_key_registry_->AssertConsistency(
    context, [this](const ResourceKey key) { return HashResourceKey(key); },
    [this](const uint64_t hash) { return content_cache_.Contains(hash); });
}

auto AssetLoader::HashResourceKey(const ResourceKey& key) const -> uint64_t
{
  internal::InternalResourceKey internal_key(key);
  const auto source_id = internal_key.GetPakIndex();

  // Special case for synthetic keys (SourceID == kSyntheticSourceId)
  if (source_id == constants::kSyntheticSourceId) {
    return std::hash<ResourceKey> {}(key);
  }

  // Look up source
  const auto source_it
    = impl_->source_registry.SourceIdToIndex().find(source_id);
  if (source_it == impl_->source_registry.SourceIdToIndex().end()) {
    // Source not found? This shouldn't happen for valid keys.
    LOG_F(ERROR, "HashResourceKey: SourceID {} not found", source_id);
    return std::hash<ResourceKey> {}(key);
  }

  const auto& source = *impl_->source_registry.Sources()[source_it->second];
  const auto source_key = source.GetSourceKey();

  // Hash(SourceGUID, Type, Index)
  size_t seed = 0;
  oxygen::HashCombine(seed, source_key);
  // We manually only include the resource type and the index in the hashing to
  // guarantee a stable hash. The source id is not stable, as it depends on the
  // load order.
  oxygen::HashCombine(seed, internal_key.GetResourceTypeIndex());
  oxygen::HashCombine(seed, internal_key.GetResourceIndex());

  const auto hash = static_cast<uint64_t>(seed);

#if !defined(NDEBUG)
  {
    const std::scoped_lock lock(impl_->hash_collision_mutex);
    const ResourceCompositeKey composite {
      .source_key = source_key,
      .resource_type_index = internal_key.GetResourceTypeIndex(),
      .resource_index = internal_key.GetResourceIndex(),
    };
    auto [it, inserted] = impl_->resource_hash_to_key.emplace(hash, composite);
    if (!inserted && it->second != composite) {
      LOG_F(WARNING,
        "ResourceKey hash collision detected: hash=0x{:016x} "
        "existing=(source={} type={} index={}) new=(source={} type={} "
        "index={}) "
        "(cache aliasing risk)",
        hash, it->second.source_key, it->second.resource_type_index,
        it->second.resource_index, composite.source_key,
        composite.resource_type_index, composite.resource_index);
    }
  }
#endif

  return hash;
}
