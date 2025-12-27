//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <limits>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Internal/ContentSource.h>
#include <Oxygen/Content/Internal/InternalResourceKey.h>
#include <Oxygen/Content/Loaders/BufferLoader.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Content/Loaders/MaterialLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <cstring>

using oxygen::content::AssetLoader;
using oxygen::content::LoaderContext;
using oxygen::content::LoadFunction;
using oxygen::content::PakFile;
using oxygen::content::PakResource;

namespace oxygen::content {

namespace {
  // Reserved source id for synthetic keys. This id must not collide with
  // mounted PAK indices (0..N) or loose cooked sources (0x8000..).
  constexpr uint16_t kSyntheticSourceId = 0xFFFF;
} // namespace

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

  uint16_t next_loose_source_id = 0x8000;

  // Keep a dense, deterministic PAK index space for ResourceKey encoding.
  // This must not be affected by registering non-PAK sources.
  std::vector<std::filesystem::path> pak_paths;
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
} // namespace

//=== Sanity Checking Helper =================================================//
constexpr uint16_t kLooseCookedSourceIdBase = 0x8000;
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
  uint64_t& actual_key_hash, oxygen::TypeId& type_id) -> bool
{
  CHECK_EQ_F(expected_key_hash, actual_key_hash);
  oxygen::content::internal::InternalResourceKey internal_key(actual_key_hash);
  const uint16_t resource_type_index = internal_key.GetResourceTypeIndex();
  const auto class_type_id = GetResourceTypeIdByIndex(resource_type_index);
  CHECK_EQ_F(type_id, class_type_id);
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
  enforce_offline_no_gpu_work_ = config.enforce_offline_no_gpu_work;

  // Register asset loaders
  RegisterLoader(loaders::LoadGeometryAsset, loaders::UnloadGeometryAsset);
  RegisterLoader(loaders::LoadMaterialAsset, loaders::UnloadMaterialAsset);

  // Register resource loaders
  RegisterLoader(loaders::LoadBufferResource, loaders::UnloadBufferResource);
  RegisterLoader(loaders::LoadTextureResource, loaders::UnloadTextureResource);
}

AssetLoader::~AssetLoader() = default;

// LiveObject activation: open the nursery used by the AssetLoader
auto AssetLoader::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
{
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
}

auto AssetLoader::IsRunning() const -> bool { return nursery_ != nullptr; }

auto AssetLoader::AddPakFile(const std::filesystem::path& path) -> void
{
  // Normalize the path to ensure consistent handling
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path);
  const auto pak_index = static_cast<uint16_t>(impl_->pak_paths.size());
  impl_->sources.push_back(
    std::make_unique<internal::PakFileSource>(normalized));
  impl_->source_ids.push_back(pak_index);
  impl_->source_id_to_index.insert_or_assign(
    pak_index, impl_->sources.size() - 1);

  impl_->pak_paths.push_back(normalized);

  LOG_F(INFO, "Mounted PAK content source: id={} path={}", pak_index,
    normalized.string());
}

auto AssetLoader::AddLooseCookedRoot(const std::filesystem::path& path) -> void
{
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path);
  impl_->sources.push_back(
    std::make_unique<internal::LooseCookedSource>(normalized));

  const auto source_id = impl_->next_loose_source_id++;
  DCHECK_F(source_id >= kLooseCookedSourceIdBase);
  impl_->source_ids.push_back(source_id);
  impl_->source_id_to_index.insert_or_assign(
    source_id, impl_->sources.size() - 1);

  LOG_F(INFO, "Mounted loose cooked content source: id={} root={}", source_id,
    normalized.string());
}

auto AssetLoader::ClearMounts() -> void
{
  impl_->sources.clear();
  impl_->source_ids.clear();
  impl_->source_id_to_index.clear();
  impl_->next_loose_source_id = kLooseCookedSourceIdBase;
  impl_->pak_paths.clear();
}

