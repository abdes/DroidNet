//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <Oxygen/Core/Types/BindlessHandle.h>
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

//=== RingUploadBuffer -----------------------------------------------------//

//! Device-local structured buffer supporting ring-style allocations.
/*!
 This class manages a single GPU buffer and a shader-visible SRV range.
 It implements a ring allocator (head/tail) in bytes while presenting an
 element-oriented API to callers. The buffer can grow; the shader-visible
 index used for the SRV is preserved across resizes.

 Key behaviors:
 - Allocate() and the SRV-related methods operate in element units.
 - ReserveElements() grows the underlying buffer when needed.
 - Clients build upload requests with BuildCopyFor/BuildCopyRange/BuildCopyAll
   and submit them via their upload subsystem.
 - FinalizeChunk() records allocations made since the last finalize and
   returns a chunk id. TryReclaim(id) reclaims the front chunk when the
   caller confirms GPU work referencing that chunk has completed. Reclamation
   is client-driven and strictly FIFO.

 Example usage:

 ```cpp
 ring.ReserveElements(N);
 ring.SetActiveElements(N);

 if (auto alloc = ring.Allocate(elem_count)) {
   auto req = ring.BuildCopyFor(*alloc, bytes, "MyStreamCopy");
   // Submit req via your upload coordinator

   if (auto id = ring.FinalizeChunk()) {
     // Save id and, once uploads complete, call:
     ring.TryReclaim(*id);
   }
 }
 ```

 @warning Reclamation is strictly client-driven and FIFO. Call FinalizeChunk()
 after enqueuing all copies for the current chunk, and call TryReclaim(id)
 only when you have externally verified completion for that chunk's uploads.
*/
class RingUploadBuffer {
public:
  OXGN_RNDR_API RingUploadBuffer(oxygen::Graphics& gfx,
    std::uint32_t element_stride, std::string debug_label);

  OXYGEN_MAKE_NON_COPYABLE(RingUploadBuffer)
  OXYGEN_DEFAULT_MOVABLE(RingUploadBuffer)

  ~RingUploadBuffer() = default;

  //! Ensure capacity for at least desired_elements; grows with slack factor.
  //! Returns true if the underlying buffer was created or resized.
  [[nodiscard]] OXGN_RNDR_API auto ReserveElements(
    std::uint64_t desired_elements, float slack = 0.5f) -> bool;

  [[nodiscard]] auto CapacityElements() const noexcept -> std::uint64_t
  {
    return capacity_elements_;
  }

  [[nodiscard]] auto Stride() const noexcept -> std::uint32_t
  {
    return element_stride_;
  }

  [[nodiscard]] auto GetBuffer() const
    -> const std::shared_ptr<oxygen::graphics::Buffer>&
  {
    return buffer_;
  }

  [[nodiscard]] auto GetBindlessIndex() const noexcept -> ShaderVisibleIndex
  {
    return bindless_index_;
  }

  //=== Ring allocation and SRV range ---------------------------------------//

  //! Allocate a contiguous range in elements from the ring.
  //! Returns element offset and count on success.
  /*!
   Allocates a contiguous region using ring head/tail. Placement tries the
   [tail..end) space first, then wraps to [0..head). Allocations are aligned
   to the element stride (Stride()). Callers provide sizes in element units;
   the allocator converts to bytes using the stride and aligns allocations to
   the stride boundary.

   @param elements Number of elements to allocate.
   @return Allocation with element offset and size, or std::nullopt on failure.

  ### Performance Characteristics

  - Time Complexity: O(1)
  - Memory: No extra allocations.

  @warning Allocation fails if the ring is full or contiguous space is
           insufficient even if total free space exists (no defragmentation).
  */
  struct Allocation {
    std::uint64_t element_offset { 0 };
    std::uint64_t elements { 0 };
  };

  [[nodiscard]] OXGN_RNDR_API auto Allocate(std::uint64_t elements)
    -> std::optional<Allocation>;

  //! Update the SRV range to expose only the first active_elements.
  //! Returns false if the buffer/view could not be updated. The bindless
  //! index is left unchanged; callers may decide when to recreate or
  //! refresh the view (e.g., via ReserveElements) if failures persist.
  [[nodiscard]] OXGN_RNDR_API auto SetActiveElements(
    std::uint64_t active_elements) -> bool;

  //! Update the SRV range with explicit base and count in elements.
  /*!
   Updates the SRV base and size in bytes to expose [base, base+count) in
   elements.

   @param base_element First element visible to shaders.
   @param active_elements Number of visible elements.
   @return True if the view was updated; false otherwise.
  */
  [[nodiscard]] OXGN_RNDR_API auto SetActiveRange(
    std::uint64_t base_element, std::uint64_t active_elements) -> bool;

