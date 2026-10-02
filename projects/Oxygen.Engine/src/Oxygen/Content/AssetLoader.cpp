//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/EnumIndexedArray.h>
#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Base/TypeList.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Console/CVar.h>
#include <Oxygen/Console/Command.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/Internal/ContentBindingBundle.h>
#include <Oxygen/Content/Internal/ContentIdentity.h>
#include <Oxygen/Content/Internal/ContentIdentityRegistry.h>
#include <Oxygen/Content/Internal/ContentLoadScopeState.h>
#include <Oxygen/Content/Internal/ContentReleaseQueue.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/ContentSourceView.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/EvictionRegistry.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/InFlightOperationTable.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Content/Internal/PakFileSource.h>
#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Content/Internal/PhysicsBindings.h>
#include <Oxygen/Content/Internal/ResourceLoadPipeline.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/Internal/SceneCatalogQueryService.h>
#include <Oxygen/Content/Internal/ScriptHotReloadService.h>
#include <Oxygen/Content/Internal/ScriptQueryService.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/LoaderFunctions.h>
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
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResidencyPin.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Content/TextureResourceLocator.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/CachePolicyContract.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/RefCountedEviction.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/ParkingLot.h>
#include <Oxygen/OxCo/TaskCancelledException.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Serio/FileStream.h>
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

} // namespace

namespace oxygen::content {

// Implement the private helper declared in the header to avoid exposing the
// internal header in the public API.
auto AssetLoader::InternResourceKey(const data::SourceInstanceId source,
  const uint16_t resource_type_index, pak::core::ResourceIndexT resource_index)
  -> ResourceKey
{
  AssertOwningThread();
  const auto id = identities_->Intern(internal::CookedResourceIdentity {
    .source = source,
    .kind = static_cast<internal::ResourceKind>(resource_type_index),
    .index = resource_index.get(),
  });
  return ResourceKey { id.get() };
}

struct AssetLoader::Impl final {
  internal::ContentSourceRegistry source_registry {};
  uint64_t mount_revision = 0;
  std::weak_ptr<const internal::ContentLoadScopeState> load_scope {};
  std::optional<internal::ContentReleaseQueue::Cache::EvictionNotificationScope>
    cache_notifications;

  // These lifetimes span direct awaits, queued callbacks and their delivery,
  // independently of the shared I/O table that Stop clears for cancellation.
  size_t accepted_loads = 0;
  co::ParkingLot accepted_loads_idle;
};

namespace internal {

  struct MountReplacementState final {
    struct Notification final {
      uint64_t cache_key = 0;
      EvictionEvent event {};
    };

    observer_ptr<AssetLoader> owner {};
    std::weak_ptr<int> lifetime {};
    std::shared_ptr<ContentReleaseQueue> epoch {};
    uint64_t revision = 0;
    ContentSourceRegistry registry {};
    std::vector<ContentReleaseQueue::Cache::RetiredEntry> retired {};
    std::vector<Notification> notifications {};
    std::unordered_map<TypeId, std::vector<EvictionRegistry::Subscriber>>
      subscribers {};
  };

} // namespace internal

PreparedMountSet::PreparedMountSet(
  std::unique_ptr<internal::MountReplacementState> state)
  : state_(std::move(state))
{
}
PreparedMountSet::~PreparedMountSet() = default;
PreparedMountSet::PreparedMountSet(PreparedMountSet&&) noexcept = default;
auto PreparedMountSet::operator=(PreparedMountSet&&) noexcept
  -> PreparedMountSet& = default;

MountRetirement::MountRetirement(
  std::unique_ptr<internal::MountReplacementState> state)
  : state_(std::move(state))
{
}
MountRetirement::~MountRetirement() { Finish(); }
MountRetirement::MountRetirement(MountRetirement&&) noexcept = default;
auto MountRetirement::operator=(MountRetirement&& other) noexcept
  -> MountRetirement&
{
  if (this != &other) {
    Finish();
    state_ = std::move(other.state_);
  }
  return *this;
}

auto MountRetirement::Finish() noexcept -> void
{
  auto state = std::move(state_);
  if (state && !state->lifetime.expired() && !state->epoch->IsClosed()) {
    state->owner->CompleteMountRetirement(*state);
  }
}
// Guards synchronous callback boundaries without extending the loader lifetime.
struct AssetLoader::OperationLifetime final {
  explicit OperationLifetime(const AssetLoader& owner)
    : owner_(&owner)
    , alive_(owner.lifetime_token_)
    , epoch_(owner.releases_)
    , closed_(epoch_->IsClosed())
  {
  }
  explicit operator bool() const noexcept
  {
    return !alive_.expired() && owner_->releases_ == epoch_
      && epoch_->IsClosed() == closed_;
  }

private:
  observer_ptr<const AssetLoader> owner_;
  std::weak_ptr<int> alive_;
  std::shared_ptr<internal::ContentReleaseQueue> epoch_;
  bool closed_;
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
    if (ids.at(i) == type_id) {
      return i;
    }
  }
  throw std::runtime_error("Unknown resource type id for ResourceRef binding");
}

inline auto IsResourceTypeId(const oxygen::TypeId type_id) -> bool
{
  static const auto ids = MakeTypeIdArray(oxygen::content::ResourceTypeList {});
  return std::ranges::any_of(
    ids, [type_id](const auto id) -> auto { return id == type_id; });
}
} // namespace

namespace {
// Helper validates eviction callback arguments. Parameter ordering chosen to
// minimize misuse. expected_cache_key: hash originally computed for resource
// actual_cache_key: hash received from eviction callback
// type_id: type id reference for validation
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto SanityCheckResourceEviction(const uint64_t expected_cache_key,
  const uint64_t actual_cache_key, const oxygen::TypeId expected_type_id,
  const oxygen::TypeId actual_type_id) -> bool
{
  CHECK_EQ_F(expected_cache_key, actual_cache_key);
  CHECK_EQ_F(expected_type_id, actual_type_id);
  return true;
}

auto ReadInputMappingContextAssetDesc(
  const internal::IContentSource& source, const oxygen::data::AssetKey& key)
  -> std::optional<oxygen::data::pak::input::InputMappingContextAssetDesc>
{
  auto desc_reader = source.CreateAssetDescriptorReader(key);
  if (!desc_reader) {
    return std::nullopt;
  }

  auto blob = desc_reader->ReadBlob(
    sizeof(oxygen::data::pak::input::InputMappingContextAssetDesc));
  if (!blob
    || blob->size()
      < sizeof(oxygen::data::pak::input::InputMappingContextAssetDesc)) {
    return std::nullopt;
  }

  oxygen::data::pak::input::InputMappingContextAssetDesc desc {};
  std::memcpy(&desc, blob->data(), sizeof(desc));
  if (static_cast<oxygen::data::AssetType>(desc.header.asset_type)
    != oxygen::data::AssetType::kInputMappingContext) {
    return std::nullopt;
  }

  return desc;
}
} // namespace

//=== Basic methods ==========================================================//

AssetLoader::AssetLoader(
  engine::EngineTag /*tag*/, const AssetLoaderConfig& config)
  : impl_(std::make_unique<Impl>())
  , releases_(std::make_shared<internal::ContentReleaseQueue>())
  , thread_pool_(config.thread_pool)
  , work_offline_(config.work_offline)
  , verify_content_hashes_(config.verify_content_hashes)
  , residency_policy_(config.residency_policy)
  , eviction_registry_(std::make_unique<internal::EvictionRegistry>())
  , identities_(std::make_unique<internal::ContentIdentityRegistry>())
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

  resource_load_pipeline_ = std::make_unique<internal::ResourceLoadPipeline>(
    impl_->source_registry, *identities_, resource_loaders_, content_cache_,
    *in_flight_ops_, releases_, thread_pool_, work_offline_,
    internal::ResourceLoadPipeline::Callbacks {
      .assert_owning_thread = [this] -> void { AssertOwningThread(); },
      .on_resource_published = [this](const ResourceKey key) -> void {
        eviction_registry_->TrackResource(key);
      },
      .default_priority_class = [this] -> LoadPriorityClass {
        return residency_policy_.default_priority_class;
      },
      .next_request_sequence
      = [this] -> uint64_t { return next_load_request_sequence_.fetch_add(1); },
      .on_resource_request = [this](const TypeId type_id) -> void {
        RecordResourceTelemetry(type_id, LoadTelemetryEvent::kRequest);
      },
      .on_resource_cache_hit = [this](const TypeId type_id) -> void {
        RecordResourceTelemetry(type_id, LoadTelemetryEvent::kCacheHit);
      },
      .on_resource_cache_miss = [this](const TypeId type_id) -> void {
        RecordResourceTelemetry(type_id, LoadTelemetryEvent::kCacheMiss);
      },
      .on_resource_joined_inflight = [this](const TypeId type_id) -> void {
        RecordResourceTelemetry(type_id, LoadTelemetryEvent::kTasksDeduped);
      },
      .on_resource_started_inflight = [this](const TypeId type_id) -> void {
        RecordResourceTelemetry(type_id, LoadTelemetryEvent::kTasksSpawned);
      },
      .on_resource_decode_failure = [this](const TypeId type_id) -> void {
        RecordResourceTelemetry(type_id, LoadTelemetryEvent::kDecodeFailure);
      },
      .on_resource_type_mismatch = [this](const TypeId type_id) -> void {
        RecordResourceTelemetry(type_id, LoadTelemetryEvent::kTypeMismatch);
      },
      .on_resource_store_retry_failed = [this](const TypeId type_id) -> void {
        RecordResourceTelemetry(
          type_id, LoadTelemetryEvent::kStoreRetryFailure);
      },
      .on_store_pressure
      = [this](const std::string_view trigger, const bool force) -> void {
        MaybeAutoTrimOnBudgetPressure(trigger, force);
      },
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
  impl_->cache_notifications.emplace(
    content_cache_.OnEviction([this](const auto& retired) {
      RetireCacheEntry(retired, EvictionReason::kRefCountZero);
    }));
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
  return kEventNames.at(event);
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
  return kMetricNames.at(metric);
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
  if (const auto metric = kEventCounters.at(event); metric.has_value()) {
    ++counters.at(*metric);
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
    stats.material_assets.at(requests) + stats.geometry_assets.at(requests)
      + stats.scene_assets.at(requests)
      + stats.physics_scene_assets.at(requests)
      + stats.script_assets.at(requests)
      + stats.input_action_assets.at(requests)
      + stats.input_mapping_context_assets.at(requests),
    stats.texture_resources.at(requests) + stats.buffer_resources.at(requests)
      + stats.script_resources.at(requests)
      + stats.physics_resources.at(requests),
    stats.material_assets.at(cache_hits) + stats.geometry_assets.at(cache_hits)
      + stats.scene_assets.at(cache_hits)
      + stats.physics_scene_assets.at(cache_hits)
      + stats.script_assets.at(cache_hits)
      + stats.input_action_assets.at(cache_hits)
      + stats.input_mapping_context_assets.at(cache_hits),
    stats.texture_resources.at(cache_hits)
      + stats.buffer_resources.at(cache_hits)
      + stats.script_resources.at(cache_hits)
      + stats.physics_resources.at(cache_hits));
  (void)console_->SetCVarFromText(
    { .name = std::string(kCVarLastStats), .text = summary },
    {
      .source = console::CommandSource::kAutomation,
      .shipping_build = false,
      .record_history = false,
    });
}

AssetLoader::~AssetLoader()
{
  lifetime_token_.reset();
  eviction_alive_token_.reset();
  impl_->cache_notifications.reset();
  releases_->Close();
  static_cast<void>(
    releases_->Drain(content_cache_, std::numeric_limits<size_t>::max()));
}

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
  if (releases_->IsClosed()) {
    const OperationLifetime operation(*this);
    eviction_registry_->Clear();
    if (!operation) {
      return;
    }
    script_hot_reload_service_->Reset();
    if (!operation) {
      return;
    }
    releases_ = std::make_shared<internal::ContentReleaseQueue>();
    eviction_alive_token_ = std::make_shared<int>(0);
  }
  in_flight_ops_->Open();
}

