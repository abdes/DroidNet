//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Content/Internal/IContentSource.h>
#include <Oxygen/Content/Internal/InternalResourceKey.h>
#include <Oxygen/Content/Internal/PakFileSource.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

#include <Oxygen/Content/Internal/ResourceLoadPipeline.h>

namespace oxygen::content::internal {
namespace {

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
    std::vector<std::byte> data_ {};
    std::unique_ptr<oxygen::serio::MemoryStream> stream_ {};
    std::unique_ptr<oxygen::serio::Reader<oxygen::serio::MemoryStream>>
      reader_ {};
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

  auto StoreDecodedByType(const TypeId resource_type,
    ResourceLoadPipeline::ContentCache& cache, const uint64_t key_hash,
    const std::shared_ptr<void>& decoded) -> bool
  {
    if (resource_type == data::TextureResource::ClassTypeId()) {
      return cache.Store(
        key_hash, std::static_pointer_cast<data::TextureResource>(decoded));
    }
    if (resource_type == data::BufferResource::ClassTypeId()) {
      return cache.Store(
        key_hash, std::static_pointer_cast<data::BufferResource>(decoded));
    }
    if (resource_type == data::ScriptResource::ClassTypeId()) {
      return cache.Store(
        key_hash, std::static_pointer_cast<data::ScriptResource>(decoded));
    }
    if (resource_type == data::PhysicsResource::ClassTypeId()) {
      return cache.Store(
        key_hash, std::static_pointer_cast<data::PhysicsResource>(decoded));
    }
    throw std::runtime_error(
      "Unsupported resource type for ResourceLoadPipeline");
  }

  template <typename ResourceT>
  auto CheckOutCached(ResourceLoadPipeline::ContentCache& cache,
    const uint64_t key_hash) -> std::shared_ptr<void>
  {
    if (auto cached = cache.CheckOut<ResourceT>(key_hash)) {
      return std::static_pointer_cast<void>(std::move(cached));
    }
    return nullptr;
  }

  auto CheckOutCachedByType(const TypeId resource_type,
    ResourceLoadPipeline::ContentCache& cache, const uint64_t key_hash)
    -> std::shared_ptr<void>
  {
    if (resource_type == data::TextureResource::ClassTypeId()) {
      return CheckOutCached<data::TextureResource>(cache, key_hash);
    }
    if (resource_type == data::BufferResource::ClassTypeId()) {
      return CheckOutCached<data::BufferResource>(cache, key_hash);
    }
    if (resource_type == data::ScriptResource::ClassTypeId()) {
      return CheckOutCached<data::ScriptResource>(cache, key_hash);
    }
    if (resource_type == data::PhysicsResource::ClassTypeId()) {
      return CheckOutCached<data::PhysicsResource>(cache, key_hash);
    }
    throw std::runtime_error(
      "Unsupported resource type for ResourceLoadPipeline");
  }

  struct PreparedResourceDecode final {
    ResourceLoadPipeline::ResourceLoadFn loader;
    SourceToken source_token {};
    std::unique_ptr<serio::AnyReader> desc_reader;
    std::unique_ptr<serio::AnyReader> buf_reader;
    std::unique_ptr<serio::AnyReader> tex_reader;
    std::unique_ptr<serio::AnyReader> script_reader;
    std::unique_ptr<serio::AnyReader> phys_reader;
    const PakFile* source_pak = nullptr;
    const IContentSource* source_content = nullptr;
  };

  struct ResolvedSourceForDecode final {
    const IContentSource* source = nullptr;
    SourceToken source_token {};
  };

