//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/TypeList.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ContentId.h>
#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Content/Internal/ContentBindingBundle.h>
#include <Oxygen/Content/Internal/ContentIdentity.h>
#include <Oxygen/Content/Internal/ContentIdentityRegistry.h>
#include <Oxygen/Content/Internal/ContentLoadScopeState.h>
#include <Oxygen/Content/Internal/ContentPublication.h>
#include <Oxygen/Content/Internal/ContentReleaseQueue.h>
#include <Oxygen/Content/Internal/ContentSourceView.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/InFlightOperationTable.h>
#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Content/Internal/PhysicsBindings.h>
#include <Oxygen/Content/Internal/ResourceLoadPipeline.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/PhysicsAssetLoader.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Shared.h>
#include <Oxygen/OxCo/TaskCancelledException.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::content {
namespace {
  template <typename Fn>
  auto WithContentType(const TypeId type, const Fn& fn) -> std::shared_ptr<void>
  {
    if (type == data::MaterialAsset::ClassTypeId()) {
      return fn.template operator()<data::MaterialAsset>();
    }
    if (type == data::GeometryAsset::ClassTypeId()) {
      return fn.template operator()<data::GeometryAsset>();
    }
    if (type == data::SceneAsset::ClassTypeId()) {
      return fn.template operator()<data::SceneAsset>();
    }
    if (type == data::PhysicsSceneAsset::ClassTypeId()) {
      return fn.template operator()<data::PhysicsSceneAsset>();
    }
    if (type == data::ScriptAsset::ClassTypeId()) {
      return fn.template operator()<data::ScriptAsset>();
    }
    if (type == data::InputActionAsset::ClassTypeId()) {
      return fn.template operator()<data::InputActionAsset>();
    }
    if (type == data::InputMappingContextAsset::ClassTypeId()) {
      return fn.template operator()<data::InputMappingContextAsset>();
    }
    if (type == data::TextureResource::ClassTypeId()) {
      return fn.template operator()<data::TextureResource>();
    }
    if (type == data::BufferResource::ClassTypeId()) {
      return fn.template operator()<data::BufferResource>();
    }
    if (type == data::ScriptResource::ClassTypeId()) {
      return fn.template operator()<data::ScriptResource>();
    }
    if (type == data::PhysicsResource::ClassTypeId()) {
      return fn.template operator()<data::PhysicsResource>();
    }
    return {};
  }

  template <typename T> constexpr auto AssetKind() -> data::AssetType
  {
    if constexpr (std::same_as<T, data::MaterialAsset>) {
      return data::AssetType::kMaterial;
    } else if constexpr (std::same_as<T, data::GeometryAsset>) {
      return data::AssetType::kGeometry;
    } else if constexpr (std::same_as<T, data::SceneAsset>) {
      return data::AssetType::kScene;
    } else if constexpr (std::same_as<T, data::PhysicsSceneAsset>) {
      return data::AssetType::kPhysicsScene;
    } else if constexpr (std::same_as<T, data::ScriptAsset>) {
      return data::AssetType::kScript;
    } else if constexpr (std::same_as<T, data::InputActionAsset>) {
      return data::AssetType::kInputAction;
    } else {
      static_assert(std::same_as<T, data::InputMappingContextAsset>);
      return data::AssetType::kInputMappingContext;
    }
  }

  //! Awaits the load of a dependency: when it fails, the dependent's load
  //! fails as `kDependencyFailed`, with the dependency's error nested and its
  //! message, the root cause, ending the dependent's.
  template <typename T>
  auto AwaitDependency(co::Co<T> load, const std::string& dependency)
    -> co::Co<T>
  {
    try {
      co_return co_await std::move(load);
    } catch (const OperationCancelledException&) {
      throw;
    } catch (const co::TaskCancelledException&) {
      throw;
    } catch (const std::exception& error) {
      std::throw_with_nested(
        ContentLoadError(ContentLoadFailure::kDependencyFailed,
          dependency + " failed to load: " + error.what()));
    }
  }

  //! A physics descriptor dependency, read and validated off the loop.
  struct ParsedPhysicsDescriptor final {
    data::AssetReferences references;
    std::variant<data::pak::physics::CollisionShapeAssetDesc,
      data::pak::physics::PhysicsMaterialAssetDesc>
      descriptor;
  };