void AssetLoader::Stop()
{
  const auto releases = releases_;
  if (releases->IsClosed()) {
    return;
  }
  releases->Close();
  const OperationLifetime operation(*this);
  in_flight_ops_->Close();
  if (!operation) {
    return;
  }
  if (nursery_) {
    nursery_->Cancel();
  }
  if (!operation) {
    return;
  }
  {
    auto eviction_guard
      = content_cache_.OnEviction([this, operation](const auto& retired) {
          if (operation) {
            RetireCacheEntry(retired, EvictionReason::kShutdown);
          }
        });
    content_cache_.Clear();
  }
  if (!operation) {
    return;
  }
  FlushResourceEvictionsForUncachedMappings(EvictionReason::kShutdown, true);
  if (!operation) {
    return;
  }
  eviction_registry_->Clear();
  if (!operation) {
    return;
  }
  script_hot_reload_service_->Reset();
  if (!operation) {
    return;
  }
  eviction_alive_token_.reset();
  static_cast<void>(
    releases->Drain(content_cache_, std::numeric_limits<size_t>::max()));
}

auto AssetLoader::IsRunning() const -> bool { return nursery_ != nullptr; }

auto AssetLoader::BeginLoadScope() -> ContentLoadScope
{
  AssertOwningThread();
  releases_->RequireOpen();
  const auto view = impl_->source_registry.CaptureView();
  auto state = impl_->load_scope.lock();
  if (!state || state->view != view || state->epoch.lock() != releases_) {
    state = std::make_shared<const internal::ContentLoadScopeState>(
      internal::ContentLoadScopeState { .view = view, .epoch = releases_ });
    impl_->load_scope = state;
  }
  ContentLoadScope scope;
  scope.state_ = std::move(state);
  return scope;
}

auto AssetLoader::AdmitAssetRequest(LoadRequest request) -> LoadRequest
{
  AssertOwningThread();
  if (!request.scope.state_) {
    request.scope = BeginLoadScope();
  }
  const auto epoch = request.scope.state_->epoch.lock();
  if (!epoch || epoch != releases_) {
    throw std::invalid_argument(
      "Content load scope belongs to another loader or stopped lifetime");
  }
  epoch->RequireOpen();
  return request;
}

auto AssetLoader::ResolveScopedRoot(const data::AssetKey& key,
  const std::optional<data::SourceKey> source_key,
  const ContentLoadScope& scope) const -> std::optional<data::SourceInstanceId>
{
  const auto& view = scope.state_->view;
  std::optional<data::SourceInstanceId> selected;
  if (source_key) {
    bool present = false;
    if (view) {
      for (const auto& layer : view->Layers()) {
        if (layer.source->GetSourceKey() != *source_key) {
          continue;
        }
        if (present) {
          return std::nullopt;
        }
        present = true;
        if (layer.source->HasAsset(key)) {
          selected = layer.id;
        }
      }
    }
    if (!present) {
      selected = ResolveExactSourceId(key, *source_key);
    }
  } else if (view) {
    selected = view->ResolveAsset(key);
  }
  return selected && ResolveSourceForId(*selected) ? selected : std::nullopt;
}

auto AssetLoader::AddPakFile(const std::filesystem::path& path) -> void
{
  (void)MountPakFile(path);
}

auto AssetLoader::MountPakFile(const std::filesystem::path& path)
  -> data::SourceInstanceId
{
  const OperationLifetime operation(*this);
  AssertOwningThread();
  std::error_code ec {};
  auto normalized = base::ToLogicalPath(
    std::filesystem::weakly_canonical(base::ToNativePath(path), ec));
  if (ec) {
    normalized = path.lexically_normal();
  }

  auto new_source = std::make_unique<internal::PakFileSource>(
    normalized, verify_content_hashes_);
  const auto source_key = new_source->GetSourceKey();
  const bool was_already_mounted
    = std::ranges::find(impl_->source_registry.PakPaths(), normalized)
    != impl_->source_registry.PakPaths().end();
#ifndef NDEBUG
  {
    if (source_key.IsNil()) {
      LOG_F(WARNING,
        "Mounted PAK has zero SourceKey (PakHeader.source_identity); cache "
        "aliasing risk: "
        "path={}",
        normalized.string());
    }
  }
#endif

  const auto mount_result
    = impl_->source_registry.MountPak(normalized, std::move(new_source));
  ++impl_->mount_revision;
#ifndef NDEBUG
  if (mount_result.source_key_conflict.has_value()) {
    const auto& conflict = *mount_result.source_key_conflict;
    LOG_F(WARNING,
      "Mounted PAK shares SourceKey with a different mounted source; cache "
      "aliasing risk: source_key={} new_path={} existing_source_id={} "
      "existing_path={}",
      conflict.source_key, normalized.string(), conflict.existing_source_id,
      conflict.existing_mount_identity);
  }
#endif
  if (mount_result.action
      == internal::ContentSourceRegistry::MountAction::kRefreshed
    || was_already_mounted) {
    LOG_F(INFO,
      "Refreshing mounted PAK content source: id={} path={} (reloading pak)",
      mount_result.source_id, normalized.string());

    {
      auto eviction_guard
        = content_cache_.OnEviction([&](const auto& retired) -> void {
            if (operation) {
              RetireCacheEntry(retired, EvictionReason::kClear);
            }
          });
      content_cache_.Clear();
      if (!operation) {
        throw OperationCancelledException("PAK refresh was superseded");
      }
    }
    FlushResourceEvictionsForUncachedMappings(EvictionReason::kClear, true);
    if (!operation) {
      throw OperationCancelledException("PAK refresh was superseded");
    }
    identities_->EraseSources(impl_->source_registry.PruneExpiredSources());

    AssertSourceKeyConsistency("AddPakFile.refresh");
    AssertResourceMappingConsistency("AddPakFile.refresh");
    return mount_result.source_id;
  }

  LOG_F(INFO, "Mounted PAK content source: id={} path={}",
    mount_result.source_id, normalized.string());
  AssertSourceKeyConsistency("AddPakFile.mount");
  return mount_result.source_id;
}

auto AssetLoader::AddLooseCookedRoot(const std::filesystem::path& path) -> void
{
  AssertOwningThread();
  if (std::filesystem::exists(base::ToNativePath(
        path / data::loose_cooked::kGenerationLeaseFileName))) {
    static_cast<void>(MountLooseCookedGeneration(path));
    return;
  }
  std::filesystem::path normalized = base::ToLogicalPath(
    std::filesystem::weakly_canonical(base::ToNativePath(path)));
  const auto normalized_s = normalized.string();

  auto new_source = std::make_unique<internal::LooseCookedSource>(normalized,
    verify_content_hashes_
      ? internal::LooseCookedSource::OpenMode::kVerifyContent
      : internal::LooseCookedSource::OpenMode::kValidateMetadata);
  const auto source_key = new_source->GetSourceKey();

  auto clear_content_caches = [this] -> void {
    const OperationLifetime operation(*this);
    auto eviction_guard
      = content_cache_.OnEviction([this, operation](const auto& retired) {
          if (operation) {
            RetireCacheEntry(retired, EvictionReason::kClear);
          }
        });
    content_cache_.Clear();
    if (!operation) {
      return;
    }
    FlushResourceEvictionsForUncachedMappings(EvictionReason::kClear, true);
    if (!operation) {
      return;
    }
    identities_->EraseSources(impl_->source_registry.PruneExpiredSources());
    AssertSourceKeyConsistency("AddLooseCookedRoot.refresh");
    AssertResourceMappingConsistency("AddLooseCookedRoot.refresh");
  };

  const bool was_already_mounted = std::ranges::any_of(
    impl_->source_registry.Sources(),
    [&](const auto& s) -> auto { return s && s->DebugName() == normalized_s; });
#ifndef NDEBUG
  {
    if (source_key.IsNil()) {
      LOG_F(WARNING,
        "Mounted loose cooked root has zero SourceKey "
        "(IndexHeader.source_identity); "
        "source-key lookup is ambiguous: root={}",
        normalized.string());
    }
  }
#endif

  const auto mount_result
    = impl_->source_registry.MountLoose(normalized_s, std::move(new_source));
  ++impl_->mount_revision;
#ifndef NDEBUG
  if (mount_result.source_key_conflict.has_value()) {
    const auto& conflict = *mount_result.source_key_conflict;
    LOG_F(WARNING,
      "Mounted loose cooked root shares SourceKey with a different mounted "
      "source; source-key lookup is ambiguous: source_key={} new_root={} "
      "existing_source_id={} existing_mount={}",
      conflict.source_key, normalized.string(), conflict.existing_source_id,
      conflict.existing_mount_identity);
  }
#endif
  if (mount_result.action
      == internal::ContentSourceRegistry::MountAction::kRefreshed
    || was_already_mounted) {
    LOG_F(INFO,
      "Refreshing loose cooked content source: root={} (reloading index)",
      normalized_s);
    clear_content_caches();
    return;
  }

  LOG_F(INFO, "Mounted loose cooked content source: id={} root={}",
    mount_result.source_id, normalized.string());
  AssertSourceKeyConsistency("AddLooseCookedRoot.mount");
}