  auto ResolveSourceById(const ContentSourceRegistry& source_registry,
    const uint16_t source_id) -> std::optional<ResolvedSourceForDecode>
  {
    const auto source_it = source_registry.SourceIdToIndex().find(source_id);
    if (source_it == source_registry.SourceIdToIndex().end()) {
      return std::nullopt;
    }
    const auto source_index = source_it->second;
    return ResolvedSourceForDecode {
      .source = source_registry.Sources().at(source_index).get(),
      .source_token = source_registry.SourceTokens().at(source_index),
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

  auto TryResolvePakSource(const IContentSource& source) -> const PakFile*
  {
    if (source.GetTypeId() != PakFileSource::ClassTypeId()) {
      return nullptr;
    }
    const auto* pak_source = static_cast<const PakFileSource*>(&source);
    return &pak_source->Pak();
  }

  auto ResolveDescriptorOffsetAndReader(const IContentSource& source,
    const TypeId resource_type, const data::pak::ResourceIndexT resource_index)
    -> std::optional<
      std::pair<std::unique_ptr<serio::AnyReader>, data::pak::OffsetT>>
  {
    std::unique_ptr<serio::AnyReader> desc_reader;
    std::optional<data::pak::OffsetT> offset;

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
    const TypeId resource_type, const uint16_t source_id,
    const data::pak::ResourceIndexT resource_index)
    -> std::optional<PreparedResourceDecode>
  {
    const auto resolved_source = ResolveSourceById(source_registry, source_id);
    if (!resolved_source.has_value()) {
      return std::nullopt;
    }
    const auto* source = resolved_source->source;

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
    prepared.source_token = resolved_source->source_token;
    prepared.desc_reader = std::move(descriptor_opt->first);
    prepared.source_pak = TryResolvePakSource(*source);
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
      .source_token = prepared.source_token,
      .desc_reader = prepared.desc_reader.get(),
      .data_readers
      = std::make_tuple(prepared.buf_reader.get(), prepared.tex_reader.get(),
        prepared.script_reader.get(), prepared.phys_reader.get()),
      .work_offline = work_offline,
      .default_priority_class = default_priority_class,
      .request_priority = request.priority,
      .request_intent = request.intent,
      .source_pak = prepared.source_pak,
      .source_content = prepared.source_content,
    };
  }

  auto BuildCookedLoaderContext(serio::AnyReader* cooked_reader,
    const bool work_offline, const LoadPriorityClass default_priority_class,
    const LoadRequest& request) -> LoaderContext
  {
    return LoaderContext {
      .current_asset_key = {},
      .source_token = {},
      .desc_reader = cooked_reader,
      .data_readers = std::make_tuple(
        cooked_reader, cooked_reader, cooked_reader, cooked_reader),
      .work_offline = work_offline,
      .default_priority_class = default_priority_class,
      .request_priority = request.priority,
      .request_intent = request.intent,
      .source_pak = nullptr,
      .source_content = nullptr,
    };
  }

  template <typename DecodeFn>
  auto RunResourceLoadSharedStages(const TypeId resource_type,
    const ResourceKey key, ResourceLoadPipeline::ContentCache& content_cache,
    InFlightOperationTable& in_flight_ops,
    const ResourceLoadPipeline::Callbacks& callbacks,
    const LoadRequest& request, const uint64_t request_sequence,
    DecodeFn&& decode_fn) -> co::Co<std::shared_ptr<void>>
  {
    callbacks.assert_owning_thread();

    const auto key_hash = callbacks.hash_resource_key(key);
    if (auto cached
      = CheckOutCachedByType(resource_type, content_cache, key_hash)) {
      callbacks.map_resource_key(key_hash, key);
      co_return cached;
    }

    if (auto shared_join = in_flight_ops.Find(resource_type, key_hash,
          InFlightOperationTable::RequestMeta {
            .priority = request.priority,
            .intent = request.intent,
            .sequence = request_sequence,
          });
      shared_join.has_value()) {
      co_return co_await *shared_join;
    }

    auto op = [resource_type, key, key_hash, &content_cache, &in_flight_ops,
                &callbacks,
                decode_fn = std::forward<DecodeFn>(
                  decode_fn)]() mutable -> co::Co<std::shared_ptr<void>> {
      oxygen::ScopeGuard erase_guard(
        [&in_flight_ops, resource_type, key_hash]() noexcept {
          in_flight_ops.Erase(resource_type, key_hash);
        });

      if (auto cached
        = CheckOutCachedByType(resource_type, content_cache, key_hash)) {
        callbacks.map_resource_key(key_hash, key);
        co_return cached;
      }

      auto decoded = co_await decode_fn();

      callbacks.assert_owning_thread();
      if (!ValidateTypeFromDecoded(resource_type, decoded)) {
        LOG_F(ERROR, "Loaded resource type mismatch: expected type_id={}",
          resource_type);
        co_return nullptr;
      }

      if (StoreDecodedByType(resource_type, content_cache, key_hash, decoded)) {
        callbacks.map_resource_key(key_hash, key);
        content_cache.Touch(key_hash);
      }

      if (resource_type == data::TextureResource::ClassTypeId()) {
        const auto typed
          = std::static_pointer_cast<data::TextureResource>(decoded);
        LOG_F(INFO,
          "AssetLoader: Decoded TextureResource {} ({}x{}, format={}, "
          "bytes={})",
          to_string(key), typed->GetWidth(), typed->GetHeight(),
          oxygen::to_string(typed->GetFormat()), typed->GetDataSize());
      }

      co_return decoded;
    }();

    co::Shared shared(std::move(op));
    in_flight_ops.InsertOrAssign(resource_type, key_hash, shared,
      InFlightOperationTable::RequestMeta {
        .priority = request.priority,
        .intent = request.intent,
        .sequence = request_sequence,
      });
    co_return co_await shared;
  }

} // namespace

ResourceLoadPipeline::ResourceLoadPipeline(
  const ContentSourceRegistry& source_registry,
  const ResourceLoaderMap& resource_loaders, ContentCache& content_cache,
  InFlightOperationTable& in_flight_ops,
  const observer_ptr<co::ThreadPool> thread_pool, const bool work_offline,
  Callbacks callbacks)
  : source_registry_(source_registry)
  , resource_loaders_(resource_loaders)
  , content_cache_(content_cache)
  , in_flight_ops_(in_flight_ops)
  , thread_pool_(thread_pool)
  , work_offline_(work_offline)
  , callbacks_(std::move(callbacks))
{
}

auto ResourceLoadPipeline::LoadErased(const TypeId resource_type,
  const ResourceKey key) -> co::Co<std::shared_ptr<void>>
{
  co_return co_await LoadErased(resource_type, key, LoadRequest {});
}

auto ResourceLoadPipeline::LoadErased(
  const TypeId resource_type, const ResourceKey key, const LoadRequest& request)
  -> co::Co<std::shared_ptr<void>>
{
  if (!thread_pool_) {
    throw std::runtime_error(
      "AssetLoader requires a thread pool for async loads (LoadResourceAsync)");
  }

  auto decode_fn
    = [this, resource_type, key, request]() -> co::Co<std::shared_ptr<void>> {
    const InternalResourceKey internal_key(key);
    const uint16_t source_id = internal_key.GetPakIndex();
    const auto resource_index = internal_key.GetResourceIndex();

    auto prepared_opt = PrepareResourceDecode(source_registry_,
      resource_loaders_, resource_type, source_id, resource_index);
    if (!prepared_opt.has_value()) {
      co_return nullptr;
    }

    co_return co_await thread_pool_->Run(
      [this, prepared = std::move(*prepared_opt), request]() mutable {
        auto context = BuildLoaderContextFromPrepared(prepared, work_offline_,
          callbacks_.default_priority_class
            ? callbacks_.default_priority_class()
            : LoadPriorityClass::kDefault,
          request);
        return prepared.loader(context);
      });
  };

  const auto request_sequence = callbacks_.next_request_sequence
    ? callbacks_.next_request_sequence()
    : 0U;
  co_return co_await RunResourceLoadSharedStages(resource_type, key,
    content_cache_, in_flight_ops_, callbacks_, request, request_sequence,
    std::move(decode_fn));
}

auto ResourceLoadPipeline::LoadErasedFromCooked(const TypeId resource_type,
  const ResourceKey key, std::span<const uint8_t> bytes)
  -> co::Co<std::shared_ptr<void>>
{
  co_return co_await LoadErasedFromCooked(
    resource_type, key, bytes, LoadRequest {});
}

auto ResourceLoadPipeline::LoadErasedFromCooked(const TypeId resource_type,
  const ResourceKey key, std::span<const uint8_t> bytes,
  const LoadRequest& request) -> co::Co<std::shared_ptr<void>>
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

