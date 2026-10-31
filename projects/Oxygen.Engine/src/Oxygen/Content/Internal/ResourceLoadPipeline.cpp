//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/Internal/ContentIdentity.h>
#include <Oxygen/Content/Internal/ContentIdentityRegistry.h>
#include <Oxygen/Content/Internal/ContentPublication.h>
#include <Oxygen/Content/Internal/ContentReleaseQueue.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/InFlightOperationTable.h>
#include <Oxygen/Content/Internal/PayloadBudget.h>
#include <Oxygen/Content/Internal/RegionReader.h>
#include <Oxygen/Content/Internal/ResourceLoadPipeline.h>
#include <Oxygen/Content/Internal/ResourceTypes.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/AnyCache.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/SourceOrigin.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Shared.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/AsyncDevice.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/ByteView.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Region.h>

namespace oxygen::content::internal {
namespace {

  struct SourceReadAccess final {
    observer_ptr<const ContentSourceRegistry> registry;
    data::SourceInstanceId instance;

    [[nodiscard]] auto IsReadable() const -> bool
    {
      return instance == data::SourceInstanceId {}
      || (registry && registry->AcquireSource(instance));
    }
  };

  auto ValidateTypeFromDecoded(
    const TypeId resource_type, const std::shared_ptr<void>& decoded) -> bool
  {
    if (!decoded) {
      return false;
    }

    if (resource_type == data::TextureResource::ClassTypeId()) {
      const auto typed
        = std::static_pointer_cast<data::TextureResource>(decoded);
      return typed
        && typed->GetTypeId() == data::TextureResource::ClassTypeId();
    }
    if (resource_type == data::BufferResource::ClassTypeId()) {
      const auto typed
        = std::static_pointer_cast<data::BufferResource>(decoded);
      return typed && typed->GetTypeId() == data::BufferResource::ClassTypeId();
    }
    if (resource_type == data::ScriptResource::ClassTypeId()) {
      const auto typed
        = std::static_pointer_cast<data::ScriptResource>(decoded);
      return typed && typed->GetTypeId() == data::ScriptResource::ClassTypeId();
    }
    if (resource_type == data::PhysicsResource::ClassTypeId()) {
      const auto typed
        = std::static_pointer_cast<data::PhysicsResource>(decoded);
      return typed
        && typed->GetTypeId() == data::PhysicsResource::ClassTypeId();
    }
    return false;
  }

  auto AcquirePublication(const ContentPublication& publication,
    ResourceLoadPipeline::ContentCache& cache, ContentReleaseQueue& releases,
    const CheckoutOwner owner) -> ContentAcquisition
  {
    if (!publication.value) {
      return {};
    }
    return WithResourceType(publication.type, [&]<typename T>() -> auto {
      return ContentAcquisition {
        .publication = publication,
        .owner = publication.Acquire<T>(cache, releases, owner),
      };
    });
  }

  using Payloads = std::optional<serio::Region<serio::AnyAsyncDevice>>;

  //! A payload read into the buffer its loader takes, with the budget lease
  //! that bounds it until the loader has taken it.
  struct LoadedPayload final {
    serio::ByteBuffer bytes;
    PayloadBudget::Lease lease;
  };

  struct PreparedResourceDecode final {
    ResourceLoadPipeline::ResourceLoadFn loader;
    data::SourceInstanceId source_instance;
    std::unique_ptr<serio::AnyReader> desc_reader;
    //! Where the descriptor's payload lies, in the space of `payloads`.
    serio::ByteRange extent;
    Payloads payloads;
    std::shared_ptr<const IContentSource> source_content;
  };

  struct ResolvedSourceForDecode final {
    std::shared_ptr<const IContentSource> source;
    data::SourceInstanceId source_instance;
  };

  auto ResolveSourceById(const ContentSourceRegistry& source_registry,
    const data::SourceInstanceId source_id)
    -> std::optional<ResolvedSourceForDecode>
  {
    auto source = source_registry.AcquireSource(source_id);
    if (!source) {
      return std::nullopt;
    }
    return ResolvedSourceForDecode {
      .source = std::move(source),
      .source_instance = source_id,
    };
  }

