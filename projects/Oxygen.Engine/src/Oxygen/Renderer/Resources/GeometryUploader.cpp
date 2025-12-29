//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.Constants.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadHelpers.h>

namespace {

// Enhanced mesh validation with detailed error messages
[[nodiscard]] auto ValidateMesh(
  const oxygen::data::Mesh& mesh, std::string& error_msg) -> bool
{
  const auto& vertices = mesh.Vertices();
  if (vertices.empty()) {
    error_msg = "Mesh has no vertices";
    return false;
  }

  // Check for reasonable vertex count (prevent memory issues)
  constexpr std::uint32_t kMaxVertexCount = 10'000'000; // 10M vertices
  if (vertices.size() > kMaxVertexCount) {
    error_msg = "Mesh vertex count exceeds maximum limit";
    return false;
  }

  // Check for finite vertex data
  for (std::size_t i = 0; i < vertices.size(); ++i) {
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

    const auto max_vertex_index
      = static_cast<std::uint32_t>(vertices.size() - 1);

    // Check indices based on type
    if (index_buffer.type == oxygen::data::detail::IndexType::kUInt16) {
      const auto indices = index_buffer.AsU16();
      for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] > max_vertex_index) {
          error_msg = "Index " + std::to_string(i) + " ("
            + std::to_string(indices[i]) + ") exceeds vertex count ("
            + std::to_string(vertices.size()) + ")";
          return false;
        }
      }
    } else if (index_buffer.type == oxygen::data::detail::IndexType::kUInt32) {
      const auto indices = index_buffer.AsU32();
      for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] > max_vertex_index) {
          error_msg = "Index " + std::to_string(i) + " ("
            + std::to_string(indices[i]) + ") exceeds vertex count ("
            + std::to_string(vertices.size()) + ")";
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

// Hash-based key for mesh deduplication. Uses object identity by design, for
// now.
auto MakeGeometryKey(const oxygen::data::Mesh& mesh) noexcept -> std::uint64_t
{
  // Use mesh object identity instead of content-based hashing This
  // automatically handles LOD switching since different LOD meshes are
  // different objects with different addresses.
  //
  // TODO: Consider hooking this with the AssetLoader to get stable IDs or be
  // notified when meshes are destroyed
  return reinterpret_cast<std::uintptr_t>(&mesh);
}

// Batch policy removed: SubmitMany now coalesces automatically.

} // namespace

namespace oxygen::renderer::resources {

GeometryUploader::GeometryUploader(observer_ptr<Graphics> gfx,
  const observer_ptr<engine::upload::UploadCoordinator> uploader,
  observer_ptr<engine::upload::StagingProvider> provider)
  : gfx_(gfx)
  , uploader_(uploader)
  , staging_provider_(provider)
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");
}

GeometryUploader::~GeometryUploader()
{
  // Best-effort cleanup: unregister our GPU buffers from the registry so they
  // don't linger until registry destruction.
  auto& registry = gfx_->GetResourceRegistry();

  auto unregister_buffers = [&](auto& entry) {
    if (entry.vertex_buffer) {
      registry.UnRegisterResource<graphics::Buffer>(*entry.vertex_buffer);
    }
    if (entry.index_buffer) {
      registry.UnRegisterResource<graphics::Buffer>(*entry.index_buffer);
    }
  };

  std::ranges::for_each(geometry_entries_, unregister_buffers);
  geometry_entries_.clear();

  pending_upload_tickets_.clear();
  mesh_to_handle_.clear();
}

auto GeometryUploader::GetOrAllocate(const data::Mesh& mesh)
  -> engine::sceneprep::GeometryHandle
{
  return GetOrAllocate(mesh, false); // Default to non-critical
}

