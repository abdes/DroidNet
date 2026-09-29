//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Logging.h>
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
#include <Oxygen/Content/Internal/ResourceLoadPipeline.h>
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
#include <Oxygen/Serio/AlignmentGuard.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::internal {
namespace {

  struct SourceReadAccess final {
    observer_ptr<const ContentSourceRegistry> registry {};
    data::SourceInstanceId instance {};

    [[nodiscard]] auto IsReadable() const -> bool
    {
      return instance == data::SourceInstanceId {}
      || (registry && registry->AcquireSource(instance));
    }
  };

  class MemoryAnyReader final : public oxygen::serio::AnyReader {
  public:
    explicit MemoryAnyReader(std::span<const uint8_t> data)
    {
      data_.resize(data.size());
      if (!data_.empty()) {
        std::memcpy(data_.data(), data.data(), data.size());
      }
      stream_
        = std::make_unique<oxygen::serio::MemoryStream>(std::span<std::byte>(
          reinterpret_cast<std::byte*>(data_.data()), data_.size()));
      reader_
        = std::make_unique<oxygen::serio::Reader<oxygen::serio::MemoryStream>>(
          *stream_);
    }

    ~MemoryAnyReader() override = default;

    auto ReadBlob(size_t size) noexcept
      -> oxygen::Result<std::vector<std::byte>> override
    {
      return reader_->ReadBlob(size);
    }

    auto ReadBlobInto(std::span<std::byte> buffer) noexcept
      -> oxygen::Result<void> override
    {
      return reader_->ReadBlobInto(buffer);
    }

    auto Position() noexcept -> oxygen::Result<size_t> override
    {
      return reader_->Position();
    }

    auto AlignTo(size_t alignment) noexcept -> oxygen::Result<void> override
    {
      return reader_->AlignTo(alignment);
    }

    auto ScopedAlignment(uint16_t alignment) noexcept(false)
      -> oxygen::serio::AlignmentGuard override
    {
      return reader_->ScopedAlignment(alignment);
    }

    auto Forward(size_t num_bytes) noexcept -> oxygen::Result<void> override
    {
      return reader_->Forward(num_bytes);
    }

    auto Seek(size_t pos) noexcept -> oxygen::Result<void> override
    {
      return reader_->Seek(pos);
    }

  private:
    std::vector<std::byte> data_;
    std::unique_ptr<oxygen::serio::MemoryStream> stream_;
    std::unique_ptr<oxygen::serio::Reader<oxygen::serio::MemoryStream>> reader_;
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

  template <typename Fn> auto WithResourceType(const TypeId type, Fn&& fn)
  {
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
    throw std::invalid_argument("Unsupported resource type for loading");
  }

  auto AcquirePublication(const ContentPublication& publication,
    ResourceLoadPipeline::ContentCache& cache, ContentReleaseQueue& releases,
    const CheckoutOwner owner) -> ContentAcquisition
  {
    if (!publication.value) {
      return {};
    }
    return WithResourceType(publication.type, [&]<typename T>() {
      return ContentAcquisition { .publication = publication,
        .owner = publication.Acquire<T>(cache, releases, owner) };
    });
  }

  struct PreparedResourceDecode final {
    ResourceLoadPipeline::ResourceLoadFn loader;
    data::SourceInstanceId source_instance {};
    std::unique_ptr<serio::AnyReader> desc_reader;
    std::unique_ptr<serio::AnyReader> buf_reader;
    std::unique_ptr<serio::AnyReader> tex_reader;
    std::unique_ptr<serio::AnyReader> script_reader;
    std::unique_ptr<serio::AnyReader> phys_reader;
    std::shared_ptr<const IContentSource> source_content;
  };

  struct ResolvedSourceForDecode final {
    std::shared_ptr<const IContentSource> source;
    data::SourceInstanceId source_instance {};
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

  auto ResolveDescriptorOffsetAndReader(const IContentSource& source,
    const TypeId resource_type,
    const data::pak::core::ResourceIndexT resource_index)
    -> std::optional<
      std::pair<std::unique_ptr<serio::AnyReader>, data::pak::core::OffsetT>>
  {
    std::unique_ptr<serio::AnyReader> desc_reader;
    std::optional<data::pak::core::OffsetT> offset;

    if (resource_type == data::TextureResource::ClassTypeId()) {
      const auto* table = source.GetTextureTable();
      desc_reader = source.CreateTextureTableReader();
      if (!table || !desc_reader) {
        return std::nullopt;
      }
      offset = table->GetResourceOffset(resource_index);
    } else if (resource_type == data::BufferResource::ClassTypeId()) {
      const auto* table = source.GetBufferTable();
      desc_reader = source.CreateBufferTableReader();
      if (!table || !desc_reader) {
        return std::nullopt;
      }
      offset = table->GetResourceOffset(resource_index);
    } else if (resource_type == data::ScriptResource::ClassTypeId()) {
      const auto* table = source.GetScriptTable();
      desc_reader = source.CreateScriptTableReader();
      if (!table || !desc_reader) {
        return std::nullopt;
      }
      offset = table->GetResourceOffset(resource_index);
    } else if (resource_type == data::PhysicsResource::ClassTypeId()) {
      const auto* table = source.GetPhysicsTable();
      desc_reader = source.CreatePhysicsTableReader();
      if (!table || !desc_reader) {
        return std::nullopt;
      }
      offset = table->GetResourceOffset(resource_index);
    } else {
      return std::nullopt;
    }

    if (!offset.has_value()) {
      return std::nullopt;
    }

    if (auto seek_res = desc_reader->Seek(static_cast<size_t>(*offset));
      !seek_res) {
      return std::nullopt;
    }

    return std::make_pair(std::move(desc_reader), *offset);
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

    auto descriptor_opt = ResolveDescriptorOffsetAndReader(
      *source, resource_type, resource_index);
    if (!descriptor_opt.has_value()) {
      return std::nullopt;
    }

    PreparedResourceDecode prepared {};
    prepared.loader = std::move(*loader_opt);
    prepared.source_instance = resolved_source->source_instance;
    prepared.desc_reader = std::move(descriptor_opt->first);
    prepared.source_content = source;
    prepared.buf_reader = source->CreateBufferDataReader();
    prepared.tex_reader = source->CreateTextureDataReader();
    prepared.script_reader = source->CreateScriptDataReader();
    prepared.phys_reader = source->CreatePhysicsDataReader();

    return prepared;
  }

