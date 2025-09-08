//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <cmath>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::resources {

// No public buffer-creation helpers are exposed; the uploader owns GPU state.

class TransformUploader {
public:
  /*!
   @note TransformUploader lifetime is entirely linked to the Renderer. We
         completely rely on the Renderer to handle the lifetime of the Graphics
         backend and we assume that for as long as we are alive, the Graphics
         backend is stable. When it is no longer stable, the Renderer is
         responsible for destroying and re-creating the TransformUploader.
  */
  OXGN_RNDR_API TransformUploader(Graphics& graphics,
    observer_ptr<engine::upload::UploadCoordinator> uploader);

  OXYGEN_MAKE_NON_COPYABLE(TransformUploader)
  OXYGEN_MAKE_NON_MOVABLE(TransformUploader)

  OXGN_RNDR_API ~TransformUploader();

  auto BeginFrame() -> void;

  // Deduplication and handle management
  auto GetOrAllocate(const glm::mat4& transform)
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

  // GPU upload API (new incremental path)
  //! Ensure a GPU buffer exists for the internally managed world matrices
  //! and upload current CPU data.
  //!
  //! Simplicity-first: performs a full upload of all matrices when called.
  //! Future: use GetDirtyIndices() for sparse updates.
  OXGN_RNDR_API auto EnsureWorldsOnGpu() -> void;

  //! Returns the bindless descriptor heap index for the world transforms SRV.
  //! EnsureWorldsOnGpu() MUST be called before this; callers that fail to do
  //! so are considered incorrect and will be aborted in debug builds.
  OXGN_RNDR_NDAPI auto GetWorldsSrvIndex() const -> std::uint32_t;

  //! Returns the bindless descriptor index for the normal matrices SRV.
  //! EnsureNormalsOnGpu() MUST be called before this; callers that fail to do
  //! so are considered incorrect and will be aborted in debug builds.
  OXGN_RNDR_NDAPI auto GetNormalsSrvIndex() const -> std::uint32_t;

  //! Ensure a GPU buffer exists for the internally managed normal matrices and
  //! upload current CPU data (sparse when beneficial).
  OXGN_RNDR_API auto EnsureNormalsOnGpu() -> void;

private:
  static auto ComputeNormalMatrix(const glm::mat4& world) noexcept -> glm::mat4;

  auto BuildSparseUploadRequests(const std::vector<std::uint32_t>& indices,
    std::span<const glm::mat4> src,
    const std::shared_ptr<graphics::Buffer>& dst, const char* debug_name) const
    -> std::vector<engine::upload::UploadRequest>;

  auto EnsureBufferAndSrv(std::shared_ptr<graphics::Buffer>& buffer,
    std::uint32_t& bindless_index, std::uint64_t size_bytes,
    const char* debug_label) -> bool;

  // Deduplication and state
  std::unordered_map<std::uint64_t, engine::sceneprep::TransformHandle>
    transform_key_to_handle_;
  std::vector<glm::mat4> transforms_;
  std::vector<glm::mat4> normal_matrices_;
  std::vector<std::uint32_t> world_versions_;
  std::vector<std::uint32_t> normal_versions_;
  std::uint32_t global_version_ { 0U };
  std::vector<std::uint32_t> dirty_epoch_;
  std::vector<std::uint32_t> dirty_indices_;
  std::uint32_t current_epoch_ { 1U }; // 0 reserved for 'never'
  engine::sceneprep::TransformHandle next_handle_ { 0U };

  // GPU upload dependencies
  Graphics& gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;

  // GPU resources (incremental)
  std::shared_ptr<graphics::Buffer> gpu_world_buffer_;
  // 0 is reserved/unset; valid indices are non-zero. The existence of the
  // corresponding buffer is the ultimate source of truth for validity.
  std::uint32_t bindless_index_ { 0u };
  std::shared_ptr<graphics::Buffer> gpu_normals_buffer_;
  std::uint32_t normals_bindless_index_ { 0u };
};

} // namespace oxygen::renderer::resources