  auto ResolveLoaderForType(
    const ResourceLoadPipeline::ResourceLoaderMap& resource_loaders,
    const TypeId resource_type)
    -> std::optional<ResourceLoadPipeline::ResourceLoadFn>
  {
    const auto loader_it = resource_loaders.find(resource_type);
    if (loader_it == resource_loaders.end()) {
      LOG_F(
        ERROR, "No loader registered for resource type id: {}", resource_type);
      return std::nullopt;
    }
    return loader_it->second;
  }

  //! The size of the descriptor of `resource_type`.
  auto DescriptorSizeOf(const TypeId resource_type) -> size_t
  {
    return WithResourceType(resource_type,
      []<typename T>() -> size_t { return sizeof(typename T::DescT); });
  }

  //! The payload extent the descriptor `row` of `resource_type` names.
  auto PayloadExtentOf(const TypeId resource_type,
    const std::span<const std::byte> row) -> serio::ByteRange
  {
    return WithResourceType(
      resource_type, [row]<typename T>() -> serio::ByteRange {
        auto desc = typename T::DescT {};
        if (row.size() < sizeof(desc)) {
          throw std::runtime_error("Resource descriptor is truncated");
        }
        std::memcpy(&desc, row.data(), sizeof(desc));
        return {
          .offset = serio::ByteOffset { desc.data_offset },
          .count = serio::ByteCount { desc.size_bytes },
        };
      });
  }

  //! Awaits the payload `extent` of `payloads` into a buffer of its size,
  //! once the budget has room for it.
  auto ReadPayload(const Payloads payloads, const serio::ByteRange extent,
    const observer_ptr<PayloadBudget> budget) -> co::Co<LoadedPayload>
  {
    if (extent.count.get() == 0) {
      co_return LoadedPayload {};
    }
    if (!payloads) {
      throw std::runtime_error("The source holds no payloads of this resource");
    }
    const auto region = IContentSource::PayloadWithin(*payloads, extent);
    if (!region) {
      throw std::runtime_error("Resource payload lies outside its region");
    }
    auto lease = co_await budget->Acquire(extent.count);
    auto buffer = serio::ByteBuffer::Allocate(extent.count);
    if (!buffer) {
      throw std::system_error(buffer.error(), "Allocate a resource payload");
    }
    const auto read = co_await serio::AsyncReadExactlyAt(
      *region, serio::ByteOffset { 0 }, buffer->Bytes());
    if (!read) {
      throw std::system_error(read.error(), "Read a resource payload");
    }
    co_return LoadedPayload {
      .bytes = std::move(buffer).value(),
      .lease = std::move(lease),
    };
  }

  //! Decodes `prepared` on `pool` once its payload is in memory.
  auto DecodePrepared(PreparedResourceDecode prepared,
    const observer_ptr<co::ThreadPool> pool,
    const observer_ptr<PayloadBudget> budget, const bool work_offline,
    const LoadPriorityClass default_priority, const LoadRequest request)
    -> co::Co<std::shared_ptr<void>>
  {
    // The lease stays in this frame, on the loop thread, until the loader has
    // taken the buffer.
    auto payload = co_await ReadPayload(
      std::move(prepared.payloads), prepared.extent, budget);
    co_return co_await pool->Run(
      [prepared = std::move(prepared), bytes = std::move(payload.bytes),
        work_offline, default_priority,
        request] mutable -> std::shared_ptr<void> {
        const auto source_key = prepared.source_content
          ? prepared.source_content->GetSourceKey()
          : data::SourceKey {};
        auto context = LoaderContext {
          .current_asset_key = {},
          .source_instance = prepared.source_instance,
          .desc_reader = prepared.desc_reader.get(),
          .payload = observer_ptr { &bytes },
          .work_offline = work_offline,
          .default_priority_class = default_priority,
          .request_priority = request.priority,
          .request_intent = request.intent,
          .source_content = prepared.source_content,
          .source_key = source_key,
        };
        return prepared.loader(context);
      });
  }