  auto ParsePhysicsDescriptor(const internal::IContentSource& source,
    const data::KeyReference& reference,
    const data::SourceInstanceId source_instance) -> ParsedPhysicsDescriptor
  {
    auto references = source.ReadAssetReferences(reference.key);
    const auto reader = source.CreateAssetDescriptorReader(reference.key);
    if (!reader) {
      throw ContentLoadError(ContentLoadFailure::kDependencyMissing,
        "Required physics descriptor is missing: "
          + data::to_string(reference.key));
    }
    const LoaderContext context {
      .current_asset_key = reference.key,
      .source_instance = source_instance,
      .desc_reader = reader.get(),
      .asset_references = observer_ptr(&references),
      .work_offline = true,
      .parse_only = true,
    };
    switch (reference.expected_type) {
    case data::AssetType::kCollisionShape: {
      auto descriptor = loaders::LoadCollisionShapeDescriptor(context);
      return { .references = std::move(references), .descriptor = descriptor };
    }
    case data::AssetType::kPhysicsMaterial: {
      auto descriptor = loaders::LoadPhysicsMaterialDescriptor(context);
      return { .references = std::move(references), .descriptor = descriptor };
    }
    default:
      throw ContentLoadError(ContentLoadFailure::kReferenceInvalid,
        "Physics dependency " + data::to_string(reference.key)
          + " is neither a collision shape nor a physics material");
    }
  }

  auto ParsePhysicsDescriptorAsync(co::ThreadPool& pool,
    std::shared_ptr<const internal::IContentSource> source,
    const data::KeyReference reference,
    const data::SourceInstanceId source_instance)
    -> co::Co<ParsedPhysicsDescriptor>
  {
    co_return co_await pool.Run([source = std::move(source), reference,
                                  source_instance] -> ParsedPhysicsDescriptor {
      return ParsePhysicsDescriptor(*source, reference, source_instance);
    });
  }

  [[noreturn]] auto RefuseSidecar(const data::AssetKey& key,
    const data::CookedDefect defect, const std::string_view what) -> void
  {
    throw data::CookedFormatError(
      defect, fmt::format("Physics sidecar descriptor {} {}", key, what));
  }

  //! The one effective physics sidecar in `view` that targets `scene_key`.
  auto FindPhysicsSidecar(const internal::ContentSourceView& view,
    const data::AssetKey& scene_key) -> std::optional<data::AssetKey>
  {
    std::optional<data::AssetKey> matched;
    for (const auto& layer : view.Layers()) {
      const auto& source = *layer.source;
      for (size_t i = 0; i < source.GetAssetCount(); ++i) {
        const auto key = source.GetAssetKeyByIndex(static_cast<uint32_t>(i));
        if (!key || source.GetAssetType(*key) != data::AssetType::kPhysicsScene
          || view.ResolveAsset(*key) != layer.id) {
          continue;
        }
        // The index lists the sidecar, so its descriptor must open.
        const auto reader = source.CreateAssetDescriptorReader(*key);
        if (!reader) {
          RefuseSidecar(*key, data::CookedDefect::kMissingPart, "is absent");
        }
        const auto bytes
          = reader->ReadBlob(sizeof(data::pak::physics::PhysicsSceneAssetDesc));
        if (!bytes) {
          RefuseSidecar(*key, data::CookedDefect::kTruncated, "is truncated");
        }
        const auto parsed
          = serio::CopyAt<data::pak::physics::PhysicsSceneAssetDesc>(*bytes);
        if (!parsed) {
          RefuseSidecar(*key, data::CookedDefect::kTruncated, "is truncated");
        }
        const auto& descriptor = *parsed;
        if (descriptor.header.asset_type
          != static_cast<uint8_t>(data::AssetType::kPhysicsScene)) {
          RefuseSidecar(
            *key, data::CookedDefect::kWrongMagic, "is not a physics scene");
        }
        if (descriptor.header.version
          != data::pak::physics::kPhysicsSceneAssetVersion) {
          RefuseSidecar(*key, data::CookedDefect::kUnsupportedVersion,
            "has an unsupported version");
        }
        if (descriptor.target_scene_key != scene_key) {
          continue;
        }
        if (matched) {
          throw ContentLoadError(ContentLoadFailure::kBindingFailed,
            "Multiple effective physics sidecars target scene "
              + data::to_string(scene_key));
        }
        matched = *key;
      }
    }
    return matched;
  }
} // namespace

