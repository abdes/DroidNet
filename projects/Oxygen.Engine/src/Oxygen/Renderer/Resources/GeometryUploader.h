//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Epoch.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/Handles.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::renderer::resources {

//===----------------------------------------------------------------------===//
// GeometryUploader: Deduplicates meshes, creates buffers, registers SRVs,
// schedules uploads
//===----------------------------------------------------------------------===//

//! Manages GPU geometry resources with deduplication and bindless access.
/*!
 GeometryUploader provides a modern, handle-based API for managing vertex and
 index buffers with automatic deduplication, persistent caching, and efficient
 upload coordination. It follows the same architectural patterns as
 TransformUploader for consistency and maintainability.

 ## Key Features:

 - **Deduplication**: Identical meshes share GPU resources using content hashing
 - **Handle-based API**: Strong-typed GeometryHandle ensures type safety
 - **Frame lifecycle**: Integrates with Renderer frame and epoch tracking
 - **Bindless rendering**: Stable SRV indices for GPU descriptor arrays
 - **Batch uploads**: Efficient upload coordination via UploadCoordinator
 - **Lazy allocation**: GPU resources created only when needed
 - **Upload timing control**: Renderer controls blocking vs. non-blocking
 uploads

 ## Performance Characteristics:

 - O(1) handle allocation and lookup after initial setup
 - Persistent caching eliminates redundant GPU resource creation
 - Batch uploads minimize GPU submission overhead
 - Frame-coherent dirty tracking reduces unnecessary work

 ## Usage Pattern:

 ```cpp
 // Frame setup (once per frame)
 geometry_uploader->OnFrameStart();

 // Register geometry (as needed)
 auto handle = geometry_uploader->GetOrAllocate(mesh);

 // Prepare GPU resources (before rendering)
 geometry_uploader->EnsureFrameResources();

 // Control upload timing (Renderer decides blocking vs. non-blocking):
 // Non-blocking: EnsureFrameResources() starts uploads asynchronously
 // Blocking: uploader_->AwaitAll(pending_tickets) waits for completion

 // Access bindless indices (during rendering)
 auto vtx_srv = geometry_uploader->GetVertexSrvIndex(handle);
 auto idx_srv = geometry_uploader->GetIndexSrvIndex(handle);
 ```

 ## Upload Timing Control:

 The Renderer controls upload timing via UploadCoordinator methods:
 - **Non-blocking**: Call `EnsureFrameResources()` - uploads happen
 asynchronously
 - **Blocking**: Call `uploader_->AwaitAll(tickets)` after
 `EnsureFrameResources()`

 Future enhancements: Async/await API integration for coroutine-based rendering.

 ## Frame Lifecycle Requirements:

 1. `OnFrameStart()` must be called once per frame before any other operations
 2. `EnsureFrameResources()` must be called before accessing SRV indices
 3. SRV indices remain stable within a frame but may change between frames

 @see TransformUploader for architectural reference
 @see UploadCoordinator for upload coordination details
 @see engine::sceneprep::GeometryHandle for handle type definition
*/
class GeometryUploader {
public:
  struct MeshShaderVisibleIndices {
    ShaderVisibleIndex vertex_srv_index { kInvalidBindlessIndex };
    ShaderVisibleIndex index_srv_index { kInvalidBindlessIndex };
  };

  /*!
   @note GeometryUploader lifetime is entirely linked to the Renderer. We
         completely rely on the Renderer to handle the lifetime of the Graphics
         backend, and we assume that for as long as we are alive, the Graphics
         backend is stable. When it is no longer stable, the Renderer is
         responsible for destroying and re-creating the GeometryUploader.
  */
  OXGN_RNDR_API GeometryUploader(observer_ptr<Graphics> gfx,
    observer_ptr<engine::upload::UploadCoordinator> uploader,
    observer_ptr<engine::upload::StagingProvider> provider);