auto GeometryUploader::GetOrAllocate(const data::Mesh& mesh,
  const bool is_critical) -> engine::sceneprep::GeometryHandle
{
  LOG_SCOPE_FUNCTION(2);

  LOG_F(2, "mesh name     = {}", mesh.GetName());
  LOG_F(2, "mesh vertices = {}", mesh.Vertices().size());
  LOG_F(2, "mesh indices  = {}", mesh.IndexBuffer().Count());

  // Enhanced validation with detailed error messages
  std::string error_msg;
  if (!ValidateMesh(mesh, error_msg)) {
    LOG_F(ERROR, "GeometryUploader::GetOrAllocate failed: {}", error_msg);
    DCHECK_F(false, "GetOrAllocate received invalid mesh: {}", error_msg);
    // ReSharper disable once CppUnreachableCode
    return engine::sceneprep::GeometryHandle { kInvalidBindlessIndex };
  }

  const auto key = MakeGeometryKey(mesh);
  LOG_F(2, "mesh key     = {}", key);
  if (const auto it = mesh_to_handle_.find(key); it != mesh_to_handle_.end()) {
    const auto h = it->second;
    const auto idx = h.get();
    DCHECK_LT_F(idx, geometry_entries_.size()); // valid index
    auto& entry = geometry_entries_[idx];
    if (entry.mesh.get() == &mesh) {
      // Found and same mesh object - update criticality only if stronger than
      // before
      entry.is_critical |= is_critical;
      return h; // Cache hit with exact match
    }
  }

  // Not found or collision mismatch: allocate new handle
  const auto handle = next_handle_;
  DLOG_F(2, "new handle : {}", handle);
  const auto idx = handle.get();

  // Resize geometry_entries_ and GPU arrays if needed
  if (geometry_entries_.size() <= idx) {
    DLOG_F(2, "resize internal storage to : {}", idx + 1U);
    geometry_entries_.resize(idx + 1U);
  }

  // Initialize or update per-handle entry
  auto& entry = geometry_entries_[idx];
  entry.mesh = observer_ptr { &mesh };
  entry.is_critical = is_critical;

  // Mark dirty for this frame
  if (entry.epoch != current_epoch_) {
    entry.epoch = current_epoch_;
    entry.is_dirty = true;
  }

  DLOG_F(2, "key         : {}", fmt::ptr(&mesh));
  DLOG_F(2, "epoch       : {}", current_epoch_);
  DLOG_F(2, "is dirty    : {}", true);
  DLOG_F(2, "is critical : {}", is_critical);

  mesh_to_handle_[key] = handle;
  ++next_handle_;

  return handle;
}

auto GeometryUploader::Update(
  engine::sceneprep::GeometryHandle handle, const data::Mesh& mesh) -> void
{
  const auto idx = static_cast<std::size_t>(handle.get());
  DCHECK_F(idx < geometry_entries_.size(),
    "Update received invalid handle index {} (size={})", idx,
    geometry_entries_.size());

  // Validate the new mesh data
  std::string error_msg;
  if (!ValidateMesh(mesh, error_msg)) {
    LOG_F(ERROR, "GeometryUploader::Update failed: {}", error_msg);
    DCHECK_F(false, "Update received invalid mesh: {}", error_msg);
    // ReSharper disable once CppUnreachableCode
    return; // Don't update with invalid data
  }

  auto& entry = geometry_entries_[idx];
  if (entry.mesh.get() == &mesh) {
    return; // no change
  }

  entry.mesh = observer_ptr { &mesh };

  if (entry.epoch != current_epoch_) {
    entry.epoch = current_epoch_;
    entry.is_dirty = true;
  }
}

auto GeometryUploader::OnFrameStart(renderer::RendererTag, frame::Slot slot)
  -> void
{
  ++current_epoch_;
  if (current_epoch_ == Epoch { 0 }) { // wrapped
    DLOG_F(INFO, "Epoch counter wrapped, resetting all entry epochs");
    current_epoch_ = Epoch { 1 };
    // Reset all per-entry epoch markers when wrap occurs
    for (auto& e : geometry_entries_) {
      e.epoch = Epoch { 0 };
    }
  }

  // Reset frame resource tracking
  frame_resources_ensured_ = false;

  // Clean up completed upload tickets
  RetireCompletedUploads();
}

auto GeometryUploader::IsHandleValid(
  engine::sceneprep::GeometryHandle handle) const -> bool
{
  const auto idx = static_cast<std::size_t>(handle.get());
  return idx < geometry_entries_.size()
    && geometry_entries_[idx].mesh != nullptr;
}