auto AssetLoader::LoadTextureAsync(ResourceKey key, bool offline)
  -> co::Co<std::shared_ptr<data::TextureResource>>
{
  // If the loader hasn't been activated (no nursery), run the load
  // synchronously on the calling thread as a best-effort fallback. This keeps
  // behavior simple and predictable for callers that accidentally call the
  // API too early.
  if (!nursery_) {
    co_return LoadResourceByKey<data::TextureResource>(key, offline);
  }

  // Otherwise run the potentially blocking read on the configured thread
  // pool and return the decoded CPU-side TextureResource.
  auto res = co_await LoadResourceAsync<data::TextureResource>(key, offline);
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

auto AssetLoader::LoadTextureFromBufferAsync(
  ResourceKey key, std::span<const uint8_t> bytes, bool offline)
  -> co::Co<std::shared_ptr<data::TextureResource>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadTextureFromBufferAsync");
  DLOG_F(2, "key     : {}", key);
  DLOG_F(2, "bytes   : {}", bytes.size());
  DLOG_F(2, "offline : {}", offline);

  auto decode_fn
    = [this, key, bytes, offline]() -> std::shared_ptr<data::TextureResource> {
    DLOG_SCOPE_F(2, "AssetLoader DecodeTextureFromBuffer");
    // Find texture resource loader
    auto it = resource_loaders_.find(data::TextureResource::ClassTypeId());
    if (it == resource_loaders_.end()) {
      LOG_F(ERROR, "No texture resource loader registered");
      return nullptr;
    }

    // Build a LoaderContext that supplies the texture data via an AnyReader.
    // Create the reader first, then use aggregate initialization to keep the
    // original style while avoiding any sequencing issues by ensuring the
    // reader object exists prior to forming the context.
    auto tex_reader = std::make_unique<MemoryAnyReader>(bytes);

    LoaderContext context {
      .asset_loader = this,
      .current_asset_key = {},
      .desc_reader = tex_reader.get(),
      .data_readers = std::make_tuple(nullptr, tex_reader.get()),
      .offline = offline,
      .enforce_offline_no_gpu_work = enforce_offline_no_gpu_work_,
      .source_pak = nullptr,
    };

    // Call the registered loader (type-erased)
    auto void_ptr = it->second(context);
    auto typed = std::static_pointer_cast<data::TextureResource>(void_ptr);
    if (!typed) {
      return nullptr;
    }

    // Cache result under the provided ResourceKey
    const auto key_hash = HashResourceKey(key);
    content_cache_.Store(key_hash, typed);
    return typed;
  };

  if (!nursery_) {
    LOG_F(2, "fallback sync (no nursery)");
    co_return decode_fn();
  }

  if (!thread_pool_) {
    LOG_F(2, "fallback sync (no thread pool)");
    co_return decode_fn();
  }

  LOG_F(2, "scheduling on thread pool");
  auto res = co_await thread_pool_->Run(decode_fn);
  co_return res;
}

void AssetLoader::StartLoadTextureFromBuffer(ResourceKey key,
  std::span<const uint8_t> bytes,
  std::function<void(std::shared_ptr<data::TextureResource>)> on_complete)
{
  if (!nursery_) {
    LOG_F(WARNING,
      "AssetLoader::StartLoadTextureFromBuffer called before ActivateAsync");
    // Synchronous fallback: perform decode on calling thread (same logic as
    // the async path) and invoke callback immediately.
    auto it = resource_loaders_.find(data::TextureResource::ClassTypeId());
    if (it == resource_loaders_.end()) {
      on_complete(nullptr);
      return;
    }
    auto tex_reader = std::make_unique<MemoryAnyReader>(bytes);
    LoaderContext context {};
    context.asset_loader = this;
    context.current_asset_key = {};
    context.desc_reader = tex_reader.get();
    context.data_readers = std::make_tuple(nullptr, tex_reader.get());
    context.offline = false;
    context.enforce_offline_no_gpu_work = enforce_offline_no_gpu_work_;
    context.source_pak = nullptr;

    auto void_ptr = it->second(context);
    auto typed = std::static_pointer_cast<data::TextureResource>(void_ptr);
    if (typed) {
      const auto key_hash = HashResourceKey(key);
      content_cache_.Store(key_hash, typed);
    }
    on_complete(std::move(typed));
    return;
  }

  nursery_->Start(
    [this, key, bytes = std::vector<uint8_t>(bytes.begin(), bytes.end()),
      on_complete = std::move(on_complete)]() mutable -> co::Co<> {
      std::span<const uint8_t> span(bytes.data(), bytes.size());
      auto res = co_await LoadTextureFromBufferAsync(key, span, false);
      on_complete(std::move(res));
      co_return;
    });
}

