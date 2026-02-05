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
#include <unordered_map>
#include <unordered_set>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Constants.h>
#include <Oxygen/Content/Internal/ContentSource.h>
#include <Oxygen/Content/Internal/DependencyCollector.h>
#include <Oxygen/Content/Internal/InternalResourceKey.h>
#include <Oxygen/Content/Internal/ResourceRef.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/SourceToken.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <thread>

using oxygen::content::AssetLoader;
using oxygen::content::LoaderContext;
using oxygen::content::LoadFunction;
using oxygen::content::PakFile;
using oxygen::content::PakResource;
namespace pak = oxygen::data::pak;
namespace internal = oxygen::content::internal;

namespace {
// Debug-only hash collision tracking for AssetKey hashes.
#if !defined(NDEBUG)
std::mutex g_asset_hash_mutex;
std::unordered_map<uint64_t, oxygen::data::AssetKey> g_asset_hash_to_key;
#endif

struct ResourceCompositeKey final {
  oxygen::data::SourceKey source_key;
  uint16_t resource_type_index = 0;
  uint32_t resource_index = 0;

  auto operator==(const ResourceCompositeKey&) const -> bool = default;
};

auto IsZeroGuidBytes(const std::array<uint8_t, 16>& bytes) -> bool
{
  for (const auto b : bytes) {
    if (b != 0) {
      return false;
    }
  }
  return true;
}
} // namespace

namespace oxygen::content {

using oxygen::content::constants::kLooseCookedSourceIdBase;
using oxygen::content::constants::kSyntheticSourceId;

// Implement the private helper declared in the header to avoid exposing the
// internal header in the public API.
auto AssetLoader::PackResourceKey(uint16_t pak_index,
  uint16_t resource_type_index, uint32_t resource_index) -> ResourceKey
{
  internal::InternalResourceKey key(
    pak_index, resource_type_index, resource_index);
  return key.GetRawKey();
}

struct AssetLoader::Impl final {
  std::vector<std::unique_ptr<internal::IContentSource>> sources;

  std::vector<uint16_t> source_ids;
  std::unordered_map<uint16_t, size_t> source_id_to_index;

  std::vector<internal::SourceToken> source_tokens;
  std::unordered_map<internal::SourceToken, uint16_t> token_to_source_id;
  uint32_t next_source_token_value = 1;

  uint16_t next_loose_source_id = 0x8000;

  // Keep a dense, deterministic PAK index space for ResourceKey encoding.
  // This must not be affected by registering non-PAK sources.
  std::vector<std::filesystem::path> pak_paths;

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
  for (const auto id : ids) {
    if (id == type_id) {
      return true;
    }
  }
  return false;
}
} // namespace

//=== Sanity Checking Helper =================================================//
thread_local bool g_has_current_source_id = false;
thread_local uint16_t g_current_source_id = 0;
class ScopedCurrentSourceId final {
public:
  explicit ScopedCurrentSourceId(const uint16_t source_id) noexcept
    : prev_has_(g_has_current_source_id)
    , prev_id_(g_current_source_id)
  {
    g_has_current_source_id = true;
    g_current_source_id = source_id;
  }

  ~ScopedCurrentSourceId() noexcept
  {
    g_has_current_source_id = prev_has_;
    g_current_source_id = prev_id_;
  }

  ScopedCurrentSourceId(const ScopedCurrentSourceId&) = delete;
  ScopedCurrentSourceId& operator=(const ScopedCurrentSourceId&) = delete;

private:
  bool prev_has_;
  uint16_t prev_id_;
};

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

//=== Basic methods ==========================================================//

AssetLoader::AssetLoader(
  [[maybe_unused]] EngineTag tag, const AssetLoaderConfig config)
  : impl_(std::make_unique<Impl>())
{
  using serio::FileStream;

  LOG_SCOPE_FUNCTION(INFO);

  owning_thread_id_ = std::this_thread::get_id();

  thread_pool_ = config.thread_pool;
  work_offline_ = config.work_offline;
  verify_content_hashes_ = config.verify_content_hashes;
  eviction_alive_token_ = std::make_shared<int>(0);

  // Register asset loaders
  RegisterLoader(loaders::LoadGeometryAsset);
  RegisterLoader(loaders::LoadMaterialAsset);
  RegisterLoader(loaders::LoadSceneAsset);

  // Register resource loaders
  RegisterLoader(loaders::LoadBufferResource);
  RegisterLoader(loaders::LoadTextureResource);
}

auto AssetLoader::SetVerifyContentHashes(const bool enable) -> void
{
  AssertOwningThread();
  verify_content_hashes_ = enable;
  LOG_F(INFO, "AssetLoader: verify_content_hashes={}",
    verify_content_hashes_ ? "enabled" : "disabled");
}

auto AssetLoader::VerifyContentHashesEnabled() const noexcept -> bool
{
  return verify_content_hashes_;
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
  // The per-operation erase guards tolerate the entry already being absent.
  in_flight_material_assets_.clear();
  in_flight_geometry_assets_.clear();
  in_flight_scene_assets_.clear();
  in_flight_textures_.clear();
  in_flight_buffers_.clear();

  {
    auto eviction_guard = content_cache_.OnEviction(
      [&](const uint64_t cache_key, std::shared_ptr<void> value,
        const TypeId type_id) {
        static_cast<void>(value);
        UnloadObject(cache_key, type_id, EvictionReason::kShutdown);
      });
    content_cache_.Clear();
  }

  resource_key_by_hash_.clear();
  asset_key_by_hash_.clear();
  eviction_subscribers_.clear();
  eviction_alive_token_.reset();
}

auto AssetLoader::IsRunning() const -> bool { return nursery_ != nullptr; }

auto AssetLoader::AddPakFile(const std::filesystem::path& path) -> void
{
  AssertOwningThread();
  // Normalize the path to ensure consistent handling
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path);
  const auto pak_index = static_cast<uint16_t>(impl_->pak_paths.size());

  auto new_source = std::make_unique<internal::PakFileSource>(
    normalized, verify_content_hashes_);
