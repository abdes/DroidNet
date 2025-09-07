// This file has been removed as part of the legacy cleanup.
#if 0
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#  pragma once

#  include <cstdint>
#  include <span>
#  include <unordered_map>
#  include <vector>

#  include <glm/glm.hpp>

#  include <Oxygen/Base/Macros.h>
#  include <Oxygen/Renderer/ScenePrep/Types.h>
#  include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::sceneprep {

//! Persistent transform management with GPU buffer allocation.
/*!
 Manages transform deduplication and GPU buffer uploads across frames.
 Maintains a cache of unique transforms to minimize redundant uploads
 and provides stable handle allocation for consistent referencing.
 */
class TransformManager {
public:
  OXGN_RNDR_API TransformManager() = default;
  OXGN_RNDR_API ~TransformManager() = default;

  OXYGEN_MAKE_NON_COPYABLE(TransformManager)
  OXYGEN_DEFAULT_MOVABLE(TransformManager)

  //! Get or allocate a handle for the given transform matrix.
  /*!
   Performs deduplication - identical transforms receive the same handle.
   New transforms are queued for upload and will be flushed on the next
   call to FlushPendingUploads().

   @param transform The world transform matrix
   @return Handle that can be used to reference this transform on GPU
   */
  OXGN_RNDR_API auto GetOrAllocate(const glm::mat4& transform)
    -> TransformHandle;

  //! Update an existing handle's transform (marks dirty & updates normal).
  OXGN_RNDR_API auto Update(TransformHandle handle, const glm::mat4& transform)
    -> void;

  //! Begin a new frame (resets per-frame dirty tracking epoch).
  OXGN_RNDR_API auto BeginFrame() -> void;

  //! Upload all pending transforms to GPU buffer.
  /*!
   Batches all transforms that have been allocated since the last flush
   and uploads them to the GPU transform buffer. Should be called once
   per frame after all GetOrAllocate() calls are complete.
   */
  OXGN_RNDR_API auto FlushPendingUploads() -> void;

  //! Get the total number of unique transforms currently managed.
  OXGN_RNDR_NDAPI auto GetUniqueTransformCount() const -> std::size_t;

  //! Get the transform matrix for a given handle.
  /*!
   @param handle The transform handle
   @return The transform matrix, or identity if handle is invalid
   */
  OXGN_RNDR_NDAPI auto GetTransform(TransformHandle handle) const -> glm::mat4;

  //! Get the (cached) normal matrix for a given handle (stored as mat4).
  OXGN_RNDR_NDAPI auto GetNormalMatrix(TransformHandle handle) const
    -> glm::mat4;

  //! Bulk access (const) to internal world matrices for zero-copy aliasing.
  OXGN_RNDR_NDAPI auto GetWorldMatricesSpan() const noexcept
    -> std::span<const glm::mat4>
  {
    return std::span<const glm::mat4>(transforms_.data(), transforms_.size());
  }

  //! Bulk access (const) to cached normal matrices (mat4 array).
  OXGN_RNDR_NDAPI auto GetNormalMatricesSpan() const noexcept
    -> std::span<const glm::mat4>
  {
    return std::span<const glm::mat4>(
      normal_matrices_.data(), normal_matrices_.size());
  }

  //! Indices (handles) of transforms dirtied/added this frame.
  OXGN_RNDR_NDAPI auto GetDirtyIndices() const noexcept
    -> const std::vector<std::uint32_t>&
  {
    return dirty_indices_;
  }

  //! Check if a handle is valid.
  OXGN_RNDR_NDAPI auto IsValidHandle(TransformHandle handle) const -> bool;

private:
  // We avoid using std::hash<glm::mat4> because GLM doesn't provide a
  // specialization and floating-point precision makes direct hashing
  // fragile. Instead, compute a deterministic quantized key from the
  // matrix components and store a map from that key to a handle. On
  // collisions we resolve by comparing the full matrix against the
  // stored `transforms_` entries.
  std::unordered_map<std::uint64_t, TransformHandle> transform_key_to_handle_;
  std::vector<glm::mat4> transforms_;
  std::vector<glm::mat4> normal_matrices_;
  std::vector<glm::mat4> pending_uploads_;
  std::vector<std::uint32_t> world_versions_;
  std::vector<std::uint32_t> normal_versions_;
  std::uint32_t global_version_ { 0U };
  // Per-frame dirty tracking using epoch tagging to avoid duplicates.
  std::vector<std::uint32_t> dirty_epoch_;
  std::vector<std::uint32_t> dirty_indices_;
  std::uint32_t current_epoch_ { 1U }; // 0 reserved for 'never'
  TransformHandle next_handle_ { 0U };

  // Compute a quantized 64-bit key for the matrix. Quantization reduces
  // the sensitivity to small floating point differences while remaining
  // deterministic. This key is used only as a fast first-stage lookup;
  // exact matrix equality is still used to resolve collisions.
  static inline auto MakeTransformKey(const glm::mat4& m) noexcept
    -> std::uint64_t
  {
    // Quantize each float to 16-bit signed fixed-point after scaling by
    // a reasonable factor (e.g., 1024). This produces 16 bits per value;
    // mix several diagonal elements and selected off-diagonals to form
    // a 64-bit key. Tune scale based on expected transform ranges.
    const float scale = 1024.0f;
    auto q = [&](float v) -> std::int32_t {
      return static_cast<std::int32_t>(std::lround(v * scale));
    };

    // Extract a handful of matrix elements for the key: use diagonal and
    // a couple off-diagonals typically significant for transforms.
    std::uint64_t a = static_cast<std::uint32_t>(q(m[0][0]) & 0xFFFF);
    std::uint64_t b = static_cast<std::uint32_t>(q(m[1][1]) & 0xFFFF);
    std::uint64_t c = static_cast<std::uint32_t>(q(m[2][2]) & 0xFFFF);
    std::uint64_t d = static_cast<std::uint32_t>(q(m[3][3]) & 0xFFFF);

    return (a) | (b << 16) | (c << 32) | (d << 48);
  }
};

} // namespace oxygen::engine::sceneprep
#endif
