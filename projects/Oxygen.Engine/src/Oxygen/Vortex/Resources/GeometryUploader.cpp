//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <deque>
#include <expected>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Nexus/FrameDrivenIndexReuse.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/ScenePrep/GeometryRef.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploadHelpers.h> // For EnsureBufferAndSrv

namespace {

constexpr oxygen::vortex::upload::Priority kCriticalUploadPriority { 1 };

// Enhanced mesh validation with detailed error messages
[[nodiscard]] auto ValidateMesh(
  const oxygen::data::Mesh& mesh, std::string& error_msg) -> bool
{
  const auto& vertices = mesh.Vertices();
  const auto vertex_count = vertices.size();
  if (vertices.empty()) {
    error_msg = "Mesh has no vertices";
    return false;
  }

  // Check for reasonable vertex count (prevent memory issues)
  constexpr std::uint32_t kMaxVertexCount = 10'000'000; // 10M vertices
  if (vertex_count > static_cast<std::size_t>(kMaxVertexCount)) {
    error_msg = "Mesh vertex count exceeds maximum limit";
    return false;
  }

  // Check for finite vertex data
  for (std::size_t i = 0; i < vertex_count; ++i) {
    const auto& vertex = vertices[i];

    if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y)
      || !std::isfinite(vertex.position.z)) {
      error_msg = "Vertex " + std::to_string(i) + " has invalid position";
      return false;
    }

    if (!std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y)
      || !std::isfinite(vertex.normal.z)) {
      error_msg = "Vertex " + std::to_string(i) + " has invalid normal";
      return false;
    }

    if (!std::isfinite(vertex.texcoord.x)
      || !std::isfinite(vertex.texcoord.y)) {
      error_msg
        = "Vertex " + std::to_string(i) + " has invalid texture coordinates";
      return false;
    }
  }

  // Validate indices if present
  const auto& index_buffer = mesh.IndexBuffer();
  if (index_buffer.Count() > 0) {
    constexpr std::uint32_t kMaxIndexCount = 30'000'000; // 30M indices
    if (index_buffer.Count() > kMaxIndexCount) {
      error_msg = "Mesh index count exceeds maximum limit";
      return false;
    }

    const auto max_vertex_index = static_cast<std::uint32_t>(vertex_count - 1U);

    // Check indices based on type
    if (index_buffer.type == oxygen::data::detail::IndexType::kUInt16) {
      const auto indices = index_buffer.AsU16();
      for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] > max_vertex_index) {
          error_msg = "Index " + std::to_string(i) + " ("
            + std::to_string(indices[i]) + ") exceeds vertex count ("
            + std::to_string(vertex_count) + ")";
          return false;
        }
      }
    } else if (index_buffer.type == oxygen::data::detail::IndexType::kUInt32) {
      const auto indices = index_buffer.AsU32();
      for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] > max_vertex_index) {
          error_msg = "Index " + std::to_string(i) + " ("
            + std::to_string(indices[i]) + ") exceeds vertex count ("
            + std::to_string(vertex_count) + ")";
          return false;
        }
      }
    }

    if (index_buffer.Count() % 3 != 0) {
      error_msg
        = "Index count is not a multiple of 3 (invalid triangle topology)";
      return false;
    }
  }

  return true;
}

// Batch policy removed: SubmitMany now coalesces automatically.

} // namespace

namespace oxygen::vortex::resources {

class GeometryUploader::Impl {
public:
  Impl(observer_ptr<Graphics> gfx,
    observer_ptr<vortex::upload::UploadCoordinator> uploader,
    observer_ptr<vortex::upload::StagingProvider> provider,
    observer_ptr<content::IAssetLoader> asset_loader);

  ~Impl();

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  auto OnFrameStart(frame::Slot slot) -> void;

  auto GetOrAllocate(const vortex::sceneprep::GeometryRef& geometry)
    -> vortex::sceneprep::GeometryHandle;
  auto GetOrAllocate(const vortex::sceneprep::GeometryRef& geometry,
    bool is_critical) -> vortex::sceneprep::GeometryHandle;

  auto Update(vortex::sceneprep::GeometryHandle handle,
    const vortex::sceneprep::GeometryRef& geometry) -> void;

  [[nodiscard]] auto IsHandleValid(
    vortex::sceneprep::GeometryHandle handle) const -> bool;

  auto EnsureFrameResources() -> void;

  auto GetShaderVisibleIndices(vortex::sceneprep::GeometryHandle handle)
    -> MeshShaderVisibleIndices;

  [[nodiscard]] auto GetPendingUploadCount() const -> std::size_t;

  [[nodiscard]] auto GetPendingUploadTickets() const
    -> std::span<const vortex::upload::UploadTicket>;

private:
  using ReuseStrategy = nexus::FrameDrivenIndexReuse<bindless::HeapIndex>;
  using VersionedIndex = nexus::VersionedIndex<bindless::HeapIndex>;
  struct GeometryEntry;

  [[nodiscard]] static auto ToVersionedIndex_(
    vortex::sceneprep::GeometryHandle handle) -> VersionedIndex;
  [[nodiscard]] auto TryGetCurrentEntry_(
    vortex::sceneprep::GeometryHandle handle) -> GeometryEntry*;
  [[nodiscard]] auto TryGetCurrentEntry_(
    vortex::sceneprep::GeometryHandle handle) const -> const GeometryEntry*;

  struct CallbackGate {
    std::mutex mutex;
    bool alive { true };
  };