  // Copy bytes eagerly to ensure the payload outlives thread-pool execution.
  auto owned_bytes
    = std::make_shared<std::vector<uint8_t>>(bytes.begin(), bytes.end());

  auto decode_fn = [this, resource_type, owned_bytes,
                     request]() -> co::Co<std::shared_ptr<void>> {
    auto loader_opt = ResolveLoaderForType(resource_loaders_, resource_type);
    if (!loader_opt.has_value()) {
      co_return nullptr;
    }

    co_return co_await thread_pool_->Run(
      [this, owned_bytes, loader = std::move(*loader_opt),
        request]() mutable -> std::shared_ptr<void> {
        std::span<const uint8_t> span(owned_bytes->data(), owned_bytes->size());
        auto reader = std::make_unique<MemoryAnyReader>(span);
        auto context = BuildCookedLoaderContext(reader.get(), work_offline_,
          callbacks_.default_priority_class
            ? callbacks_.default_priority_class()
            : LoadPriorityClass::kDefault,
          request);

        return loader(context);
      });
  };

  const auto request_sequence = callbacks_.next_request_sequence
    ? callbacks_.next_request_sequence()
    : 0U;
  co_return co_await RunResourceLoadSharedStages(resource_type, key,
    content_cache_, in_flight_ops_, callbacks_, request, request_sequence,
    std::move(decode_fn));
}

} // namespace oxygen::content::internal