#ifndef NDEBUG
auto AssetLoader::PeekCachedAsset(const uint64_t key) const
  -> std::shared_ptr<const data::Asset>
{
  auto erased = WithContentType(
    content_cache_.GetTypeId(key), [&]<typename T> -> std::shared_ptr<void> {
      if constexpr (std::derived_from<T, data::Asset>) {
        return std::static_pointer_cast<data::Asset>(
          content_cache_.Peek<T>(key));
      } else {
        return {};
      }
    });
  return std::static_pointer_cast<const data::Asset>(erased);
}
#endif

auto AssetLoader::AcquireCached(const TypeId type, const ContentId key)
  -> std::shared_ptr<void>
{
  AssertOwningThread();
  if (releases_->IsClosed()) {
    return {};
  }
  return WithContentType(type, [&]<typename T> -> std::shared_ptr<void> {
    auto acquired = internal::ContentAcquisition::FromCache<T>(
      content_cache_, *releases_, key.get(), CheckoutOwner::kExternal);
    return std::move(acquired.owner);
  });
}

auto AssetLoader::AcquireBoundAsset(
  const TypeId type, const data::AssetKey& key, const data::Asset& context)
  -> std::shared_ptr<void>
{
  AssertOwningThread();
  if (releases_->IsClosed()) {
    return {};
  }
  const auto& retained = context.GetRuntimeBindings();
  if (retained
    && retained->GetTypeId() == internal::ContentBindingBundle::ClassTypeId()) {
    const auto bundle
      = std::static_pointer_cast<const internal::ContentBindingBundle>(
        retained);
    if (const auto* bound = bundle->FindAssetBinding(key)) {
      return WithContentType(type, [&]<typename T> -> std::shared_ptr<void> {
        return bound->publication.Acquire<T>(
          content_cache_, *releases_, CheckoutOwner::kExternal);
      });
    }
  }
  return nullptr;
}

template <typename T>
auto AssetLoader::LoadAssetPublicationAsync(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> source, LoadRequest request,
  const CheckoutOwner role) -> co::Co<internal::ContentAcquisition>
{
  BeginAcceptedLoad();
  const auto completion
    = Finally([this] noexcept -> void { EndAcceptedLoad(); });
  AssertOwningThread();
  request = AdmitAssetRequest(NormalizeLoadRequest(std::move(request)));
  constexpr auto kind = AssetKind<T>();
  RecordAssetTelemetry(kind, LoadTelemetryEvent::kRequest);
  const auto target = PrepareAssetLoadRequest(key, source, request.scope);
  if (!target) {
    co_return internal::ContentAcquisition {};
  }
  const auto releases = releases_;
  const auto from_cache = [&] -> internal::ContentAcquisition {
    return internal::ContentAcquisition::FromCache<T>(
      content_cache_, *releases, target->cache_key, role);
  };
  const internal::InFlightOperationTable::OperationKey operation_key {
    .type_id = T::ClassTypeId(),
    .cache_key = target->cache_key,
  };
  auto cached = from_cache();
  while (
    !cached && co_await in_flight_ops_->AwaitClosedOperation(operation_key)) {
    cached = from_cache();
  }
  if (cached) {
    RecordAssetTelemetry(kind, LoadTelemetryEvent::kCacheHit);
    co_return cached;
  }
  RecordAssetTelemetry(kind, LoadTelemetryEvent::kCacheMiss);
  const internal::InFlightOperationTable::RequestMeta meta {
    .priority = request.priority,
    .intent = request.intent,
    .sequence = next_load_request_sequence_.fetch_add(1),
  };
  internal::SharedContentResult result;
  if (auto joined = in_flight_ops_->Find(operation_key, meta)) {
    RecordAssetTelemetry(kind, LoadTelemetryEvent::kTasksDeduped);
    result = co_await *joined;
  } else {
    RecordAssetTelemetry(kind, LoadTelemetryEvent::kTasksSpawned);
    const auto operation = in_flight_ops_->NewOperationId();
    co::Shared shared(internal::RunToCompletion(DecodeAndPublishAssetAsync<T>(
      key, *target, request, operation.get(), releases)));
    in_flight_ops_->Insert(operation_key, operation, shared, meta);
    result = co_await shared;
  }
  if (!result || !ResolveSourceForId(target->source_id)) {
    co_return internal::ContentAcquisition {};
  }
  auto owner = result.publication.Acquire<T>(content_cache_, *releases, role);
  co_return internal::ContentAcquisition {
    .publication = result.publication,
    .owner = std::move(owner),
  };
}