  auto PrepareResourceDecode(const ContentSourceRegistry& source_registry,
    const ResourceLoadPipeline::ResourceLoaderMap& resource_loaders,
    const TypeId resource_type, const data::SourceInstanceId source_id,
    const data::pak::core::ResourceIndexT resource_index)
    -> std::optional<PreparedResourceDecode>
  {
    const auto resolved_source = ResolveSourceById(source_registry, source_id);
    if (!resolved_source.has_value()) {
      return std::nullopt;
    }
    const auto& source = resolved_source->source;

    auto loader_opt = ResolveLoaderForType(resource_loaders, resource_type);
    if (!loader_opt.has_value()) {
      return std::nullopt;
    }

    const auto row
      = source->ResourceDescriptorBytes(resource_type, resource_index);
    if (!row) {
      return std::nullopt;
    }

    PreparedResourceDecode prepared {};
    prepared.loader = std::move(*loader_opt);
    prepared.source_instance = resolved_source->source_instance;
    prepared.desc_reader
      = RegionReader<serio::ByteView>::Owning(serio::ByteView(*row));
    prepared.extent = PayloadExtentOf(resource_type, *row);
    prepared.payloads = source->PayloadRegion(resource_type);
    prepared.source_content = source;

    return prepared;
  }

  template <typename DecodeFn>
  auto DecodeAndPublishResource(const TypeId resource_type,
    const ResourceKey key, SourceReadAccess access,
    observer_ptr<ResourceLoadPipeline::ContentCache> cache,
    observer_ptr<InFlightOperationTable> in_flight_ops,
    observer_ptr<const ResourceLoadPipeline::Callbacks> callbacks,
    std::shared_ptr<ContentReleaseQueue> releases,
    InFlightOperationTable::OperationId operation_id, DecodeFn decode_fn)
    -> co::Co<SharedContentResult>
  {
    const auto erase = Finally([&]() noexcept -> auto {
      in_flight_ops->Erase(resource_type, key.get(), operation_id);
    });
    if (!access.IsReadable()) {
      co_return SharedContentResult {};
    }
    auto cached = WithResourceType(resource_type, [&]<typename T>() -> auto {
      return SharedContentResult::FromCache<T>(*cache, *releases, key.get());
    });
    if (cached) {
      co_return cached;
    }
    releases->RequireOpen();
    auto decoded = co_await decode_fn();
    releases->RequireOpen();
    callbacks->assert_owning_thread();
    if (!decoded) {
      if (callbacks->on_resource_decode_failure) {
        callbacks->on_resource_decode_failure(resource_type);
      }
      co_return SharedContentResult {};
    }
    if (!ValidateTypeFromDecoded(resource_type, decoded)) {
      if (callbacks->on_resource_type_mismatch) {
        callbacks->on_resource_type_mismatch(resource_type);
      }
      co_return SharedContentResult {};
    }
    if (!access.IsReadable()) {
      co_return SharedContentResult {};
    }
    const auto publish = [&]() -> auto {
      return WithResourceType(resource_type, [&]<typename T>() -> auto {
        return SharedContentResult::Publish(
          *cache, *releases, key.get(), std::static_pointer_cast<T>(decoded));
      });
    };
    auto result = publish();
    if (!result.publication.entry && callbacks->on_store_pressure) {
      if (callbacks->on_resource_store_retry) {
        callbacks->on_resource_store_retry(resource_type);
      }
      callbacks->on_store_pressure("resource_store_failed", true);
      if (!access.IsReadable()) {
        co_return SharedContentResult {};
      }
      result = publish();
      if (!result.publication.entry
        && callbacks->on_resource_store_retry_failed) {
        callbacks->on_resource_store_retry_failed(resource_type);
      }
    }
    if (result.publication.entry
      && cache->Contains(*result.publication.entry)) {
      callbacks->on_resource_published(key);
      if (callbacks->on_store_pressure && cache->IsOverBudget()) {
        callbacks->on_store_pressure("resource_store_over_budget", false);
      }
    }
    co_return access.IsReadable() ? std::move(result) : SharedContentResult {};
  }