  struct PendingAssetEviction {
    data::AssetKey asset_key;
    content::EvictionReason reason { content::EvictionReason::kRefCountZero };
  };

  struct GeometryIdentityKey {
    data::AssetKey asset_key;
    std::uint32_t lod_index { 0U };

    [[nodiscard]] auto operator==(
      const GeometryIdentityKey& other) const noexcept -> bool
    {
      return asset_key == other.asset_key && lod_index == other.lod_index;
    }
  };

  struct GeometryIdentityKeyHash {
    auto operator()(const GeometryIdentityKey& k) const noexcept -> size_t
    {
      size_t seed = 0;
      oxygen::HashCombine(seed, k.asset_key);
      oxygen::HashCombine(seed, k.lod_index);
      return seed;
    }
  };

  struct GeometryEntry {
    data::AssetKey asset_key;
    std::uint32_t lod_index { 0U };
    std::shared_ptr<const data::Mesh> mesh;

    bool is_dirty { true };
    bool is_critical { false };
    bool evicted { false };
    std::uint32_t handle_generation { 0U };
    std::uint64_t generation { 0U };

    std::shared_ptr<graphics::Buffer> vertex_buffer;
    std::shared_ptr<graphics::Buffer> index_buffer;

    ShaderVisibleIndex vertex_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex index_srv_index { kInvalidShaderVisibleIndex };

    ShaderVisibleIndex pending_vertex_srv_index { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex pending_index_srv_index { kInvalidShaderVisibleIndex };

    std::optional<vortex::upload::UploadTicket> pending_vertex_ticket;
    std::optional<vortex::upload::UploadTicket> pending_index_ticket;
    std::uint64_t pending_vertex_generation { 0U };
    std::uint64_t pending_index_generation { 0U };

    // When source indices are 16-bit, we widen to 32-bit for GPU consumption
    // and keep this staging buffer alive until the upload ticket retires.
    std::shared_ptr<std::vector<std::uint32_t>> pending_widened_indices;
  };

  auto UploadBuffers() -> void;
  auto UploadVertexBuffer(GeometryEntry& dirty_entry)
    -> std::expected<vortex::upload::UploadRequest, bool>;
  auto UploadIndexBuffer(GeometryEntry& dirty_entry)
    -> std::expected<vortex::upload::UploadRequest, bool>;
  auto RetireCompletedUploads() -> void;
  auto ProcessEvictions() -> void;
  auto ReleaseEntryBuffers(GeometryEntry& entry) -> void;

  observer_ptr<Graphics> gfx_;
  observer_ptr<vortex::upload::UploadCoordinator> uploader_;
  observer_ptr<vortex::upload::StagingProvider> staging_provider_;
  observer_ptr<content::IAssetLoader> asset_loader_;

  std::shared_ptr<CallbackGate> callback_gate_;
  std::mutex eviction_mutex_;
  std::deque<PendingAssetEviction> pending_evictions_;
  content::IAssetLoader::EvictionSubscription eviction_subscription_;

  std::uint32_t next_handle_index_ { 0U };
  std::shared_ptr<std::vector<bindless::HeapIndex>> free_indices_;
  graphics::detail::DeferredReclaimer slot_reclaimer_;
  ReuseStrategy slot_reuse_;
  std::uint64_t frames_started_count_ { 0U };
  bool has_started_frame_ { false };
  bool frame_resources_ensured_ { false };

  std::vector<GeometryEntry> geometry_entries_;
  std::unordered_map<GeometryIdentityKey, vortex::sceneprep::GeometryHandle,
    GeometryIdentityKeyHash>
    mesh_identity_to_handle_;
  std::vector<vortex::upload::UploadTicket> pending_upload_tickets_;
};

GeometryUploader::GeometryUploader(observer_ptr<Graphics> gfx,
  const observer_ptr<vortex::upload::UploadCoordinator> uploader,
  observer_ptr<vortex::upload::StagingProvider> provider,
  observer_ptr<content::IAssetLoader> asset_loader)
  : impl_(std::make_unique<Impl>(gfx, uploader, provider, asset_loader))
{
}

GeometryUploader::~GeometryUploader() = default;

GeometryUploader::Impl::Impl(observer_ptr<Graphics> gfx,
  const observer_ptr<vortex::upload::UploadCoordinator> uploader,
  observer_ptr<vortex::upload::StagingProvider> provider,
  observer_ptr<content::IAssetLoader> asset_loader)
  : gfx_(gfx)
  , uploader_(uploader)
  , staging_provider_(provider)
  , asset_loader_(asset_loader)
  , free_indices_(std::make_shared<std::vector<bindless::HeapIndex>>())
  , slot_reuse_(slot_reclaimer_,
      [free_indices = free_indices_](
        bindless::HeapIndex index, std::monostate /*unused*/) -> void {
        if (free_indices) {
          free_indices->push_back(index);
        }
      })
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");

  CHECK_NOTNULL_F(asset_loader_, "IAssetLoader cannot be null");

  callback_gate_ = std::make_shared<CallbackGate>();
  CHECK_NOTNULL_F(callback_gate_, "Failed to create callback gate");