auto AssetLoader::MountLooseCookedGeneration(const std::filesystem::path& path,
  const std::optional<data::SourceKey> replaces) -> data::SourceKey
{
  AssertOwningThread();
  const auto normalized = base::ToLogicalPath(
    std::filesystem::weakly_canonical(base::ToNativePath(path)));
  auto lock = serio::FileLock::TryAcquire(
    normalized / data::loose_cooked::kGenerationLeaseFileName,
    serio::FileLockMode::kShared);
  if (!lock) {
    throw std::system_error(lock.error(), "Acquire cooked generation lease");
  }
  auto source = std::make_shared<internal::LooseCookedSource>(normalized,
    verify_content_hashes_
      ? internal::LooseCookedSource::OpenMode::kVerifyContent
      : internal::LooseCookedSource::OpenMode::kValidateMetadata,
    std::move(lock).value());
  const auto key = source->GetSourceKey();
  static_cast<void>(
    impl_->source_registry.MountGeneration(std::move(source), replaces));
  ++impl_->mount_revision;
  AssertSourceKeyConsistency("MountLooseCookedGeneration");
  return key;
}

auto AssetLoader::RetireLooseCookedGeneration(const data::SourceKey source_key)
  -> bool
{
  AssertOwningThread();
  if (!impl_->source_registry.RetireGeneration(source_key)) {
    return false;
  }
  ++impl_->mount_revision;
  return true;
}

auto AssetLoader::PrepareLooseCookedRootsAsync(
  std::vector<std::filesystem::path> roots) -> co::Co<PreparedMountSet>
{
  AssertOwningThread();
  if (!thread_pool_ || releases_->IsClosed()) {
    throw std::logic_error(
      "Mount preparation requires an active loader and thread pool");
  }
  auto state = std::make_unique<internal::MountReplacementState>();
  state->owner = observer_ptr(this);
  state->lifetime = lifetime_token_;
  state->epoch = releases_;
  state->revision = impl_->mount_revision;
  return PrepareMountSetAsync(
    std::move(state), thread_pool_, std::move(roots), verify_content_hashes_);
}

auto AssetLoader::PrepareMountSetAsync(
  std::unique_ptr<internal::MountReplacementState> state,
  const observer_ptr<co::ThreadPool> pool,
  std::vector<std::filesystem::path> roots, const bool verify_content)
  -> co::Co<PreparedMountSet>
{
  if (state->lifetime.expired() || state->epoch->IsClosed()) {
    throw OperationCancelledException(
      "Loader stopped before mount preparation began");
  }
  state->owner->AssertOwningThread();
  if (state->epoch != state->owner->releases_
    || state->revision != state->owner->impl_->mount_revision) {
    throw OperationCancelledException(
      "Content mounts changed before preparation began");
  }
  auto sources = co_await pool->Run([paths = std::move(roots),
                                      verify = verify_content] {
    std::vector<internal::ContentSourceRegistry::PreparedSource> prepared;
    prepared.reserve(paths.size());
    for (const auto& path : paths) {
      const auto normalized = base::ToLogicalPath(
        std::filesystem::weakly_canonical(base::ToNativePath(path)));
      const auto marker
        = normalized / data::loose_cooked::kGenerationLeaseFileName;
      if (std::filesystem::exists(base::ToNativePath(marker))) {
        auto lease
          = serio::FileLock::TryAcquire(marker, serio::FileLockMode::kShared);
        if (!lease) {
          throw std::system_error(
            lease.error(), "Acquire candidate generation lease");
        }
        prepared.push_back(
          { .source = std::make_shared<internal::LooseCookedSource>(normalized,
              verify ? internal::LooseCookedSource::OpenMode::kVerifyContent
                     : internal::LooseCookedSource::OpenMode::kValidateMetadata,
              std::move(lease).value()),
            .generation = true });
      } else {
        prepared.push_back({ .source
          = std::make_shared<internal::LooseCookedSource>(normalized,
            verify ? internal::LooseCookedSource::OpenMode::kVerifyContent
                   : internal::LooseCookedSource::OpenMode::kValidateMetadata),
          .generation = false });
      }
    }
    return prepared;
  });
  if (state->lifetime.expired() || state->epoch->IsClosed()) {
    throw OperationCancelledException(
      "Loader stopped during mount preparation");
  }
  auto& owner = *state->owner;
  owner.AssertOwningThread();
  if (state->epoch != owner.releases_
    || state->revision != owner.impl_->mount_revision) {
    throw OperationCancelledException(
      "Content mounts changed during preparation");
  }
  owner.identities_->EraseSources(
    owner.impl_->source_registry.PruneExpiredSources());
  state->registry = owner.impl_->source_registry.PrepareReplacement(sources);
  co_return PreparedMountSet(std::move(state));
}

auto AssetLoader::CommitPreparedMounts(PreparedMountSet&& prepared)
  -> MountRetirement
{
  AssertOwningThread();
  if (!prepared.state_ || prepared.state_->owner.get() != this) {
    throw std::invalid_argument(
      "Prepared content mounts belong to another loader or were consumed");
  }
  if (prepared.state_->lifetime.expired() || prepared.state_->epoch != releases_
    || releases_->IsClosed()
    || prepared.state_->revision != impl_->mount_revision) {
    throw OperationCancelledException(
      "Prepared content mounts no longer match this loader");
  }
  if (impl_->accepted_loads != 0U) {
    throw std::logic_error(
      "Drain accepted loads before committing content mounts");
  }
  auto owned = std::move(prepared.state_);
  auto& state = *owned;
  const auto keys = content_cache_.KeysSnapshot();
  state.retired.reserve(keys.size());
  state.notifications.reserve(
    keys.size() + eviction_registry_->TrackedResources().size());
  const auto prepare_event = [&](const uint64_t key, const TypeId type) {
    EvictionEvent event {
      .asset_key = {},
      .key = {},
      .type_id = type,
      .reason = EvictionReason::kClear,
#ifndef NDEBUG
      .cache_key = key,
#endif
    };
    if (IsResourceTypeId(type)) {
      event.key = ResourceKey { key };
    } else {
      const auto* identity
        = identities_->FindAsset(internal::ContentId { key });
      if (identity == nullptr) {
        throw std::logic_error("Cached asset has no source-qualified identity");
      }
      event.asset_key = identity->asset;
    }
    if (!state.subscribers.contains(type)) {
      state.subscribers.emplace(
        type, eviction_registry_->SnapshotSubscribers(type));
    }
    state.notifications.push_back({ .cache_key = key, .event = event });
  };
  for (const auto key : keys) {
    prepare_event(key, content_cache_.GetTypeId(key));
  }
  for (const auto key : eviction_registry_->TrackedResources()) {
    if (!content_cache_.Contains(key.get())) {
      if (const auto kind
        = identities_->FindResourceKind(internal::ContentId { key.get() })) {
        prepare_event(
          key.get(), GetResourceTypeIdByIndex(static_cast<size_t>(*kind)));
      }
    }
  }
  {
    auto collect = content_cache_.OnEviction(
      [&state](const auto& retired) { state.retired.push_back(retired); });
    content_cache_.Clear();
  }
  // All allocations and validation precede this publication point. Payloads and
  // callbacks remain owned until the caller has switched its peer resolver.
  impl_->source_registry.Swap(state.registry);
  ++impl_->mount_revision;
  for (const auto& notification : state.notifications) {
    RecordEviction(notification.event.reason);
    if (!notification.event.asset_key.has_value()) {
      eviction_registry_->ForgetResource(notification.event.key);
    }
  }
  return MountRetirement(std::move(owned));
}

auto AssetLoader::CompleteMountRetirement(
  internal::MountReplacementState& state) noexcept -> void
{
  AssertOwningThread();
  if (state.epoch != releases_ || releases_->IsClosed()) {
    return;
  }
  const OperationLifetime operation(*this);
  for (const auto& notification : state.notifications) {
    internal::EvictionRegistry::ActiveEviction active {
      .key = notification.cache_key, .previous = nullptr
    };
    if (!eviction_registry_->TryEnterEviction(active)) {
      continue;
    }
    const auto leave = Finally([this, &active, operation] noexcept {
      if (operation) {
        eviction_registry_->ExitEviction(active);
      }
    });
    const auto& subscribers = state.subscribers.at(notification.event.type_id);
    for (const auto& subscriber : subscribers) {
      if (!subscriber.handler
        || !eviction_registry_->IsSubscribed(
          notification.event.type_id, subscriber.id)) {
        continue;
      }
      try {
        subscriber.handler(notification.event);
      } catch (const std::exception& error) {
        LOG_F(ERROR, "Eviction observer failed after mount publication: {}",
          error.what());
      } catch (...) {
        LOG_F(ERROR, "Eviction observer failed after mount publication");
      }
      if (!operation) {
        return;
      }
    }
  }
}

auto AssetLoader::EnsureLoadEpoch(
  const std::shared_ptr<internal::ContentReleaseQueue>& epoch) const -> void
{
  epoch->RequireOpen();
  if (epoch != releases_) {
    throw OperationCancelledException(
      "Content request belongs to a retired loader epoch");
  }
}

auto AssetLoader::BeginAcceptedLoad() -> void
{
  AssertOwningThread();
  ++impl_->accepted_loads;
}

auto AssetLoader::EndAcceptedLoad() noexcept -> void
{
  if (impl_->accepted_loads == 0U) {
    std::terminate(); // A duplicate ticket release violates loader lifetime.
  }
  --impl_->accepted_loads;
  if (impl_->accepted_loads == 0U) {
    try {
      impl_->accepted_loads_idle.UnParkAll();
    } catch (...) {
      // Ticket finalization cannot propagate a parking-lot invariant failure.
      std::terminate();
    }
  }
}