  template <typename DecodeFn>
  auto RunResourceLoadSharedStages(const TypeId resource_type,
    const ResourceKey key, SourceReadAccess access,
    ResourceLoadPipeline::ContentCache& cache,
    InFlightOperationTable& in_flight_ops,
    const ResourceLoadPipeline::Callbacks& callbacks,
    std::shared_ptr<ContentReleaseQueue> releases, const LoadRequest request,
    const CheckoutOwner owner, const uint64_t request_sequence,
    DecodeFn decode_fn) -> co::Co<ContentAcquisition>
  {
    callbacks.assert_owning_thread();
    if (!access.IsReadable()) {
      co_return ContentAcquisition {};
    }
    if (callbacks.on_resource_request) {
      callbacks.on_resource_request(resource_type);
    }
    auto cached = WithResourceType(resource_type, [&]<typename T>() -> auto {
      return ContentAcquisition::FromCache<T>(
        cache, *releases, key.get(), owner);
    });
    if (cached) {
      if (callbacks.on_resource_cache_hit) {
        callbacks.on_resource_cache_hit(resource_type);
      }
      co_return cached;
    }
    if (callbacks.on_resource_cache_miss) {
      callbacks.on_resource_cache_miss(resource_type);
    }
    const InFlightOperationTable::RequestMeta meta {
      .priority = request.priority,
      .intent = request.intent,
      .sequence = request_sequence,
    };
    if (auto joined = in_flight_ops.Find(resource_type, key.get(), meta)) {
      if (callbacks.on_resource_joined_inflight) {
        callbacks.on_resource_joined_inflight(resource_type);
      }
      const auto result = co_await *joined;
      co_return access.IsReadable()
        ? AcquirePublication(result.publication, cache, *releases, owner)
        : ContentAcquisition {};
    }
    if (callbacks.on_resource_started_inflight) {
      callbacks.on_resource_started_inflight(resource_type);
    }
    const auto operation_id = in_flight_ops.NewOperationId();
    auto op = DecodeAndPublishResource(resource_type, key, access,
      observer_ptr { &cache }, observer_ptr { &in_flight_ops },
      observer_ptr { &callbacks }, releases, operation_id,
      std::move(decode_fn));
    co::Shared shared(std::move(op));
    in_flight_ops.Insert(resource_type, key.get(), operation_id, shared, meta);
    const auto result = co_await shared;
    co_return access.IsReadable()
      ? AcquirePublication(result.publication, cache, *releases, owner)
      : ContentAcquisition {};
  }

} // namespace

ResourceLoadPipeline::ResourceLoadPipeline(
  const ContentSourceRegistry& source_registry,
  const ContentIdentityRegistry& identities,
  const ResourceLoaderMap& resource_loaders, ContentCache& content_cache,
  InFlightOperationTable& in_flight_ops,
  const std::shared_ptr<ContentReleaseQueue>& releases,
  const observer_ptr<co::ThreadPool> thread_pool,
  const observer_ptr<PayloadBudget> payload_budget, const bool work_offline,
  Callbacks callbacks)
  : source_registry_(source_registry)
  , identities_(identities)
  , resource_loaders_(resource_loaders)
  , content_cache_(content_cache)
  , in_flight_ops_(in_flight_ops)
  , releases_(releases)
  , thread_pool_(thread_pool)
  , payload_budget_(payload_budget)
  , work_offline_(work_offline)
  , callbacks_(std::move(callbacks))
{
}

auto ResourceLoadPipeline::LoadErased(const TypeId resource_type,
  const ResourceKey key, LoadRequest request, const CheckoutOwner owner)
  -> co::Co<ContentAcquisition>
{
  if (!thread_pool_) {
    throw std::runtime_error(
      "AssetLoader requires a thread pool for async loads (LoadResourceAsync)");
  }

  const auto identity = ContentId { key.get() };
  const auto kind = identities_.FindResourceKind(identity);
  if (!kind || ResourceTypeId(*kind) != resource_type) {
    co_return ContentAcquisition {};
  }
  const auto* locator = identities_.FindCookedResource(identity);
  const auto source_id = locator ? locator->source : data::SourceInstanceId {};
  const auto resource_index
    = data::pak::core::ResourceIndexT { locator ? locator->index : 0U };
  const auto source_owner
    = locator ? source_registry_.AcquireSource(source_id) : nullptr;

  auto decode_fn = [this, resource_type, request, source_id, resource_index,
                     source_owner] -> co::Co<std::shared_ptr<void>> {
    if (!source_owner) {
      co_return nullptr;
    }
    auto prepared_opt = PrepareResourceDecode(source_registry_,
      resource_loaders_, resource_type, source_id, resource_index);
    if (!prepared_opt.has_value()) {
      co_return nullptr;
    }

    const auto default_priority = callbacks_.default_priority_class
      ? callbacks_.default_priority_class()
      : LoadPriorityClass::kDefault;
    auto decoded = co_await DecodePrepared(std::move(*prepared_opt),
      thread_pool_, payload_budget_, work_offline_, default_priority, request);
    co_return source_registry_.AcquireSource(source_id) ? std::move(decoded)
                                                        : nullptr;
  };

  const auto request_sequence = callbacks_.next_request_sequence
    ? callbacks_.next_request_sequence()
    : 0U;
  co_return co_await RunResourceLoadSharedStages(resource_type, key,
    SourceReadAccess {
      .registry = observer_ptr { &source_registry_ },
      .instance = source_id,
    },
    content_cache_, in_flight_ops_, callbacks_, releases_, request, owner,
    request_sequence, std::move(decode_fn));
}

auto ResourceLoadPipeline::LoadErasedFromCooked(const TypeId resource_type,
  const ResourceKey key, std::span<const uint8_t> bytes, LoadRequest request,
  const CheckoutOwner owner) -> co::Co<ContentAcquisition>
{
  if (!thread_pool_) {
    throw std::runtime_error("AssetLoader requires a thread pool for async "
                             "loads (LoadResourceAsyncFromCookedErased)");
  }

  if (resource_type != data::TextureResource::ClassTypeId()
    && resource_type != data::BufferResource::ClassTypeId()
    && resource_type != data::ScriptResource::ClassTypeId()) {
    throw std::runtime_error(
      "LoadResourceAsync(cooked) is not implemented for this resource type");
  }

  const auto kind = identities_.FindResourceKind(ContentId { key.get() });
  const auto* identity = identities_.Resolve(ContentId { key.get() });
  if (!kind || ResourceTypeId(*kind) != resource_type || identity == nullptr
    || !std::holds_alternative<SyntheticResourceIdentity>(*identity)) {
    co_return ContentAcquisition {};
  }

  // Copy bytes eagerly to ensure the payload outlives thread-pool execution.
  auto owned_bytes
    = std::make_shared<std::vector<uint8_t>>(bytes.begin(), bytes.end());

  auto decode_fn = [this, resource_type, owned_bytes,
                     request] -> co::Co<std::shared_ptr<void>> {
    auto loader_opt = ResolveLoaderForType(resource_loaders_, resource_type);
    if (!loader_opt.has_value()) {
      co_return nullptr;
    }

    const auto default_priority = callbacks_.default_priority_class
      ? callbacks_.default_priority_class()
      : LoadPriorityClass::kDefault;
    // Cooked bytes are a descriptor followed by its payload, whose data
    // offset is an offset of the bytes.
    const auto bytes = std::as_bytes(std::span(*owned_bytes));
    const auto device = serio::AnyAsyncDevice(serio::ByteView(bytes));
    auto prepared = PreparedResourceDecode {
      .loader = std::move(*loader_opt),
      .source_instance = {},
      .desc_reader
      = RegionReader<serio::ByteView>::Owning(serio::ByteView(bytes.first(
        (std::min)(bytes.size(), DescriptorSizeOf(resource_type))))),
      .extent = PayloadExtentOf(resource_type, bytes),
      .payloads = serio::Region<serio::AnyAsyncDevice>::Whole(device).value(),
      .source_content = nullptr,
    };
    co_return co_await DecodePrepared(std::move(prepared), thread_pool_,
      payload_budget_, work_offline_, default_priority, request);
  };

  const auto request_sequence = callbacks_.next_request_sequence
    ? callbacks_.next_request_sequence()
    : 0U;
  co_return co_await RunResourceLoadSharedStages(resource_type, key,
    SourceReadAccess {}, content_cache_, in_flight_ops_, callbacks_, releases_,
    request, owner, request_sequence, std::move(decode_fn));
}

} // namespace oxygen::content::internal
