//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
namespace graphics {
  class Buffer;
} // namespace graphics
} // namespace oxygen

namespace oxygen::engine::upload {
class UploadCoordinator;
} // fwd kept if needed elsewhere

namespace oxygen::renderer::upload {

//! Device-local structured buffer with N-partition ring allocator for frames in
//! flight.
/*!
 This class manages a single GPU buffer with N partitions (one per frame in
 flight) and a single shader-visible SRV that exposes the entire buffer. It
 implements a ring allocator (head/tail) per partition while presenting an
 element-oriented API to callers. The buffer can grow; the shader-visible index
 is preserved across resizes.

 @warning Allocation indices are absolute to the entire buffer and remain stable
 within the full cycle of frames in flight. Frame N+1 will not overwrite data
 from frame N until the GPU has finished with frame N.
*/
class RingUploadBuffer {
public:
  struct Allocation {
    // Absolute first index in the structured buffer (from start of buffer)
    std::uint64_t first_index { 0 };
    // Number of elements in the allocation
    std::uint64_t count { 0 };
  };

  OXGN_RNDR_API RingUploadBuffer(oxygen::Graphics& gfx,
    frame::SlotCount partitions, std::uint32_t element_stride,
    std::string debug_label);

  OXYGEN_MAKE_NON_COPYABLE(RingUploadBuffer)
  OXYGEN_DEFAULT_MOVABLE(RingUploadBuffer)

  ~RingUploadBuffer() = default;

  [[nodiscard]] auto GetBuffer() const
    -> const std::shared_ptr<oxygen::graphics::Buffer>&
  {
    return buffer_;
  }

  [[nodiscard]] auto GetBindlessIndex() const noexcept -> ShaderVisibleIndex
  {
    return bindless_index_;
  }

  //! Ensure capacity for at least desired_elements; grows with slack factor.
  //! Returns true if the underlying buffer was created or resized.
  OXGN_RNDR_NDAPI auto ReserveElements(
    std::uint64_t desired_elements, float slack = 0.5f) -> bool;

  //! Allocate a contiguous range in elements from the active partition.
  /*!
   Allocates a contiguous region using ring head/tail within the active
   partition. Placement tries the [tail..end) space first, then wraps to
   [0..head). Allocations are aligned to the element stride (Stride()). Callers
   provide sizes in element units; the allocator converts to bytes using the
   stride.

   @param elements Number of elements to allocate.
   @return Allocation with absolute element index and count, or std::nullopt on
   failure.

   @warning Allocation fails if the active partition's ring is full or
   contiguous space is insufficient even if total free space exists (no
   defragmentation). Returns absolute indices that remain stable within the
   frame cycling period.
  */
  OXGN_RNDR_NDAPI auto Allocate(std::uint64_t elements)
    -> std::optional<Allocation>;

  //! Select which frame-partition subsequent operations target.
  /*!
   Sets the active frame slot and resets the target partition if it's safe
   (frame cycling ensures old partitions are no longer referenced by GPU).

   @param slot Frame slot to activate.
   @warning Logs error and maintains current partition if slot is invalid.
  */
  OXGN_RNDR_API auto SetActivePartition(oxygen::frame::Slot slot) noexcept
    -> void;

  //! Build an upload request targeting a prior Allocate() result.
  /*!
   @param alloc Allocation returned by Allocate().
   @param bytes Source payload.
   @param debug Debug label.
   @return UploadRequest targeting this buffer.
  */
  OXGN_RNDR_NDAPI auto MakeCopyFor(const Allocation& alloc,
    std::span<const std::byte> bytes, std::string_view debug) const noexcept
    -> oxygen::engine::upload::UploadRequest;

  //! Build an upload request covering all allocations in the active partition.
  /*!
   @param bytes Source payload covering all allocated elements sequentially.
   @param debug Debug label.
   @return UploadRequest targeting the range of all allocations, or nullopt if
   no allocations.
  */
  OXGN_RNDR_NDAPI auto MakeUploadRequestForAllocatedRange(
    std::span<const std::byte> bytes, std::string_view debug) const noexcept
    -> std::optional<oxygen::engine::upload::UploadRequest>;

  [[nodiscard]] auto Stride() const noexcept -> std::uint32_t
  {
    return element_stride_;
  }

  [[nodiscard]] auto CapacityElements() const noexcept -> std::uint64_t
  {
    return capacity_elements_per_partition_;
  }

  [[nodiscard]] auto CapacityBytes() const noexcept -> std::uint64_t
  {
    return buffer_ ? buffer_->GetSize() : 0ULL;
  }

  [[nodiscard]] auto UsedBytes() const noexcept -> std::uint64_t
  {
    DCHECK_F(active_partition_ < used_bytes_.size());
    return used_bytes_[active_partition_];
  }

  [[nodiscard]] auto FreeBytes() const noexcept -> std::uint64_t
  {
    const auto stride = static_cast<std::uint64_t>(element_stride_);
    const auto cap_single
      = static_cast<std::uint64_t>(capacity_elements_per_partition_) * stride;
    const auto used = UsedBytes();
    return cap_single >= used ? (cap_single - used) : 0ULL;
  }

  [[nodiscard]] auto IsFull() const noexcept -> bool
  {
    const auto stride = static_cast<std::uint64_t>(element_stride_);
    const auto cap_single
      = static_cast<std::uint64_t>(capacity_elements_per_partition_) * stride;
    return cap_single > 0 && UsedBytes() >= cap_single;
  }

  //! Log telemetry counters (INFO).
  OXGN_RNDR_API auto LogTelemetryStats() const -> void;

private:
  // Minimum elements to allocate on first growth to avoid tiny heaps
  inline static constexpr std::uint32_t kMinElements = 1024;

  oxygen::Graphics& gfx_;
  std::uint32_t element_stride_ { 0 };
  std::uint64_t capacity_elements_per_partition_ { 0 };
  std::shared_ptr<oxygen::graphics::Buffer> buffer_;
  // Single shader-visible index for the SRV
  ShaderVisibleIndex bindless_index_ { kInvalidShaderVisibleIndex };
  std::string debug_label_;

  // Frame-partitioning
  frame::SlotCount partitions_count_;
  frame::Slot active_partition_ { 0 };

  // Ring state per partition (bytes)
  std::vector<std::uint64_t> head_bytes_ {};
  std::vector<std::uint64_t> tail_bytes_ {};
  std::vector<std::uint64_t> used_bytes_ {};

  // Telemetry counters
  std::uint64_t max_used_bytes_ { 0 };
  std::uint32_t failed_allocations_ { 0 };
  std::uint32_t buffer_reallocations_ { 0 };

  // Helper methods
  auto UpdatePerFrameTelemetry() -> void;
};

} // namespace oxygen::renderer::upload