#if !defined(NDEBUG)
  {
    const auto source_key = new_source->GetSourceKey();
    const auto u_source_key = source_key.get();
    if (IsZeroGuidBytes(u_source_key)) {
      LOG_F(WARNING,
        "Mounted PAK has zero SourceKey (PakHeader.guid); cache aliasing risk: "
        "path={}",
        normalized.string());
    }
    for (const auto& existing : impl_->sources) {
      if (existing && existing->GetSourceKey() == source_key) {
        LOG_F(WARNING,
          "Mounted PAK shares SourceKey with an existing source; cache "
          "aliasing "
          "risk: source_key={} new_path={}",
          data::to_string(source_key), normalized.string());
        break;
      }
    }
  }
#endif

  impl_->sources.push_back(std::move(new_source));
  impl_->source_ids.push_back(pak_index);
  impl_->source_id_to_index.insert_or_assign(
    pak_index, impl_->sources.size() - 1);

  const internal::SourceToken token { impl_->next_source_token_value++ };
  impl_->source_tokens.push_back(token);
  impl_->token_to_source_id.insert_or_assign(token, pak_index);

  impl_->pak_paths.push_back(normalized);

  LOG_F(INFO, "Mounted PAK content source: id={} path={}", pak_index,
    normalized.string());
}

auto AssetLoader::AddLooseCookedRoot(const std::filesystem::path& path) -> void
{
  AssertOwningThread();
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path);
  const auto normalized_s = normalized.string();

  auto new_source = std::make_unique<internal::LooseCookedSource>(
    normalized, verify_content_hashes_);

  auto clear_content_caches = [this]() {
    auto eviction_guard = content_cache_.OnEviction(
      [&](const uint64_t cache_key, std::shared_ptr<void> value,
        const TypeId type_id) {
        static_cast<void>(value);
        UnloadObject(cache_key, type_id, EvictionReason::kClear);
      });
    content_cache_.Clear();
    resource_key_by_hash_.clear();
    asset_key_by_hash_.clear();
  };

  for (size_t source_index = 0; source_index < impl_->sources.size();
    ++source_index) {
    auto& existing = impl_->sources[source_index];
    if (!existing) {
      continue;
    }
    const auto source_id = impl_->source_ids.at(source_index);
    if (source_id < kLooseCookedSourceIdBase) {
      continue;
    }
    if (existing->DebugName() == normalized_s) {
      LOG_F(INFO,
        "Refreshing loose cooked content source: root={} (reloading index)",
        normalized_s);
      existing = std::move(new_source);
      clear_content_caches();
      return;
    }
  }
#if !defined(NDEBUG)
  {
    const auto source_key = new_source->GetSourceKey();
    const auto u_source_key = source_key.get();
    if (IsZeroGuidBytes(u_source_key)) {
      LOG_F(WARNING,
        "Mounted loose cooked root has zero SourceKey (IndexHeader.guid); "
        "cache aliasing risk: root={}",
        normalized.string());
    }
    for (const auto& existing : impl_->sources) {
      if (existing && existing->GetSourceKey() == source_key) {
        LOG_F(WARNING,
          "Mounted loose cooked root shares SourceKey with an existing source; "
          "cache aliasing risk: source_key={} new_root={}",
          data::to_string(source_key), normalized.string());
        break;
      }
    }
  }
#endif

  impl_->sources.push_back(std::move(new_source));

  const auto source_id = impl_->next_loose_source_id++;
  DCHECK_F(source_id >= kLooseCookedSourceIdBase);
  impl_->source_ids.push_back(source_id);
  impl_->source_id_to_index.insert_or_assign(
    source_id, impl_->sources.size() - 1);

  const internal::SourceToken token { impl_->next_source_token_value++ };
  impl_->source_tokens.push_back(token);
  impl_->token_to_source_id.insert_or_assign(token, source_id);

  LOG_F(INFO, "Mounted loose cooked content source: id={} root={}", source_id,
    normalized.string());
}

auto AssetLoader::ClearMounts() -> void
{
  LOG_F(INFO, "AssetLoader::ClearMounts thread={} owner={}",
    std::hash<std::thread::id> {}(std::this_thread::get_id()),
    std::hash<std::thread::id> {}(owning_thread_id_));
  AssertOwningThread(); // Ensure this method is called on the owning thread
  impl_->sources.clear();
  impl_->source_ids.clear();
  impl_->source_id_to_index.clear();
  impl_->source_tokens.clear();
  impl_->token_to_source_id.clear();
  impl_->next_source_token_value = 1;
  impl_->next_loose_source_id = kLooseCookedSourceIdBase;
  impl_->pak_paths.clear();

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

  resource_key_by_hash_.clear();
  asset_key_by_hash_.clear();
}

auto AssetLoader::TrimCache() -> void
{
  LOG_F(INFO, "AssetLoader::TrimCache thread={} owner={}",
    std::hash<std::thread::id> {}(std::this_thread::get_id()),
    std::hash<std::thread::id> {}(owning_thread_id_));
  AssertOwningThread();

  auto eviction_guard = content_cache_.OnEviction(
    [&](const uint64_t cache_key, std::shared_ptr<void> value,
      const TypeId type_id) {
      static_cast<void>(value);
      UnloadObject(cache_key, type_id, EvictionReason::kClear);
    });

  const auto keys = content_cache_.KeysSnapshot();
  for (const auto& key : keys) {
    if (content_cache_.GetValueUseCount(key) <= 1U) {
      (void)content_cache_.Remove(key);
    }
  }
}

auto AssetLoader::BindResourceRefToKey(const internal::ResourceRef& ref)
  -> ResourceKey
{
  AssertOwningThread();

  const auto token_it = impl_->token_to_source_id.find(ref.source);
  if (token_it == impl_->token_to_source_id.end()) {
    throw std::runtime_error("Unknown SourceToken for ResourceRef binding");
  }

  const uint16_t source_id = token_it->second;
  const uint16_t resource_type_index
    = GetResourceTypeIndexByTypeId(ref.resource_type_id);

  return PackResourceKey(source_id, resource_type_index, ref.resource_index);
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

  auto res = co_await LoadResourceAsync<data::TextureResource>(key);
  co_return res;
}

