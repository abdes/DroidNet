//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.Constants.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace {

// Enhanced mesh validation with detailed error messages
[[nodiscard]] auto ValidateMeshDetailed(
  const oxygen::data::Mesh& mesh, std::string& error_msg) -> bool
{
  const auto vertices = mesh.Vertices();
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
  const auto index_buffer = mesh.IndexBuffer();
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

// Hash-based key for mesh deduplication
auto MakeGeometryKey(const oxygen::data::Mesh& mesh) noexcept -> std::uint64_t
{
  // Simple hash based on vertex count and first few vertices
  const auto vertices = mesh.Vertices();
  if (vertices.empty()) {
    return 0;
  }

  std::uint64_t hash = vertices.size();

  // Hash first few vertices for quick differentiation
  const auto hash_count
    = std::min(static_cast<std::size_t>(4), vertices.size());
  for (std::size_t i = 0; i < hash_count; ++i) {
    const auto& v = vertices[i];
    hash ^= std::hash<float> {}(v.position.x) + 0x9e3779b9 + (hash << 6)
      + (hash >> 2);
    hash ^= std::hash<float> {}(v.position.y) + 0x9e3779b9 + (hash << 6)
      + (hash >> 2);
    hash ^= std::hash<float> {}(v.position.z) + 0x9e3779b9 + (hash << 6)
      + (hash >> 2);
  }

  return hash;
}

} // namespace

namespace oxygen::renderer::resources {

GeometryUploader::GeometryUploader(
  Graphics& gfx, observer_ptr<engine::upload::UploadCoordinator> uploader)
  : gfx_(gfx)
  , uploader_(uploader)
{
}

GeometryUploader::~GeometryUploader()
{
  // Best-effort cleanup: unregister our GPU buffers from the registry so they
  // don't linger until registry destruction.
  auto& registry = gfx_.GetResourceRegistry();

  for (auto& buffer : vertex_buffers_) {
    if (buffer) {
      registry.UnRegisterResource(*buffer);
    }
  }

  for (auto& buffer : index_buffers_) {
    if (buffer) {
      registry.UnRegisterResource(*buffer);
    }
  }

  vertex_buffers_.clear();
  index_buffers_.clear();
}

auto GeometryUploader::GetOrAllocate(const data::Mesh& mesh)
  -> engine::sceneprep::GeometryHandle
{
  return GetOrAllocate(mesh, false); // Default to non-critical
}

auto GeometryUploader::GetOrAllocate(const data::Mesh& mesh, bool is_critical)
  -> engine::sceneprep::GeometryHandle
{
  // Enhanced validation with detailed error messages
  std::string error_msg;
  if (!ValidateMeshDetailed(mesh, error_msg)) {
    LOG_F(ERROR, "GeometryUploader::GetOrAllocate failed: {}", error_msg);
    DCHECK_F(false, "GetOrAllocate received invalid mesh: {}", error_msg);
    // ReSharper disable once CppUnreachableCode
    return engine::sceneprep::GeometryHandle { kInvalidBindlessIndex };
  }

  const auto key = ::MakeGeometryKey(mesh);
  if (const auto it = geometry_key_to_handle_.find(key);
    it != geometry_key_to_handle_.end()) {
    const auto h = it->second;
    const auto idx = static_cast<std::size_t>(h.get());
    if (idx < meshes_.size() && meshes_[idx] == &mesh) {
      // Update criticality if this is a new request
      if (is_critical && idx < is_critical_.size()) {
        is_critical_[idx] = true;
      }
      return h; // Cache hit with exact match
    }
  }

  // Not found or collision mismatch: allocate new handle
  const auto handle = next_handle_;
  const auto idx = static_cast<std::size_t>(handle.get());

  // Resize vectors if needed
  if (meshes_.size() <= idx) {
    meshes_.resize(idx + 1U);
    versions_.resize(idx + 1U);
    is_critical_.resize(idx + 1U);
    dirty_epoch_.resize(idx + 1U);
    vertex_buffers_.resize(idx + 1U);
    index_buffers_.resize(idx + 1U);
    vertex_srv_indices_.resize(idx + 1U, kInvalidShaderVisibleIndex);
    index_srv_indices_.resize(idx + 1U, kInvalidShaderVisibleIndex);
  }

  meshes_[idx] = &mesh;
  versions_[idx] = global_version_;
  is_critical_[idx] = is_critical;

  // Mark dirty for this frame
  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }

  geometry_key_to_handle_[key] = handle;
  next_handle_ = engine::sceneprep::GeometryHandle { handle.get() + 1U };

  return handle;
}