  auto BuildLoaderContextFromPrepared(PreparedResourceDecode& prepared,
    const bool work_offline, const LoadPriorityClass default_priority_class,
    const LoadRequest& request) -> LoaderContext
  {
    return LoaderContext {
      .current_asset_key = {},
      .source_instance = prepared.source_instance,
      .desc_reader = prepared.desc_reader.get(),
      .data_readers
      = std::make_tuple(prepared.buf_reader.get(), prepared.tex_reader.get(),
        prepared.script_reader.get(), prepared.phys_reader.get()),
      .work_offline = work_offline,
      .default_priority_class = default_priority_class,
      .request_priority = request.priority,
      .request_intent = request.intent,
      .source_content = prepared.source_content,
      .source_key = prepared.source_content->GetSourceKey(),
    };
  }

  auto BuildCookedLoaderContext(serio::AnyReader* cooked_reader,
    const bool work_offline, const LoadPriorityClass default_priority_class,
    const LoadRequest& request) -> LoaderContext
  {
    return LoaderContext {
      .current_asset_key = {},
      .source_instance = {},
      .desc_reader = cooked_reader,
      .data_readers = std::make_tuple(
        cooked_reader, cooked_reader, cooked_reader, cooked_reader),
      .work_offline = work_offline,
      .default_priority_class = default_priority_class,
      .request_priority = request.priority,
      .request_intent = request.intent,
      .source_content = nullptr,
    };
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
    const auto erase = Finally([&]() noexcept {
      in_flight_ops->Erase(resource_type, key.get(), operation_id);
    });
    if (!access.IsReadable()) {
      co_return SharedContentResult {};
    }
    auto cached = WithResourceType(resource_type, [&]<typename T>() {
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
    const auto publish = [&]() {
      return WithResourceType(resource_type, [&]<typename T>() {
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
    auto cached = WithResourceType(resource_type, [&]<typename T>() {
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
    const InFlightOperationTable::RequestMeta meta { .priority
      = request.priority,
      .intent = request.intent,
      .sequence = request_sequence };
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
  const observer_ptr<co::ThreadPool> thread_pool, const bool work_offline,
  Callbacks callbacks)
  : source_registry_(source_registry)
  , identities_(identities)
  , resource_loaders_(resource_loaders)
  , content_cache_(content_cache)
  , in_flight_ops_(in_flight_ops)
  , releases_(releases)
  , thread_pool_(thread_pool)
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
    auto decoded = co_await thread_pool_->Run(
      [offline = work_offline_, prepared = std::move(*prepared_opt), request,
        default_priority] mutable -> std::shared_ptr<void> {
        auto context = BuildLoaderContextFromPrepared(
          prepared, offline, default_priority, request);
        return prepared.loader(context);
      });
    co_return source_registry_.AcquireSource(source_id) ? std::move(decoded)
                                                        : nullptr;
  };

  const auto request_sequence = callbacks_.next_request_sequence
    ? callbacks_.next_request_sequence()
    : 0U;
  co_return co_await RunResourceLoadSharedStages(resource_type, key,
    SourceReadAccess {
      .registry = observer_ptr { &source_registry_ }, .instance = source_id },
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
    co_return co_await thread_pool_->Run(
      [offline = work_offline_, owned_bytes, loader = std::move(*loader_opt),
        request, default_priority] mutable -> std::shared_ptr<void> {
        std::span<const uint8_t> span(owned_bytes->data(), owned_bytes->size());
        auto reader = std::make_unique<MemoryAnyReader>(span);
        auto context = BuildCookedLoaderContext(
          reader.get(), offline, default_priority, request);

        return loader(context);
      });
  };

  const auto request_sequence = callbacks_.next_request_sequence
    ? callbacks_.next_request_sequence()
    : 0U;
  co_return co_await RunResourceLoadSharedStages(resource_type, key,
    SourceReadAccess {}, content_cache_, in_flight_ops_, callbacks_, releases_,
    request, owner, request_sequence, std::move(decode_fn));
}

} // namespace oxygen::content::internal
