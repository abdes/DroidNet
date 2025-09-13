//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Upload/Types.h>

namespace oxygen::engine::upload {

//=== StagingProvider ------------------------------------------------------//
//! Minimal contract for providing CPU-visible staging allocations.
/*! Implementations may sub-allocate from a larger arena (ring/linear) or
    allocate one buffer per request. Returned memory must be CPU-visible and
    persistently mapped for the lifetime of the Allocation object.
*/
class StagingProvider {
public:
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
  struct Allocation {
    std::shared_ptr<oxygen::graphics::Buffer> buffer;
    std::uint64_t offset { 0 };
    std::uint64_t size { 0 };
    std::byte* ptr { nullptr }; // pointer to (buffer + offset)
    FenceValue fence { oxygen::graphics::fence::kInvalidValue };

    ~Allocation()
    {
      if (buffer && buffer->IsMapped()) {
        buffer->UnMap();
      }
    }
  };

  virtual ~StagingProvider() = default;

  //! Allocate a persistently mapped upload region of at least 'size' bytes.
  virtual auto Allocate(Bytes size, std::string_view debug_name) -> Allocation
    = 0;

  //! Retire allocations whose GPU fence has completed (for recycling).
  virtual auto RetireCompleted(FenceValue completed) -> void = 0;

  //! Optional telemetry; providers may override to expose stats.
  virtual auto GetStats() const -> StagingStats { return {}; }

  //! Optional lifecycle notification for frame slot changes.
  //! Default no-op allows non-partitioned providers to ignore it.
  virtual auto OnFrameStart(oxygen::frame::Slot /*slot*/) -> void { }
};

} // namespace oxygen::engine::upload