auto GeometryUploader::EnsureFrameResources() -> void
{
  if (frame_resources_ensured_) {
    return; // already done this frame
  }

  // Contract: OnFrameStart() must have been called this frame
  DCHECK_F(current_epoch_ > Epoch { 0 },
    "EnsureFrameResources() called before OnFrameStart() - frame lifecycle "
    "violation");

  UploadBuffers();

  // Mark that frame resources have been ensured this frame
  frame_resources_ensured_ = true;
}

auto GeometryUploader::UploadBuffers() -> void
{
  DCHECK_NOTNULL_F(uploader_);

  DLOG_SCOPE_FUNCTION(2);

  std::vector<engine::upload::UploadRequest> uploads;

  // NOTE: we do modify the entries dirty flag during iteration, but this is
  // fine because view yields references to elements
  auto dirty_entries = geometry_entries_
    | std::views::filter([](const auto& entry) { return entry.is_dirty; });

  for (auto& entry : dirty_entries) {
    DCHECK_NOTNULL_F(entry.mesh);

    DLOG_F(2, "mesh : {}", entry.mesh->GetName());
    DLOG_SCOPE_F(2, fmt::format("Mesh '{}'", entry.mesh->GetName()).c_str());

    entry.is_dirty = false; // reset dirty flag

    if (auto result = UploadVertexBuffer(entry)) {
      uploads.emplace_back(std::move(result.value()));
    } else {
      LOG_F(ERROR, "-failed- vertex buffer upload, frame may be garbage");
      entry.is_dirty = true; // mark dirty again for retry next frame
      continue; // skip index upload if vertex upload failed
    }

    if (entry.mesh->IsIndexed()) {
      if (auto result = UploadIndexBuffer(entry)) {
        uploads.emplace_back(std::move(result.value()));
      } else {
        LOG_F(ERROR, "-failed- index buffer upload, frame may be garbage");
        entry.is_dirty = true; // mark dirty again for retry next frame
      }
    }
  }

  // Submit all vertex uploads in a single batch and track tickets
  if (!uploads.empty()) {
    // Submit to uploader, and let the uploader handle batching, prioritization,
    // and error handling.
    // TODO: consider marking the SubmitMany as noexcept
    auto tickets_result = uploader_->SubmitMany(uploads, *staging_provider_);
    if (tickets_result.has_value()) {
      auto& tickets = tickets_result.value();
      pending_upload_tickets_.insert(
        pending_upload_tickets_.end(), tickets.begin(), tickets.end());
      LOG_F(1, "{} uploads submitted", uploads.size());
    } else {
      std::error_code ec = tickets_result.error();
      LOG_F(ERROR, "Transform upload submission failed: [{}] {}",
        ec.category().name(), ec.message());
    }
  } else {
    LOG_F(1, "no uploads needed this frame");
  }
}

auto GeometryUploader::UploadVertexBuffer(const GeometryEntry& dirty_entry)
  -> std::expected<engine::upload::UploadRequest, bool>
{
  DCHECK_NOTNULL_F(dirty_entry.mesh);

  auto& vertex_buffer = dirty_entry.vertex_buffer;
  auto& srv_index = dirty_entry.vertex_srv_index;

  const auto& vertices = dirty_entry.mesh->Vertices();
  DCHECK_F(!vertices.empty()); // should not have passed through validation
  const uint64_t buffer_size = vertices.size() * sizeof(data::Vertex);
  constexpr auto stride = sizeof(data::Vertex);
  DCHECK_EQ_F(buffer_size % stride, 0);

  DLOG_F(2, "vertex buffer upload: {} bytes", buffer_size);
  using oxygen::engine::upload::internal::EnsureBufferAndSrv;
  if (!EnsureBufferAndSrv(
        *gfx_, vertex_buffer, srv_index, buffer_size, stride, "VertexBuffer")) {
    // logging is done in the helper
    return std::unexpected { false };
  }
  DCHECK_NOTNULL_F(vertex_buffer);
  DCHECK_NE_F(srv_index, kInvalidShaderVisibleIndex);

  // Prepare vertex data upload request
  return engine::upload::UploadRequest {
    .kind = engine::upload::UploadKind::kBuffer,
    .debug_name = "VertexUpload",
    .desc = engine::upload::UploadBufferDesc {
        .dst = vertex_buffer,
        .size_bytes = buffer_size,
        .dst_offset = 0,
    },
    .data = engine::upload::UploadDataView {
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(vertices.data()),
            buffer_size)
    },
  };
}