  //=== Chunk lifecycle (client-controlled reclamation) ---------------------//
  using ChunkId = std::uint64_t;
  //! Finalize the current chunk (bytes allocated since last finalize).
  /*!
   Call after you’ve enqueued all copies that reference allocations taken
   since the last finalize. Returns a monotonically increasing id you can
   associate with your upload tickets.

   @return Chunk id if any bytes were recorded, or std::nullopt.
  */
  [[nodiscard]] OXGN_RNDR_API auto FinalizeChunk() -> std::optional<ChunkId>;
  //! Attempt to reclaim the front chunk if its id matches (FIFO only).
  /*!
   Reclaims space only when the provided id equals the front chunk id.
   Use your UploadCoordinator tickets to determine completion and call this
   when safe. Advances the ring head and frees the corresponding bytes.

   @param id Chunk id previously returned by FinalizeChunk().
   @return True if reclaimed; false if not the front or no chunks pending.
  */
  OXGN_RNDR_API auto TryReclaim(ChunkId id) -> bool;

  //! Build an upload request to copy the entire payload to the start.
  /*!
   Convenience for full-buffer copy to offset 0.

   @param bytes Source payload.
   @param debug Debug label.
   @return UploadRequest targeting this buffer.
  */
  [[nodiscard]] OXGN_RNDR_API auto BuildCopyAll(
    std::span<const std::byte> bytes, std::string_view debug) const noexcept
    -> oxygen::engine::upload::UploadRequest;

  //! Build an upload request to copy bytes at element offset.
  /*!
   @param element_offset Destination element offset.
   @param bytes Source payload.
   @param debug Debug label.
   @return UploadRequest targeting this buffer.
  */
  [[nodiscard]] OXGN_RNDR_API auto BuildCopyRange(std::uint64_t element_offset,
    std::span<const std::byte> bytes, std::string_view debug) const noexcept
    -> oxygen::engine::upload::UploadRequest;

  //! Build an upload request targeting a prior Allocate() result.
  /*!
   @param alloc Allocation returned by Allocate().
   @param bytes Source payload.
   @param debug Debug label.
   @return UploadRequest targeting this buffer.
  */
  [[nodiscard]] OXGN_RNDR_API auto BuildCopyFor(const Allocation& alloc,
    std::span<const std::byte> bytes, std::string_view debug) const noexcept
    -> oxygen::engine::upload::UploadRequest;

  // Telemetry
  [[nodiscard]] auto CapacityBytes() const noexcept -> std::uint64_t
  {
    return buffer_ ? buffer_->GetSize() : 0ULL;
  }
  [[nodiscard]] auto UsedBytes() const noexcept -> std::uint64_t
  {
    return used_bytes_;
  }
  [[nodiscard]] auto FreeBytes() const noexcept -> std::uint64_t
  {
    const auto cap = CapacityBytes();
    return cap >= used_bytes_ ? (cap - used_bytes_) : 0ULL;
  }
  [[nodiscard]] auto IsFull() const noexcept -> bool
  {
    const auto cap = CapacityBytes();
    return cap > 0 && used_bytes_ >= cap;
  }

private:
  // Minimum elements to allocate on first growth to avoid tiny heaps
  inline static constexpr std::uint32_t kMinElements = 1024;

  oxygen::Graphics& gfx_;
  std::uint32_t element_stride_ { 0 };
  std::uint64_t capacity_elements_ { 0 };
  std::shared_ptr<oxygen::graphics::Buffer> buffer_;
  ShaderVisibleIndex bindless_index_ { kInvalidShaderVisibleIndex };
  std::string debug_label_;

  // Ring state (bytes)
  std::uint64_t head_bytes_ { 0 };
  std::uint64_t tail_bytes_ { 0 };
  std::uint64_t used_bytes_ { 0 };
  std::uint64_t curr_frame_bytes_ { 0 };
  struct FrameTail {
    ChunkId id;
    std::uint64_t tail;
    std::uint64_t size;
  };
  std::deque<FrameTail> completed_frames_;
  ChunkId next_chunk_id_ { 1 };

  // Telemetry counters
  std::uint64_t max_used_bytes_ { 0 };
  std::uint32_t allocations_this_frame_ { 0 };
  std::uint32_t failed_allocations_ { 0 };
  std::uint32_t total_reallocations_ { 0 };
};

} // namespace oxygen::renderer::upload
