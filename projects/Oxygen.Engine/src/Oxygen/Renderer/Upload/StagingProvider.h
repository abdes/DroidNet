//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

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
  enum class MapPolicy : uint8_t { kPinned, kPerOp };

  //! Represents an allocation of upload memory.
  struct Allocation {
    std::shared_ptr<graphics::Buffer> buffer;
    std::uint64_t offset { 0 };
    std::uint64_t size { 0 };
    std::byte* ptr { nullptr }; // pointer to (buffer + offset)
    FenceValue fence { graphics::fence::kInvalidValue };

    ~Allocation()
    {
      if (buffer && buffer->IsMapped()) {
        buffer->UnMap();
      }
    }
  };

  StagingProvider(UploaderTag) { }

  OXYGEN_MAKE_NON_COPYABLE(StagingProvider)
  OXYGEN_DEFAULT_MOVABLE(StagingProvider)

  virtual ~StagingProvider() = default;

  //! Allocate a persistently mapped upload region of at least 'size' bytes.
  virtual auto Allocate(Bytes size, std::string_view debug_name) -> Allocation
    = 0;

  //! Retire allocations whose GPU fence has completed (for recycling).
  virtual auto RetireCompleted(FenceValue completed) -> void = 0;

  //! Optional lifecycle notification for frame slot changes. Default no-op
  //! allows non-partitioned providers to ignore it.
  virtual auto OnFrameStart(frame::Slot /*slot*/) -> void { }

  //! Statistics for telemetry and diagnostics.
  struct StagingStats {
    std::uint64_t allocations { 0 };
    std::uint64_t bytes_requested { 0 };
    std::uint64_t ensure_capacity_calls { 0 };
    std::uint64_t buffers_created { 0 };
    std::uint64_t map_calls { 0 };
    std::uint64_t unmap_calls { 0 };
    std::uint64_t peak_buffer_size { 0 };
    std::uint64_t current_buffer_size { 0 };
  };

  //! Optional telemetry; providers may override to expose stats.
  [[nodiscard]] virtual auto GetStats() const -> StagingStats { return {}; }
};

} // namespace oxygen::engine::upload