auto AssetLoader::WaitForPendingLoadsAsync() -> co::Co<>
{
  AssertOwningThread();
  for (;;) {
    co_await in_flight_ops_->WaitUntilEmpty();
    if (impl_->accepted_loads == 0U) {
      co_return;
    }
    co_await impl_->accepted_loads_idle.Park();
  }
}

auto AssetLoader::ClearMounts() -> void
{
  const OperationLifetime operation(*this);
  LOG_F(INFO, "AssetLoader::ClearMounts thread={} owner={}",
    std::hash<std::thread::id> {}(std::this_thread::get_id()),
    std::hash<std::thread::id> {}(owning_thread_id_));
  AssertOwningThread(); // Ensure this method is called on the owning thread
  impl_->source_registry.Clear();
  ++impl_->mount_revision;

  // Clear the content cache to prevent stale assets from being returned
  // when switching content sources (e.g. scene swap).
  {
    auto eviction_guard
      = content_cache_.OnEviction([&](const auto& retired) -> void {
          if (operation) {
            RetireCacheEntry(retired, EvictionReason::kClear);
          }
        });
    content_cache_.Clear();
    if (!operation) {
      return;
    }
  }
  FlushResourceEvictionsForUncachedMappings(EvictionReason::kClear, true);
  if (!operation) {
    return;
  }

  identities_->EraseSources(impl_->source_registry.PruneExpiredSources());

  AssertSourceKeyConsistency("ClearMounts");
  AssertResourceMappingConsistency("ClearMounts");
}

auto AssetLoader::ExecuteTrimPass(
  const std::string_view trigger, const bool automatic) -> void
{
  LOG_F(INFO, "trim start trigger={} automatic={} thread={} owner={}", trigger,
    automatic, std::hash<std::thread::id> {}(std::this_thread::get_id()),
    std::hash<std::thread::id> {}(owning_thread_id_));
  AssertOwningThread();
  const auto releases = releases_;
  const OperationLifetime operation(*this);
  static_cast<void>(
    releases->Drain(content_cache_, std::numeric_limits<size_t>::max()));
  if (!operation) {
    return;
  }
  RecordTrimAttempt(trigger, automatic);
  ++trim_telemetry_.attempts;
  const auto before = content_cache_.SnapshotStats();

  auto eviction_guard
    = content_cache_.OnEviction([&](const auto& retired) -> void {
        if (operation) {
          RetireCacheEntry(retired, EvictionReason::kTrim);
        }
      });

  struct TrimResult final {
    size_t trim_roots = 0;
    size_t orphan_resources = 0;
    size_t pruned_live_branches = 0;
    size_t blocked_priority_roots = 0;
  } trim_result;
  for (;;) {
    size_t removed = 0;
    for (const auto key : content_cache_.KeysSnapshot()) {
      const auto type = content_cache_.GetTypeId(key);
      const bool evicted = content_cache_.Remove(key);
      if (!operation) {
        return;
      }
      if (evicted) {
        ++removed;
        if (IsResourceTypeId(type)) {
          ++trim_result.orphan_resources;
        } else {
          ++trim_result.trim_roots;
        }
      }
    }
    const auto returned
      = releases->Drain(content_cache_, std::numeric_limits<size_t>::max());
    if (!operation) {
      return;
    }
    if (removed == 0U && returned == 0U) {
      break;
    }
  }
  trim_result.blocked_priority_roots = content_cache_.Size();

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
  if (!operation) {
    return;
  }
  identities_->EraseSources(impl_->source_registry.PruneExpiredSources());
}

auto AssetLoader::MaybeAutoTrimOnBudgetPressure(
  const std::string_view trigger, const bool force) -> void
{
  AssertOwningThread();
  RecordStorePressureEvent(trigger, force);
  if (!force && !content_cache_.IsOverBudget()) {
    return;
  }
  const auto releases = releases_;
  const OperationLifetime operation(*this);
  static_cast<void>(
    releases->Drain(content_cache_, std::numeric_limits<size_t>::max()));
  if (!operation
    || residency_policy_.trim_mode != ResidencyTrimMode::kAutoOnOverBudget) {
    return;
  }
  ExecuteTrimPass(trigger, true);
}

auto AssetLoader::ProcessPendingReleases() -> void
{
  AssertOwningThread();
  const OperationLifetime operation(*this);
  constexpr std::size_t kReleaseRecordsPerFrame = 128;
  const auto releases = releases_;
  static_cast<void>(releases->Drain(content_cache_, kReleaseRecordsPerFrame));
  if (!operation || releases != releases_) {
    return;
  }
  static_cast<void>(identities_->ProcessExpiredViews(kReleaseRecordsPerFrame));
}

auto AssetLoader::TrimCache() -> void { ExecuteTrimPass("manual_trim", false); }

auto AssetLoader::EnumerateMountedScenes() const
  -> std::vector<IAssetLoader::MountedSceneEntry>
{
  AssertOwningThread();
  return scene_catalog_query_service_->EnumerateMountedScenes(
    impl_->source_registry);
}

auto AssetLoader::EnumerateMountedInputContexts() const
  -> std::vector<IAssetLoader::MountedInputContextEntry>
{
  AssertOwningThread();
  std::vector<IAssetLoader::MountedInputContextEntry> contexts;
  const auto view = impl_->source_registry.CaptureView();
  if (!view) {
    return contexts;
  }
  contexts.reserve(view->Layers().size());
  for (const auto& layer : view->Layers()) {
    const auto source = ResolveSourceForId(layer.id);
    if (!source) {
      continue;
    }

    const auto source_key = source->GetSourceKey();
    const auto asset_count = source->GetAssetCount();
    for (size_t i = 0; i < asset_count; ++i) {
      const auto asset_key_opt
        = source->GetAssetKeyByIndex(static_cast<uint32_t>(i));
      if (!asset_key_opt
        || source->GetAssetType(*asset_key_opt)
          != data::AssetType::kInputMappingContext
        || view->ResolveAsset(*asset_key_opt) != layer.id) {
        continue;
      }

      const auto desc_opt
        = ReadInputMappingContextAssetDesc(*source, *asset_key_opt);
      if (!desc_opt.has_value()) {
        continue;
      }

      IAssetLoader::MountedInputContextEntry entry {};
      entry.asset_key = *asset_key_opt;
      entry.source_key = source_key;
      const auto name = std::span(desc_opt->header.name);
      entry.name.assign(name.begin(), std::ranges::find(name, '\0'));
      entry.flags = desc_opt->flags;
      entry.default_priority = desc_opt->default_priority;
      contexts.push_back(std::move(entry));
    }
  }

  return contexts;
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
    const auto& source = sources.at(i);
    if (!source) {
      continue;
    }
    IAssetLoader::MountedSourceEntry entry {};
    entry.source_key = source->GetSourceKey();
    entry.source_id = source_ids.at(i);
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
    .default_value = false,
    .flags = console::CVarFlags::kArchive,
  }, console::CVarRegistrationOptions {
    .initial = console::StampedCVarValue {
      .value = verify_content_hashes_,
      .origin = console::CVarValueOrigin::kAppDefault,
    },
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

      const auto scope = args.empty() ? std::string("all") : args.at(0);
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
          log_kv(nostd::to_string(metric), counters.at(metric));
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
        stats.material_assets.at(TypedLoadMetric::kRequests)
          + stats.geometry_assets.at(TypedLoadMetric::kRequests)
          + stats.scene_assets.at(TypedLoadMetric::kRequests)
          + stats.physics_scene_assets.at(TypedLoadMetric::kRequests)
          + stats.script_assets.at(TypedLoadMetric::kRequests)
          + stats.input_action_assets.at(TypedLoadMetric::kRequests)
          + stats.input_mapping_context_assets.at(TypedLoadMetric::kRequests),
        stats.texture_resources.at(TypedLoadMetric::kRequests)
          + stats.buffer_resources.at(TypedLoadMetric::kRequests)
          + stats.script_resources.at(TypedLoadMetric::kRequests)
          + stats.physics_resources.at(TypedLoadMetric::kRequests));
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
  if (!impl_->source_registry.AcquireSource(ref.source)) {
    throw std::runtime_error(
      "Resource dependency source is no longer readable");
  }
  return InternResourceKey(ref.source,
    GetResourceTypeIndexByTypeId(ref.resource_type_id), ref.resource_index);
}