  eviction_subscription_ = asset_loader_->SubscribeResourceEvictions(
    data::GeometryAsset::ClassTypeId(),
    [gate = callback_gate_, this](const content::EvictionEvent& event) -> void {
      if (!gate) {
        return;
      }

      std::scoped_lock lock(gate->mutex);
      if (!gate->alive) {
        return;
      }

      if (!event.asset_key.has_value()) {
        return;
      }

      LOG_F(2,
        "GeometryUploader: eviction notification for asset {} "
        "(reason={})",
        data::to_string(*event.asset_key), event.reason);

      std::scoped_lock eviction_lock(eviction_mutex_);
      pending_evictions_.push_back(PendingAssetEviction {
        .asset_key = *event.asset_key,
        .reason = event.reason,
      });
    });
}

GeometryUploader::Impl::~Impl()
{
  // Drain Nexus deferred recycle callbacks before final telemetry snapshot.
  slot_reclaimer_.ProcessAllDeferredReleases();

  const auto telemetry = slot_reuse_.GetTelemetrySnapshot();
  const auto expected_zero_marker = [](const uint64_t value) -> const char* {
    return value == 0U ? " \u2713" : " (expected 0) !";
  };
  LOG_SCOPE_F(INFO, "GeometryUploader Statistics");
  LOG_F(INFO, "frames started            : {}", frames_started_count_);
  LOG_F(INFO, "nexus.allocate_calls      : {}", telemetry.allocate_calls);
  LOG_F(INFO, "nexus.release_calls       : {}", telemetry.release_calls);
  LOG_F(INFO, "nexus.stale_reject_count  : {}{}", telemetry.stale_reject_count,
    expected_zero_marker(telemetry.stale_reject_count));
  LOG_F(INFO, "nexus.duplicate_rejects   : {}{}",
    telemetry.duplicate_reject_count,
    expected_zero_marker(telemetry.duplicate_reject_count));
  LOG_F(INFO, "nexus.reclaimed_count     : {}", telemetry.reclaimed_count);
  LOG_F(INFO, "nexus.pending_count       : {}{}", telemetry.pending_count,
    expected_zero_marker(telemetry.pending_count));
  LOG_F(INFO, "peak geometry slots       : {}", geometry_entries_.size());

  if (callback_gate_) {
    std::scoped_lock lock(callback_gate_->mutex);
    callback_gate_->alive = false;
  }

  // GPU-safety: wait for in-flight work to complete before unregistering
  // descriptor views and buffers.
  if (gfx_ != nullptr) {
    gfx_->Flush();
  }

  if (gfx_ == nullptr) {
    return;
  }

  // Best-effort cleanup: unregister our GPU buffers from the registry so they
  // don't linger until registry destruction.
  auto& registry = gfx_->GetResourceRegistry();

  for (const auto& entry : geometry_entries_) {
    if (entry.vertex_buffer) {
      registry.UnRegisterResource<graphics::Buffer>(*entry.vertex_buffer);
    }
    if (entry.index_buffer) {
      registry.UnRegisterResource<graphics::Buffer>(*entry.index_buffer);
    }
  }
  geometry_entries_.clear();

  pending_upload_tickets_.clear();
  mesh_identity_to_handle_.clear();
}

auto GeometryUploader::GetOrAllocate(
  const vortex::sceneprep::GeometryRef& geometry)
  -> vortex::sceneprep::GeometryHandle
{
  return impl_->GetOrAllocate(geometry);
}

auto GeometryUploader::GetOrAllocate(
  const vortex::sceneprep::GeometryRef& geometry, const bool is_critical)
  -> vortex::sceneprep::GeometryHandle
{
  return impl_->GetOrAllocate(geometry, is_critical);
}

auto GeometryUploader::Impl::ToVersionedIndex_(
  vortex::sceneprep::GeometryHandle handle) -> VersionedIndex
{
  return {
    .index = bindless::HeapIndex { handle.get() },
    .generation = handle.GenerationValue(),
  };
}

auto GeometryUploader::Impl::TryGetCurrentEntry_(
  vortex::sceneprep::GeometryHandle handle) -> GeometryEntry*
{
  if (!handle.IsValid()) {
    return nullptr;
  }
  if (!slot_reuse_.IsHandleCurrent(ToVersionedIndex_(handle))) {
    return nullptr;
  }
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx >= geometry_entries_.size()) {
    return nullptr;
  }
  auto& entry = geometry_entries_[idx];
  if (entry.mesh == nullptr || entry.evicted) {
    return nullptr;
  }
  if (entry.handle_generation != handle.GenerationValue().get()) {
    return nullptr;
  }
  return &entry;
}

auto GeometryUploader::Impl::TryGetCurrentEntry_(
  vortex::sceneprep::GeometryHandle handle) const -> const GeometryEntry*
{
  if (!handle.IsValid()) {
    return nullptr;
  }
  if (!slot_reuse_.IsHandleCurrent(ToVersionedIndex_(handle))) {
    return nullptr;
  }
  const auto idx = static_cast<std::size_t>(handle.get());
  if (idx >= geometry_entries_.size()) {
    return nullptr;
  }
  const auto& entry = geometry_entries_[idx];
  if (entry.mesh == nullptr || entry.evicted) {
    return nullptr;
  }
  if (entry.handle_generation != handle.GenerationValue().get()) {
    return nullptr;
  }
  return &entry;
}

auto GeometryUploader::Impl::GetOrAllocate(
  const vortex::sceneprep::GeometryRef& geometry)
  -> vortex::sceneprep::GeometryHandle
{
  return GetOrAllocate(geometry, false); // Default to non-critical
}