template <typename T>
auto AssetLoader::DecodeAndPublishAssetAsync(data::AssetKey key,
  AssetLoadRequest target, LoadRequest request, const uint64_t operation_id,
  std::shared_ptr<internal::ContentReleaseQueue> releases)
  -> co::Co<internal::SharedContentResult>
{
  const auto erase = Finally([&] noexcept -> void {
    in_flight_ops_->Erase(
      {
        .type_id = T::ClassTypeId(),
        .cache_key = target.cache_key,
      },
      internal::InFlightOperationTable::OperationId { operation_id });
  });
  constexpr auto kind = AssetKind<T>();
  try {
    releases->RequireOpen();
    auto decoded = co_await DecodeAssetAsyncErasedImpl(
      T::ClassTypeId(), key, target.source_id, request);
    releases->RequireOpen();
    // The decoder registered for `T` decodes a `T`, or nothing when the source
    // left the mounts or no longer lists the asset.
    const auto typed = std::static_pointer_cast<T>(decoded.asset);
    if (!typed) {
      RecordAssetTelemetry(kind, LoadTelemetryEvent::kDecodeFailure);
      co_return internal::SharedContentResult {};
    }
    DCHECK_F(typed->GetTypeId() == T::ClassTypeId());
    DCHECK_NOTNULL_F(decoded.dependency_collector);
    typed->SetReferences(std::move(decoded.references));
    const auto& view = request.scope.state_->view;
    internal::ContentBindingBuilder builder(releases);
    BindExternalTextures(*typed, view.get(), builder);
    co_await BindDependenciesAsync(
      *typed, *decoded.dependency_collector, builder, request);
    releases->RequireOpen();
    const auto token = !typed->GetReferences().Keys().empty() && view
      ? view->IdentityOwner()
      : std::shared_ptr<const internal::BindingViewId> {};
    const auto bindings = std::move(builder).Freeze(
      internal::ContentId { target.cache_key }, token);
    typed->SetRuntimeBindings(bindings);
    if (!ResolveSourceForId(target.source_id)) {
      co_return internal::SharedContentResult {};
    }
    auto result = internal::SharedContentResult::Publish(
      content_cache_, *releases, target.cache_key, typed);
    if (!result.publication.entry) {
      MaybeAutoTrimOnBudgetPressure("asset_store_failed", true);
      if (!ResolveSourceForId(target.source_id)) {
        co_return internal::SharedContentResult {};
      }
      result = internal::SharedContentResult::Publish(
        content_cache_, *releases, target.cache_key, typed);
      if (!result.publication.entry) {
        RecordAssetTelemetry(kind, LoadTelemetryEvent::kStoreRetryFailure);
      }
    }
    if (result.publication.entry
      && content_cache_.Contains(*result.publication.entry)) {
      MaybeAutoTrimOnBudgetPressure("asset_store_succeeded");
    }
    co_return ResolveSourceForId(target.source_id)
      ? std::move(result)
      : internal::SharedContentResult {};
  } catch (const co::TaskCancelledException& error) {
    RecordAssetTelemetry(kind, LoadTelemetryEvent::kCancellation);
    throw OperationCancelledException(error.what());
  }
}

template <typename T>
auto AssetLoader::BindAssetAsync(const data::AssetKey& key,
  const data::Asset& parent, internal::ContentBindingBuilder& bindings,
  LoadRequest request) -> co::Co<>
{
  bindings.RequireOpen();
  if (key.IsNil() || !bindings.TryBeginAsset(key, T::ClassTypeId())) {
    co_return;
  }
  const auto dependency = fmt::format("Required {} dependency {} of asset {}",
    T::ClassTypeNamePretty(), key, parent.GetAssetKey());
  const auto source = ResolveScopedRoot(key, std::nullopt, request.scope);
  if (!source) {
    throw ContentLoadError(
      ContentLoadFailure::kDependencyMissing, dependency + " is missing");
  }
  auto acquired
    = co_await AwaitDependency(LoadAssetPublicationAsync<T>(key, source,
                                 request, CheckoutOwner::kInternal),
      dependency);
  bindings.RequireOpen();
  if (!acquired) {
    // Its source left the mounts while it loaded.
    throw ContentLoadError(
      ContentLoadFailure::kDependencyMissing, dependency + " is missing");
  }
  static_cast<void>(bindings.AddAsset<T>(std::move(acquired)));
}

