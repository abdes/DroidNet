//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Renderer/Test/Upload/UploadCoordinatorTest.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

namespace oxygen::engine::upload::testing {

//! Fixture for tests that specifically exercise RingBufferStaging.
class RingBufferStagingFixture : public UploadCoordinatorTest {
protected:
  auto MakeRingBuffer(frame::SlotCount partitions, std::uint32_t alignment,
    float slack = 0.5f) -> std::shared_ptr<StagingProvider>
  {
    auto p = Uploader().CreateRingBufferStaging(partitions, alignment, slack);
    SetStagingProvider(p);
    return p;
  }

  auto CaptureStats() const
    -> oxygen::engine::upload::StagingProvider::StagingStats
  {
    return Staging().GetStats();
  }

  static auto ComputeStatsDelta(
    const oxygen::engine::upload::StagingProvider::StagingStats& before,
    const oxygen::engine::upload::StagingProvider::StagingStats& after)
    -> oxygen::engine::upload::StagingProvider::StagingStats
  {
    using Stats = oxygen::engine::upload::StagingProvider::StagingStats;
    Stats d {};
    d.total_allocations = after.total_allocations - before.total_allocations;
    d.total_bytes_allocated
      = after.total_bytes_allocated - before.total_bytes_allocated;
    d.allocations_this_frame
      = after.allocations_this_frame - before.allocations_this_frame;
    d.avg_allocation_size
      = after.avg_allocation_size; // not a delta-friendly field
    d.buffer_growth_count
      = after.buffer_growth_count - before.buffer_growth_count;
    d.current_buffer_size
      = after.current_buffer_size - before.current_buffer_size;
    d.map_calls = after.map_calls - before.map_calls;
    d.unmap_calls = after.unmap_calls - before.unmap_calls;
    d.implementation_info = after.implementation_info;
    return d;
  }

  // For RingBufferStaging tests we want to notify providers via the
  // InlineTransfersCoordinator path (InlineCoordinatorTag) because the
  // provider implements OnFrameStart(InlineCoordinatorTag,...). This hides
  // the UploadCoordinatorTest::SimulateFrameStart which routes through the
  // UploadCoordinator (UploaderTag path).
  auto SimulateFrameStart(frame::Slot slot) -> void
  {
    auto tag = oxygen::engine::upload::internal::InlineCoordinatorTagFactory::Get();
    Staging().OnFrameStart(tag, slot);
  }
};

} // namespace oxygen::engine::upload::testing