auto GeometryUploader::Impl::GetOrAllocate(
  const vortex::sceneprep::GeometryRef& geometry, const bool is_critical)
  -> vortex::sceneprep::GeometryHandle
{
  LOG_SCOPE_FUNCTION(3);

  if (!geometry.IsValid()) {
    LOG_F(ERROR,
      "GeometryUploader::GetOrAllocate failed: GeometryRef has null mesh");
    DCHECK_F(false, "GetOrAllocate received null mesh");
    return vortex::sceneprep::kInvalidGeometryHandle;
  }

  const auto& mesh = *geometry.mesh;
  const GeometryIdentityKey key {
    .asset_key = geometry.asset_key,
    .lod_index = geometry.lod_index,
  };
  DLOG_F(3, "lod index    = {}", key.lod_index);
  if (auto it = mesh_identity_to_handle_.find(key);
    it != mesh_identity_to_handle_.end()) {
    const auto h = it->second;
    auto* entry = TryGetCurrentEntry_(h);
    if (entry == nullptr) {
      mesh_identity_to_handle_.erase(it);
    } else {
      // Found identity - update criticality only if stronger than before.
      entry->is_critical |= is_critical;

      // If the mesh instance changed for the same stable identity
      // (hot-reload), update and mark dirty to ensure data is reuploaded.
      if (entry->mesh != geometry.mesh) {
        // Validate only when we see a new mesh instance. This avoids repeated
        // O(N) scans on cache hits.
        DLOG_F(3, "mesh name     = {}", mesh.GetName());
        DLOG_F(3, "mesh vertices = {}", mesh.Vertices().size());
        DLOG_F(3, "mesh indices  = {}", mesh.IndexBuffer().Count());

        std::string error_msg;
        if (!ValidateMesh(mesh, error_msg)) {
          LOG_F(ERROR, "GeometryUploader::GetOrAllocate hot-reload ignored: {}",
            error_msg);
          DCHECK_F(false, "GetOrAllocate received invalid mesh: {}", error_msg);
          return h;
        }

        entry->mesh = geometry.mesh;
        entry->is_dirty = true;

        // Do not render with stale SRVs; publish only after new upload
        // completes.
        if (entry->pending_vertex_srv_index == kInvalidShaderVisibleIndex
          && entry->vertex_srv_index.IsValid()) {
          entry->pending_vertex_srv_index = entry->vertex_srv_index;
        }
        if (entry->pending_index_srv_index == kInvalidShaderVisibleIndex
          && entry->index_srv_index.IsValid()) {
          entry->pending_index_srv_index = entry->index_srv_index;
        }
        entry->vertex_srv_index = kInvalidShaderVisibleIndex;
        entry->index_srv_index = kInvalidShaderVisibleIndex;
        entry->pending_vertex_ticket.reset();
        entry->pending_index_ticket.reset();
      }

      entry->handle_generation = h.GenerationValue().get();
      return h;
    }
  }

  // New identity: validate once before allocating resources.
  DLOG_F(3, "mesh name     = {}", mesh.GetName());
  DLOG_F(3, "mesh vertices = {}", mesh.Vertices().size());
  DLOG_F(3, "mesh indices  = {}", mesh.IndexBuffer().Count());

  std::string error_msg;
  if (!ValidateMesh(mesh, error_msg)) {
    LOG_F(ERROR, "GeometryUploader::GetOrAllocate failed: {}", error_msg);
    DCHECK_F(false, "GetOrAllocate received invalid mesh: {}", error_msg);
    return vortex::sceneprep::kInvalidGeometryHandle;
  }

  bindless::HeapIndex handle_index {};
  if (free_indices_->empty()) {
    handle_index = bindless::HeapIndex { next_handle_index_++ };
  } else {
    handle_index = free_indices_->back();
    free_indices_->pop_back();
  }
  const auto versioned_handle = slot_reuse_.ActivateSlot(handle_index);
  const auto handle = vortex::sceneprep::GeometryHandle {
    vortex::sceneprep::GeometryHandle::Index { versioned_handle.index.get() },
    vortex::sceneprep::GeometryHandle::Generation {
      versioned_handle.generation.get() },
  };
  DLOG_F(3, "new handle : {}", handle);
  const auto u_handle = handle.get();

  // Resize geometry_entries_ and GPU arrays if needed
  if (geometry_entries_.size() <= u_handle) {
    DLOG_F(3, "resize internal storage to : {}", u_handle + 1U);
    geometry_entries_.resize(u_handle + 1U);
  }

  // Initialize or update per-handle entry
  auto& entry = geometry_entries_[u_handle];
  entry.asset_key = geometry.asset_key;
  entry.lod_index = geometry.lod_index;
  entry.mesh = geometry.mesh;
  entry.is_dirty = true;
  entry.handle_generation = handle.GenerationValue().get();
  entry.is_critical = is_critical;
  entry.evicted = false;
  entry.vertex_srv_index = kInvalidShaderVisibleIndex;
  entry.index_srv_index = kInvalidShaderVisibleIndex;
  entry.pending_vertex_srv_index = kInvalidShaderVisibleIndex;
  entry.pending_index_srv_index = kInvalidShaderVisibleIndex;
  entry.pending_vertex_ticket.reset();
  entry.pending_index_ticket.reset();
  entry.pending_vertex_generation = 0U;
  entry.pending_index_generation = 0U;
  entry.pending_widened_indices.reset();

  DLOG_F(3, "asset key   : {}", oxygen::data::to_string(entry.asset_key));
  DLOG_F(3, "is dirty    : {}", entry.is_dirty);
  DLOG_F(3, "is critical : {}", is_critical);

  mesh_identity_to_handle_[key] = handle;

  return handle;
}

auto GeometryUploader::Update(vortex::sceneprep::GeometryHandle handle,
  const vortex::sceneprep::GeometryRef& geometry) -> void
{
  impl_->Update(handle, geometry);
}