template <typename T>
auto AssetLoader::BindResourceAsync(ResourceKey key,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  bindings.RequireOpen();
  if (key.get() == 0U || key.IsError() || !bindings.TryBeginResource(key)) {
    co_return;
  }
  const auto dependency = fmt::format(
    "Required {} resource {}", T::ClassTypeNamePretty(), key.get());
  auto acquired = co_await AwaitDependency(
    resource_load_pipeline_->LoadErased(
      T::ClassTypeId(), key, request, CheckoutOwner::kInternal),
    dependency);
  bindings.RequireOpen();
  if (!acquired) {
    throw ContentLoadError(
      ContentLoadFailure::kDependencyMissing, dependency + " is missing");
  }
  static_cast<void>(bindings.AddResource<T>(key, std::move(acquired)));
}

template <typename T>
auto AssetLoader::BindCollectedResourcesAsync(
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  auto keys = std::vector<ResourceKey> {};
  for (const auto& reference : collector.ResourceRefDependencies()) {
    if (reference.resource_type_id == T::ClassTypeId()) {
      keys.push_back(BindResourceRefToKey(reference));
    }
  }
  for (const auto key : collector.ResourceKeyDependencies()) {
    const auto kind
      = identities_->FindResourceKind(internal::ContentId { key.get() });
    if (kind && internal::ResourceTypeId(*kind) == T::ClassTypeId()) {
      keys.push_back(key);
    }
  }
  // Independent loads start together, so their payload reads are in flight
  // on the device at once.
  OXCO_WITH_NURSERY(nursery)
  {
    for (const auto key : keys) {
      nursery.Start([this, key, &bindings, request] -> co::Co<> {
        co_await BindResourceAsync<T>(key, bindings, request);
      });
    }
    co_return co::kJoin;
  };
}

auto AssetLoader::BindDependenciesAsync(data::MaterialAsset& asset,
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  BindMaterialTextureKeys(asset, asset.GetSourceOrigin().instance, bindings);
  const std::array keys {
    asset.GetBaseColorTextureKey(),
    asset.GetNormalTextureKey(),
    asset.GetMetallicTextureKey(),
    asset.GetRoughnessTextureKey(),
    asset.GetAmbientOcclusionTextureKey(),
    asset.GetEmissiveTextureKey(),
    asset.GetSpecularTextureKey(),
    asset.GetSheenColorTextureKey(),
    asset.GetClearcoatTextureKey(),
    asset.GetClearcoatNormalTextureKey(),
    asset.GetTransmissionTextureKey(),
    asset.GetThicknessTextureKey(),
  };
  OXCO_WITH_NURSERY(nursery)
  {
    for (const auto key : keys) {
      nursery.Start([this, key, &bindings, request] -> co::Co<> {
        co_await BindResourceAsync<data::TextureResource>(
          key, bindings, request);
      });
    }
    nursery.Start([this, &collector, &bindings, request] -> co::Co<> {
      co_await BindCollectedResourcesAsync<data::TextureResource>(
        collector, bindings, request);
    });
    co_return co::kJoin;
  };
}

