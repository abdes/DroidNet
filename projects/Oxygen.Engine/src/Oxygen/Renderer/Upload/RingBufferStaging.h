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

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>

namespace oxygen::renderer::upload {

//! Simple ring/linear staging allocator over a single mapped upload buffer.
/*! Allocations are sub-ranges within one CPU-visible buffer. The allocator
         linearly bumps an offset for each Allocate() and grows the buffer with
   a slack factor when capacity is insufficient. On RetireCompleted(), the
         allocator resets the bump pointer so the entire buffer is reused in the
         next epoch. This avoids per-allocation fence tracking and works with
   the coordinator's retire cycle.

         Notes
         - The buffer is persistently mapped; unmapping happens only when
   resizing or explicitly on RetireCompleted() if desired in future revisions.
         - Offsets are aligned to a conservative boundary (default 256 bytes) to
   be safe across backends for CopyBuffer operations.
*/
class RingBufferStaging final : public oxygen::engine::upload::StagingProvider {
public:
  explicit RingBufferStaging(std::shared_ptr<oxygen::Graphics> gfx,
    oxygen::frame::SlotCount partitions, std::uint32_t alignment,
    float slack = 0.5f)
    : gfx_(std::move(gfx))
    , partitions_count_(partitions)
    , alignment_(alignment)
    , slack_(slack)
  {
    DCHECK_F(alignment_ > 0, "RingBufferStaging requires non-zero alignment");
    DCHECK_F((alignment_ & (alignment_ - 1)) == 0,
      "RingBufferStaging alignment must be power-of-two, got %u", alignment_);
    heads_.assign(partitions_count_.get(), 0ULL);
  }

  ~RingBufferStaging() override = default;

  auto Allocate(oxygen::engine::upload::Bytes size, std::string_view debug_name)
    -> Allocation override;

  auto RetireCompleted(oxygen::engine::upload::FenceValue /*completed*/)
    -> void override;

  // Notify of frame slot change without RTTI
  auto OnFrameStart(oxygen::frame::Slot slot) -> void override
  {
    SetActivePartition(slot);
  }

  auto GetStats() const -> StagingStats override { return stats_; }

  // Select active partition (frame slot) and reset its bump pointer.
  auto SetActivePartition(oxygen::frame::Slot slot) noexcept -> void
  {
    if (slot >= partitions_count_) {
      return;
    }
    active_partition_ = slot;
    // Reset this partition for the new frame epoch
    heads_[active_partition_] = 0ULL;
  }

private:
  void EnsureCapacity_(std::uint64_t required, std::string_view debug_name);
  static constexpr std::uint64_t AlignUp_(std::uint64_t v, std::uint64_t a)
  {
    return (v + (a - 1)) & ~(a - 1);
  }

  std::shared_ptr<oxygen::Graphics> gfx_;
  std::shared_ptr<oxygen::graphics::Buffer> buffer_;
  std::byte* mapped_ptr_ { nullptr };
  // Partitioning
  oxygen::frame::SlotCount partitions_count_ { 1 };
  oxygen::frame::Slot active_partition_ { 0 };
  std::uint64_t capacity_per_partition_ { 0 }; // bytes per partition
  std::vector<std::uint64_t> heads_ {}; // bump per partition
  std::uint64_t capacity_ { 0 }; // total bytes
  std::uint32_t alignment_ { 256u };
  float slack_ { 0.5f };
  StagingStats stats_ {};
};

} // namespace oxygen::renderer::upload