// Helper: create an AnyReader backed by an in-memory buffer. This owns the
// backing storage and implements the AnyReader interface by delegating to
// a concrete Reader<MemoryStream>.
namespace {
class MemoryAnyReader final : public oxygen::serio::AnyReader {
public:
  explicit MemoryAnyReader(std::span<const uint8_t> data)
  {
    data_.resize(data.size());
    if (!data_.empty()) {
      std::memcpy(data_.data(), data.data(), data.size());
    }
    // Create a MemoryStream over the owned std::vector<std::byte>
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
} // namespace

auto AssetLoader::LoadTextureAsync(
  CookedResourceData<data::TextureResource> cooked)
  -> co::Co<std::shared_ptr<data::TextureResource>>
{
  auto decoded = co_await LoadResourceAsyncFromCookedErased(
    data::TextureResource::ClassTypeId(), cooked.key, cooked.bytes);
  co_return std::static_pointer_cast<data::TextureResource>(std::move(decoded));
}

auto AssetLoader::LoadResourceAsyncFromCookedErased(
  const TypeId type_id, const ResourceKey key, std::span<const uint8_t> bytes)
  -> co::Co<std::shared_ptr<void>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadResourceAsync (cooked)");
  DLOG_F(2, "type_id : {}", type_id);
  DLOG_F(2, "key     : {}", key);
  DLOG_F(2, "bytes   : {}", bytes.size());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  if (!nursery_) {
    throw std::runtime_error("AssetLoader must be activated before async loads "
                             "(LoadResourceAsyncFromCookedErased)");
  }
  if (!thread_pool_) {
    throw std::runtime_error(
      "AssetLoader requires a thread pool for async loads "
      "(LoadResourceAsyncFromCookedErased)");
  }

  const auto key_hash = HashResourceKey(key);
  if (type_id == data::TextureResource::ClassTypeId()) {
    if (auto cached
      = content_cache_.CheckOut<data::TextureResource>(key_hash)) {
      resource_key_by_hash_.try_emplace(key_hash, key);
      co_return cached;
    }
  } else if (type_id == data::BufferResource::ClassTypeId()) {
    if (auto cached = content_cache_.CheckOut<data::BufferResource>(key_hash)) {
      resource_key_by_hash_.try_emplace(key_hash, key);
      co_return cached;
    }
  } else {
    throw std::runtime_error(
      "LoadResourceAsync(cooked) is not implemented for this resource type");
  }

  // Copy bytes eagerly to ensure the payload outlives thread-pool execution.
  std::vector<uint8_t> owned_bytes(bytes.begin(), bytes.end());

  auto decode_fn =
    [this, type_id,
      owned_bytes = std::move(owned_bytes)]() mutable -> std::shared_ptr<void> {
    DLOG_SCOPE_F(2, "AssetLoader DecodeResource (cooked)");

    auto it = resource_loaders_.find(type_id);
    if (it == resource_loaders_.end()) {
      LOG_F(ERROR, "No resource loader registered for type_id={}", type_id);
      return nullptr;
    }

    std::span<const uint8_t> span(owned_bytes.data(), owned_bytes.size());
    auto reader = std::make_unique<MemoryAnyReader>(span);

    LoaderContext context {
      .current_asset_key = {},
      .desc_reader = reader.get(),
      .data_readers = std::make_tuple(reader.get(), reader.get()),
      .work_offline = work_offline_,
      .source_pak = nullptr,
    };

    return it->second(context);
  };