auto GeometryUploader::Impl::Update(vortex::sceneprep::GeometryHandle handle,
  const vortex::sceneprep::GeometryRef& geometry) -> void
{
  auto* entry = TryGetCurrentEntry_(handle);
  if (entry == nullptr) {
    LOG_F(WARNING, "GeometryUploader::Update ignored stale/invalid handle {}",
      to_string(handle));
    return;
  }

  if (!geometry.IsValid()) {
    LOG_F(ERROR, "GeometryUploader::Update failed: GeometryRef has null mesh");
    DCHECK_F(false, "Update received null mesh");
    return;
  }

  const auto& mesh = *geometry.mesh;

  // Validate the new mesh data
  std::string error_msg;
  if (!ValidateMesh(mesh, error_msg)) {
    LOG_F(ERROR, "GeometryUploader::Update failed: {}", error_msg);
    DCHECK_F(false, "Update received invalid mesh: {}", error_msg);
    return; // Don't update with invalid data
  }

  if (entry->mesh != nullptr) {
    const bool same_identity = entry->asset_key == geometry.asset_key
      && entry->lod_index == geometry.lod_index;
    DCHECK_F(same_identity,
      "Update attempted to rebind handle {} from ({}, lod={}) to ({}, lod={})",
      handle.get(), oxygen::data::to_string(entry->asset_key), entry->lod_index,
      oxygen::data::to_string(geometry.asset_key), geometry.lod_index);
    if (!same_identity) {
      return;
    }
  }

  const GeometryIdentityKey key {
    .asset_key = geometry.asset_key,
    .lod_index = geometry.lod_index,
  };
  mesh_identity_to_handle_[key] = handle;

  entry->asset_key = geometry.asset_key;
  entry->lod_index = geometry.lod_index;
  entry->mesh = geometry.mesh;
  entry->handle_generation = handle.GenerationValue().get();
  entry->is_dirty = true;

  // Hot-reload semantics: invalidate published SRVs and only republish once the
  // new data upload completes.
  if (entry->pending_vertex_srv_index == kInvalidShaderVisibleIndex
    && entry->vertex_srv_index.IsValid()) {
    entry->pending_vertex_srv_index = entry->vertex_srv_index;
  }
  if (entry->pending_index_srv_index == kInvalidShaderVisibleIndex
    && entry->index_srv_index.IsValid()) {
    entry->pending_index_srv_index = entry->index_srv_index;
  }
  entry->vertex_srv_index = kInvalidShaderVisibleIndex;
  entry->index_srv_index = kInvalidShaderVisibleIndex;
  entry->pending_vertex_ticket.reset();
  entry->pending_index_ticket.reset();
}

auto GeometryUploader::OnFrameStart(
  vortex::RendererTag /*tag*/, frame::Slot slot) -> void
{
  impl_->OnFrameStart(slot);
}

auto GeometryUploader::Impl::OnFrameStart(frame::Slot slot) -> void
{
  slot_reuse_.OnBeginFrame(slot);
  ++frames_started_count_;
  has_started_frame_ = true;

  // Reset frame resource tracking
  frame_resources_ensured_ = false;

  // Clean up completed upload tickets
  ProcessEvictions();
  RetireCompletedUploads();
}

auto GeometryUploader::IsHandleValid(
  vortex::sceneprep::GeometryHandle handle) const -> bool
{
  return impl_->IsHandleValid(handle);
}

auto GeometryUploader::Impl::IsHandleValid(
  vortex::sceneprep::GeometryHandle handle) const -> bool
{
  return TryGetCurrentEntry_(handle) != nullptr;
}

auto GeometryUploader::EnsureFrameResources() -> void
{
  impl_->EnsureFrameResources();
}

auto GeometryUploader::Impl::EnsureFrameResources() -> void
{
  if (frame_resources_ensured_) {
    return; // already done this frame
  }

  // Contract: OnFrameStart() must have been called this frame
  DCHECK_F(has_started_frame_,
    "EnsureFrameResources() called before OnFrameStart() - frame lifecycle "
    "violation");

  UploadBuffers();

  // Mark that frame resources have been ensured this frame
  frame_resources_ensured_ = true;
}

//=== Eviction handling =====================================================//