auto AssetLoader::BindDependenciesAsync(data::GeometryAsset& asset,
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  OXCO_WITH_NURSERY(nursery)
  {
    nursery.Start([this, &collector, &bindings, request] -> co::Co<> {
      co_await BindCollectedResourcesAsync<data::BufferResource>(
        collector, bindings, request);
    });
    for (const auto& key : collector.AssetDependencies()) {
      nursery.Start([this, key, &asset, &bindings, request] -> co::Co<> {
        co_await BindAssetAsync<data::MaterialAsset>(
          key, asset, bindings, request);
      });
    }
    co_return co::kJoin;
  };
  LoadedGeometryBuffersByIndex buffers;
  for (const auto& reference : collector.ResourceRefDependencies()) {
    if (reference.resource_type_id != data::BufferResource::ClassTypeId()) {
      continue;
    }
    const auto key = BindResourceRefToKey(reference);
    if (auto buffer = bindings.FindResource<data::BufferResource>(key)) {
      buffers.insert_or_assign(reference.resource_index.get(),
        LoadedGeometryBuffer { .key = key, .resource = std::move(buffer) });
    }
  }
  for (const auto key : collector.ResourceKeyDependencies()) {
    auto buffer = bindings.FindResource<data::BufferResource>(key);
    const auto* locator
      = identities_->FindCookedResource(internal::ContentId { key.get() });
    if (buffer && locator) {
      buffers.insert_or_assign(locator->index,
        LoadedGeometryBuffer { .key = key, .resource = std::move(buffer) });
    }
  }
  LoadedGeometryMaterialsByKey materials;
  for (const auto& key : collector.AssetDependencies()) {
    if (auto material = bindings.FindAsset<data::MaterialAsset>(key)) {
      materials.insert_or_assign(key, std::move(material));
    }
  }
  BindGeometryRuntimePointers(asset, buffers, materials);
}

auto AssetLoader::BindDependenciesAsync(data::SceneAsset& asset,
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  OXCO_WITH_NURSERY(nursery)
  {
    nursery.Start([this, &collector, &bindings, request] -> co::Co<> {
      co_await BindCollectedResourcesAsync<data::TextureResource>(
        collector, bindings, request);
    });
    nursery.Start([this, &collector, &bindings, request] -> co::Co<> {
      co_await BindCollectedResourcesAsync<data::BufferResource>(
        collector, bindings, request);
    });
    nursery.Start(
      [this, key = asset.GetAssetKey(), &bindings, request] -> co::Co<> {
        co_await BindPhysicsSidecarAsync(key, bindings, request);
      });
    for (const auto& renderable :
      asset.GetComponents<data::pak::world::RenderableRecord>()) {
      nursery.Start([this, key = renderable.geometry_key, &asset, &bindings,
                      request] -> co::Co<> {
        co_await BindAssetAsync<data::GeometryAsset>(
          key, asset, bindings, request);
      });
    }
    for (const auto& record :
      asset.GetComponents<data::pak::world::OverrideRecord>()) {
      const auto* property
        = data::schema::FindOverrideProperty(record.override_id);
      // The scene validated every override against the schema when it
      // loaded.
      DCHECK_NOTNULL_F(property);
      if (property->type == data::schema::ValueType::kAsset) {
        nursery.Start(
          [this, key = record.value.as_asset, type = property->asset_type,
            &asset, &bindings, request] -> co::Co<> {
            co_await BindSceneAssetValueAsync(
              key, type, asset, bindings, request);
          });
      }
    }
    for (const auto& component :
      asset.GetComponents<data::pak::scripting::ScriptingComponentRecord>()) {
      for (const auto& slot : GetHydratedScriptSlots(asset, component)) {
        nursery.Start([this, key = slot.script_asset_key, &asset, &bindings,
                        request] -> co::Co<> {
          co_await BindAssetAsync<data::ScriptAsset>(
            key, asset, bindings, request);
        });
        for (const auto& value : slot.params | std::views::values) {
          if (const auto* param = std::get_if<data::ScriptAssetParam>(&value)) {
            nursery.Start([this, key = param->key, type = param->asset_type,
                            &asset, &bindings, request] -> co::Co<> {
              co_await BindSceneAssetValueAsync(
                key, type, asset, bindings, request);
            });
          }
        }
      }
    }
    co_return co::kJoin;
  };
}

auto AssetLoader::BindSceneAssetValueAsync(const data::AssetKey& key,
  const data::AssetType type, data::SceneAsset& asset,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  // An asset a script parameter or an override names stays resident with the
  // scene, as the scene's own components do. Scenes and physics assets are not
  // loaded as dependencies of another scene.
  if (key.IsNil()) {
    co_return;
  }
  switch (type) {
  case data::AssetType::kMaterial:
    co_await BindAssetAsync<data::MaterialAsset>(key, asset, bindings, request);
    break;
  case data::AssetType::kGeometry:
    co_await BindAssetAsync<data::GeometryAsset>(key, asset, bindings, request);
    break;
  case data::AssetType::kScript:
    co_await BindAssetAsync<data::ScriptAsset>(key, asset, bindings, request);
    break;
  case data::AssetType::kInputAction:
    co_await BindAssetAsync<data::InputActionAsset>(
      key, asset, bindings, request);
    break;
  case data::AssetType::kInputMappingContext:
    co_await BindAssetAsync<data::InputMappingContextAsset>(
      key, asset, bindings, request);
    break;
  default:
    break;
  }
}