  try {
    LOG_F(2, "scheduling on thread pool");
    auto decoded = co_await thread_pool_->Run(std::move(decode_fn));
    AssertOwningThread();
    if (!decoded) {
      co_return nullptr;
    }

    if (type_id == data::TextureResource::ClassTypeId()) {
      if (auto cached
        = content_cache_.CheckOut<data::TextureResource>(key_hash)) {
        resource_key_by_hash_.try_emplace(key_hash, key);
        co_return cached;
      }
      auto typed = std::static_pointer_cast<data::TextureResource>(decoded);
      if (!typed
        || typed->GetTypeId() != data::TextureResource::ClassTypeId()) {
        LOG_F(ERROR, "Loaded resource type mismatch (cooked): expected {}",
          data::TextureResource::ClassTypeNamePretty());
        co_return nullptr;
      }
      if (content_cache_.Store(key_hash, typed)) {
        resource_key_by_hash_.insert_or_assign(key_hash, key);
      }
    } else if (type_id == data::BufferResource::ClassTypeId()) {
      if (auto cached
        = content_cache_.CheckOut<data::BufferResource>(key_hash)) {
        resource_key_by_hash_.try_emplace(key_hash, key);
        co_return cached;
      }
      auto typed = std::static_pointer_cast<data::BufferResource>(decoded);
      if (!typed || typed->GetTypeId() != data::BufferResource::ClassTypeId()) {
        LOG_F(ERROR, "Loaded resource type mismatch (cooked): expected {}",
          data::BufferResource::ClassTypeNamePretty());
        co_return nullptr;
      }
      if (content_cache_.Store(key_hash, typed)) {
        resource_key_by_hash_.insert_or_assign(key_hash, key);
      }
    }

    co_return decoded;
  } catch (const co::TaskCancelledException& e) {
    throw OperationCancelledException(e.what());
  }
}

void AssetLoader::StartLoadTexture(
  CookedResourceData<data::TextureResource> cooked, TextureCallback on_complete)
{
  StartLoadResource<data::TextureResource>(
    std::move(cooked), std::move(on_complete));
}

void AssetLoader::StartLoadBuffer(
  const ResourceKey key, BufferCallback on_complete)
{
  StartLoadResource<data::BufferResource>(key, std::move(on_complete));
}

void AssetLoader::StartLoadBuffer(
  CookedResourceData<data::BufferResource> cooked, BufferCallback on_complete)
{
  StartLoadResource<data::BufferResource>(
    std::move(cooked), std::move(on_complete));
}

void AssetLoader::StartLoadTexture(ResourceKey key, TextureCallback on_complete)
{
  AssertOwningThread();
  if (!nursery_) {
    throw std::runtime_error(
      "AssetLoader must be activated before StartLoadTexture");
  }

  if (!thread_pool_) {
    throw std::runtime_error(
      "AssetLoader requires a thread pool for StartLoadTexture");
  }

  LOG_F(INFO, "AssetLoader: StartLoadTexture {}", to_string(key));

  nursery_->Start(
    [this, key, on_complete = std::move(on_complete)]() mutable -> co::Co<> {
      try {
        auto res = co_await LoadTextureAsync(key);
        if (res) {
          LOG_F(INFO,
            "AssetLoader: Texture ready {} ({}x{}, format={}, bytes={})",
            to_string(key), res->GetWidth(), res->GetHeight(),
            oxygen::to_string(res->GetFormat()), res->GetDataSize());
        } else {
          LOG_F(WARNING, "AssetLoader: Texture load returned null {}",
            to_string(key));
        }
        on_complete(std::move(res));
      } catch (const std::exception& e) {
        LOG_F(ERROR, "StartLoadTexture failed: {}", e.what());
        on_complete(nullptr);
      }
      co_return;
    });
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

  // Cycle detection: adding edge dependent -> dependency must not create a path
  // dependency -> ... -> dependent (checked via DetectCycle).
  if (DetectCycle(dependency, dependent)) {
    LOG_F(ERROR, "Rejecting asset dependency that introduces a cycle: {} -> {}",
      nostd::to_string(dependent).c_str(),
      nostd::to_string(dependency).c_str());
#if !defined(NDEBUG)
    DCHECK_F(false, "Cycle detected in asset dependency graph");
#endif
    return; // Do not insert
  }

  // Add forward dependency only (reference counting handled by cache Touch)
  asset_dependencies_[dependent].insert(dependency);

  // Touch the dependency asset in the cache to increment its reference count
  content_cache_.Touch(HashAssetKey(dependency));
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

  // Add forward dependency only (reference counting handled by cache Touch)
  resource_dependencies_[dependent].insert(resource_key);

  // Touch the dependency resource in the cache to increment its reference count
  content_cache_.Touch(HashResourceKey(resource_key));
}

//=== Asset Loading Implementations ==========================================//

auto AssetLoader::GetCurrentSourceId() const -> uint16_t
{
  if (!g_has_current_source_id) {
    throw std::runtime_error(
      "Current source id is not set (invalid outside load operation)");
  }
  return g_current_source_id;
}

auto AssetLoader::ReleaseAsset(const data::AssetKey& key) -> bool
{
  AssertOwningThread();
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
  const bool still_present = content_cache_.Contains(HashAssetKey(key));
  LOG_F(2, "ReleaseAsset key={} evicted={}", data::to_string(key),
    still_present ? "false" : "true");
  return !still_present;
}

auto AssetLoader::SubscribeResourceEvictions(
  const TypeId resource_type, EvictionHandler handler) -> EvictionSubscription
{
  AssertOwningThread();
  const auto id = next_eviction_subscriber_id_++;
  auto& subscribers = eviction_subscribers_[resource_type];
  subscribers.push_back(EvictionSubscriber {
    .id = id,
    .handler = std::move(handler),
  });

  return MakeEvictionSubscription(resource_type, id,
    observer_ptr<IAssetLoader> { this }, eviction_alive_token_);
}

void AssetLoader::UnsubscribeResourceEvictions(
  const TypeId resource_type, const uint64_t id) noexcept
{
  auto it = eviction_subscribers_.find(resource_type);
  if (it == eviction_subscribers_.end()) {
    return;
  }

  auto& subscribers = it->second;
  const auto erase_from = std::remove_if(subscribers.begin(), subscribers.end(),
    [id](const EvictionSubscriber& subscriber) { return subscriber.id == id; });
  subscribers.erase(erase_from, subscribers.end());

  if (subscribers.empty()) {
    eviction_subscribers_.erase(it);
  }
}

auto AssetLoader::ReleaseAssetTree(const data::AssetKey& key) -> void
{
  AssertOwningThread();

#if !defined(NDEBUG)
  static thread_local std::unordered_set<data::AssetKey> release_visit_set;
  const bool inserted = release_visit_set.emplace(key).second;
  DCHECK_F(inserted, "Cycle encountered during ReleaseAssetTree recursion");
  class VisitGuard final {
  public:
    VisitGuard(
      std::unordered_set<data::AssetKey>& set, data::AssetKey k) noexcept
      : set_(set)
      , key_(std::move(k))
    {
    }
    ~VisitGuard() { set_.erase(key_); }
    VisitGuard(const VisitGuard&) = delete;
    VisitGuard& operator=(const VisitGuard&) = delete;

  private:
    std::unordered_set<data::AssetKey>& set_;
    data::AssetKey key_;
  } visit_guard(release_visit_set, key);
#endif

  // Release resource dependencies first
  auto res_dep_it = resource_dependencies_.find(key);
  if (res_dep_it != resource_dependencies_.end()) {
    for (const auto& res_key : res_dep_it->second) {
      content_cache_.CheckIn(HashResourceKey(res_key));
    }
    resource_dependencies_.erase(res_dep_it);
  }
  // Then release asset dependencies
  auto dep_it = asset_dependencies_.find(key);
  if (dep_it != asset_dependencies_.end()) {
    for (const auto& dep_key : dep_it->second) {
      ReleaseAssetTree(dep_key);
    }
    asset_dependencies_.erase(dep_it);
  }
  // Release the asset itself
  content_cache_.CheckIn(HashAssetKey(key));
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
  const internal::DependencyCollector& collector) -> co::Co<>
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

    auto res = co_await LoadResourceAsync<ResourceT>(dep_key);
    if (!res) {
      continue;
    }

    AddResourceDependency(dependent_asset_key, dep_key);
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

    auto res = co_await LoadResourceAsync<ResourceT>(dep_key);
    if (!res) {
      continue;
    }

    AddResourceDependency(dependent_asset_key, dep_key);
  }

  co_return;
}