auto GeometryUploader::Update(
  engine::sceneprep::GeometryHandle handle, const data::Mesh& mesh) -> void
{
  const auto idx = static_cast<std::size_t>(handle.get());
  DCHECK_F(idx < meshes_.size(),
    "Update received invalid handle index {} (size={})", idx, meshes_.size());

  // Validate the new mesh data
  std::string error_msg;
  if (!ValidateMeshDetailed(mesh, error_msg)) {
    LOG_F(ERROR, "GeometryUploader::Update failed: {}", error_msg);
    DCHECK_F(false, "Update received invalid mesh: {}", error_msg);
    // ReSharper disable once CppUnreachableCode
    return; // Don't update with invalid data
  }

  if (meshes_[idx] == &mesh) {
    return; // no change
  }

  ++global_version_;
  meshes_[idx] = &mesh;
  versions_[idx] = global_version_;

  if (dirty_epoch_[idx] != current_epoch_) {
    dirty_epoch_[idx] = current_epoch_;
    dirty_indices_.push_back(static_cast<std::uint32_t>(idx));
  }
}

auto GeometryUploader::OnFrameStart() -> void
{
  // BeginFrame must be called once per frame by the orchestrator (Renderer).
  ++current_epoch_;
  if (current_epoch_ == 0U) { // wrapped
    current_epoch_ = 1U;
    std::ranges::fill(dirty_epoch_, 0U);
  }
  dirty_indices_.clear();

  // Reset frame resource tracking
  frame_resources_ensured_ = false;

  // Clean up completed upload tickets
  RetireCompletedUploads();
}

auto GeometryUploader::IsValidHandle(
  engine::sceneprep::GeometryHandle handle) const -> bool
{
  const auto idx = static_cast<std::size_t>(handle.get());
  return idx < meshes_.size() && meshes_[idx] != nullptr;
}

auto GeometryUploader::MakeGeometryKey(const data::Mesh& mesh) noexcept
  -> std::uint64_t
{
  return ::MakeGeometryKey(mesh);
}

// ReSharper disable once CppMemberFunctionMayBeConst
auto GeometryUploader::EnsureBufferAndSrv(
  std::shared_ptr<graphics::Buffer>& buffer, ShaderVisibleIndex& bindless_index,
  std::uint64_t size_bytes, const char* debug_label) -> bool
{
  bool recreated = false;
  if (!buffer || buffer->GetSize() < size_bytes) {
    graphics::BufferDesc desc;
    desc.size_bytes = size_bytes;
    desc.usage = graphics::BufferUsage::kStorage;
    desc.memory = graphics::BufferMemory::kDeviceLocal;
    desc.debug_name = debug_label;

    try {
      buffer = gfx_.CreateBuffer(desc);
      if (!buffer) {
        LOG_F(ERROR,
          "GeometryUploader: Failed to create buffer '{}' ({} bytes)",
          debug_label, size_bytes);
        return false;
      }
    } catch (const std::exception& e) {
      LOG_F(ERROR, "GeometryUploader: Exception creating buffer '{}': {}",
        debug_label, e.what());
      return false;
    }

    auto& registry = gfx_.GetResourceRegistry();
    auto& allocator = gfx_.GetDescriptorAllocator();
    registry.Register(buffer);

    graphics::BufferViewDescription view_desc;
    view_desc.view_type = graphics::ResourceViewType::kStructuredBuffer_SRV;
    view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    view_desc.range = { 0, size_bytes };

    // Set appropriate stride based on buffer type
    if (std::string_view(debug_label).find("Vertex")
      != std::string_view::npos) {
      view_desc.stride = sizeof(data::Vertex);
    } else if (std::string_view(debug_label).find("Index")
      != std::string_view::npos) {
      view_desc.stride = sizeof(std::uint32_t); // Assume 32-bit indices for now
    } else {
      view_desc.stride = 4; // Default fallback
    }

    try {
      auto handle
        = allocator.Allocate(graphics::ResourceViewType::kStructuredBuffer_SRV,
          graphics::DescriptorVisibility::kShaderVisible);
      const auto index = allocator.GetShaderVisibleIndex(handle);
      bindless_index = ShaderVisibleIndex(index.get());
      registry.RegisterView(*buffer, std::move(handle), view_desc);

      DLOG_F(1, "GeometryUploader: {} SRV assigned to heap index {}",
        debug_label, bindless_index);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "GeometryUploader: Failed to allocate SRV for '{}': {}",
        debug_label, e.what());
      return false;
    }

    recreated = true;
  }
  return recreated;
}

