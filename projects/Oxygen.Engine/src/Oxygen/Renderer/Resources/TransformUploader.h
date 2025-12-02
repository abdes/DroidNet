//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
}

namespace oxygen::renderer::resources {

//! Manages transform matrix uploads to GPU with per-frame deduplication.
class TransformUploader {
public:
  OXGN_RNDR_API TransformUploader(observer_ptr<Graphics> gfx,
    observer_ptr<engine::upload::StagingProvider> provider,
    observer_ptr<engine::upload::InlineTransfersCoordinator> inline_transfers);

  OXYGEN_MAKE_NON_COPYABLE(TransformUploader)
  OXYGEN_MAKE_NON_MOVABLE(TransformUploader)

  OXGN_RNDR_API ~TransformUploader();

  //! Start a new frame - must be called once per frame before any operations
  OXGN_RNDR_API auto OnFrameStart(
    renderer::RendererTag, oxygen::frame::Slot slot) -> void;

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
  static auto MatrixAlmostEqual(const glm::mat4& a, const glm::mat4& b) noexcept
    -> bool;

  // Core state
  observer_ptr<Graphics> gfx_;
  observer_ptr<engine::upload::StagingProvider> staging_provider_;
  observer_ptr<engine::upload::InlineTransfersCoordinator> inline_transfers_;

  // Transient per-frame GPU buffers for transforms (direct-write strategy)
  engine::upload::TransientStructuredBuffer worlds_buffer_;
  engine::upload::TransientStructuredBuffer normals_buffer_;

  // Cached SRV indices for fast access
  ShaderVisibleIndex worlds_srv_index_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex normals_srv_index_ { kInvalidShaderVisibleIndex };

  // Transform storage and deduplication
  std::vector<glm::mat4> transforms_;
  std::vector<glm::mat4> normal_matrices_;

  // Structure for cached transform entries. Storing the index into
  // transforms_ allows validating the stored matrix to avoid false
  // positives from quantization or hash collisions.
  struct TransformCacheEntry {
    engine::sceneprep::TransformHandle handle;
    std::uint32_t index;
  };

  std::unordered_map<std::uint64_t, TransformCacheEntry> key_to_handle_;

  std::uint32_t current_epoch_ { 1U };
  bool uploaded_this_frame_ { false };
  // Per-frame write cursor to reuse existing slots in call order and avoid
  // unbounded growth when transforms are dynamic. Reset at OnFrameStart.
  std::uint32_t frame_write_count_ { 0U };

  // Statistics
  //! Total number of new logical transforms allocated (grows monotonically)
  std::uint64_t total_allocations_ { 0U };
  //! Number of cache hits where existing transforms were reused (performance
  //! metric)
  std::uint64_t cache_hits_ { 0U };
  //! Total number of GetOrAllocate() calls made (usage metric)
  std::uint64_t total_calls_ { 0U };
};

} // namespace oxygen::renderer::resources
