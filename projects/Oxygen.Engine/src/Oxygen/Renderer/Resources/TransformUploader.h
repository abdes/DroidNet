//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/Handles.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {
class InlineTransfersCoordinator;
}

namespace oxygen::renderer::resources {

//! Uploads per-object world transforms and corresponding normal matrices to
//! transient GPU buffers for rendering.
/*!
 TransformUploader provides a lightweight, frame-local mechanism to collect
 world transform matrices during scene preparation and publish them as GPU
 shader-visible resources (SRVs). It is optimized for the common case where
 transforms are produced per-frame and small batches of matrices are written
 directly into transient, GPU-visible buffers.

 ### Key responsibilities

 - Collect world-space transform matrices (`glm::mat4`) and compute their normal
   matrices (inverse-transpose of the upper 3x3) for use in shading.
 - Maintain stable, integer `TransformHandle` indices for sceneprep callers by
   reusing storage slots in frame-order. Handles are simply indices into the
   internal arrays and remain valid while less than the current
   `GetWorldMatrices().size()`.
 - Allocate per-frame transient GPU buffers via `TransientStructuredBuffer` and
   publish shader-visible indices (`GetWorldsSrvIndex()` /
   `GetNormalsSrvIndex()`) once `EnsureFrameResources()` runs (or lazily on
   first const access).

 ### Frame lifecycle and usage contract

 - Call `OnFrameStart()` at the beginning of each frame with the current
   `SequenceNumber` and `Slot` before any `GetOrAllocate()` calls.
 - Call `GetOrAllocate(transform)` for each object to obtain a
   `TransformHandle`. Internally this either appends a new slot (if the
   per-frame cursor exceeded the stored size) or reuses an existing slot at the
   cursor position and updates the stored matrix. The cursor advances on each
   call and is reset by `OnFrameStart()` to enable deterministic slot-reuse
   across frames.
 - After allocations, call `EnsureFrameResources()` to perform the actual buffer
   allocations and direct writes to GPU memory. The class also performs lazy
   upload when `GetWorldsSrvIndex()`/`GetNormalsSrvIndex()` is called from a
   const context and the SRV indices are not yet populated.

 ### Semantics and guarantees

 - Handle stability: A `TransformHandle` is valid iff its underlying index is
   less than `GetWorldMatrices().size()`. Handles are stable across frames when
   the allocation pattern reuses slots in the same order.
 - Slot reuse: The uploader reuses existing slots in the order transforms are
   requested within a frame. This is intentional and allows handles to be
   deterministic across frames for common allocation patterns. If a frame writes
   fewer items than the stored size, existing slots are updated rather than
   appended.
 - Threading: Not thread-safe. All calls must be made from the thread that owns
   the `Graphics` and command queue used by `InlineTransfersCoordinator`.

 ### Performance notes

 - The uploader favors inline/direct writes to transient buffers (via the
   provided `InlineTransfersCoordinator`) which is low-latency for many small
   uploads. For very large or highly asynchronous upload workloads, consider a
   batched/staged uploader path.
 - The class computes normal matrices on the CPU to avoid duplicating that work
   in shaders and to keep SRV contents self-contained.
*/
class TransformUploader {
public:
  using ProviderT = engine::upload::StagingProvider;
  using CoordinatorT = engine::upload::InlineTransfersCoordinator;

  OXGN_RNDR_API TransformUploader(observer_ptr<Graphics> gfx,
    observer_ptr<ProviderT> provider,
    observer_ptr<CoordinatorT> inline_transfers);

  OXYGEN_MAKE_NON_COPYABLE(TransformUploader)
  OXYGEN_MAKE_NON_MOVABLE(TransformUploader)

  OXGN_RNDR_API ~TransformUploader();

  //! Start a new frame - must be called once per frame before any operations
  OXGN_RNDR_API auto OnFrameStart(renderer::RendererTag,
    oxygen::frame::SequenceNumber sequence, oxygen::frame::Slot slot) -> void;

  //! Get or allocate a handle for the given transform matrix
  OXGN_RNDR_API auto GetOrAllocate(const glm::mat4& transform)
    -> engine::sceneprep::TransformHandle;

  //! Check if a handle is valid
  OXGN_RNDR_NDAPI auto IsHandleValid(
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

  // Core state
  observer_ptr<Graphics> gfx_;
  observer_ptr<ProviderT> staging_provider_;
  observer_ptr<CoordinatorT> inline_transfers_;

  // Transient per-frame GPU buffers for transforms (direct-write strategy)
  using StagingBufferT = engine::upload::TransientStructuredBuffer;
  StagingBufferT worlds_buffer_;
  StagingBufferT normals_buffer_;

  // Cached SRV indices for fast access
  ShaderVisibleIndex worlds_srv_index_ { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex normals_srv_index_ { kInvalidShaderVisibleIndex };

  // Transform storage
  std::vector<glm::mat4> transforms_;
  std::vector<glm::mat4> normal_matrices_;

  std::uint32_t current_epoch_ { 1U };
  bool uploaded_this_frame_ { false };
  // Per-frame write cursor to reuse existing slots in call order and maintain
  // stable indices across frames. Reset at OnFrameStart.
  std::uint32_t frame_write_count_ { 0U };

  // Statistics
  //! Total number of allocations (grows monotonically)
  std::uint64_t total_allocations_ { 0U };
  //! Total number of GetOrAllocate() calls made (usage metric)
  std::uint64_t total_calls_ { 0U };
};

} // namespace oxygen::renderer::resources