/*!
 Drain pending geometry asset evictions and invalidate all GPU residency for
 matching entries.

 This runs on the render thread during OnFrameStart(). Handles become invalid
 until the asset is reloaded and GetOrAllocate() repopulates the entry.
*/
auto GeometryUploader::Impl::ProcessEvictions() -> void
{
  std::deque<PendingAssetEviction> evictions;
  {
    std::scoped_lock lock(eviction_mutex_);
    if (pending_evictions_.empty()) {
      return;
    }
    evictions.swap(pending_evictions_);
  }

  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");

  std::size_t evictions_without_resident_geometry = 0U;

  for (const auto& eviction : evictions) {
    std::size_t matched_entries = 0U;
    for (std::size_t entry_index = 0; entry_index < geometry_entries_.size();
      ++entry_index) {
      auto& entry = geometry_entries_[entry_index];
      if (entry.asset_key != eviction.asset_key) {
        continue;
      }

      ++matched_entries;
      if (entry.evicted) {
        continue;
      }

      const GeometryIdentityKey key {
        .asset_key = entry.asset_key,
        .lod_index = entry.lod_index,
      };
      mesh_identity_to_handle_.erase(key);

      if (entry.handle_generation != 0U) {
        const auto handle = vortex::sceneprep::GeometryHandle {
          vortex::sceneprep::GeometryHandle::Index {
            static_cast<std::uint32_t>(entry_index) },
          vortex::sceneprep::GeometryHandle::Generation {
            entry.handle_generation },
        };
        if (handle.IsValid()) {
          slot_reuse_.Release(ToVersionedIndex_(handle));
        }
      }

      entry.evicted = true;
      entry.handle_generation = 0U;
      ++entry.generation;

      entry.pending_vertex_ticket.reset();
      entry.pending_index_ticket.reset();
      entry.pending_vertex_generation = 0U;
      entry.pending_index_generation = 0U;
      entry.pending_widened_indices.reset();

      entry.vertex_srv_index = kInvalidShaderVisibleIndex;
      entry.index_srv_index = kInvalidShaderVisibleIndex;
      entry.pending_vertex_srv_index = kInvalidShaderVisibleIndex;
      entry.pending_index_srv_index = kInvalidShaderVisibleIndex;

      ReleaseEntryBuffers(entry);
      entry.mesh.reset();
      entry.is_dirty = false;

      LOG_F(2,
        "GeometryUploader: eviction processed for asset {} "
        "(reason={})",
        data::to_string(eviction.asset_key), eviction.reason);
    }
    if (matched_entries == 0U) {
      ++evictions_without_resident_geometry;
      LOG_F(2,
        "GeometryUploader: eviction for asset {} (reason={}) had no resident "
        "geometry entries (likely never uploaded this run or already released)",
        data::to_string(eviction.asset_key), eviction.reason);
    }
  }

  if (evictions_without_resident_geometry != 0U) {
    LOG_F(INFO,
      "GeometryUploader: {} eviction(s) had no resident geometry entries",
      evictions_without_resident_geometry);
  }
}

//! Release GPU buffers for an entry and unregister them from the registry.
auto GeometryUploader::Impl::ReleaseEntryBuffers(GeometryEntry& entry) -> void
{
  if (!gfx_) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& reclaimer = gfx_->GetDeferredReclaimer();
  std::uint64_t released_vertex_bytes = 0U;
  std::uint64_t released_index_bytes = 0U;

  if (entry.vertex_buffer) {
    released_vertex_bytes = entry.vertex_buffer->GetDescriptor().size_bytes;
    registry.UnRegisterResource<graphics::Buffer>(*entry.vertex_buffer);
    reclaimer.RegisterDeferredRelease(std::move(entry.vertex_buffer));
  }

  if (entry.index_buffer) {
    released_index_bytes = entry.index_buffer->GetDescriptor().size_bytes;
    registry.UnRegisterResource<graphics::Buffer>(*entry.index_buffer);
    reclaimer.RegisterDeferredRelease(std::move(entry.index_buffer));
  }

  if (released_vertex_bytes != 0U || released_index_bytes != 0U) {
    LOG_F(INFO,
      "GeometryUploader: released GPU buffers for asset {} lod={} "
      "(vertex_bytes={}, index_bytes={}, total={})",
      data::to_string(entry.asset_key), entry.lod_index, released_vertex_bytes,
      released_index_bytes, released_vertex_bytes + released_index_bytes);
  }
}

auto GeometryUploader::Impl::UploadBuffers() -> void
{
  DCHECK_NOTNULL_F(uploader_);

  DLOG_SCOPE_FUNCTION(3);

  for (auto& entry : geometry_entries_) {
    if (!entry.is_dirty) {
      continue;
    }
    if (entry.mesh == nullptr) {
      continue;
    }
    if (entry.evicted) {
      continue;
    }

    DLOG_F(3, "mesh : {}", entry.mesh->GetName());
    const auto scope_name = fmt::format("Mesh '{}'", entry.mesh->GetName());
    DLOG_SCOPE_F(3, scope_name.c_str()); // NOLINT

    // Vertex upload ------------------------------------------------------
    if (entry.vertex_srv_index == kInvalidShaderVisibleIndex
      && !entry.pending_vertex_ticket.has_value()) {
      if (auto req = UploadVertexBuffer(entry)) {
        auto ticket_exp = uploader_->Submit(req.value(), *staging_provider_);
        if (ticket_exp.has_value()) {
          entry.pending_vertex_ticket = ticket_exp.value();
          entry.pending_vertex_generation = entry.generation;
        } else {
          const std::error_code ec = ticket_exp.error();
          LOG_F(ERROR,
            "GeometryUploader: Vertex upload submission failed: [{}] {}",
            ec.category().name(), ec.message());
        }
      } else {
        LOG_F(ERROR, "-failed- vertex buffer preparation, will retry");
      }
    }

    // Index upload -------------------------------------------------------
    if (entry.mesh->IsIndexed()
      && entry.index_srv_index == kInvalidShaderVisibleIndex
      && !entry.pending_index_ticket.has_value()) {
      if (auto req = UploadIndexBuffer(entry)) {
        auto ticket_exp = uploader_->Submit(req.value(), *staging_provider_);
        if (ticket_exp.has_value()) {
          entry.pending_index_ticket = ticket_exp.value();
          entry.pending_index_generation = entry.generation;
        } else {
          const std::error_code ec = ticket_exp.error();
          LOG_F(ERROR,
            "GeometryUploader: Index upload submission failed: [{}] {}",
            ec.category().name(), ec.message());
        }
      } else {
        LOG_F(ERROR, "-failed- index buffer preparation, will retry");
      }
    }

    const bool vertex_pending_or_ready = entry.vertex_srv_index.IsValid()
      || entry.pending_vertex_ticket.has_value();
    const bool index_required = entry.mesh->IsIndexed();
    const bool index_pending_or_ready = !index_required
      || entry.index_srv_index.IsValid()
      || entry.pending_index_ticket.has_value();

    // Keep dirty until all required uploads are at least pending. This ensures
    // failed submissions are retried.
    entry.is_dirty = !(vertex_pending_or_ready && index_pending_or_ready);
  }

  // Rebuild the pending ticket list from per-entry state.
  pending_upload_tickets_.clear();
  pending_upload_tickets_.reserve(geometry_entries_.size() * 2);
  for (const auto& entry : geometry_entries_) {
    if (entry.pending_vertex_ticket.has_value()) {
      pending_upload_tickets_.push_back(*entry.pending_vertex_ticket);
    }
    if (entry.pending_index_ticket.has_value()) {
      pending_upload_tickets_.push_back(*entry.pending_index_ticket);
    }
  }
}