auto AssetLoader::BindDependenciesAsync(data::ScriptAsset& /*asset*/,
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  co_await BindCollectedResourcesAsync<data::ScriptResource>(
    collector, bindings, request);
}

auto AssetLoader::BindDependenciesAsync(data::InputMappingContextAsset& asset,
  const internal::DependencyCollector& /*collector*/,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  auto keys = std::vector<data::AssetKey> {};
  for (const auto& mapping : asset.GetMappings()) {
    keys.push_back(mapping.action_asset_key);
  }
  for (const auto& trigger : asset.GetTriggers()) {
    keys.push_back(trigger.linked_action_asset_key);
  }
  for (const auto& auxiliary : asset.GetTriggerAuxRecords()) {
    keys.push_back(auxiliary.action_asset_key);
  }
  OXCO_WITH_NURSERY(nursery)
  {
    for (const auto& key : keys) {
      nursery.Start([this, key, &asset, &bindings, request] -> co::Co<> {
        co_await BindAssetAsync<data::InputActionAsset>(
          key, asset, bindings, request);
      });
    }
    co_return co::kJoin;
  };
}

auto AssetLoader::BindPhysicsSidecarAsync(const data::AssetKey scene_key,
  internal::ContentBindingBuilder& bindings, const LoadRequest request)
  -> co::Co<>
{
  auto view = request.scope.state_->view;
  if (!view) {
    co_return;
  }
  // Every physics scene descriptor of the mounts is read to find the target.
  const auto sidecar = co_await thread_pool_->Run(
    [view = std::move(view), scene_key] -> std::optional<data::AssetKey> {
      return FindPhysicsSidecar(*view, scene_key);
    });
  bindings.RequireOpen();
  if (sidecar.has_value()) {
    auto physics = std::make_unique<internal::PhysicsBindings>();
    physics->sidecar = sidecar;
    bindings.SetPhysics(std::move(physics));
  }
}