auto AssetLoader::DecodeAssetAsyncErasedImpl(const TypeId type_id,
  const data::AssetKey& key) -> co::Co<DecodedAssetAsyncResult>
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
  const PakFile* source_pak = nullptr;

  bool found = false;
  for (size_t source_index = 0; source_index < impl_->sources.size();
    ++source_index) {
    const auto& source = *impl_->sources[source_index];
    const auto locator_opt = source.FindAsset(key);
    if (!locator_opt) {
      continue;
    }

    source_id = impl_->source_ids.at(source_index);
    source_token = impl_->source_tokens.at(source_index);
    desc_reader = source.CreateAssetDescriptorReader(*locator_opt);
    if (!desc_reader) {
      continue;
    }

    buf_reader = source.CreateBufferDataReader();
    tex_reader = source.CreateTextureDataReader();

    if (source.GetTypeId() == internal::PakFileSource::ClassTypeId()) {
      const auto* pak_source
        = static_cast<const internal::PakFileSource*>(&source);
      source_pak = &pak_source->Pak();
    }

    found = true;
    break;
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
    [this, key, type_id, source_id, source_pak, collector,
      desc_reader = std::move(desc_reader), buf_reader = std::move(buf_reader),
      tex_reader = std::move(tex_reader),
      source_token]() mutable -> std::shared_ptr<void> {
      ScopedCurrentSourceId source_guard(source_id);

      LoaderContext context {
        .current_asset_key = key,
        .source_token = source_token,
        .desc_reader = desc_reader.get(),
        .data_readers = std::make_tuple(buf_reader.get(), tex_reader.get()),
        .work_offline = work_offline_,
        .dependency_collector = collector,
        .source_pak = source_pak,
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

auto AssetLoader::LoadMaterialAssetAsyncImpl(const data::AssetKey& key)
  -> co::Co<std::shared_ptr<data::MaterialAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadMaterialAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  const auto hash_key = HashAssetKey(key);
  if (auto cached = content_cache_.CheckOut<data::MaterialAsset>(hash_key)) {
    co_return cached;
  }

  if (auto it = in_flight_material_assets_.find(hash_key);
    it != in_flight_material_assets_.end()) {
    co_return co_await it->second;
  }

  auto op
    = [this, key, hash_key]() -> co::Co<std::shared_ptr<data::MaterialAsset>> {
    struct EraseOnExit final {
      AssetLoader* loader;
      uint64_t key_hash;
      ~EraseOnExit() noexcept
      {
        loader->in_flight_material_assets_.erase(key_hash);
      }
    } erase { this, hash_key };

    try {
      if (auto cached
        = content_cache_.CheckOut<data::MaterialAsset>(hash_key)) {
        co_return cached;
      }

      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::MaterialAsset::ClassTypeId(), key);
      auto decoded
        = std::static_pointer_cast<data::MaterialAsset>(decoded_result.asset);
      if (!decoded
        || decoded->GetTypeId() != data::MaterialAsset::ClassTypeId()) {
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::MaterialAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }
      if (!decoded_result.dependency_collector) {
        LOG_F(ERROR, "Missing dependency collector for decoded material asset");
        co_return nullptr;
      }

      // Publish (owning thread): store asset, then ensure resource dependencies
      // are loaded and held via dependency edges.
      {
        using data::pak::kNoResourceIndex;
        using data::pak::ResourceIndexT;

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
        texture_keys.reserve(6);
        texture_keys.push_back(
          make_texture_key(decoded->GetBaseColorTexture()));
        texture_keys.push_back(make_texture_key(decoded->GetNormalTexture()));
        texture_keys.push_back(make_texture_key(decoded->GetMetallicTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetRoughnessTexture()));
        texture_keys.push_back(
          make_texture_key(decoded->GetAmbientOcclusionTexture()));
        texture_keys.push_back(make_texture_key(decoded->GetEmissiveTexture()));
        decoded->SetTextureResourceKeys(std::move(texture_keys));
      }

      content_cache_.Store(hash_key, decoded);
      asset_key_by_hash_.insert_or_assign(hash_key, key);

      co_await PublishResourceDependenciesAsync<data::TextureResource>(
        key, *decoded_result.dependency_collector);

      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      throw OperationCancelledException(e.what());
    }
  }();

  co::Shared shared(std::move(op));
  in_flight_material_assets_.insert_or_assign(hash_key, shared);
  co_return co_await shared;
}

auto AssetLoader::LoadGeometryBufferDependenciesAsync(
  const internal::DependencyCollector& collector)
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

    auto res = co_await LoadResourceAsync<BufferResource>(dep_key);
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
  const internal::DependencyCollector& collector)
  -> co::Co<LoadedGeometryMaterialsByKey>
{
  AssertOwningThread();

  using data::MaterialAsset;

  LoadedGeometryMaterialsByKey loaded_materials;
  std::unordered_set<uint64_t> seen_asset_hashes;
  seen_asset_hashes.reserve(collector.AssetDependencies().size());

  for (const auto& dep_asset_key : collector.AssetDependencies()) {
    const auto dep_hash = HashAssetKey(dep_asset_key);
    if (!seen_asset_hashes.insert(dep_hash).second) {
      continue;
    }

    auto asset = co_await LoadMaterialAssetAsyncImpl(dep_asset_key);
    if (!asset) {
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
          i, oxygen::data::to_string(mat_key));
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
  const LoadedGeometryMaterialsByKey& materials_by_key) -> void
{
  AssertOwningThread();

  for (const auto& [resource_index, loaded] : buffers_by_index) {
    (void)resource_index;
    if (!loaded.resource) {
      continue;
    }
    AddResourceDependency(dependent_asset_key, loaded.key);
  }

  for (const auto& [dep_key, dep_asset] : materials_by_key) {
    if (!dep_asset) {
      continue;
    }
    AddAssetDependency(dependent_asset_key, dep_key);
    content_cache_.CheckIn(HashAssetKey(dep_key));
  }
}

auto AssetLoader::LoadGeometryAssetAsyncImpl(const data::AssetKey& key)
  -> co::Co<std::shared_ptr<data::GeometryAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadGeometryAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  const auto hash_key = HashAssetKey(key);
  if (auto cached = content_cache_.CheckOut<data::GeometryAsset>(hash_key)) {
    co_return cached;
  }

  if (auto it = in_flight_geometry_assets_.find(hash_key);
    it != in_flight_geometry_assets_.end()) {
    co_return co_await it->second;
  }

  auto op
    = [this, key, hash_key]() -> co::Co<std::shared_ptr<data::GeometryAsset>> {
    struct EraseOnExit final {
      AssetLoader* loader;
      uint64_t key_hash;
      ~EraseOnExit() noexcept
      {
        loader->in_flight_geometry_assets_.erase(key_hash);
      }
    } erase { this, hash_key };

    try {
      if (auto cached
        = content_cache_.CheckOut<data::GeometryAsset>(hash_key)) {
        co_return cached;
      }

      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::GeometryAsset::ClassTypeId(), key);
      auto decoded
        = std::static_pointer_cast<data::GeometryAsset>(decoded_result.asset);
      if (!decoded
        || decoded->GetTypeId() != data::GeometryAsset::ClassTypeId()) {
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::GeometryAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }
      if (!decoded_result.dependency_collector) {
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
        = co_await LoadGeometryBufferDependenciesAsync(collector);
      const auto loaded_materials
        = co_await LoadGeometryMaterialDependenciesAsync(collector);

      BindGeometryRuntimePointers(
        *decoded, loaded_buffers_by_index, loaded_materials);

      // Store the fully published asset.
      content_cache_.Store(hash_key, decoded);
      asset_key_by_hash_.insert_or_assign(hash_key, key);

      PublishGeometryDependencyEdges(
        key, loaded_buffers_by_index, loaded_materials);

      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      throw OperationCancelledException(e.what());
    }
  }();

  co::Shared shared(std::move(op));
  in_flight_geometry_assets_.insert_or_assign(hash_key, shared);
  co_return co_await shared;
}