auto GeometryUploader::Impl::UploadVertexBuffer(GeometryEntry& dirty_entry)
  -> std::expected<vortex::upload::UploadRequest, bool>
{
  DCHECK_NOTNULL_F(dirty_entry.mesh);

  auto& vertex_buffer = dirty_entry.vertex_buffer;
  auto& srv_index = dirty_entry.pending_vertex_srv_index;

  const auto& vertices = dirty_entry.mesh->Vertices();
  DCHECK_F(!vertices.empty()); // should not have passed through validation
  const auto buffer_size
    = static_cast<std::uint64_t>(vertices.size()) * sizeof(data::Vertex);
  constexpr auto stride = sizeof(data::Vertex);
  DCHECK_EQ_F(buffer_size % stride, 0);

  DLOG_F(2, "vertex buffer upload: {} bytes", buffer_size);
  using oxygen::vortex::upload::internal::EnsureBufferAndSrv;
  if (!EnsureBufferAndSrv(
        *gfx_, vertex_buffer, srv_index, buffer_size, stride, "VertexBuffer")
        .has_value()) {
    // logging is done in the helper
    return std::unexpected { false };
  }
  DCHECK_NOTNULL_F(vertex_buffer);
  DCHECK_NE_F(srv_index, kInvalidShaderVisibleIndex);

  const auto vertex_bytes = std::as_bytes(std::span { vertices });

  // Prepare vertex data upload request
  return vortex::upload::UploadRequest {
    .kind = vortex::upload::UploadKind::kBuffer,
    .priority
    = dirty_entry.is_critical ? kCriticalUploadPriority : vortex::upload::Priority { 0 },
    .debug_name = "VertexUpload",
    .desc = vortex::upload::UploadBufferDesc {
        .dst = vertex_buffer,
        .size_bytes = buffer_size,
        .dst_offset = 0,
    },
    .data = vortex::upload::UploadDataView { vertex_bytes },
  };
}

auto GeometryUploader::Impl::UploadIndexBuffer(GeometryEntry& dirty_entry)
  -> std::expected<vortex::upload::UploadRequest, bool>
{
  DCHECK_NOTNULL_F(dirty_entry.mesh);

  auto& index_buffer = dirty_entry.index_buffer;
  auto& srv_index = dirty_entry.pending_index_srv_index;

  const auto& mesh = dirty_entry.mesh;
  DCHECK_F(mesh->IsIndexed());
  const auto& indices = mesh->IndexBuffer();
  constexpr std::uint32_t kGpuIndexStride = sizeof(std::uint32_t);

  std::span<const std::byte> index_bytes = indices.bytes;
  if (indices.type == oxygen::data::detail::IndexType::kUInt16) {
    auto widened = std::make_shared<std::vector<std::uint32_t>>();
    widened->reserve(indices.Count());
    for (const auto v : indices.Widened()) {
      widened->push_back(v);
    }
    dirty_entry.pending_widened_indices = widened;
    index_bytes = std::as_bytes(std::span { *widened });
  } else {
    dirty_entry.pending_widened_indices.reset();
  }

  const uint64_t buffer_size = index_bytes.size();
  DCHECK_EQ_F(buffer_size % kGpuIndexStride, 0);

  DLOG_F(2, "index buffer upload: {} bytes", buffer_size);
  using oxygen::vortex::upload::internal::EnsureBufferAndSrv;
  if (!EnsureBufferAndSrv(*gfx_, index_buffer, srv_index, buffer_size,
        kGpuIndexStride, "IndexBuffer")
        .has_value()) {
    return std::unexpected { false };
  }
  DCHECK_NOTNULL_F(index_buffer);
  DCHECK_NE_F(srv_index, kInvalidShaderVisibleIndex);

  // Prepare index data upload request
  return vortex::upload::UploadRequest {
    .kind = vortex::upload::UploadKind::kBuffer,
    .priority
    = dirty_entry.is_critical ? kCriticalUploadPriority : vortex::upload::Priority { 0 },
    .debug_name = "IndexUpload",
    .desc = vortex::upload::UploadBufferDesc {
        .dst = index_buffer,
        .size_bytes = buffer_size,
        .dst_offset = 0,
    },
    .data = vortex::upload::UploadDataView { index_bytes },
  };
}

auto GeometryUploader::GetShaderVisibleIndices(
  vortex::sceneprep::GeometryHandle handle) -> MeshShaderVisibleIndices
{
  return impl_->GetShaderVisibleIndices(handle);
}

