//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <deque>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Upload/RingUploadBuffer.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::resources {

class TransformUploader {
public:
  /*!
   @note TransformUploader lifetime is entirely linked to the Renderer. We
         completely rely on the Renderer to handle the lifetime of the Graphics
         backend, and we assume that for as long as we are alive, the Graphics
         backend is stable. When it is no longer stable, the Renderer is
         responsible for destroying and re-creating the TransformUploader.
  */
  OXGN_RNDR_API TransformUploader(
    Graphics& gfx, observer_ptr<engine::upload::UploadCoordinator> uploader);

  OXYGEN_MAKE_NON_COPYABLE(TransformUploader)
  OXYGEN_MAKE_NON_MOVABLE(TransformUploader)

  OXGN_RNDR_API ~TransformUploader();

  //! Release a previously allocated transform handle. After release the
  //! handle may be reused by future allocations.
  auto Release(engine::sceneprep::TransformHandle handle) -> void;

  auto OnFrameStart() -> void;

  // Deduplication and handle management
  OXGN_RNDR_NDAPI auto GetOrAllocate(const glm::mat4& transform)
    -> engine::sceneprep::TransformHandle;

  auto Update(engine::sceneprep::TransformHandle handle,
    const glm::mat4& transform) -> void;

  [[nodiscard]] auto IsValidHandle(
    engine::sceneprep::TransformHandle handle) const -> bool;

  [[nodiscard]] auto GetWorldMatrices() const noexcept
    -> std::span<const glm::mat4>;
  [[nodiscard]] auto GetNormalMatrices() const noexcept
    -> std::span<const glm::mat4>;
  [[nodiscard]] auto GetDirtyIndices() const noexcept
    -> std::span<const std::uint32_t>;

  //! Ensures all transform GPU resources are prepared for the current frame.
  //! MUST be called after BeginFrame() and before any Get*SrvIndex() calls.
  //! Safe to call multiple times per frame - internally optimized.
  OXGN_RNDR_API auto EnsureFrameResources() -> void;

  //! Returns the bindless descriptor heap index for the world transforms SRV.
  //! REQUIRES: EnsureFrameResources() must have been called this frame.
  [[nodiscard]] OXGN_RNDR_API auto GetWorldsSrvIndex() const
    -> ShaderVisibleIndex;

  //! Returns the bindless descriptor index for the normal matrices SRV.
  //! REQUIRES: EnsureFrameResources() must have been called this frame.
  [[nodiscard]] OXGN_RNDR_API auto GetNormalsSrvIndex() const
    -> ShaderVisibleIndex;

private:
  static auto ComputeNormalMatrix(const glm::mat4& world) noexcept -> glm::mat4;

  auto BuildSparseUploadRequests(const std::vector<std::uint32_t>& indices,
    std::span<const glm::mat4> src, renderer::upload::RingUploadBuffer& ring,
    const char* debug_name) const -> std::vector<engine::upload::UploadRequest>;

  //! Internal methods for resource preparation
  auto UploadWorldMatrices() -> void;
  auto UploadNormalMatrices() -> void;
  //! Retire any completed upload chunks (FIFO) via UploadCoordinator tickets.
  auto ReclaimCompletedChunks() -> void;

  struct ChunkRecord {
    renderer::upload::RingUploadBuffer::ChunkId id { 0 };
    std::vector<engine::upload::UploadTicket> tickets;
  };

  // Deduplication and state
  std::unordered_map<std::uint64_t, engine::sceneprep::TransformHandle>
    transform_key_to_handle_;
  // Free-list of released handle indices for reuse.
  std::vector<std::uint32_t> free_handles_;
  // Reverse mapping from index -> key to allow O(1) removal during Release().
  // Uses std::numeric_limits<uint64_t>::max() as sentinel for 'no key'.
  std::vector<std::uint64_t> index_to_key_;
  std::vector<glm::mat4> transforms_;
  std::vector<glm::mat4> normal_matrices_;
  std::vector<std::uint32_t> world_versions_;
  std::vector<std::uint32_t> normal_versions_;
  std::uint32_t global_version_ { 0U };
  std::vector<std::uint32_t> dirty_epoch_;
  std::vector<std::uint32_t> dirty_indices_;
  std::uint32_t current_epoch_ { 1U }; // 0 reserved for 'never'
  engine::sceneprep::TransformHandle next_handle_ { 0U };
  bool uploaded_ { false };

  // GPU upload dependencies
  Graphics& gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;

  // GPU resources (incremental)
  renderer::upload::RingUploadBuffer worlds_ring_;
  renderer::upload::RingUploadBuffer normals_ring_;
  std::deque<ChunkRecord> world_chunks_;
  std::deque<ChunkRecord> normal_chunks_;
  // Statistics
  // Number of times a transform lookup returned an existing handle (cache hit)
  std::uint64_t transform_reuse_count_ { 0U };
  // Number of times the transforms (world) GPU buffer was recreated/resized
  std::uint64_t worlds_grow_count_ { 0U };
  // Total number of GetOrAllocate calls
  std::uint64_t total_get_calls_ { 0U };
  // Total number of allocations (new handles created)
  std::uint64_t total_allocations_ { 0U };
};

} // namespace oxygen::renderer::resources
