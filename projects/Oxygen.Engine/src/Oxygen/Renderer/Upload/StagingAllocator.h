//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine::upload {

// Simple initial allocator: one upload buffer per allocation, persistently
// mapped. We'll evolve to arena ring later.
class StagingAllocator {
public:
  explicit StagingAllocator(std::shared_ptr<oxygen::Graphics> gfx)
    : gfx_(std::move(gfx))
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(StagingAllocator)
  OXYGEN_DEFAULT_MOVABLE(StagingAllocator)

  struct Allocation {
    std::shared_ptr<oxygen::graphics::Buffer> buffer;
    uint64_t offset { 0 };
    uint64_t size { 0 };
    std::byte* ptr { nullptr }; // persistent mapped pointer
    FenceValue fence { oxygen::graphics::fence::kInvalidValue };

    // Ensure buffers are not left mapped at destruction time.
    ~Allocation()
    {
      if (buffer && buffer->IsMapped()) {
        buffer->UnMap();
      }
    }
  };

  // Allocate a persistently mapped upload buffer of at least 'size' bytes.
  OXGN_RNDR_NDAPI auto Allocate(Bytes size, std::string_view debug_name)
    -> Allocation;

  // Mark allocations completed for a given fence so we can recycle later.
  OXGN_RNDR_API auto RetireCompleted(FenceValue completed) -> void;

private:
  std::shared_ptr<oxygen::Graphics> gfx_;
};

} // namespace oxygen::engine::upload