auto AssetLoader::LoadSceneAssetAsyncImpl(const data::AssetKey& key)
  -> co::Co<std::shared_ptr<data::SceneAsset>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadSceneAssetAsync");
  DLOG_F(2, "key     : {}", nostd::to_string(key).c_str());
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

  const auto hash_key = HashAssetKey(key);
  if (auto cached = content_cache_.CheckOut<data::SceneAsset>(hash_key)) {
    co_return cached;
  }

  if (auto it = in_flight_scene_assets_.find(hash_key);
    it != in_flight_scene_assets_.end()) {
    co_return co_await it->second;
  }

  auto op
    = [this, key, hash_key]() -> co::Co<std::shared_ptr<data::SceneAsset>> {
    struct EraseOnExit final {
      AssetLoader* loader;
      uint64_t key_hash;
      ~EraseOnExit() noexcept
      {
        loader->in_flight_scene_assets_.erase(key_hash);
      }
    } erase { this, hash_key };

    try {
      if (auto cached = content_cache_.CheckOut<data::SceneAsset>(hash_key)) {
        co_return cached;
      }

      auto decoded_result = co_await DecodeAssetAsyncErasedImpl(
        data::SceneAsset::ClassTypeId(), key);
      auto decoded
        = std::static_pointer_cast<data::SceneAsset>(decoded_result.asset);
      if (!decoded || decoded->GetTypeId() != data::SceneAsset::ClassTypeId()) {
        LOG_F(ERROR, "Loaded asset type mismatch (async): expected {}, got {}",
          data::SceneAsset::ClassTypeNamePretty(),
          decoded ? decoded->GetTypeName() : "nullptr");
        co_return nullptr;
      }
      if (!decoded_result.dependency_collector) {
        LOG_F(ERROR, "Missing dependency collector for decoded scene asset");
        co_return nullptr;
      }

      // Publish: store the scene asset, then load asset dependencies and
      // register dependency edges.
      content_cache_.Store(hash_key, decoded);
      asset_key_by_hash_.insert_or_assign(hash_key, key);

      // Publish only what needs async residency management:
      // geometry assets referenced by renderable components.
      // Other scene node components (camera/light/etc.) are embedded records
      // and are not assets/resources.
      std::unordered_set<data::AssetKey> seen_geometry_keys;
      for (const auto& renderable :
        decoded->GetComponents<pak::RenderableRecord>()) {
        if (!seen_geometry_keys.insert(renderable.geometry_key).second) {
          continue;
        }

        auto geom
          = co_await LoadGeometryAssetAsyncImpl(renderable.geometry_key);
        if (!geom) {
          continue;
        }

        AddAssetDependency(key, renderable.geometry_key);
        content_cache_.CheckIn(HashAssetKey(renderable.geometry_key));
      }

      co_return decoded;
    } catch (const co::TaskCancelledException& e) {
      throw OperationCancelledException(e.what());
    }
  }();

  co::Shared shared(std::move(op));
  in_flight_scene_assets_.insert_or_assign(hash_key, shared);
  co_return co_await shared;
}