void AssetLoader::StartLoadTexture(ResourceKey key,
  std::function<void(std::shared_ptr<data::TextureResource>)> on_complete)
{
  if (!nursery_) {
    LOG_F(WARNING, "AssetLoader::StartLoadTexture called before ActivateAsync");
    auto res = LoadResourceByKey<data::TextureResource>(key, false);
    on_complete(std::move(res));
    return;
  }

  nursery_->Start(
    [this, key, on_complete = std::move(on_complete)]() mutable -> co::Co<> {
      auto res = co_await LoadTextureAsync(key, false);
      on_complete(std::move(res));
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

auto AssetLoader::AddTypeErasedUnloader(const TypeId type_id,
  const std::string_view type_name, UnloadFnErased&& unloader) -> void
{
  auto [it, inserted] = unloaders_.insert_or_assign(
    type_id, std::forward<UnloadFnErased>(unloader));
  if (!inserted) {
    LOG_F(WARNING, "Replacing unloader for type: {}/{}", type_id, type_name);
  } else {
    LOG_F(INFO, "Registered unloader for type: {}/{}", type_id, type_name);
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
  content_cache_.Touch(resource_key);
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

template <oxygen::IsTyped T>
auto AssetLoader::LoadAsset(const oxygen::data::AssetKey& key, bool offline)
  -> std::shared_ptr<T>
{
  // Check cache first
  auto hash_key = HashAssetKey(key);
  if (auto cached = content_cache_.CheckOut<T>(hash_key)) {
    return cached;
  }

  // Not cached - load from registered content sources.
  for (size_t source_index = 0; source_index < impl_->sources.size();
    ++source_index) {
    const auto& source = *impl_->sources[source_index];

    const auto locator_opt = source.FindAsset(key);
    if (!locator_opt) {
      continue;
    }

    const auto source_id = impl_->source_ids.at(source_index);
    ScopedCurrentSourceId source_guard(source_id);

    auto desc_reader = source.CreateAssetDescriptorReader(*locator_opt);
    if (!desc_reader) {
      continue;
    }

    auto buf_reader = source.CreateBufferDataReader();
    auto tex_reader = source.CreateTextureDataReader();

    const PakFile* source_pak = nullptr;
    if (source.GetTypeId() == internal::PakFileSource::ClassTypeId()) {
      const auto* pak_source
        = static_cast<const internal::PakFileSource*>(&source);
      source_pak = &pak_source->Pak();
    }

    LoaderContext context {
      .asset_loader = this,
      .current_asset_key = key,
      .desc_reader = desc_reader.get(),
      .data_readers = std::make_tuple(buf_reader.get(), tex_reader.get()),
      .offline = offline,
      .enforce_offline_no_gpu_work = enforce_offline_no_gpu_work_,
      .source_pak = source_pak,
    };

    auto loader_it = asset_loaders_.find(T::ClassTypeId());
    if (loader_it == asset_loaders_.end()) {
      LOG_F(ERROR, "No loader registered for asset type: {}",
        T::ClassTypeNamePretty());
      return nullptr;
    }

    auto void_ptr = loader_it->second(context);
    auto typed = std::static_pointer_cast<T>(void_ptr);
    if (typed && typed->GetTypeId() == T::ClassTypeId()) {
      content_cache_.Store(hash_key, typed);
      return typed;
    }

    LOG_F(ERROR, "Loaded asset type mismatch: expected {}, got {}",
      T::ClassTypeNamePretty(), typed ? typed->GetTypeName() : "nullptr");
    return nullptr;
  }

  LOG_F(WARNING, "Asset not found: key={} type={}",
    nostd::to_string(key).c_str(), T::ClassTypeNamePretty());
  for (size_t i = 0; i < impl_->sources.size(); ++i) {
    LOG_F(INFO, "Searched source: idx={} id={} name={}", i,
      impl_->source_ids[i], impl_->sources[i]->DebugName());
  }
  return nullptr;
}

auto AssetLoader::ReleaseAsset(const data::AssetKey& key, bool offline) -> bool
{
  AssertOwningThread();
  // Enable eviction notifications for the whole release cascade, since
  // dependency check-ins may evict multiple entries (resources and/or assets).
  auto eviction_guard
    = content_cache_.OnEviction([&]([[maybe_unused]] uint64_t cache_key,
                                  std::shared_ptr<void> value, TypeId type_id) {
        LOG_F(2, "Evict entry: key_hash={} type_id={}", cache_key, type_id);
        UnloadObject(type_id, value, offline);
      });

  // Recursively release (check in) the asset and all its dependencies.
  ReleaseAssetTree(key, offline);
  // Return true if the asset is no longer present in the cache
  return !content_cache_.Contains(HashAssetKey(key));
}

auto AssetLoader::ReleaseAssetTree(const data::AssetKey& key, bool offline)
  -> void
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
      content_cache_.CheckIn(res_key);
    }
    resource_dependencies_.erase(res_dep_it);
  }
  // Then release asset dependencies
  auto dep_it = asset_dependencies_.find(key);
  if (dep_it != asset_dependencies_.end()) {
    for (const auto& dep_key : dep_it->second) {
      ReleaseAssetTree(dep_key, offline);
    }
    asset_dependencies_.erase(dep_it);
  }
  // Release the asset itself
  content_cache_.CheckIn(HashAssetKey(key));
}

//=== Resource Loading  ======================================================//
template <PakResource T>
auto AssetLoader::LoadResource(const PakFile& pak,
  data::pak::ResourceIndexT resource_index, bool offline) -> std::shared_ptr<T>
{
  const auto pak_index = GetPakIndex(pak);
  ScopedCurrentSourceId source_guard(pak_index);
  return LoadResource<T>(resource_index, offline);
}

template <PakResource T>
auto AssetLoader::LoadResource(
  data::pak::ResourceIndexT resource_index, bool offline) -> std::shared_ptr<T>
{
  const auto source_id = GetCurrentSourceId();
  const auto resource_type_index
    = static_cast<uint16_t>(IndexOf<T, ResourceTypeList>::value);
  const internal::InternalResourceKey internal_key(
    source_id, resource_type_index, resource_index);
  const auto key_hash
    = std::hash<internal::InternalResourceKey> {}(internal_key);

  if (auto cached = content_cache_.CheckOut<T>(key_hash)) {
    return cached;
  }

  const auto source_it = impl_->source_id_to_index.find(source_id);
  if (source_it == impl_->source_id_to_index.end()) {
    return nullptr;
  }
  const auto& source = *impl_->sources.at(source_it->second);

  const PakFile* source_pak = nullptr;
  if (source.GetTypeId() == internal::PakFileSource::ClassTypeId()) {
    const auto* pak_source
      = static_cast<const internal::PakFileSource*>(&source);
    source_pak = &pak_source->Pak();
  }

  const ResourceTable<T>* resource_table = nullptr;
  std::unique_ptr<serio::AnyReader> table_reader;

  if constexpr (std::same_as<T, data::BufferResource>) {
    resource_table = source.GetBufferTable();
    table_reader = source.CreateBufferTableReader();
  } else if constexpr (std::same_as<T, data::TextureResource>) {
    resource_table = source.GetTextureTable();
    table_reader = source.CreateTextureTableReader();
  } else {
    static_assert(std::same_as<T, data::BufferResource>
        || std::same_as<T, data::TextureResource>,
      "Unsupported resource type");
  }

  if (!resource_table || !table_reader) {
    return nullptr;
  }

  const auto offset = resource_table->GetResourceOffset(resource_index);
  if (!offset) {
    return nullptr;
  }

  if (auto seek_res = table_reader->Seek(static_cast<size_t>(*offset));
    !seek_res) {
    return nullptr;
  }

  auto buf_reader = source.CreateBufferDataReader();
  auto tex_reader = source.CreateTextureDataReader();

  LoaderContext context {
    .asset_loader = this,
    .current_asset_key = {},
    .desc_reader = table_reader.get(),
    .data_readers = std::make_tuple(buf_reader.get(), tex_reader.get()),
    .offline = offline,
    .enforce_offline_no_gpu_work = enforce_offline_no_gpu_work_,
    .source_pak = source_pak,
  };

  auto loader_it = resource_loaders_.find(T::ClassTypeId());
  if (loader_it == resource_loaders_.end()) {
    return nullptr;
  }

  auto void_ptr = loader_it->second(context);
  auto typed = std::static_pointer_cast<T>(void_ptr);
  if (typed && typed->GetTypeId() == T::ClassTypeId()) {
    content_cache_.Store(key_hash, typed);
    return typed;
  }
  return nullptr;
}

auto AssetLoader::LoadResourceByKeyErased(
  TypeId type_id, const oxygen::content::ResourceKey key, const bool offline)
  -> std::shared_ptr<void>
{
  if (type_id == data::BufferResource::ClassTypeId()) {
    return LoadResourceByKey<data::BufferResource>(key, offline);
  }
  if (type_id == data::TextureResource::ClassTypeId()) {
    return LoadResourceByKey<data::TextureResource>(key, offline);
  }
  LOG_F(ERROR, "Unsupported resource type id for by-key load: {}", type_id);
  return nullptr;
}

template <PakResource T>
auto AssetLoader::LoadResourceByKey(const oxygen::content::ResourceKey key,
  const bool offline) -> std::shared_ptr<T>
{
  const internal::InternalResourceKey internal_key(key);
  const auto expected_type_index
    = static_cast<uint16_t>(IndexOf<T, ResourceTypeList>::value);
  if (internal_key.GetResourceTypeIndex() != expected_type_index) {
    LOG_F(ERROR,
      "ResourceKey type mismatch for {}: key_type={} expected_type={}",
      T::ClassTypeNamePretty(), internal_key.GetResourceTypeIndex(),
      expected_type_index);
    return nullptr;
  }

  ScopedCurrentSourceId source_guard(internal_key.GetPakIndex());
  return LoadResource<T>(internal_key.GetResourceIndex(), offline);
}

template <PakResource T>
auto AssetLoader::LoadResourceAsync(const oxygen::content::ResourceKey key,
  const bool offline) -> co::Co<std::shared_ptr<T>>
{
  DLOG_SCOPE_F(2, "AssetLoader LoadResourceAsync");
  DLOG_F(2, "type    : {}", T::ClassTypeNamePretty());
  DLOG_F(2, "key     : {}", key);
  DLOG_F(2, "offline : {}", offline);

  if (!thread_pool_) {
    LOG_F(2, "fallback sync (no thread pool)");
    co_return LoadResourceByKey<T>(key, offline);
  }

  LOG_F(2, "scheduling on thread pool");
  auto result = co_await thread_pool_->Run(
    [this, key, offline]() { return LoadResourceByKey<T>(key, offline); });
  co_return result;
}

void oxygen::content::AssetLoader::UnloadObject(
  oxygen::TypeId& type_id, std::shared_ptr<void>& value, bool offline)
{
  // Invoke the resource unload function
  auto unload_it = unloaders_.find(type_id);
  if (unload_it != unloaders_.end()) {
    unload_it->second(value, *this, offline);
  } else {
    LOG_F(WARNING, "No unload function registered for type: {}", type_id);
  }
}

auto AssetLoader::ReleaseResource(const ResourceKey key, bool offline) -> bool
{
  AssertOwningThread();
  const auto key_hash = HashResourceKey(key);

  // The resource should always be checked in on release. Whether it remains in
  // the cache or gets evicted is dependent on the eviction policy.
  auto guard
    = content_cache_.OnEviction([&]([[maybe_unused]] uint64_t cache_key,
                                  std::shared_ptr<void> value, TypeId type_id) {
        DCHECK_F(SanityCheckResourceEviction(key_hash, cache_key, type_id));
        LOG_F(2, "Evict resource: key_hash={} type_id={}", cache_key, type_id);
        UnloadObject(type_id, value, offline);
      });
  content_cache_.CheckIn(HashResourceKey(key));
  return !content_cache_.Contains(key_hash);
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

// Instantiate for all supported asset types
template OXGN_CNTT_API auto AssetLoader::LoadAsset<oxygen::data::GeometryAsset>(
  const data::AssetKey& key, bool offline)
  -> std::shared_ptr<data::GeometryAsset>;
template OXGN_CNTT_API auto AssetLoader::LoadAsset<oxygen::data::MaterialAsset>(
  const data::AssetKey& key, bool offline)
  -> std::shared_ptr<data::MaterialAsset>;

// Instantiate for all supported resource types
template OXGN_CNTT_API auto
AssetLoader::LoadResource<oxygen::data::BufferResource>(
  const PakFile& pak, data::pak::ResourceIndexT resource_index, bool)
  -> std::shared_ptr<data::BufferResource>;
template OXGN_CNTT_API auto
AssetLoader::LoadResource<oxygen::data::TextureResource>(
  const PakFile& pak, data::pak::ResourceIndexT resource_index, bool)
  -> std::shared_ptr<data::TextureResource>;

template OXGN_CNTT_API auto
AssetLoader::LoadResource<oxygen::data::BufferResource>(
  data::pak::ResourceIndexT resource_index, bool)
  -> std::shared_ptr<data::BufferResource>;
template OXGN_CNTT_API auto
AssetLoader::LoadResource<oxygen::data::TextureResource>(
  data::pak::ResourceIndexT resource_index, bool)
  -> std::shared_ptr<data::TextureResource>;

template OXGN_CNTT_API auto
AssetLoader::LoadResourceByKey<oxygen::data::TextureResource>(
  oxygen::content::ResourceKey, bool)
  -> std::shared_ptr<oxygen::data::TextureResource>;

template OXGN_CNTT_API auto
AssetLoader::LoadResourceAsync<oxygen::data::TextureResource>(
  oxygen::content::ResourceKey, bool)
  -> oxygen::co::Co<std::shared_ptr<oxygen::data::TextureResource>>;

//=== Hash Key Generation ====================================================//

auto AssetLoader::HashAssetKey(const data::AssetKey& key) -> uint64_t
{
  return std::hash<data::AssetKey> {}(key);
}

auto AssetLoader::HashResourceKey(const ResourceKey& key) -> uint64_t
{
  const internal::InternalResourceKey internal_key(key);
  return std::hash<internal::InternalResourceKey> {}(internal_key);
}