auto GeometryUploader::UploadIndexBuffer(const GeometryEntry& dirty_entry)
  -> std::expected<engine::upload::UploadRequest, bool>
{
  DCHECK_NOTNULL_F(dirty_entry.mesh);

  auto& index_buffer = dirty_entry.index_buffer;
  auto& srv_index = dirty_entry.index_srv_index;

  const auto mesh = dirty_entry.mesh;
  DCHECK_F(mesh->IsIndexed());
  const auto& indices = mesh->IndexBuffer();
  const uint64_t buffer_size = indices.bytes.size();
  const auto stride = static_cast<uint32_t>(indices.ElementSize());
  DCHECK_EQ_F(buffer_size % stride, 0);

  DLOG_F(2, "index buffer upload: {} bytes", buffer_size);
  using oxygen::engine::upload::internal::EnsureBufferAndSrv;
  if (!EnsureBufferAndSrv(
        *gfx_, index_buffer, srv_index, buffer_size, stride, "IndexBuffer")) {
    return std::unexpected { false };
  }
  DCHECK_NOTNULL_F(index_buffer);
  DCHECK_NE_F(srv_index, kInvalidShaderVisibleIndex);

  // Prepare index data upload request
  return engine::upload::UploadRequest {
    .kind = engine::upload::UploadKind::kBuffer,
    .debug_name = "IndexUpload",
    .desc = engine::upload::UploadBufferDesc {
        .dst = index_buffer,
        .size_bytes = buffer_size,
        .dst_offset = 0,
    },
    .data = engine::upload::UploadDataView { indices.bytes },
  };
}

auto GeometryUploader::GetShaderVisibleIndices(
  engine::sceneprep::GeometryHandle handle) -> MeshShaderVisibleIndices
{
  EnsureFrameResources();

  const auto idx = static_cast<std::size_t>(handle.get());
  DCHECK_F(idx < geometry_entries_.size(),
    "Invalid geometry handle {} (out of range, max={})", handle.get(),
    geometry_entries_.size());
  const auto& entry = geometry_entries_[idx];
  return {
    .vertex_srv_index = entry.vertex_srv_index,
    .index_srv_index = entry.index_srv_index,
  };
}

auto GeometryUploader::RetireCompletedUploads() -> void
{
  if (!uploader_ || pending_upload_tickets_.empty()) {
    return;
  }

  // Check for completed uploads and handle errors
  std::size_t completed_count = 0;
  std::size_t error_count = 0;

  // Use std::remove_if (from <algorithm>) to partition completed tickets and
  // then erase the tail. We must also accumulate counts in the predicate.
  auto predicate = [this, &completed_count, &error_count](
                     const engine::upload::UploadTicket& ticket) {
    if (!uploader_->IsComplete(ticket)) {
      return false; // keep
    }

    ++completed_count;

    // Check for upload errors
    if (const auto result = uploader_->TryGetResult(ticket)) {
      if (!result->success) {
        ++error_count;
        DCHECK_F(result->error.has_value());
        std::error_code ec = *(result->error);
        LOG_F(ERROR, "Could not get result for ticket {}: [{}] {}",
          ticket.id.get(), ec.category().name(), ec.message());
      } else {
        DLOG_F(2, "GeometryUploader: Upload completed successfully ({} bytes)",
          result->bytes_uploaded);
      }
    }

    return true; // remove this ticket
  };

  const auto remove_it
    = std::ranges::remove_if(pending_upload_tickets_, predicate).begin();

  if (remove_it != pending_upload_tickets_.end()) {
    pending_upload_tickets_.erase(remove_it, pending_upload_tickets_.end());

    if (error_count > 0) {
      LOG_F(WARNING, "GeometryUploader: Retired {} upload tickets ({} errors)",
        completed_count, error_count);
    } else if (completed_count > 0) {
      DLOG_F(2, "GeometryUploader: Retired {} completed upload tickets",
        completed_count);
    }
  }
}

} // namespace oxygen::renderer::resources