template <PakResource T>
auto AssetLoader::LoadResourceAsync(const oxygen::content::ResourceKey key)
  -> co::Co<std::shared_ptr<T>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadResourceAsync");
  DLOG_F(2, "type    : {}", T::ClassTypeNamePretty());
  DLOG_F(2, "key     : {}", key);
  DLOG_F(2, "offline : {}", work_offline_);

  AssertOwningThread();

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
    LOG_F(ERROR,
      "ResourceKey type mismatch for {}: key_type={} expected_type={}",
      T::ClassTypeNamePretty(), internal_key.GetResourceTypeIndex(),
      expected_type_index);
    co_return nullptr;
  }

  const auto key_hash = HashResourceKey(key);
  if (auto cached = content_cache_.CheckOut<T>(key_hash)) {
    resource_key_by_hash_.try_emplace(key_hash, key);
    co_return cached;
  }

  if constexpr (std::same_as<T, data::TextureResource>) {
    if (auto it = in_flight_textures_.find(key_hash);
      it != in_flight_textures_.end()) {
      co_return co_await it->second;
    }

    auto op = [this, key,
                key_hash]() -> co::Co<std::shared_ptr<data::TextureResource>> {
      struct EraseOnExit final {
        AssetLoader* loader;
        uint64_t key_hash;
        ~EraseOnExit() noexcept { loader->in_flight_textures_.erase(key_hash); }
      } erase { this, key_hash };

      try {
        if (auto cached
          = content_cache_.CheckOut<data::TextureResource>(key_hash)) {
          co_return cached;
        }

        LOG_F(INFO, "AssetLoader: Decode TextureResource {}", to_string(key));

        const internal::InternalResourceKey internal_key(key);
        const uint16_t source_id = internal_key.GetPakIndex();
        const auto resource_index = internal_key.GetResourceIndex();

        // Resolve on owning thread: choose source, open independent readers,
        // and position the descriptor reader to the resource.
        struct PreparedDecode final {
          LoadFnErased loader;
          std::unique_ptr<serio::AnyReader> desc_reader;
          std::unique_ptr<serio::AnyReader> buf_reader;
          std::unique_ptr<serio::AnyReader> tex_reader;
          const PakFile* source_pak = nullptr;
        } prepared;

        {
          const auto source_it = impl_->source_id_to_index.find(source_id);
          if (source_it == impl_->source_id_to_index.end()) {
            co_return nullptr;
          }
          const auto& source = *impl_->sources.at(source_it->second);

          if (source.GetTypeId() == internal::PakFileSource::ClassTypeId()) {
            const auto* pak_source
              = static_cast<const internal::PakFileSource*>(&source);
            prepared.source_pak = &pak_source->Pak();
          }

          const auto* resource_table = source.GetTextureTable();
          prepared.desc_reader = source.CreateTextureTableReader();
          if (!resource_table || !prepared.desc_reader) {
            co_return nullptr;
          }

          const auto offset = resource_table->GetResourceOffset(resource_index);
          if (!offset) {
            co_return nullptr;
          }
          if (auto seek_res
            = prepared.desc_reader->Seek(static_cast<size_t>(*offset));
            !seek_res) {
            co_return nullptr;
          }

          prepared.buf_reader = source.CreateBufferDataReader();
          prepared.tex_reader = source.CreateTextureDataReader();

          auto loader_it
            = resource_loaders_.find(data::TextureResource::ClassTypeId());
          if (loader_it == resource_loaders_.end()) {
            LOG_F(ERROR, "No loader registered for resource type id: {}",
              data::TextureResource::ClassTypeId());
            co_return nullptr;
          }
          prepared.loader = loader_it->second;
        }

        LOG_F(INFO,
          "AssetLoader: Scheduling TextureResource decode {} on thread pool",
          to_string(key));
        auto decoded = co_await thread_pool_->Run(
          [this, source_id, prepared = std::move(prepared)]() mutable {
            ScopedCurrentSourceId source_guard(source_id);

            LoaderContext context {
              .current_asset_key = {},
              .desc_reader = prepared.desc_reader.get(),
              .data_readers = std::make_tuple(
                prepared.buf_reader.get(), prepared.tex_reader.get()),
              .work_offline = work_offline_,
              .source_pak = prepared.source_pak,
            };

            auto void_ptr = prepared.loader(context);
            auto typed
              = std::static_pointer_cast<data::TextureResource>(void_ptr);
            if (!typed
              || typed->GetTypeId() != data::TextureResource::ClassTypeId()) {
              return std::shared_ptr<data::TextureResource> {};
            }
            return typed;
          });

        AssertOwningThread();
        if (!decoded) {
          co_return nullptr;
        }

        if (content_cache_.Store(key_hash, decoded)) {
          resource_key_by_hash_.insert_or_assign(key_hash, key);
        }

        LOG_F(INFO,
          "AssetLoader: Decoded TextureResource {} ({}x{}, format={}, "
          "bytes={})",
          to_string(key), decoded->GetWidth(), decoded->GetHeight(),
          oxygen::to_string(decoded->GetFormat()), decoded->GetDataSize());
        co_return decoded;
      } catch (const co::TaskCancelledException& e) {
        throw OperationCancelledException(e.what());
      }
    }();

    co::Shared shared(std::move(op));
    in_flight_textures_.insert_or_assign(key_hash, shared);
    co_return co_await shared;
  } else if constexpr (std::same_as<T, data::BufferResource>) {
    if (auto it = in_flight_buffers_.find(key_hash);
      it != in_flight_buffers_.end()) {
      co_return co_await it->second;
    }

    auto op = [this, key,
                key_hash]() -> co::Co<std::shared_ptr<data::BufferResource>> {
      struct EraseOnExit final {
        AssetLoader* loader;
        uint64_t key_hash;
        ~EraseOnExit() noexcept { loader->in_flight_buffers_.erase(key_hash); }
      } erase { this, key_hash };

      try {
        if (auto cached
          = content_cache_.CheckOut<data::BufferResource>(key_hash)) {
          co_return cached;
        }

        const internal::InternalResourceKey internal_key(key);
        const uint16_t source_id = internal_key.GetPakIndex();
        const auto resource_index = internal_key.GetResourceIndex();

        // Resolve on owning thread: choose source, open independent readers,
        // and position the descriptor reader to the resource.
        struct PreparedDecode final {
          LoadFnErased loader;
          std::unique_ptr<serio::AnyReader> desc_reader;
          std::unique_ptr<serio::AnyReader> buf_reader;
          std::unique_ptr<serio::AnyReader> tex_reader;
          const PakFile* source_pak = nullptr;
        } prepared;

        {
          const auto source_it = impl_->source_id_to_index.find(source_id);
          if (source_it == impl_->source_id_to_index.end()) {
            co_return nullptr;
          }
          const auto& source = *impl_->sources.at(source_it->second);

          if (source.GetTypeId() == internal::PakFileSource::ClassTypeId()) {
            const auto* pak_source
              = static_cast<const internal::PakFileSource*>(&source);
            prepared.source_pak = &pak_source->Pak();
          }

          const auto* resource_table = source.GetBufferTable();
          prepared.desc_reader = source.CreateBufferTableReader();
          if (!resource_table || !prepared.desc_reader) {
            co_return nullptr;
          }

          const auto offset = resource_table->GetResourceOffset(resource_index);
          if (!offset) {
            co_return nullptr;
          }
          if (auto seek_res
            = prepared.desc_reader->Seek(static_cast<size_t>(*offset));
            !seek_res) {
            co_return nullptr;
          }

          prepared.buf_reader = source.CreateBufferDataReader();
          prepared.tex_reader = source.CreateTextureDataReader();

          auto loader_it
            = resource_loaders_.find(data::BufferResource::ClassTypeId());
          if (loader_it == resource_loaders_.end()) {
            LOG_F(ERROR, "No loader registered for resource type id: {}",
              data::BufferResource::ClassTypeId());
            co_return nullptr;
          }
          prepared.loader = loader_it->second;
        }

        LOG_F(2, "scheduling buffer decode on thread pool");
        auto decoded = co_await thread_pool_->Run(
          [this, source_id, prepared = std::move(prepared)]() mutable {
            ScopedCurrentSourceId source_guard(source_id);

            LoaderContext context {
              .current_asset_key = {},
              .desc_reader = prepared.desc_reader.get(),
              .data_readers = std::make_tuple(
                prepared.buf_reader.get(), prepared.tex_reader.get()),
              .work_offline = work_offline_,
              .source_pak = prepared.source_pak,
            };

            auto void_ptr = prepared.loader(context);
            auto typed
              = std::static_pointer_cast<data::BufferResource>(void_ptr);
            if (!typed
              || typed->GetTypeId() != data::BufferResource::ClassTypeId()) {
              return std::shared_ptr<data::BufferResource> {};
            }
            return typed;
          });

        AssertOwningThread();
        if (!decoded) {
          co_return nullptr;
        }

        if (content_cache_.Store(key_hash, decoded)) {
          resource_key_by_hash_.insert_or_assign(key_hash, key);
        }
        co_return decoded;
      } catch (const co::TaskCancelledException& e) {
        throw OperationCancelledException(e.what());
      }
    }();

    co::Shared shared(std::move(op));
    in_flight_buffers_.insert_or_assign(key_hash, shared);
    co_return co_await shared;
  } else {
    static_assert(std::same_as<T, data::TextureResource>
        || std::same_as<T, data::BufferResource>,
      "Unsupported resource type for LoadResourceAsync");
    co_return nullptr;
  }
}

