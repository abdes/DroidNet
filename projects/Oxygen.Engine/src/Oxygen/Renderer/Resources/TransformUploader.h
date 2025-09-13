//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Upload/RingUploadBuffer.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::renderer::resources {

//! Manages transform matrix uploads to GPU with deduplication and ring
//! buffering
class TransformUploader {
public:
  OXGN_RNDR_API TransformUploader(
    Graphics& gfx, observer_ptr<engine::upload::UploadCoordinator> uploader);

  OXYGEN_MAKE_NON_COPYABLE(TransformUploader)
  OXYGEN_MAKE_NON_MOVABLE(TransformUploader)

  OXGN_RNDR_API ~TransformUploader();

  //! Start a new frame - must be called once per frame before any operations
  OXGN_RNDR_API auto OnFrameStart(oxygen::frame::Slot slot) -> void;

  //! Get or allocate a handle for the given transform matrix
  OXGN_RNDR_API auto GetOrAllocate(const glm::mat4& transform)
    -> engine::sceneprep::TransformHandle;

  //! Check if a handle is valid
  OXGN_RNDR_NDAPI auto IsValidHandle(
    engine::sceneprep::TransformHandle handle) const -> bool;

  //! Upload transforms to GPU - must be called after OnFrameStart()
  OXGN_RNDR_API auto EnsureFrameResources() -> void;

  //! Get the shader-visible index for world transforms buffer
  OXGN_RNDR_NDAPI auto GetWorldsSrvIndex() const -> ShaderVisibleIndex;

  //! Get the shader-visible index for normal matrices buffer
  OXGN_RNDR_NDAPI auto GetNormalsSrvIndex() const -> ShaderVisibleIndex;

  //! Get read-only access to world matrices for debugging
  OXGN_RNDR_NDAPI auto GetWorldMatrices() const noexcept
    -> std::span<const glm::mat4>;

  //! Get read-only access to normal matrices for debugging
  OXGN_RNDR_NDAPI auto GetNormalMatrices() const noexcept
    -> std::span<const glm::mat4>;

private:
  //! Compute normal matrix (inverse transpose of upper 3x3)
  static auto ComputeNormalMatrix(const glm::mat4& world) noexcept -> glm::mat4;

  //! Generate hash key for transform deduplication
  static auto MakeTransformKey(const glm::mat4& transform) noexcept
    -> std::uint64_t;

  //! Check if two matrices are approximately equal
  static auto MatrixAlmostEqual(
    const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f) noexcept -> bool;

  // Core state
  Graphics& gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;

  // Ring buffers for GPU upload
  renderer::upload::RingUploadBuffer worlds_ring_;
  renderer::upload::RingUploadBuffer normals_ring_;

  // Transform storage and deduplication
  std::vector<glm::mat4> transforms_;
  std::vector<glm::mat4> normal_matrices_;
  std::unordered_map<std::uint64_t, engine::sceneprep::TransformHandle>
    key_to_handle_;

  // Simple dirty tracking
  std::vector<std::uint32_t> dirty_epoch_;
  std::uint32_t current_epoch_ { 1U };
  bool uploaded_this_frame_ { false };

  // Cache lifecycle tracking
  std::optional<oxygen::frame::Slot> cache_creation_slot_;

  // Statistics
  std::uint64_t total_allocations_ { 0U };
  std::uint64_t cache_hits_ { 0U };
};

} // namespace oxygen::renderer::resources