  OXYGEN_MAKE_NON_COPYABLE(GeometryUploader)
  OXYGEN_MAKE_NON_MOVABLE(GeometryUploader)

  OXGN_RNDR_API ~GeometryUploader();

  //! Called once per frame to reset dirty tracking and advance epoch
  OXGN_RNDR_API auto OnFrameStart(
    renderer::RendererTag, oxygen::frame::Slot slot) -> void;

  //! Deduplication and handle management
  auto GetOrAllocate(const data::Mesh& mesh)
    -> engine::sceneprep::GeometryHandle;

  //! GetOrAllocate with explicit criticality hint for intelligent batch policy
  //! selection
  auto GetOrAllocate(const data::Mesh& mesh, bool is_critical)
    -> engine::sceneprep::GeometryHandle;

  auto Update(engine::sceneprep::GeometryHandle handle, const data::Mesh& mesh)
    -> void;
  [[nodiscard]] auto IsHandleValid(
    engine::sceneprep::GeometryHandle handle) const -> bool;

  //! Ensures all geometry GPU resources are prepared for the current frame.
  //! MUST be called after OnFrameStart() and before any Get*SrvIndex() calls.
  //! Safe to call multiple times per frame - internally optimized.
  OXGN_RNDR_API auto EnsureFrameResources() -> void;

  //! Returns the bindless descriptor heap index for vertex buffer SRV.
  //! Will automatically call EnsureFrameResources() if it hasn't been called
  //! this frame.
  OXGN_RNDR_NDAPI auto GetShaderVisibleIndices(
    engine::sceneprep::GeometryHandle handle) -> MeshShaderVisibleIndices;

  //! Returns the number of pending upload operations.
  //! Useful for debugging and monitoring upload queue health.
  [[nodiscard]] auto GetPendingUploadCount() const -> std::size_t
  {
    return pending_upload_tickets_.size();
  }

  //! Returns span of pending upload tickets for synchronous waiting.
  //! Used by Renderer to wait for all uploads to complete this frame.
  [[nodiscard]] auto GetPendingUploadTickets() const
    -> std::span<const engine::upload::UploadTicket>
  {
    return pending_upload_tickets_;
  }

private:
  struct GeometryEntry {
    observer_ptr<const data::Mesh> mesh { nullptr };
    Epoch epoch { epoch::kNever };
    bool is_dirty { true };
    bool is_critical { false };

    mutable std::shared_ptr<graphics::Buffer> vertex_buffer { nullptr };
    mutable std::shared_ptr<graphics::Buffer> index_buffer { nullptr };
    mutable ShaderVisibleIndex vertex_srv_index { kInvalidBindlessIndex };
    mutable ShaderVisibleIndex index_srv_index { kInvalidBindlessIndex };
  };

  auto UploadBuffers() -> void;
  auto UploadVertexBuffer(const GeometryEntry& dirty_entry)
    -> std::expected<engine::upload::UploadRequest, bool>;
  auto UploadIndexBuffer(const GeometryEntry& dirty_entry)
    -> std::expected<engine::upload::UploadRequest, bool>;

  //! Clean up completed upload tickets
  auto RetireCompletedUploads() -> void;

  Epoch current_epoch_ { 1 }; // 0 reserved for 'never'

  using MeshKey = std::uint64_t; // Simple hash key for deduplication
  using GeometryHandle = engine::sceneprep::GeometryHandle;
  std::unordered_map<MeshKey, GeometryHandle> mesh_to_handle_;
  GeometryHandle next_handle_ { 0U };
  std::vector<GeometryEntry> geometry_entries_;

  observer_ptr<Graphics> gfx_;

  // Upload tracking
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
  observer_ptr<engine::upload::StagingProvider> staging_provider_;
  std::vector<engine::upload::UploadTicket> pending_upload_tickets_;

  // Frame lifecycle tracking
  bool frame_resources_ensured_ { false };
};

} // namespace oxygen::renderer::resources