auto GeometryUploader::PrepareVertexBuffers() -> void
{
  if (dirty_indices_.empty()) {
    return; // No dirty geometry to upload
  }

  std::vector<engine::upload::UploadRequest> uploads;
  uploads.reserve(dirty_indices_.size());

  for (const auto dirty_idx : dirty_indices_) {
    if (dirty_idx >= meshes_.size() || !meshes_[dirty_idx]) {
      continue;
    }

    const auto* mesh = meshes_[dirty_idx];
    const auto vertices = mesh->Vertices();
    if (vertices.empty()) {
      continue;
    }

    const uint64_t buffer_size = vertices.size() * sizeof(data::Vertex);

    // Ensure buffer and SRV exist
    auto& vertex_buffer = vertex_buffers_[dirty_idx];
    auto& srv_index = vertex_srv_indices_[dirty_idx];

    const auto prev_srv_index = srv_index;
    const bool need_recreate = EnsureBufferAndSrv(
      vertex_buffer, srv_index, buffer_size, "VertexBuffer");

    // Check if buffer creation failed
    if (!vertex_buffer) {
      LOG_F(ERROR,
        "GeometryUploader: Failed to create vertex buffer for mesh (size: {} "
        "bytes)",
        buffer_size);
      continue; // Skip this mesh
    }

    // Stability check: once assigned, the bindless index should remain stable
    if (!need_recreate && prev_srv_index != kInvalidShaderVisibleIndex) {
      DCHECK_F(srv_index == prev_srv_index,
        "Vertex SRV index changed from {} to {} unexpectedly", prev_srv_index,
        srv_index);
    }

    // Prepare vertex data upload request
    if (uploader_ && vertex_buffer) {
      const bool is_mesh_critical
        = (dirty_idx < is_critical_.size()) ? is_critical_[dirty_idx] : false;
      const auto batch_policy
        = SelectBatchPolicy(buffer_size, is_mesh_critical);

      engine::upload::UploadRequest req;
      req.kind = engine::upload::UploadKind::kBuffer;
      req.batch_policy = batch_policy;
      req.debug_name = "VertexUpload";
      req.desc = engine::upload::UploadBufferDesc {
        .dst = vertex_buffer,
        .size_bytes = buffer_size,
        .dst_offset = 0,
      };
      const auto* src = reinterpret_cast<const std::byte*>(vertices.data());
      req.data = engine::upload::UploadDataView { std::span<const std::byte>(
        src, buffer_size) };

      uploads.emplace_back(std::move(req));
    }
  }

  // Submit all vertex uploads in a single batch and track tickets
  if (!uploads.empty() && uploader_) {
    auto tickets = uploader_->SubmitMany(uploads);
    pending_upload_tickets_.insert(
      pending_upload_tickets_.end(), tickets.begin(), tickets.end());
  }
}

auto GeometryUploader::PrepareIndexBuffers() -> void
{
  if (dirty_indices_.empty()) {
    return; // No dirty geometry to upload
  }

  std::vector<engine::upload::UploadRequest> uploads;
  uploads.reserve(dirty_indices_.size());

  for (const auto dirty_idx : dirty_indices_) {
    if (dirty_idx >= meshes_.size() || !meshes_[dirty_idx]) {
      continue;
    }

    const auto* mesh = meshes_[dirty_idx];
    const auto index_view = mesh->IndexBuffer();
    if (index_view.Count() == 0) {
      continue; // No indices
    }

    const uint64_t buffer_size = index_view.bytes.size();

    // Ensure buffer and SRV exist
    auto& index_buffer = index_buffers_[dirty_idx];
    auto& srv_index = index_srv_indices_[dirty_idx];

    const auto prev_srv_index = srv_index;
    const bool need_recreate
      = EnsureBufferAndSrv(index_buffer, srv_index, buffer_size, "IndexBuffer");

    // Check if buffer creation failed
    if (!index_buffer) {
      LOG_F(ERROR,
        "GeometryUploader: Failed to create index buffer for mesh (size: {} "
        "bytes)",
        buffer_size);
      continue; // Skip this mesh
    }

    // Stability check: once assigned, the bindless index should remain stable
    if (!need_recreate && prev_srv_index != kInvalidShaderVisibleIndex) {
      DCHECK_F(srv_index == prev_srv_index,
        "Index SRV index changed from {} to {} unexpectedly", prev_srv_index,
        srv_index);
    }

    // Prepare index data upload request
    if (uploader_ && index_buffer) {
      const bool is_mesh_critical
        = (dirty_idx < is_critical_.size()) ? is_critical_[dirty_idx] : false;
      const auto batch_policy
        = SelectBatchPolicy(buffer_size, is_mesh_critical);

      engine::upload::UploadRequest req;
      req.kind = engine::upload::UploadKind::kBuffer;
      req.batch_policy = batch_policy;
      req.debug_name = "IndexUpload";
      req.desc = engine::upload::UploadBufferDesc {
        .dst = index_buffer,
        .size_bytes = buffer_size,
        .dst_offset = 0,
      };
      req.data = engine::upload::UploadDataView { index_view.bytes };

      uploads.emplace_back(std::move(req));
    }
  }

  // Submit all index uploads in a single batch and track tickets
  if (!uploads.empty() && uploader_) {
    auto tickets = uploader_->SubmitMany(uploads);
    pending_upload_tickets_.insert(
      pending_upload_tickets_.end(), tickets.begin(), tickets.end());
  }
}