auto AssetLoader::BindDependenciesAsync(data::PhysicsSceneAsset& asset,
  const internal::DependencyCollector& /*collector*/,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  const auto& view = request.scope.state_->view;
  auto frozen = std::make_unique<internal::PhysicsBindings>();
  const auto roots = asset.GetReferences().Keys();
  std::vector<data::KeyReference> pending(roots.begin(), roots.end());
  std::unordered_map<data::AssetKey, data::AssetType> descriptors;
  std::unordered_set<data::AssetKey> payloads;
  while (!pending.empty()) {
    bindings.RequireOpen();
    const auto reference = pending.back();
    pending.pop_back();
    if (reference.kind == data::KeyReferenceKind::kLogical) {
      continue;
    }
    if (!view) {
      throw ContentLoadError(ContentLoadFailure::kDependencyMissing,
        "Required physics dependency " + data::to_string(reference.key)
          + " is missing: nothing is mounted");
    }
    if (reference.kind == data::KeyReferenceKind::kPhysicsResource) {
      if (!payloads.insert(reference.key).second) {
        continue;
      }
      auto index = std::optional<data::pak::core::ResourceIndexT> {};
      const auto match = internal::ResolveByPrecedence(
        view->Layers(),
        [&](const internal::ContentSourceLayer& layer) -> bool {
          return layer.deleted.contains(reference.key);
        },
        [&](const internal::ContentSourceLayer& layer) -> bool {
          index = layer.source->FindPhysicsResource(reference.key);
          return index.has_value();
        });
      if (match.status != internal::KeyResolutionStatus::kFound
        || !index.has_value()) {
        throw ContentLoadError(ContentLoadFailure::kDependencyMissing,
          "Required physics payload is missing: "
            + data::to_string(reference.key));
      }
      const auto& layer = *match.layer;
      const auto source = ResolveSourceForId(layer.id);
      if (!source) {
        throw ContentLoadError(ContentLoadFailure::kDependencyMissing,
          "Required physics payload " + data::to_string(reference.key)
            + " left the mounts");
      }
      constexpr auto type_index = static_cast<uint16_t>(
        IndexOf<data::PhysicsResource, ResourceTypeList>::value);
      frozen->resources.push_back({
        .asset_key = reference.key,
        .key = InternResourceKey(layer.id, type_index, *index),
        .source = layer.id,
        .owner = source,
      });
      continue;
    }
    const auto [previous, inserted]
      = descriptors.emplace(reference.key, reference.expected_type);
    if (!inserted) {
      if (previous->second != reference.expected_type) {
        throw ContentLoadError(ContentLoadFailure::kReferenceInvalid,
          "Physics dependency " + data::to_string(reference.key)
            + " is referenced as two asset types");
      }
      continue;
    }
    const auto dependency
      = "Required physics descriptor " + data::to_string(reference.key);
    const auto source_id = view->ResolveAsset(reference.key);
    const auto source = source_id ? ResolveSourceForId(*source_id) : nullptr;
    if (!source_id || !source) {
      throw ContentLoadError(
        ContentLoadFailure::kDependencyMissing, dependency + " is missing");
    }
    if (source->GetAssetType(reference.key) != reference.expected_type) {
      throw ContentLoadError(ContentLoadFailure::kReferenceInvalid,
        dependency + " has another asset type");
    }
    auto parsed = co_await AwaitDependency(
      ParsePhysicsDescriptorAsync(*thread_pool_, source, reference, *source_id),
      dependency);
    bindings.RequireOpen();
    if (const auto* shape
      = std::get_if<data::pak::physics::CollisionShapeAssetDesc>(
        &parsed.descriptor)) {
      frozen->shapes.push_back({ .key = reference.key, .descriptor = *shape });
    } else {
      frozen->materials.push_back({
        .key = reference.key,
        .descriptor = std::get<data::pak::physics::PhysicsMaterialAssetDesc>(
          parsed.descriptor),
      });
    }
    const auto children = parsed.references.Keys();
    pending.insert(pending.end(), children.begin(), children.end());
  }
  std::ranges::sort(frozen->shapes, {}, &internal::BoundPhysicsShape::key);
  std::ranges::sort(
    frozen->materials, {}, &internal::BoundPhysicsMaterial::key);
  std::ranges::sort(
    frozen->resources, {}, &internal::BoundPhysicsResource::asset_key);
  bindings.SetPhysics(std::move(frozen));
  co_return;
}

auto AssetLoader::BindDependenciesAsync(data::InputActionAsset& /*asset*/,
  const internal::DependencyCollector& /*collector*/,
  internal::ContentBindingBuilder& /*bindings*/, LoadRequest /*request*/)
  -> co::Co<>
{
  co_return;
}

auto AssetLoader::LoadMaterialAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  LoadRequest request) -> co::Co<std::shared_ptr<data::MaterialAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::MaterialAsset>(
    key, preferred_source_id, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::MaterialAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadGeometryAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  LoadRequest request) -> co::Co<std::shared_ptr<data::GeometryAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::GeometryAsset>(
    key, preferred_source_id, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::GeometryAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadSceneAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  LoadRequest request) -> co::Co<std::shared_ptr<data::SceneAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::SceneAsset>(
    key, preferred_source_id, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::SceneAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadPhysicsSceneAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  LoadRequest request) -> co::Co<std::shared_ptr<data::PhysicsSceneAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::PhysicsSceneAsset>(
    key, preferred_source_id, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::PhysicsSceneAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadScriptAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  LoadRequest request) -> co::Co<std::shared_ptr<data::ScriptAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::ScriptAsset>(
    key, preferred_source_id, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::ScriptAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadInputActionAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  LoadRequest request) -> co::Co<std::shared_ptr<data::InputActionAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::InputActionAsset>(
    key, preferred_source_id, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::InputActionAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadInputMappingContextAssetAsyncImpl(
  const data::AssetKey& key,
  std::optional<data::SourceInstanceId> preferred_source_id,
  LoadRequest request)
  -> co::Co<std::shared_ptr<data::InputMappingContextAsset>>
{
  auto acquired
    = co_await LoadAssetPublicationAsync<data::InputMappingContextAsset>(
      key, preferred_source_id, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::InputMappingContextAsset>(
    std::move(acquired.owner));
}

} // namespace oxygen::content
