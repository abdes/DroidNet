//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/TypeList.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Internal/ContentBindingBundle.h>
#include <Oxygen/Content/Internal/ContentIdentity.h>
#include <Oxygen/Content/Internal/ContentIdentityRegistry.h>
#include <Oxygen/Content/Internal/ContentLoadScopeState.h>
#include <Oxygen/Content/Internal/ContentPublication.h>
#include <Oxygen/Content/Internal/ContentReleaseQueue.h>
#include <Oxygen/Content/Internal/ContentSourceView.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/InFlightOperationTable.h>
#include <Oxygen/Content/Internal/ResourceLoadPipeline.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/PhysicsAssetLoader.h>
#include <Oxygen/Content/OperationCancelledException.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Shared.h>
#include <Oxygen/OxCo/TaskCancelledException.h>

namespace oxygen::content {
namespace {
  template <typename Fn>
  auto WithContentType(const TypeId type, Fn&& fn) -> std::shared_ptr<void>
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
}

#ifndef NDEBUG
auto AssetLoader::PeekCachedAsset(const uint64_t key) const
  -> std::shared_ptr<const data::Asset>
{
  auto erased = WithContentType(
    content_cache_.GetTypeId(key), [&]<typename T>() -> std::shared_ptr<void> {
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

auto AssetLoader::AcquireCached(const TypeId type, const uint64_t key)
  -> std::shared_ptr<void>
{
  AssertOwningThread();
  if (releases_->IsClosed()) {
    return {};
  }
  return WithContentType(type, [&]<typename T>() {
    auto acquired = internal::ContentAcquisition::FromCache<T>(
      content_cache_, *releases_, key, CheckoutOwner::kExternal);
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
      return WithContentType(type, [&]<typename T>() -> std::shared_ptr<void> {
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
  const auto completion = Finally([this]() noexcept { EndAcceptedLoad(); });
  AssertOwningThread();
  request = AdmitAssetRequest(NormalizeLoadRequest(std::move(request)));
  constexpr auto kind = AssetKind<T>();
  RecordAssetTelemetry(kind, LoadTelemetryEvent::kRequest);
  const auto target = PrepareAssetLoadRequest(key, source, request.scope);
  if (!target) {
    co_return internal::ContentAcquisition {};
  }
  const auto releases = releases_;
  auto cached = internal::ContentAcquisition::FromCache<T>(
    content_cache_, *releases, target->cache_key, role);
  if (cached) {
    RecordAssetTelemetry(kind, LoadTelemetryEvent::kCacheHit);
    co_return cached;
  }
  RecordAssetTelemetry(kind, LoadTelemetryEvent::kCacheMiss);
  const internal::InFlightOperationTable::RequestMeta meta { .priority
    = request.priority,
    .intent = request.intent,
    .sequence = next_load_request_sequence_.fetch_add(1) };
  internal::SharedContentResult result;
  if (auto joined
    = in_flight_ops_->Find(T::ClassTypeId(), target->cache_key, meta)) {
    RecordAssetTelemetry(kind, LoadTelemetryEvent::kTasksDeduped);
    result = co_await *joined;
  } else {
    RecordAssetTelemetry(kind, LoadTelemetryEvent::kTasksSpawned);
    const auto operation = in_flight_ops_->NewOperationId();
    co::Shared shared(DecodeAndPublishAssetAsync<T>(
      key, *target, request, operation.get(), releases));
    in_flight_ops_->Insert(
      T::ClassTypeId(), target->cache_key, operation, shared, meta);
    result = co_await shared;
  }
  if (!result || !ResolveSourceForId(target->source_id)) {
    co_return internal::ContentAcquisition {};
  }
  auto owner = result.publication.Acquire<T>(content_cache_, *releases, role);
  co_return internal::ContentAcquisition { .publication = result.publication,
    .owner = std::move(owner) };
}

template <typename T>
auto AssetLoader::DecodeAndPublishAssetAsync(data::AssetKey key,
  AssetLoadRequest target, LoadRequest request, const uint64_t operation_id,
  std::shared_ptr<internal::ContentReleaseQueue> releases)
  -> co::Co<internal::SharedContentResult>
{
  const auto erase = Finally([&]() noexcept {
    in_flight_ops_->Erase(T::ClassTypeId(), target.cache_key,
      internal::InFlightOperationTable::OperationId { operation_id });
  });
  constexpr auto kind = AssetKind<T>();
  try {
    releases->RequireOpen();
    auto decoded = co_await DecodeAssetAsyncErasedImpl(
      T::ClassTypeId(), key, target.source_id, request);
    releases->RequireOpen();
    const auto typed = std::static_pointer_cast<T>(decoded.asset);
    if (!typed || typed->GetTypeId() != T::ClassTypeId()) {
      LOG_F(ERROR, "Loaded asset type mismatch: expected {}, got {}",
        T::ClassTypeNamePretty(), typed ? typed->GetTypeName() : "nullptr");
      RecordAssetTelemetry(kind, LoadTelemetryEvent::kTypeMismatch);
      if (!typed) {
        RecordAssetTelemetry(kind, LoadTelemetryEvent::kDecodeFailure);
      }
      co_return internal::SharedContentResult {};
    }
    if (!decoded.dependency_collector) {
      RecordAssetTelemetry(kind, LoadTelemetryEvent::kDecodeFailure);
      co_return internal::SharedContentResult {};
    }
    typed->SetReferences(std::move(decoded.references));
    internal::ContentBindingBuilder builder(releases);
    co_await BindDependenciesAsync(
      *typed, *decoded.dependency_collector, builder, request);
    releases->RequireOpen();
    const auto& view = request.scope.state_->view;
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
  LoadRequest request) -> co::Co<std::shared_ptr<T>>
{
  bindings.RequireOpen();
  if (key.IsNil()) {
    co_return nullptr;
  }
  if (!bindings.TryBeginAsset(key, T::ClassTypeId())) {
    co_return bindings.FindAsset<T>(key);
  }
  const auto source = ResolveScopedRoot(key, std::nullopt, request.scope);
  if (!source) {
    throw std::runtime_error(
      fmt::format("Required {} dependency {} is missing for asset {}",
        T::ClassTypeNamePretty(), key, parent.GetAssetKey()));
  }
  auto acquired = co_await LoadAssetPublicationAsync<T>(
    key, source, request, CheckoutOwner::kInternal);
  bindings.RequireOpen();
  if (!acquired) {
    throw std::runtime_error(
      fmt::format("Required {} dependency {} could not load for asset {}",
        T::ClassTypeNamePretty(), key, parent.GetAssetKey()));
  }
  co_return bindings.AddAsset<T>(std::move(acquired));
}

template <typename T>
auto AssetLoader::BindResourceAsync(ResourceKey key,
  internal::ContentBindingBuilder& bindings, LoadRequest request)
  -> co::Co<std::shared_ptr<T>>
{
  bindings.RequireOpen();
  if (key.get() == 0U || key.IsError()) {
    co_return nullptr;
  }
  if (!bindings.TryBeginResource(key)) {
    co_return bindings.FindResource<T>(key);
  }
  auto acquired = co_await resource_load_pipeline_->LoadErased(
    T::ClassTypeId(), key, request, CheckoutOwner::kInternal);
  bindings.RequireOpen();
  if (!acquired) {
    throw std::runtime_error(
      fmt::format("Required {} resource {} could not load",
        T::ClassTypeNamePretty(), key.get()));
  }
  co_return bindings.AddResource<T>(key, std::move(acquired));
}

template <typename T>
auto AssetLoader::BindCollectedResourcesAsync(
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  for (const auto& reference : collector.ResourceRefDependencies()) {
    if (reference.resource_type_id == T::ClassTypeId()) {
      static_cast<void>(co_await BindResourceAsync<T>(
        BindResourceRefToKey(reference), bindings, request));
    }
  }
  for (const auto key : collector.ResourceKeyDependencies()) {
    const auto kind
      = identities_->FindResourceKind(internal::ContentId { key.get() });
    if (kind && internal::ResourceTypeId(*kind) == T::ClassTypeId()) {
      static_cast<void>(co_await BindResourceAsync<T>(key, bindings, request));
    }
  }
}

auto AssetLoader::BindDependenciesAsync(data::MaterialAsset& asset,
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  BindMaterialTextureKeys(asset, asset.GetSourceOrigin().instance);
  const std::array keys { asset.GetBaseColorTextureKey(),
    asset.GetNormalTextureKey(), asset.GetMetallicTextureKey(),
    asset.GetRoughnessTextureKey(), asset.GetAmbientOcclusionTextureKey(),
    asset.GetEmissiveTextureKey(), asset.GetSpecularTextureKey(),
    asset.GetSheenColorTextureKey(), asset.GetClearcoatTextureKey(),
    asset.GetClearcoatNormalTextureKey(), asset.GetTransmissionTextureKey(),
    asset.GetThicknessTextureKey() };
  for (const auto key : keys) {
    static_cast<void>(co_await BindResourceAsync<data::TextureResource>(
      key, bindings, request));
  }
  co_await BindCollectedResourcesAsync<data::TextureResource>(
    collector, bindings, request);
}

auto AssetLoader::BindDependenciesAsync(data::GeometryAsset& asset,
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  co_await BindCollectedResourcesAsync<data::BufferResource>(
    collector, bindings, request);
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
    if (auto material = co_await BindAssetAsync<data::MaterialAsset>(
          key, asset, bindings, request)) {
      materials.insert_or_assign(key, std::move(material));
    }
  }
  BindGeometryRuntimePointers(asset, buffers, materials);
}

auto AssetLoader::BindDependenciesAsync(data::SceneAsset& asset,
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  co_await BindCollectedResourcesAsync<data::TextureResource>(
    collector, bindings, request);
  co_await BindCollectedResourcesAsync<data::BufferResource>(
    collector, bindings, request);
  for (const auto& renderable :
    asset.GetComponents<data::pak::world::RenderableRecord>()) {
    static_cast<void>(co_await BindAssetAsync<data::GeometryAsset>(
      renderable.geometry_key, asset, bindings, request));
  }
  for (const auto& assignment :
    asset.GetComponents<data::pak::world::MaterialOverrideRecord>()) {
    static_cast<void>(co_await BindAssetAsync<data::MaterialAsset>(
      assignment.material_key, asset, bindings, request));
  }
  for (const auto& component :
    asset.GetComponents<data::pak::scripting::ScriptingComponentRecord>()) {
    const auto slots = GetHydratedScriptSlots(asset, component);
    for (const auto& slot : slots) {
      static_cast<void>(co_await BindAssetAsync<data::ScriptAsset>(
        slot.script_asset_key, asset, bindings, request));
    }
  }
}

auto AssetLoader::BindDependenciesAsync(data::ScriptAsset&,
  const internal::DependencyCollector& collector,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  co_await BindCollectedResourcesAsync<data::ScriptResource>(
    collector, bindings, request);
}

auto AssetLoader::BindDependenciesAsync(data::InputMappingContextAsset& asset,
  const internal::DependencyCollector&,
  internal::ContentBindingBuilder& bindings, LoadRequest request) -> co::Co<>
{
  for (const auto& mapping : asset.GetMappings()) {
    static_cast<void>(co_await BindAssetAsync<data::InputActionAsset>(
      mapping.action_asset_key, asset, bindings, request));
  }
  for (const auto& trigger : asset.GetTriggers()) {
    static_cast<void>(co_await BindAssetAsync<data::InputActionAsset>(
      trigger.linked_action_asset_key, asset, bindings, request));
  }
  for (const auto& auxiliary : asset.GetTriggerAuxRecords()) {
    static_cast<void>(co_await BindAssetAsync<data::InputActionAsset>(
      auxiliary.action_asset_key, asset, bindings, request));
  }
}

auto AssetLoader::BindDependenciesAsync(data::PhysicsSceneAsset& asset,
  const internal::DependencyCollector&,
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
      throw std::runtime_error("Physics dependency has no content layer view");
    }
    if (reference.kind == data::KeyReferenceKind::kPhysicsResource) {
      if (!payloads.insert(reference.key).second) {
        continue;
      }
      bool found = false;
      for (const auto& layer : view->Layers() | std::views::reverse) {
        if (layer.deleted.contains(reference.key)) {
          break;
        }
        const auto* table = layer.source->GetPhysicsTable();
        if (!table || table->Size().get() == 0U) {
          continue;
        }
        const auto source = ResolveSourceForId(layer.id);
        if (!source) {
          throw std::runtime_error(
            "Physics payload source is no longer readable");
        }
        const auto index = source->FindPhysicsResource(reference.key);
        if (!index) {
          continue;
        }
        constexpr auto type_index = static_cast<uint16_t>(
          IndexOf<data::PhysicsResource, ResourceTypeList>::value);
        frozen->resources.push_back({ .asset_key = reference.key,
          .key = InternResourceKey(layer.id, type_index, *index),
          .source = layer.id,
          .owner = source });
        found = true;
        break;
      }
      if (!found) {
        throw std::runtime_error("Required physics payload is missing: "
          + data::to_string(reference.key));
      }
      continue;
    }
    const auto [previous, inserted]
      = descriptors.emplace(reference.key, reference.expected_type);
    if (!inserted) {
      if (previous->second != reference.expected_type) {
        throw std::runtime_error(
          "Physics dependency has conflicting asset types");
      }
      continue;
    }
    const auto source_id = view->ResolveAsset(reference.key);
    const auto source = source_id ? ResolveSourceForId(*source_id) : nullptr;
    if (!source
      || source->GetAssetType(reference.key) != reference.expected_type) {
      throw std::runtime_error(
        "Required physics descriptor is missing or has the wrong type: "
        + data::to_string(reference.key));
    }
    const auto references = source->ReadAssetReferences(reference.key);
    const auto reader = source->CreateAssetDescriptorReader(reference.key);
    if (!reader) {
      throw std::runtime_error(
        "Cannot read physics descriptor: " + data::to_string(reference.key));
    }
    const LoaderContext context { .current_asset_key = reference.key,
      .source_instance = *source_id,
      .desc_reader = reader.get(),
      .asset_references = observer_ptr(&references),
      .work_offline = true,
      .parse_only = true };
    switch (reference.expected_type) {
    case data::AssetType::kCollisionShape:
      frozen->shapes.push_back({ .key = reference.key,
        .descriptor = loaders::LoadCollisionShapeDescriptor(context) });
      break;
    case data::AssetType::kPhysicsMaterial:
      frozen->materials.push_back({ .key = reference.key,
        .descriptor = loaders::LoadPhysicsMaterialDescriptor(context) });
      break;
    default:
      throw std::runtime_error(
        "Unsupported physics descriptor dependency type");
    }
    const auto children = references.Keys();
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

auto AssetLoader::BindDependenciesAsync(data::InputActionAsset&,
  const internal::DependencyCollector&, internal::ContentBindingBuilder&,
  LoadRequest) -> co::Co<>
{
  co_return;
}

auto AssetLoader::LoadMaterialAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> source, LoadRequest request)
  -> co::Co<std::shared_ptr<data::MaterialAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::MaterialAsset>(
    key, source, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::MaterialAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadGeometryAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> source, LoadRequest request)
  -> co::Co<std::shared_ptr<data::GeometryAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::GeometryAsset>(
    key, source, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::GeometryAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadSceneAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> source, LoadRequest request)
  -> co::Co<std::shared_ptr<data::SceneAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::SceneAsset>(
    key, source, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::SceneAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadPhysicsSceneAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> source, LoadRequest request)
  -> co::Co<std::shared_ptr<data::PhysicsSceneAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::PhysicsSceneAsset>(
    key, source, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::PhysicsSceneAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadScriptAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> source, LoadRequest request)
  -> co::Co<std::shared_ptr<data::ScriptAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::ScriptAsset>(
    key, source, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::ScriptAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadInputActionAssetAsyncImpl(const data::AssetKey& key,
  std::optional<data::SourceInstanceId> source, LoadRequest request)
  -> co::Co<std::shared_ptr<data::InputActionAsset>>
{
  auto acquired = co_await LoadAssetPublicationAsync<data::InputActionAsset>(
    key, source, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::InputActionAsset>(
    std::move(acquired.owner));
}

auto AssetLoader::LoadInputMappingContextAssetAsyncImpl(
  const data::AssetKey& key, std::optional<data::SourceInstanceId> source,
  LoadRequest request)
  -> co::Co<std::shared_ptr<data::InputMappingContextAsset>>
{
  auto acquired
    = co_await LoadAssetPublicationAsync<data::InputMappingContextAsset>(
      key, source, request, CheckoutOwner::kExternal);
  co_return std::static_pointer_cast<data::InputMappingContextAsset>(
    std::move(acquired.owner));
}

} // namespace oxygen::content