auto GeometryUploader::EnsureFrameResources() -> void
{
  // Contract: OnFrameStart() must have been called this frame
  DCHECK_F(current_epoch_ > 0U,
    "EnsureFrameResources() called before OnFrameStart() - frame lifecycle "
    "violation");

  // Always ensure both resources are prepared (idempotent operations)
  PrepareVertexBuffers();
  PrepareIndexBuffers();

  // Mark that frame resources have been ensured this frame
  frame_resources_ensured_ = true;
}

auto GeometryUploader::GetVertexSrvIndex(
  engine::sceneprep::GeometryHandle handle) const -> ShaderVisibleIndex
{
  // Check that EnsureFrameResources() was called this frame
  DCHECK_F(frame_resources_ensured_,
    "EnsureFrameResources() must be called before GetVertexSrvIndex() for "
    "handle {}",
    handle.get());

  const auto idx = static_cast<std::size_t>(handle.get());
  DCHECK_F(idx < vertex_srv_indices_.size(),
    "Invalid geometry handle {} (out of range, max={})", handle.get(),
    vertex_srv_indices_.size());

  return vertex_srv_indices_[idx];
}

auto GeometryUploader::GetIndexSrvIndex(
  engine::sceneprep::GeometryHandle handle) const -> ShaderVisibleIndex
{
  // Check that EnsureFrameResources() was called this frame
  DCHECK_F(frame_resources_ensured_,
    "EnsureFrameResources() must be called before GetIndexSrvIndex() for "
    "handle {}",
    handle.get());

  const auto idx = static_cast<std::size_t>(handle.get());
  DCHECK_F(idx < index_srv_indices_.size(),
    "Invalid geometry handle {} (out of range, max={})", handle.get(),
    index_srv_indices_.size());

  return index_srv_indices_[idx];
}

auto GeometryUploader::RetireCompletedUploads() -> void
{
  if (!uploader_ || pending_upload_tickets_.empty()) {
    return;
  }

  // Check for completed uploads and handle errors
  std::size_t completed_count = 0;
  std::size_t error_count = 0;

  auto remove_it = std::ranges::remove_if(pending_upload_tickets_,
    [this, &completed_count, &error_count](
      const engine::upload::UploadTicket& ticket) {
      if (!uploader_->IsComplete(ticket)) {
        return false; // Not completed yet
      }

      ++completed_count;

      // Check for upload errors
      if (const auto result = uploader_->TryGetResult(ticket)) {
        if (!result->success) {
          ++error_count;
          LOG_F(ERROR, "GeometryUploader: Upload failed with error {} - {}",
            static_cast<int>(result->error), result->message);
        } else {
          DLOG_F(2,
            "GeometryUploader: Upload completed successfully ({} bytes)",
            result->bytes_uploaded);
        }
      }

      return true; // Remove this ticket
    }).begin();

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

auto GeometryUploader::SelectBatchPolicy(std::uint64_t size_bytes,
  bool is_critical) const -> engine::upload::BatchPolicy
{
  // Critical geometry (e.g., immediately visible meshes) use immediate upload
  if (is_critical) {
    return engine::upload::BatchPolicy::kImmediate;
  }

  // Large geometry goes to background processing to avoid blocking
  // Use a simple size threshold directly in the method
  constexpr std::uint64_t large_geometry_threshold = 2ULL * 1024 * 1024; // 2MB
  if (size_bytes >= large_geometry_threshold) {
    return engine::upload::BatchPolicy::kBackground;
  }

  // Small to medium geometry uses coalescing for efficiency
  return engine::upload::BatchPolicy::kCoalesce;
}

auto GeometryUploader::GetVertexSrvIndices() const noexcept
  -> std::span<const ShaderVisibleIndex>
{
  return { vertex_srv_indices_.data(), vertex_srv_indices_.size() };
}

auto GeometryUploader::GetIndexSrvIndices() const noexcept
  -> std::span<const ShaderVisibleIndex>
{
  return { index_srv_indices_.data(), index_srv_indices_.size() };
}

} // namespace oxygen::renderer::resources
