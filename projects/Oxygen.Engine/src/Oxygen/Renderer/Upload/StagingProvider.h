//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

//! Interface for CPU-visible GPU upload memory providers.
/*!
 Abstracts allocation and recycling of CPU-visible (UPLOAD heap) memory for GPU
 resource uploads. Implementations may use different strategies (single buffer,
 ring buffer, partitioned, etc.) and mapping policies (persistently mapped,
 per-operation mapping, etc.).

 ### Core Responsibilities

 - Provide allocations of upload memory for staging data to the GPU.
 - Manage buffer growth, mapping/unmapping, and recycling as needed.
 - Support recycling/retirement of allocations after GPU usage completes (via
   fence value).
 - Optionally expose telemetry and per-frame lifecycle hooks.

 ### Usage Pattern

 - Call `Allocate()` to obtain a region of CPU-visible memory for upload.
 - Use the returned pointer to write data; pointer validity and mapping lifetime
   are implementation-defined (could be unmapped after use or per frame).
 - After GPU work completes, call `RetireCompleted()` with the completed fence
   value to allow the provider to recycle memory.
 - Call `OnFrameStart()` at the start of a new frame.

 ### Implementation Notes

 - Not all providers guarantee persistent mapping; pointer validity is only for
   the intended upload period.
 - Buffer growth, mapping, and recycling policies are implementation-specific.
 - Telemetry and stats are optional and may be used for diagnostics.
*/
class StagingProvider {
public:
  //! Represents a valid allocation of upload memory.
  /*!
   An Allocation is returned only when the operation was successful. This
   effectively eliminate the need to do any validation over a returned
   Allocation object.

   @see UploadCoordinator::Submit(), UploadTracker::IsComplete()
  */
  //! Represents an allocation of upload memory.
  class Allocation final {
  public:
    // Non-default constructible - all tickets must be explicitly created with
    // valid values
    Allocation() = delete;

    //! Construct a well-formed Allocation.
    OXGN_RNDR_API Allocation(std::shared_ptr<graphics::Buffer> buf,
      OffsetBytes offset, SizeBytes size, std::byte* ptr) noexcept;

    // Rule of 5: explicit copy/move semantics
    OXYGEN_MAKE_NON_COPYABLE(Allocation)
    OXYGEN_DEFAULT_MOVABLE(Allocation)

    ~Allocation() = default;

    //! Access the backing buffer.
    auto Buffer() const noexcept -> auto& { return *buffer; }

    //! Access the byte offset within the buffer.
    auto Offset() const noexcept { return offset; }

    //! Access the size in bytes of the allocation.
    auto Size() const noexcept { return size; }

    //! Access the mapped pointer for CPU writes.
    auto Ptr() const noexcept { return ptr; }

    //! Update the fence value for this allocation.
    auto SetFenceValue(FenceValue fence_v) noexcept -> void { fence = fence_v; }

    //! Access the fence value associated with this allocation.
    auto FenceValue() const noexcept { return fence; }

  private:
    std::shared_ptr<graphics::Buffer> buffer; //! Underlying upload buffer
    OffsetBytes offset; //! Byte offset within the buffer
    SizeBytes size; //! Size of the allocation in bytes
    std::byte* ptr; //! Pointer to (buffer mapped address + offset)

    graphics::FenceValue fence; //! Associated fence value for completion
  };

  //! Statistics for telemetry and diagnostics.
  struct StagingStats {
    // Core allocation metrics
    std::uint64_t total_allocations { 0 };
    std::uint64_t total_bytes_allocated { 0 };
    std::uint32_t allocations_this_frame { 0 };
    std::uint32_t avg_allocation_size { 0 }; // Moving average in bytes

    // Buffer management
    std::uint32_t buffer_growth_count { 0 }; // How many times buffer grew
    std::uint64_t current_buffer_size { 0 };

    // Map/unmap tracking
    std::uint32_t map_calls { 0 };
    std::uint32_t unmap_calls { 0 };

    // Implementation-specific data (partition info, etc.)
    std::string implementation_info;
  };

  StagingProvider(UploaderTag) { }

  OXYGEN_MAKE_NON_COPYABLE(StagingProvider)
  OXYGEN_DEFAULT_MOVABLE(StagingProvider)

  OXGN_RNDR_API virtual ~StagingProvider();

  //! Allocate a persistently mapped upload region of at least 'size' bytes.
  virtual auto Allocate(SizeBytes size, std::string_view debug_name)
    -> std::expected<Allocation, UploadError>
    = 0;

  //! Retire allocations whose GPU fence has completed (for recycling).
  virtual auto RetireCompleted(UploaderTag, FenceValue completed) -> void = 0;

  //! Optional lifecycle notification for frame slot changes. Default no-op
  //! allows non-partitioned providers to ignore it.
  virtual auto OnFrameStart(UploaderTag, frame::Slot) -> void { }
  virtual auto OnFrameStart(InlineCoordinatorTag, frame::Slot) -> void { }

  //! Optional telemetry; providers may override to expose stats.
  [[nodiscard]] auto GetStats() const -> const StagingStats& { return stats_; }

protected:
  virtual auto FinalizeStats() -> void { };
  [[nodiscard]] auto Stats() -> StagingStats& { return stats_; }

private:
  StagingStats stats_ {};
};

} // namespace oxygen::engine::upload