auto GeometryUploader::Impl::GetShaderVisibleIndices(
  vortex::sceneprep::GeometryHandle handle) -> MeshShaderVisibleIndices
{
  // Invalid/stale handles should safely return invalid indices even if caller
  // has not started a frame yet.
  const auto* entry = TryGetCurrentEntry_(handle);
  if (entry == nullptr) {
    return {};
  }

  EnsureFrameResources();

  // Re-validate after uploads/retirement work; handle may become stale.
  entry = TryGetCurrentEntry_(handle);
  if (entry == nullptr) {
    return {};
  }
  return {
    .vertex_srv_index = entry->vertex_srv_index,
    .index_srv_index = entry->index_srv_index,
  };
}

auto GeometryUploader::Impl::RetireCompletedUploads() -> void
{
  if (!uploader_) {
    return;
  }

  std::size_t completed_count = 0;
  std::size_t error_count = 0;

  auto retire_one = [&](auto& entry,
                        std::optional<vortex::upload::UploadTicket>& ticket_opt,
                        ShaderVisibleIndex& published,
                        ShaderVisibleIndex& pending,
                        std::uint64_t& pending_generation) -> void {
    if (!ticket_opt.has_value()) {
      return;
    }

    if (entry.evicted || pending_generation != entry.generation) {
      ticket_opt.reset();
      pending = kInvalidShaderVisibleIndex;
      pending_generation = 0U;
      return;
    }

    const auto ticket = *ticket_opt;
    const auto u_ticket_id = ticket.id.get();
    const auto is_complete = uploader_->IsComplete(ticket);
    if (!is_complete.has_value()) {
      ++error_count;
      const std::error_code ec = is_complete.error();
      LOG_F(ERROR, "GeometryUploader: IsComplete failed for ticket {}: [{}] {}",
        u_ticket_id, ec.category().name(), ec.message());

      // TicketNotFound is terminal in practice (tracker frame-slot cleanup).
      if (is_complete.error() == vortex::upload::UploadError::kTicketNotFound) {
        LOG_F(WARNING,
          "GeometryUploader: Dropping unknown ticket {} (TicketNotFound)",
          u_ticket_id);
        ticket_opt.reset();
        entry.is_dirty = true;
      }
      return;
    }

    if (!is_complete.value()) {
      return;
    }

    ++completed_count;

    if (const auto result = uploader_->TryGetResult(ticket)) {
      if (!result->success) {
        ++error_count;
        DCHECK_F(result->error.has_value());
        const std::error_code ec = *(result->error);
        LOG_F(ERROR, "GeometryUploader: Upload failed for ticket {}: [{}] {}",
          u_ticket_id, ec.category().name(), ec.message());
        entry.is_dirty = true;
      } else {
        // Only publish indices after data is known good.
        if (pending.IsValid()) {
          published = pending;
        }
        DLOG_F(2, "GeometryUploader: Upload completed successfully ({} bytes)",
          result->bytes_uploaded);
      }
    }

    ticket_opt.reset();
    pending_generation = 0U;
  };

  for (auto& entry : geometry_entries_) {
    if (entry.mesh == nullptr) {
      entry.pending_vertex_ticket.reset();
      entry.pending_index_ticket.reset();
      entry.pending_vertex_generation = 0U;
      entry.pending_index_generation = 0U;
      entry.pending_widened_indices.reset();
      continue;
    }

    retire_one(entry, entry.pending_vertex_ticket, entry.vertex_srv_index,
      entry.pending_vertex_srv_index, entry.pending_vertex_generation);

    const bool had_index_ticket = entry.pending_index_ticket.has_value();
    retire_one(entry, entry.pending_index_ticket, entry.index_srv_index,
      entry.pending_index_srv_index, entry.pending_index_generation);
    if (had_index_ticket && !entry.pending_index_ticket.has_value()) {
      entry.pending_widened_indices.reset();
    }

    // Dirty until all required resources are published.
    const bool vertex_ready = entry.vertex_srv_index.IsValid();
    const bool index_ready
      = !entry.mesh->IsIndexed() || entry.index_srv_index.IsValid();
    entry.is_dirty = !(vertex_ready && index_ready);
  }

  // Rebuild pending tickets.
  pending_upload_tickets_.clear();
  pending_upload_tickets_.reserve(geometry_entries_.size() * 2);
  for (const auto& entry : geometry_entries_) {
    if (entry.pending_vertex_ticket.has_value()) {
      pending_upload_tickets_.push_back(*entry.pending_vertex_ticket);
    }
    if (entry.pending_index_ticket.has_value()) {
      pending_upload_tickets_.push_back(*entry.pending_index_ticket);
    }
  }

  if (completed_count > 0 || error_count > 0) {
    if (error_count > 0) {
      LOG_F(WARNING, "GeometryUploader: Retired {} upload tickets ({} errors)",
        completed_count, error_count);
    } else {
      DLOG_F(2, "GeometryUploader: Retired {} completed upload tickets",
        completed_count);
    }
  }
}

auto GeometryUploader::GetPendingUploadCount() const -> std::size_t
{
  return impl_->GetPendingUploadCount();
}

auto GeometryUploader::Impl::GetPendingUploadCount() const -> std::size_t
{
  return pending_upload_tickets_.size();
}

auto GeometryUploader::GetPendingUploadTickets() const
  -> std::span<const vortex::upload::UploadTicket>
{
  return impl_->GetPendingUploadTickets();
}

auto GeometryUploader::Impl::GetPendingUploadTickets() const
  -> std::span<const vortex::upload::UploadTicket>
{
  return pending_upload_tickets_;
}

} // namespace oxygen::vortex::resources