void oxygen::content::AssetLoader::UnloadObject(const uint64_t cache_key,
  const oxygen::TypeId& type_id, const EvictionReason reason)
{
  EvictionEvent event {
    .key = ResourceKey {},
    .type_id = type_id,
    .reason = reason,
#if !defined(NDEBUG)
    .cache_key_hash = cache_key,
#endif
  };

  if (IsResourceTypeId(type_id)) {
    const auto it = resource_key_by_hash_.find(cache_key);
    if (it == resource_key_by_hash_.end()) {
      LOG_F(WARNING,
        "Eviction without ResourceKey mapping: key_hash={} type_id={}",
        cache_key, type_id);
      return;
    }

    event.key = it->second;
    resource_key_by_hash_.erase(it);
    LOG_F(2, "Evicted resource {} type_id={} reason={}", to_string(event.key),
      type_id, reason);
  } else {
    const auto it = asset_key_by_hash_.find(cache_key);
    if (it == asset_key_by_hash_.end()) {
      LOG_F(WARNING,
        "Eviction without AssetKey mapping: key_hash={} type_id={}", cache_key,
        type_id);
      return;
    }

    event.asset_key = it->second;
    asset_key_by_hash_.erase(it);
    LOG_F(2, "Evicted asset {} type_id={} reason={}",
      data::to_string(*event.asset_key), type_id, reason);
  }

  const auto sub_it = eviction_subscribers_.find(type_id);
  if (sub_it == eviction_subscribers_.end()) {
    return;
  }

  // Prevent re-entrant eviction notifications for the same cache key.
  if (eviction_in_progress_.contains(cache_key)) {
    LOG_F(
      2, "AssetLoader: nested eviction ignored for cache_key={}", cache_key);
    return;
  }

  eviction_in_progress_.insert(cache_key);
  // Ensure the guard is cleared on all exit paths
  struct Guard {
    std::unordered_set<uint64_t>& s;
    uint64_t key;
    ~Guard() noexcept { s.erase(key); }
  } guard { eviction_in_progress_, cache_key };

  for (const auto& subscriber : sub_it->second) {
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

auto AssetLoader::GetPakIndex(const PakFile& pak) const -> uint16_t
{
  // Normalize the path of the input pak
  const auto& pak_path = std::filesystem::weakly_canonical(pak.FilePath());

  for (size_t i = 0; i < impl_->pak_paths.size(); ++i) {
    if (impl_->pak_paths[i] == pak_path) {
      return static_cast<uint16_t>(i);
    }
  }

  LOG_F(ERROR, "PAK file not found in AssetLoader collection (by path)");
  throw std::runtime_error("PAK file not found in AssetLoader collection");
}

auto AssetLoader::MintSyntheticTextureKey() -> ResourceKey
{
  const uint32_t synthetic_index
    = next_synthetic_texture_index_.fetch_add(1, std::memory_order_relaxed);
  const auto resource_type_index = static_cast<uint16_t>(
    IndexOf<data::TextureResource, ResourceTypeList>::value);
  return PackResourceKey(
    kSyntheticSourceId, resource_type_index, synthetic_index);
}

auto AssetLoader::MintSyntheticBufferKey() -> ResourceKey
{
  const uint32_t synthetic_index
    = next_synthetic_buffer_index_.fetch_add(1, std::memory_order_relaxed);
  const auto resource_type_index = static_cast<uint16_t>(
    IndexOf<data::BufferResource, ResourceTypeList>::value);
  return PackResourceKey(
    kSyntheticSourceId, resource_type_index, synthetic_index);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto AssetLoader::DetectCycle(
  const data::AssetKey& start, const data::AssetKey& target) -> bool
{
#if !defined(NDEBUG)
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
    auto it = asset_dependencies_.find(current);
    if (it != asset_dependencies_.end()) {
      for (const auto& dep : it->second) {
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

//=== Explicit Template Instantiations =======================================//

template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::BufferResource>(
    oxygen::content::ResourceKey)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::BufferResource>>;

template OXGN_CNTT_API auto
  AssetLoader::LoadResourceAsync<oxygen::data::TextureResource>(
    oxygen::content::ResourceKey)
    -> oxygen::co::Co<std::shared_ptr<oxygen::data::TextureResource>>;

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
        hash, data::to_string(collision_it->second), data::to_string(key));
    }
  }
#endif

  return hash;
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
  const auto source_it = impl_->source_id_to_index.find(source_id);
  if (source_it == impl_->source_id_to_index.end()) {
    // Source not found? This shouldn't happen for valid keys.
    LOG_F(ERROR, "HashResourceKey: SourceID {} not found", source_id);
    return std::hash<ResourceKey> {}(key);
  }

  const auto& source = *impl_->sources[source_it->second];
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
        hash, data::to_string(it->second.source_key),
        it->second.resource_type_index, it->second.resource_index,
        data::to_string(composite.source_key), composite.resource_type_index,
        composite.resource_index);
    }
  }
#endif

  return hash;
}
