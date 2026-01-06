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
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine::upload {

//! Simple ring/linear staging allocator over a single mapped upload buffer.
/*! Allocations are sub-ranges within one CPU-visible buffer. The allocator
    linearly bumps an offset for each Allocate() and grows the buffer with a
    slack factor when capacity is insufficient. On RetireCompleted(), the
    allocator resets the bump pointer so the entire buffer is reused in the next
    epoch. This avoids per-allocation fence tracking and works with the
    coordinator's retire cycle.

    Must be created via UploadCoordinator::CreateRingBufferStaging.

    Notes
    - The buffer is persistently mapped; unmapping happens only when resizing or
      explicitly on RetireCompleted() if desired in future revisions.
    - Offsets are aligned to the configured boundary (e.g. 16 or 256 bytes).
    - **Structured Buffers**: If using this provider for Structured Buffers (via
      TransientStructuredBuffer), ensure that the structure stride is a multiple
      of the alignment. Otherwise, the SRV offset (which must be stride-aligned)
      may not match the byte offset returned by Allocate().
*/
class RingBufferStaging final : public StagingProvider {
  friend class UploadCoordinator;

public:
  explicit RingBufferStaging(UploaderTag tag, observer_ptr<Graphics> gfx,
    frame::SlotCount partitions, std::uint32_t alignment, float slack)
    : StagingProvider(tag)
    , gfx_(gfx)
    , partitions_count_(partitions)
    , alignment_(alignment)
    , slack_(slack)
  {
    DCHECK_F(alignment_ > 0, "RingBufferStaging requires non-zero alignment");
    DCHECK_F((alignment_ & (alignment_ - 1)) == 0,
      "RingBufferStaging alignment must be power-of-two, got %u", alignment_);
    heads_.assign(partitions_count_.get(), 0ULL);
  }
  ~RingBufferStaging() override;

  auto Allocate(SizeBytes size, std::string_view debug_name)
    -> std::expected<Allocation, UploadError> override;

  auto RetireCompleted(UploaderTag, FenceValue completed) -> void override;

  // Notify of frame slot change without RTTI
  OXGN_RNDR_API auto OnFrameStart(UploaderTag, frame::Slot slot)
    -> void override;

  // Notify of frame slot change without RTTI
  OXGN_RNDR_API auto OnFrameStart(InlineCoordinatorTag, frame::Slot slot)
    -> void override;

protected:
  auto FinalizeStats() -> void override;

private:
  auto OnFrameStartInternal(frame::Slot slot) -> void;

  auto MaybeShrinkAfterIdle(std::string_view debug_name) -> void;

  auto RecreateBuffer(std::uint64_t aligned_per_partition,
    std::string_view debug_name) -> std::expected<void, UploadError>;

  // Select active partition (frame slot) and reset its bump pointer.
  auto SetActivePartition(frame::Slot slot) noexcept -> void
  {
    if (slot >= partitions_count_) {
      return;
    }
    active_partition_ = slot;

    // Optional guard: if we are cycling back to this partition and have not
    // observed any retirement since it was last used, log a warning. We still
    // overwrite as designed; this is a diagnostic only.
    static constexpr bool kWarnOnPartitionReuseWithoutRetire = true;
    if (kWarnOnPartitionReuseWithoutRetire) {
      const auto last_seen = partition_last_seen_retire_count_.empty()
        ? 0ULL
        : partition_last_seen_retire_count_[active_partition_];
      if (last_seen == retire_count_) {
        LOG_F(WARNING,
          "RingBufferStaging: Reusing partition {} without observed retirement;"
          " overwriting staging data. head={} cap_per_partition={}",
          active_partition_, heads_.empty() ? 0ULL : heads_[active_partition_],
          capacity_per_partition_);
      }
    }

    // When we cycle back to this partition, all GPU work for it has completed
    // so we can safely reclaim the space by resetting the head
    heads_[active_partition_] = 0ULL;
  }

  auto EnsureCapacity(std::uint64_t required, std::string_view debug_name)
    -> std::expected<void, UploadError>;
  auto Map() -> std::expected<void, UploadError>;
  auto UnMap() noexcept -> void;

  observer_ptr<Graphics> gfx_;
  std::shared_ptr<graphics::Buffer> buffer_;
  std::byte* mapped_ptr_ { nullptr };
  // Partitioning
  frame::SlotCount partitions_count_ { 1 };
  frame::Slot active_partition_ { 0 };
  std::uint64_t capacity_per_partition_ { 0 }; // bytes per partition
  std::vector<std::uint64_t> heads_ {}; // bump per partition
  std::uint64_t capacity_ { 0 }; // total bytes
  std::uint32_t alignment_;
  float slack_ { 0.0f };

  // Retirement observation: incremented on RetireCompleted(); at Allocate()
  // we record the current value per active partition. On reuse, if no new
  // retirement was observed, we log a warning before overwriting.
  std::uint64_t retire_count_ { 0ULL };
  std::vector<std::uint64_t> partition_last_seen_retire_count_ {};
  FenceValue last_completed_fence_ { 0 };

  // Idle trimming: if no allocations happen for a while, shrink back toward the
  // initial size to reclaim CPU-visible upload memory after large bursts.
  std::uint32_t consecutive_idle_frames_ { 0U };
};

} // namespace oxygen::engine::upload