auto AssetLoader::GetHydratedScriptSlots(const data::SceneAsset& scene_asset,
  const data::pak::scripting::ScriptingComponentRecord& component) const
  -> std::vector<IAssetLoader::HydratedScriptSlot>
{
  AssertOwningThread();

  std::vector<IAssetLoader::HydratedScriptSlot> hydrated_slots;
  std::vector<data::pak::scripting::ScriptSlotRecord> slot_records;
  try {
    slot_records = scene_asset.ReadScriptSlots(
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
      hydrated.params = scene_asset.ReadScriptParameters(slot_record);
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
  BeginAcceptedLoad();
  const auto completion
    = Finally([this] noexcept -> void { EndAcceptedLoad(); });
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
    auto acquired = co_await resource_load_pipeline_->LoadErasedFromCooked(
      type_id, key, bytes, request);
    co_return std::move(acquired.owner);
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

auto AssetLoader::PinAsset(const data::AssetKey& key) -> ResidencyPin
{
  AssertOwningThread();
  if (releases_->IsClosed()) {
    return {};
  }
  const auto identity = ResolveAssetIdentityForKey(key);
  if (!identity) {
    return {};
  }
  auto usage
    = content_cache_.AcquirePin(identity->cache_key, CheckoutOwner::kExternal);
  return usage ? ResidencyPin(releases_->Pin(content_cache_, std::move(*usage)))
               : ResidencyPin {};
}

auto AssetLoader::AssetCacheKey(const data::Asset& asset) const -> uint64_t
{
  const auto& retained = asset.GetRuntimeBindings();
  if (retained
    && retained->GetTypeId() == internal::ContentBindingBundle::ClassTypeId()) {
    const auto bindings
      = std::static_pointer_cast<const internal::ContentBindingBundle>(
        retained);
    const auto* identity = identities_->FindAsset(bindings->Identity());
    if (identity && identity->source == asset.GetSourceOrigin().instance
      && identity->asset == asset.GetAssetKey()
      && identity->view == bindings->ViewIdentity()) {
      return bindings->Identity().get();
    }
  }
  throw std::invalid_argument("Asset has no identity in this loader");
}

//=== Asset Loading Implementations ==========================================//

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
  const OperationLifetime operation(*this);

  const auto identity = ResolveAssetIdentityForKey(key);
  if (!identity.has_value()) {
    return;
  }
  const auto hash = identity->cache_key;
  const auto source_id = identity->source_id;
  auto asset = content_cache_.Peek<data::ScriptAsset>(hash);

  if (asset) {
    // Invalidate script-specific resources if applicable
    const auto resource_type_index = static_cast<uint16_t>(
      IndexOf<data::ScriptResource, ResourceTypeList>::value);

    auto invalidate_resource
      = [&](const data::ResourceReferenceIndex reference) -> void {
      const auto resolved = asset->GetReferences().ResolveResource(
        reference, data::ResourceKind::kScript);
      if (!resolved) {
        throw std::runtime_error(resolved.error());
      }
      const auto& index = *resolved;
      if (index) {
        const auto rkey
          = InternResourceKey(source_id, resource_type_index, *index);
        static_cast<void>(content_cache_.Invalidate(rkey.get()));
      }
    };

    invalidate_resource(asset->GetBytecodeResourceIndex());
    if (!operation) {
      return;
    }
    invalidate_resource(asset->GetSourceResourceIndex());
    if (!operation) {
      return;
    }
  }

  // Finally remove the asset itself
  static_cast<void>(content_cache_.Invalidate(hash));
}

auto AssetLoader::ReloadScript(const std::filesystem::path& path) -> void
{
  AssertOwningThread();
  if ((nursery_ == nullptr) || !thread_pool_) {
    return;
  }

  const OperationLifetime operation(*this);
  const internal::ScriptHotReloadService::ReloadCallbacks callbacks {
    .enumerate_loaded_script_keys = [this] -> std::vector<data::AssetKey> {
      std::vector<data::AssetKey> script_keys;
      for (const auto& [id, locator] : identities_->Entries()) {
        if (const auto* asset = std::get_if<internal::AssetIdentity>(locator);
          asset != nullptr
          && content_cache_.Peek<data::ScriptAsset>(id.get())) {
          script_keys.push_back(asset->asset);
        }
      }
      return script_keys;
    },
    .get_script_asset = [this](const data::AssetKey& key)
      -> std::shared_ptr<oxygen::data::ScriptAsset> {
      return GetAsset<data::ScriptAsset>(key);
    },
    .invalidate_asset_tree
    = [this](const data::AssetKey& key) -> void { InvalidateAssetTree(key); },
    .start_load_script_asset
    = [this](const data::AssetKey& key,
        std::function<void(std::shared_ptr<data::ScriptAsset>)> done) -> void {
      StartLoadAsset<data::ScriptAsset>(key, std::move(done));
    },
    .acquire_bytecode =
      [this](const data::ScriptAsset& asset) {
        return AcquireScriptBytecode(asset);
      },
    .is_current = [operation]() { return static_cast<bool>(operation); },
  };
  script_hot_reload_service_->ReloadScript(path, callbacks);
}

auto AssetLoader::ReloadAllScripts() -> void
{
  AssertOwningThread();
  const OperationLifetime operation(*this);
  const internal::ScriptHotReloadService::ReloadCallbacks callbacks {
    .enumerate_loaded_script_keys = [this] -> std::vector<data::AssetKey> {
      std::vector<data::AssetKey> script_keys;
      for (const auto& [id, locator] : identities_->Entries()) {
        if (const auto* asset = std::get_if<internal::AssetIdentity>(locator);
          asset != nullptr
          && content_cache_.Peek<data::ScriptAsset>(id.get())) {
          script_keys.push_back(asset->asset);
        }
      }
      return script_keys;
    },
    .get_script_asset = [this](const data::AssetKey& key)
      -> std::shared_ptr<oxygen::data::ScriptAsset> {
      return GetAsset<data::ScriptAsset>(key);
    },
    .invalidate_asset_tree
    = [this](const data::AssetKey& key) -> void { InvalidateAssetTree(key); },
    .start_load_script_asset
    = [this](const data::AssetKey& key,
        std::function<void(std::shared_ptr<data::ScriptAsset>)> done) -> void {
      StartLoadAsset<data::ScriptAsset>(key, std::move(done));
    },
    .acquire_bytecode =
      [this](const data::ScriptAsset& asset) {
        return AcquireScriptBytecode(asset);
      },
    .is_current = [operation]() { return static_cast<bool>(operation); },
  };
  script_hot_reload_service_->ReloadAllScripts(callbacks);
}

auto AssetLoader::AcquireScriptBytecode(const data::ScriptAsset& asset)
  -> std::shared_ptr<data::ScriptResource>
{
  const auto& retained = asset.GetRuntimeBindings();
  if (!retained
    || retained->GetTypeId() != internal::ContentBindingBundle::ClassTypeId()) {
    return {};
  }
  const auto resolved = asset.GetReferences().ResolveResource(
    asset.GetBytecodeResourceIndex(), data::ResourceKind::kScript);
  if (!resolved) {
    throw std::runtime_error(resolved.error());
  }
  const auto& index = *resolved;
  if (!index) {
    return {};
  }
  const auto id = identities_->Find(internal::CookedResourceIdentity {
    .source = asset.GetSourceOrigin().instance,
    .kind = internal::ResourceKind::kScript,
    .index = index->get() });
  const auto bundle
    = std::static_pointer_cast<const internal::ContentBindingBundle>(retained);
  const auto* binding = bundle->FindResourceBinding(ResourceKey { id.get() });
  return binding ? binding->publication.Acquire<data::ScriptResource>(
                     content_cache_, *releases_, CheckoutOwner::kExternal)
                 : nullptr;
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

auto AssetLoader::DecodeAssetAsyncErasedImpl(const TypeId type_id,
  const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  LoadRequest request) -> co::Co<DecodedAssetAsyncResult>
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

  // The caller selected the origin before admission; keep it through decode.
  const auto resolved_id = preferred_source_id.has_value()
    ? preferred_source_id
    : ResolveLoadSourceId(key);
  const auto source_content = resolved_id.has_value()
    ? impl_->source_registry.AcquireSource(*resolved_id)
    : nullptr;
  if (!resolved_id || !source_content || !source_content->HasAsset(key)) {
    co_return {
      .source_id = {},
      .asset = nullptr,
      .dependency_collector = nullptr,
      .references = {},
    };
  }
  const auto source_id = *resolved_id;
  const auto source_instance = source_id;
  auto desc_reader = source_content->CreateAssetDescriptorReader(key);
  if (!desc_reader) {
    throw std::runtime_error(
      "Selected asset source did not provide its descriptor");
  }
  auto buf_reader = source_content->CreateBufferDataReader();
  auto tex_reader = source_content->CreateTextureDataReader();
  auto script_reader = source_content->CreateScriptDataReader();
  auto phys_reader = source_content->CreatePhysicsDataReader();
  auto collector = std::make_shared<internal::DependencyCollector>();

  const auto registered = asset_loaders_.find(type_id);
  if (registered == asset_loaders_.end()) {
    LOG_F(ERROR, "No loader registered for asset type id: {}", type_id);
    co_return DecodedAssetAsyncResult {};
  }
  auto decoder = registered->second;
  const auto default_priority = residency_policy_.default_priority_class;
  LOG_F(2, "scheduling asset decode on thread pool: type_id={}", type_id);
  auto decoded = co_await thread_pool_->Run(
    [key, source_content, collector, decoder = std::move(decoder),
      offline = work_offline_, default_priority, request,
      desc_reader = std::move(desc_reader), buf_reader = std::move(buf_reader),
      tex_reader = std::move(tex_reader),
      script_reader = std::move(script_reader),
      phys_reader = std::move(phys_reader),
      source_instance] mutable -> DecodedAssetAsyncResult {
      auto references = source_content->ReadAssetReferences(key);
      LoaderContext context {
        .current_asset_key = key,
        .source_instance = source_instance,
        .desc_reader = desc_reader.get(),
        .asset_references
        = observer_ptr<const data::AssetReferences> { &references },
        .data_readers = std::make_tuple(buf_reader.get(), tex_reader.get(),
          script_reader.get(), phys_reader.get()),
        .work_offline = offline,
        .default_priority_class = default_priority,
        .request_priority = request.priority,
        .request_intent = request.intent,
        .dependency_collector = collector,
        .source_content = source_content,
        .source_key = source_content->GetSourceKey(),
        .parse_only = false,
      };

      return {
        .source_id = source_instance,
        .asset = decoder(context),
        .dependency_collector = collector,
        .references = std::move(references),
      };
    });

  AssertOwningThread();
  if (!impl_->source_registry.AcquireSource(source_id)) {
    decoded.asset.reset();
  }
  co_return decoded;
}

auto AssetLoader::ResolveDependencySourceId(
  const data::Asset& owner, const data::AssetKey& dependency) const
  -> std::optional<data::SourceInstanceId>
{
  const auto origin = ResolveAssetSourceId(owner);
  if (!origin) {
    return std::nullopt;
  }
  if (const auto& bindings = owner.GetRuntimeBindings()) {
    if (const auto bound = bindings->FindAsset(dependency)) {
      return bound->GetSourceOrigin().instance;
    }
  }
  return ResolveLoadSourceId(dependency, origin);
}

auto AssetLoader::ResolveAssetSourceId(const data::Asset& asset) const
  -> std::optional<data::SourceInstanceId>
{
  const auto origin = asset.GetSourceOrigin();
  const auto source = impl_->source_registry.AcquireSource(origin.instance);
  if (!source || source->GetSourceKey() != origin.key) {
    return std::nullopt;
  }
  return origin.instance;
}

auto AssetLoader::ResolveExactSourceId(
  const data::AssetKey& key, const data::SourceKey source_key) const
  -> std::optional<data::SourceInstanceId>
{
  if (source_key.IsNil()) {
    return std::nullopt;
  }
  const auto id = impl_->source_registry.FindSourceIdByKey(source_key);
  if (!id || !ResolveAssetIdentityForKey(key, id)) {
    return std::nullopt;
  }
  return id;
}

auto AssetLoader::PrepareAssetLoadRequest(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  const ContentLoadScope& scope) -> std::optional<AssetLoadRequest>
{
  const auto source_id = preferred_source_id
    ? preferred_source_id
    : ResolveScopedRoot(key, std::nullopt, scope);
  if (!source_id) {
    return std::nullopt;
  }
  auto source = ResolveSourceForId(*source_id);
  if (!source || !source->HasAsset(key)) {
    return std::nullopt;
  }
  const auto& view = scope.state_->view;
  const auto token = source->HasKeyReferences(key) && view
    ? view->IdentityOwner()
    : std::shared_ptr<const internal::BindingViewId> {};
  const auto id = identities_->Intern(
    internal::AssetIdentity {
      .source = *source_id,
      .asset = key,
      .view = token ? *token : internal::BindingViewId {},
    },
    token);
  return AssetLoadRequest {
    .source_id = *source_id,
    .cache_key = id.get(),
    .source = std::move(source),
  };
}

auto AssetLoader::BindMaterialTextureKeys(
  data::MaterialAsset& material, const data::SourceInstanceId source) -> void
{
  const std::array indices {
    material.GetBaseColorTexture(),
    material.GetNormalTexture(),
    material.GetMetallicTexture(),
    material.GetRoughnessTexture(),
    material.GetAmbientOcclusionTexture(),
    material.GetEmissiveTexture(),
    material.GetSpecularTexture(),
    material.GetSheenColorTexture(),
    material.GetClearcoatTexture(),
    material.GetClearcoatNormalTexture(),
    material.GetTransmissionTexture(),
    material.GetThicknessTexture(),
  };
  std::vector<ResourceKey> keys;
  keys.reserve(indices.size());
  constexpr auto kTextureTypeIndex = static_cast<uint16_t>(
    IndexOf<data::TextureResource, ResourceTypeList>::value);
  for (const auto reference : indices) {
    const auto resolved = material.GetReferences().ResolveResource(
      reference, data::ResourceKind::kTexture);
    if (!resolved) {
      throw std::runtime_error(resolved.error());
    }
    const auto& index = *resolved;
    if (index && *index == data::pak::core::kErrorTextureResourceIndex) {
      keys.push_back(ResourceKey::kError);
      continue;
    }
    keys.push_back(!index || *index == data::pak::core::kFallbackResourceIndex
        ? ResourceKey {}
        : InternResourceKey(source, kTextureTypeIndex, *index));
  }
  material.SetTextureResourceKeys(std::move(keys));
}

auto AssetLoader::BindGeometryRuntimePointers(data::GeometryAsset& asset,
  const LoadedGeometryBuffersByIndex& buffers_by_index,
  const LoadedGeometryMaterialsByKey& materials_by_key) -> void
{
  AssertOwningThread();

  using data::BufferResource;
  using data::MaterialAsset;
  using data::MeshType;

  const auto find_buffer
    = [&asset, &buffers_by_index](const data::ResourceReferenceIndex reference)
    -> std::shared_ptr<BufferResource> {
    const auto resolved = asset.GetReferences().ResolveResource(
      reference, data::ResourceKind::kBuffer);
    if (!resolved) {
      throw std::runtime_error(resolved.error());
    }
    const auto& index = *resolved;
    if (!index) {
      return {};
    }
    const auto found = buffers_by_index.find(index->get());
    return found == buffers_by_index.end() ? nullptr : found->second.resource;
  };

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

      mesh.SetBufferResources(
        find_buffer(info.vertex_buffer), find_buffer(info.index_buffer));
    } else if (mesh_desc_opt && mesh_desc_opt->IsSkinned()) {
      const auto& info = mesh_desc_opt->info.skinned;
      mesh.SetBufferResources(
        find_buffer(info.vertex_buffer), find_buffer(info.index_buffer));
      if (mesh.IsSkinned()) {
        mesh.SetSkiningBufferResources(find_buffer(info.joint_index_buffer),
          find_buffer(info.joint_weight_buffer),
          find_buffer(info.inverse_bind_buffer),
          find_buffer(info.joint_remap_buffer));
      }
    }

    const auto submeshes = mesh.SubMeshes();
    for (size_t i = 0; i < submeshes.size(); ++i) {
      const auto& sm_desc_opt
        = oxygen::base::CheckedAt(submeshes, i).Descriptor();
      if (!sm_desc_opt) {
        continue;
      }

      const auto mat_key = sm_desc_opt->material_asset_key;
      if (mat_key == data::AssetKey {}) {
        continue;
      }

      auto mat_it = materials_by_key.find(mat_key);
      if (mat_it == materials_by_key.end() || !mat_it->second) {
        throw std::runtime_error(fmt::format(
          "Required submesh material {} was not bound for geometry {}", mat_key,
          asset.GetAssetKey()));
      }

      mesh.SetSubMeshMaterial(i, mat_it->second);
    }
  }
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
  BeginAcceptedLoad();
  const auto completion
    = Finally([this] noexcept -> void { EndAcceptedLoad(); });
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

  const auto kind
    = identities_->FindResourceKind(internal::ContentId { key.get() });
  const auto expected_type_index
    = static_cast<uint16_t>(IndexOf<T, ResourceTypeList>::value);
  if (!kind || static_cast<uint16_t>(*kind) != expected_type_index) {
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
      T::ClassTypeNamePretty(),
      kind ? static_cast<uint16_t>(*kind)
           : std::numeric_limits<uint16_t>::max(),
      expected_type_index);
    co_return nullptr;
  }

  if constexpr (std::same_as<T, data::TextureResource>) {
    LOG_F(INFO, "AssetLoader: Decode TextureResource {}", to_string(key));
  }

  try {
    const auto decoded = co_await resource_load_pipeline_->LoadErased(
      T::ClassTypeId(), key, request);
    if (!decoded) {
      co_return nullptr;
    }
    auto typed = std::static_pointer_cast<T>(decoded.owner);
    if (typed->GetTypeId() != T::ClassTypeId()) {
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

auto AssetLoader::RetireCacheEntry(
  const internal::ContentReleaseQueue::Cache::RetiredEntry& retired,
  const EvictionReason reason) -> void
{
  UnloadObject(retired.entry.GetKey(), retired.type, reason);
}

void oxygen::content::AssetLoader::UnloadObject(const uint64_t cache_key,
  const oxygen::TypeId& type_id, const EvictionReason reason)
{
  const OperationLifetime operation(*this);
  RecordEviction(reason);
  EvictionEvent event {
    .key = ResourceKey {},
    .type_id = type_id,
    .reason = reason,
#ifndef NDEBUG
    .cache_key = cache_key,
#endif
  };

  if (IsResourceTypeId(type_id)) {
    event.key = ResourceKey { cache_key };
    if (reason != EvictionReason::kRefCountZero) {
      eviction_registry_->ForgetResource(event.key);
    }

    LOG_F(2, "Evicted resource {} type_id={} reason={}", to_string(event.key),
      type_id, reason);
  } else {
    const auto* asset_identity
      = identities_->FindAsset(internal::ContentId { cache_key });
    const auto* asset_key = asset_identity ? &asset_identity->asset : nullptr;
    if (asset_key == nullptr) {
      LOG_F(WARNING,
        "Eviction without AssetKey mapping: cache_key={} type_id={}", cache_key,
        type_id);
      return;
    }

    event.asset_key = *asset_key;

    LOG_F(2, "Evicted asset {} type_id={} reason={}", *event.asset_key, type_id,
      reason);
  }

  const auto subscribers = eviction_registry_->SnapshotSubscribers(type_id);
  if (subscribers.empty()) {
    return;
  }

  // Prevent re-entrant eviction notifications for the same cache key.
  internal::EvictionRegistry::ActiveEviction active { .key = cache_key,
    .previous = nullptr };
  if (!eviction_registry_->TryEnterEviction(active)) {
    LOG_F(
      2, "AssetLoader: nested eviction ignored for cache_key={}", cache_key);
    return;
  }
  // Ensure the guard is cleared on all exit paths.
  ScopeGuard clear_eviction_guard([this, operation, &active] noexcept -> void {
    if (operation) {
      eviction_registry_->ExitEviction(active);
    }
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
    if (!operation) {
      return;
    }
  }
}

auto AssetLoader::FlushResourceEvictionsForUncachedMappings(
  const EvictionReason reason, const bool force_emit_all) -> void
{
  AssertOwningThread();
  const OperationLifetime operation(*this);
  static_cast<void>(force_emit_all);
  std::vector<std::pair<uint64_t, TypeId>> pending;
  pending.reserve(eviction_registry_->TrackedResources().size());
  for (const auto key : eviction_registry_->TrackedResources()) {
    if (!content_cache_.Contains(key.get())) {
      const auto kind
        = identities_->FindResourceKind(internal::ContentId { key.get() });
      if (kind) {
        pending.emplace_back(
          key.get(), GetResourceTypeIdByIndex(static_cast<size_t>(*kind)));
      }
    }
  }
  for (const auto& [id, type] : pending) {
    UnloadObject(id, type, reason);
    if (!operation) {
      return;
    }
  }
}

auto AssetLoader::PinResource(const ResourceKey key) -> ResidencyPin
{
  AssertOwningThread();
  if (releases_->IsClosed()) {
    return {};
  }
  auto usage = content_cache_.AcquirePin(key.get(), CheckoutOwner::kExternal);
  return usage ? ResidencyPin(releases_->Pin(content_cache_, std::move(*usage)))
               : ResidencyPin {};
}

auto AssetLoader::GetPakIndex(const PakFile& pak) const
  -> data::SourceInstanceId
{
  // Normalize the path of the input pak
  const auto& pak_path = base::ToLogicalPath(
    std::filesystem::weakly_canonical(base::ToNativePath(pak.FilePath())));

  if (const auto source_id = impl_->source_registry.FindPakId(pak_path);
    source_id) {
    return *source_id;
  }

  LOG_F(ERROR, "PAK file not found in AssetLoader collection (by path)");
  throw std::runtime_error("PAK file not found in AssetLoader collection");
}

auto AssetLoader::MakePhysicsResourceKey(const data::SourceKey source_key,
  const data::pak::core::ResourceIndexT resource_index)
  -> std::optional<ResourceKey>
{
  AssertOwningThread();
  const auto source_id = impl_->source_registry.FindSourceIdByKey(source_key);
  if (!source_id || resource_index == data::pak::core::kNoResourceIndex) {
    return std::nullopt;
  }
  constexpr auto type_index = static_cast<uint16_t>(
    IndexOf<data::PhysicsResource, ResourceTypeList>::value);
  return InternResourceKey(*source_id, type_index, resource_index);
}

auto AssetLoader::MakeTextureResourceKey(const data::SourceKey source_key,
  const data::pak::core::ResourceIndexT resource_index)
  -> std::optional<ResourceKey>
{
  AssertOwningThread();
  if (source_key.IsNil()
    || resource_index == data::pak::core::kNoResourceIndex) {
    return std::nullopt;
  }
  const auto source_id = impl_->source_registry.FindSourceIdByKey(source_key);
  const auto source = source_id ? ResolveSourceForId(*source_id) : nullptr;
  if (!source_id || !source) {
    return std::nullopt;
  }
  const auto* table = source->GetTextureTable();
  if (!table || !table->IsValidKey(resource_index)) {
    return std::nullopt;
  }
  constexpr auto kTextureTypeIndex = static_cast<uint16_t>(
    IndexOf<data::TextureResource, ResourceTypeList>::value);
  return InternResourceKey(*source_id, kTextureTypeIndex, resource_index);
}

auto AssetLoader::ResolveTextureResourceKey(
  const TextureResourceLocator& locator) -> std::optional<ResourceKey>
{
  AssertOwningThread();
  if (!locator.cooked_root.is_absolute()
    || locator.descriptor_relative_path.empty()
    || locator.descriptor_relative_path.has_root_path()
    || std::ranges::any_of(locator.descriptor_relative_path,
      [](const auto& part) -> bool { return part == ".."; })) {
    throw std::invalid_argument(
      "Texture locator requires an absolute cooked root and a contained "
      "relative descriptor path");
  }
  const auto root = base::ToLogicalPath(
    std::filesystem::weakly_canonical(base::ToNativePath(locator.cooked_root)));
  const auto& sources = impl_->source_registry.Sources();
  for (std::size_t i = 0; i < sources.size(); ++i) {
    const auto& source = sources.at(i);
    std::error_code error;
    if (source->GetTypeId() != internal::LooseCookedSource::ClassTypeId()
      || !std::filesystem::equivalent(base::ToNativePath(source->SourcePath()),
        base::ToNativePath(root), error)
      || error) {
      continue;
    }
    const auto path = FindTextureResourceDescriptorPath(
      root / locator.descriptor_relative_path);
    if (!path) {
      return std::nullopt;
    }
    const auto relative = base::ToLogicalPath(
      std::filesystem::weakly_canonical(base::ToNativePath(*path)))
                            .lexically_relative(root);
    if (relative.empty() || relative.has_root_path()
      || *relative.begin() == "..") {
      throw std::invalid_argument(
        "Texture descriptor resolves outside its mounted source");
    }
    if (std::filesystem::file_size(base::ToNativePath(*path))
      != data::kTextureResourceDescriptorSize) {
      throw std::runtime_error(
        "Texture descriptor does not use the current OTEX layout");
    }
    serio::FileStream<> stream(*path, std::ios::in);
    serio::Reader reader(stream);
    const auto bytes = reader.ReadBlob(data::kTextureResourceDescriptorSize);
    if (!bytes) {
      throw std::runtime_error("Texture descriptor could not be read");
    }
    const auto decoded = data::DecodeTextureResourceDescriptor(*bytes);
    if (!decoded) {
      throw std::runtime_error(decoded.error());
    }
    const auto* table = source->GetTextureTable();
    if (decoded->index == data::pak::core::kNoResourceIndex || table == nullptr
      || !table->IsValidKey(decoded->index)) {
      throw std::runtime_error(
        "Texture descriptor index is outside its mounted source table");
    }
    auto table_reader = source->CreateTextureTableReader();
    auto descriptor = data::pak::core::TextureResourceDesc {};
    const auto offset = table->GetResourceOffset(decoded->index);
    if (!table_reader || !offset || !table_reader->Seek(*offset)
      || !serio::Load(*table_reader, descriptor)
      || std::memcmp(&descriptor, &decoded->descriptor, sizeof(descriptor))
        != 0) {
      throw std::runtime_error("Texture descriptor is stale or does not match "
                               "its mounted source table");
    }
    constexpr auto kTextureTypeIndex = static_cast<uint16_t>(
      IndexOf<data::TextureResource, ResourceTypeList>::value);
    return InternResourceKey(impl_->source_registry.SourceIds().at(i),
      kTextureTypeIndex, decoded->index);
  }
  return std::nullopt;
}

auto AssetLoader::MakeTextureResourceKeyForAsset(
  const data::Asset& context_asset,
  const data::ResourceReferenceIndex reference) -> std::optional<ResourceKey>
{
  AssertOwningThread();
  const auto resolved = context_asset.GetReferences().ResolveResource(
    reference, data::ResourceKind::kTexture);
  if (!resolved) {
    return {};
  }
  const auto& index = *resolved;
  if (!index) {
    return {};
  }
  const auto resource_index = *index;

  if (resource_index == data::pak::core::kNoResourceIndex) {
    return std::nullopt;
  }
  if (resource_index == data::pak::core::kErrorTextureResourceIndex) {
    return ResourceKey::kError;
  }
  const auto source_id = ResolveAssetSourceId(context_asset);
  if (!source_id) {
    return std::nullopt;
  }
  const auto source = ResolveSourceForId(*source_id);
  const auto* table = source != nullptr ? source->GetTextureTable() : nullptr;
  if (table == nullptr || !table->IsValidKey(resource_index)) {
    return std::nullopt;
  }
  constexpr auto kTextureTypeIndex = static_cast<uint16_t>(
    IndexOf<data::TextureResource, ResourceTypeList>::value);
  return InternResourceKey(*source_id, kTextureTypeIndex, resource_index);
}

auto AssetLoader::MakeScriptResourceKeyForAsset(
  const data::Asset& context_asset,
  const data::ResourceReferenceIndex reference) -> std::optional<ResourceKey>
{
  AssertOwningThread();
  const auto resolved = context_asset.GetReferences().ResolveResource(
    reference, data::ResourceKind::kScript);
  if (!resolved) {
    return {};
  }
  const auto& index = *resolved;
  if (!index) {
    return {};
  }
  const auto resource_index = *index;

  const internal::ScriptQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this, &context_asset](
        const data::AssetKey&) -> std::optional<data::SourceInstanceId> {
      return ResolveAssetSourceId(context_asset);
    },
    .resolve_source_for_id = [this](const data::SourceInstanceId source_id)
      -> std::shared_ptr<const internal::IContentSource> {
      return ResolveSourceForId(source_id);
    },
    .make_script_resource_key
    = [this](const data::SourceInstanceId source_id,
        const data::pak::core::ResourceIndexT index) -> ResourceKey {
      const auto resource_type_index = static_cast<uint16_t>(
        IndexOf<data::ScriptResource, ResourceTypeList>::value);
      return InternResourceKey(source_id, resource_type_index, index);
    },
  };
  return script_query_service_->MakeScriptResourceKeyForAsset(
    context_asset.GetAssetKey(), resource_index, callbacks);
}

auto AssetLoader::ReadScriptResourceForAsset(const data::Asset& context_asset,
  const data::ResourceReferenceIndex reference) const
  -> std::shared_ptr<const data::ScriptResource>
{
  const auto resolved = context_asset.GetReferences().ResolveResource(
    reference, data::ResourceKind::kScript);
  if (!resolved) {
    return {};
  }
  const auto& index = *resolved;
  if (!index) {
    return {};
  }
  const auto resource_index = *index;

  const internal::ScriptQueryService::Callbacks callbacks {
    .resolve_source_id_for_asset
    = [this, &context_asset](
        const data::AssetKey&) -> std::optional<data::SourceInstanceId> {
      return ResolveAssetSourceId(context_asset);
    },
    .resolve_source_for_id = [this](const data::SourceInstanceId source_id)
      -> std::shared_ptr<const internal::IContentSource> {
      return ResolveSourceForId(source_id);
    },
    .make_script_resource_key = {},
  };
  return script_query_service_->ReadScriptResourceForAsset(
    context_asset.GetAssetKey(), resource_index, callbacks);
}

auto AssetLoader::MakePhysicsResourceKeyForAsset(
  const data::Asset& context_asset,
  const data::pak::core::ResourceIndexT resource_index)
  -> std::optional<ResourceKey>
{
  AssertOwningThread();
  const auto source_id = ResolveAssetSourceId(context_asset);
  if (!source_id || resource_index == data::pak::core::kNoResourceIndex) {
    return std::nullopt;
  }
  constexpr auto type_index = static_cast<uint16_t>(
    IndexOf<data::PhysicsResource, ResourceTypeList>::value);
  return InternResourceKey(*source_id, type_index, resource_index);
}

auto AssetLoader::GetPhysicsBindings(const data::Asset& context) const
  -> const internal::PhysicsBindings*
{
  AssertOwningThread();
  static_cast<void>(AssetCacheKey(context));
  const auto& bindings = context.GetRuntimeBindings();
  if (!bindings
    || bindings->GetTypeId() != internal::ContentBindingBundle::ClassTypeId()) {
    return nullptr;
  }
  return std::static_pointer_cast<const internal::ContentBindingBundle>(
    bindings)
    ->Physics();
}

auto AssetLoader::MakePhysicsResourceKeyForAsset(
  const data::Asset& context_asset, const data::AssetKey& resource_asset_key)
  -> std::optional<ResourceKey>
{
  const auto* bindings = GetPhysicsBindings(context_asset);
  if (!bindings) {
    return std::nullopt;
  }
  const auto found = std::ranges::lower_bound(bindings->resources,
    resource_asset_key, {}, &internal::BoundPhysicsResource::asset_key);
  if (found == bindings->resources.end()
    || found->asset_key != resource_asset_key
    || !ResolveSourceForId(found->source)) {
    return std::nullopt;
  }
  return found->key;
}

auto AssetLoader::ReadCollisionShapeAssetDescForAsset(
  const data::Asset& context_asset, const data::AssetKey& shape_asset_key) const
  -> std::optional<data::pak::physics::CollisionShapeAssetDesc>
{
  const auto* bindings = GetPhysicsBindings(context_asset);
  if (!bindings) {
    return std::nullopt;
  }
  const auto found = std::ranges::lower_bound(
    bindings->shapes, shape_asset_key, {}, &internal::BoundPhysicsShape::key);
  return found != bindings->shapes.end() && found->key == shape_asset_key
    ? std::optional(found->descriptor)
    : std::nullopt;
}

auto AssetLoader::ReadPhysicsMaterialAssetDescForAsset(
  const data::Asset& context_asset,
  const data::AssetKey& material_asset_key) const
  -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc>
{
  const auto* bindings = GetPhysicsBindings(context_asset);
  if (!bindings) {
    return std::nullopt;
  }
  const auto found = std::ranges::lower_bound(bindings->materials,
    material_asset_key, {}, &internal::BoundPhysicsMaterial::key);
  return found != bindings->materials.end() && found->key == material_asset_key
    ? std::optional(found->descriptor)
    : std::nullopt;
}

auto AssetLoader::FindPhysicsSidecarAssetKeyForScene(
  const data::Asset& scene_asset, const ContentLoadScope& scope)
  -> std::optional<data::AssetKey>
{
  const auto request = AdmitAssetRequest(LoadRequest { .scope = scope });
  const auto& view = request.scope.state_->view;
  if (!view) {
    return std::nullopt;
  }
  std::optional<data::AssetKey> matched;
  for (const auto& layer : view->Layers()) {
    for (size_t i = 0; i < layer.source->GetAssetCount(); ++i) {
      const auto key
        = layer.source->GetAssetKeyByIndex(static_cast<uint32_t>(i));
      if (!key
        || layer.source->GetAssetType(*key) != data::AssetType::kPhysicsScene
        || view->ResolveAsset(*key) != layer.id) {
        continue;
      }
      const auto source = ResolveSourceForId(layer.id);
      if (!source) {
        throw std::runtime_error(
          "Physics sidecar source is no longer readable");
      }
      const auto reader = source->CreateAssetDescriptorReader(*key);
      if (!reader) {
        throw std::runtime_error("Cannot read physics sidecar descriptor");
      }
      const auto bytes
        = reader->ReadBlob(sizeof(data::pak::physics::PhysicsSceneAssetDesc));
      if (!bytes) {
        throw std::runtime_error("Cannot read physics sidecar descriptor");
      }
      data::pak::physics::PhysicsSceneAssetDesc descriptor {};
      std::memcpy(&descriptor, bytes->data(), sizeof(descriptor));
      if (descriptor.header.asset_type
          != static_cast<uint8_t>(data::AssetType::kPhysicsScene)
        || descriptor.header.version
          != data::pak::physics::kPhysicsSceneAssetVersion) {
        throw std::runtime_error(
          "Invalid physics sidecar descriptor type/version");
      }
      if (descriptor.target_scene_key != scene_asset.GetAssetKey()) {
        continue;
      }
      if (matched) {
        throw std::runtime_error(
          "Multiple effective physics sidecars target scene "
          + data::to_string(scene_asset.GetAssetKey()));
      }
      matched = *key;
    }
  }
  return matched;
}

auto AssetLoader::ResolveAssetIdentityForKey(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id) const
  -> std::optional<ResolvedAssetIdentity>
{
  if (preferred_source_id.has_value()) {
    const auto source
      = impl_->source_registry.AcquireSource(*preferred_source_id);
    if (source && source->HasAsset(key)
      && !impl_->source_registry.IsSourceTombstoningAsset(
        *preferred_source_id, key)) {
      return ResolvedAssetIdentity {
        .cache_key = FindAssetId(key, *preferred_source_id),
        .source_id = *preferred_source_id,
      };
    }
    return std::nullopt;
  }

  const internal::KeyResolutionCallbacks callbacks {
    .source_has_asset = [this](const data::SourceInstanceId source_id,
                          const data::AssetKey& candidate_key) -> bool {
      const auto source_index_opt
        = impl_->source_registry.FindSourceIndexById(source_id);
      if (!source_index_opt.has_value()) {
        return false;
      }
      const auto& source
        = impl_->source_registry.Sources().at(*source_index_opt);
      return source && source->HasAsset(candidate_key);
    },
    .source_tombstones_asset = [this](const data::SourceInstanceId source_id,
                                 const data::AssetKey& candidate_key) -> bool {
      return impl_->source_registry.IsSourceTombstoningAsset(
        source_id, candidate_key);
    },
  };

  const auto resolution = internal::ResolveAssetKeyByPrecedence(
    impl_->source_registry.SourceIds(), key, callbacks);
  if (resolution.status == internal::KeyResolutionStatus::kFound
    && resolution.source_id.has_value()) {
    return ResolvedAssetIdentity {
      .cache_key = FindAssetId(key, *resolution.source_id),
      .source_id = *resolution.source_id,
    };
  }

  return std::nullopt;
}

auto AssetLoader::ResolveSourceIdForAsset(
  const data::AssetKey& context_asset_key) const
  -> std::optional<data::SourceInstanceId>
{
  if (const auto identity = ResolveAssetIdentityForKey(context_asset_key);
    identity.has_value()) {
    return identity->source_id;
  }
  return std::nullopt;
}

auto AssetLoader::ResolveLoadSourceId(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id) const
  -> std::optional<data::SourceInstanceId>
{
  if (preferred_source_id.has_value()) {
    const auto source
      = impl_->source_registry.AcquireSource(*preferred_source_id);
    if (!source
      || impl_->source_registry.IsSourceTombstoningAsset(
        *preferred_source_id, key)) {
      return std::nullopt;
    }
    if (source->HasAsset(key)) {
      return *preferred_source_id;
    }
  }

  const internal::KeyResolutionCallbacks callbacks {
    .source_has_asset = [this](const data::SourceInstanceId source_id,
                          const data::AssetKey& candidate_key) -> bool {
      const auto source_index_opt
        = impl_->source_registry.FindSourceIndexById(source_id);
      if (!source_index_opt.has_value()) {
        return false;
      }
      const auto& source
        = impl_->source_registry.Sources().at(*source_index_opt);
      return source && source->HasAsset(candidate_key);
    },
    .source_tombstones_asset = [this](const data::SourceInstanceId source_id,
                                 const data::AssetKey& candidate_key) -> bool {
      return impl_->source_registry.IsSourceTombstoningAsset(
        source_id, candidate_key);
    },
  };

  const auto resolution = internal::ResolveAssetKeyByPrecedence(
    impl_->source_registry.SourceIds(), key, callbacks);
  if (resolution.status == internal::KeyResolutionStatus::kFound) {
    return resolution.source_id;
  }

  return std::nullopt;
}

auto AssetLoader::ResolveResourceSource(const ResourceKey key) const
  -> std::shared_ptr<const internal::IContentSource>
{
  const auto* identity
    = identities_->FindCookedResource(internal::ContentId { key.get() });
  return identity ? ResolveSourceForId(identity->source) : nullptr;
}

auto AssetLoader::ResolveSourceForId(
  const data::SourceInstanceId source_id) const
  -> std::shared_ptr<const internal::IContentSource>
{
  return impl_->source_registry.AcquireSource(source_id);
}

auto AssetLoader::MintSyntheticResourceKey(const TypeId resource_type)
  -> ResourceKey
{
  AssertOwningThread();
  if (next_synthetic_serial_ == std::numeric_limits<uint64_t>::max()) {
    throw std::length_error(
      "Synthetic resource identity namespace is exhausted");
  }
  const auto kind = static_cast<internal::ResourceKind>(
    GetResourceTypeIndexByTypeId(resource_type));
  const auto id = identities_->Intern(internal::SyntheticResourceIdentity {
    .kind = kind,
    .serial = next_synthetic_serial_++,
  });
  return ResourceKey { id.get() };
}

auto AssetLoader::MintSyntheticTextureKey() -> ResourceKey
{
  return MintSyntheticResourceKey(data::TextureResource::ClassTypeId());
}

auto AssetLoader::MintSyntheticBufferKey() -> ResourceKey
{
  return MintSyntheticResourceKey(data::BufferResource::ClassTypeId());
}

auto AssetLoader::MintSyntheticScriptKey() -> ResourceKey
{
  return MintSyntheticResourceKey(data::ScriptResource::ClassTypeId());
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)

#ifndef NDEBUG
auto AssetLoader::GetDebugAssetDependencyMap() const -> DebugAssetDependencyMap
{
  AssertOwningThread();
  DebugAssetDependencyMap result;
  for (const auto key : content_cache_.KeysSnapshot()) {
    const auto parent = PeekCachedAsset(key);
    if (!parent) {
      continue;
    }
    const auto& retained = parent->GetRuntimeBindings();
    if (!retained
      || retained->GetTypeId()
        != internal::ContentBindingBundle::ClassTypeId()) {
      continue;
    }
    const auto bundle
      = std::static_pointer_cast<const internal::ContentBindingBundle>(
        retained);
    for (const auto& child : bundle->Assets()) {
      const auto id = AssetCacheKey(*child.owner);
      if (id != 0U) {
        result.try_emplace(key).first->second.insert(id);
      }
    }
  }
  return result;
}
auto AssetLoader::GetDebugAssetKey(const uint64_t cache_key) const
  -> std::optional<data::AssetKey>
{
  const auto* identity
    = identities_->FindAsset(internal::ContentId { cache_key });
  return identity ? std::optional { identity->asset } : std::nullopt;
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

auto AssetLoader::FindAssetId(const data::AssetKey& key,
  const data::SourceInstanceId source_id) const noexcept -> uint64_t
{
  const auto source = ResolveSourceForId(source_id);
  if (!source) {
    return 0;
  }
  const auto view = source->HasKeyReferences(key)
    ? impl_->source_registry.CurrentViewId()
    : internal::BindingViewId {};
  return identities_
    ->Find(internal::AssetIdentity {
      .source = source_id,
      .asset = key,
      .view = view,
    })
    .get();
}

auto AssetLoader::AssertSourceKeyConsistency(std::string_view context) const
  -> void
{
  impl_->source_registry.AssertStructuralConsistency(context);
}

auto AssetLoader::AssertResourceMappingConsistency(
  std::string_view context) const -> void
{
#ifndef NDEBUG
  for (const auto key : eviction_registry_->TrackedResources()) {
    DCHECK_F(identities_->FindResourceKind(internal::ContentId { key.get() })
               .has_value(),
      "[invariant:{}] tracked resource has no identity: {}", context, key);
  }
#else
  static_cast<void>(context);
#endif
}
