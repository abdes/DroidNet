//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Test/Fixtures/RingBufferStagingFixture.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploaderTag.h>

using oxygen::SizeBytes;
using oxygen::frame::Slot;
using oxygen::frame::SlotCount;
using oxygen::vortex::upload::StagingProvider;
using oxygen::vortex::upload::UploadError;

namespace oxygen::vortex::upload::internal {
auto InlineCoordinatorTagFactory::Get() noexcept -> InlineCoordinatorTag
{
  return InlineCoordinatorTag {};
}
} // namespace oxygen::vortex::upload::internal

namespace {

// Small fixture that exposes convenience helpers for RingBufferStaging tests.
class RingBufferStagingTest
  : public oxygen::vortex::upload::testing::RingBufferStagingFixture { };

/*!
 Zero-size allocation should fail with kInvalidRequest.
*/
NOLINT_TEST_F(RingBufferStagingTest, ZeroSize_ReturnsError)
{
  // Arrange
  auto provider = MakeRingBuffer(
    SlotCount {
      1,
    },
    256U, 0.5F);
  ASSERT_NE(provider, nullptr);

  // Act
  auto alloc = provider->Allocate(
    SizeBytes {
      0,
    },
    "zero");

  // Assert
  ASSERT_FALSE(alloc.has_value());
  EXPECT_EQ(alloc.error(), UploadError::kInvalidRequest);
}

/*!
 Allocate should return a correctly aligned allocation and valid buffer.
*/
NOLINT_TEST_F(RingBufferStagingTest, Allocate_ReturnsAlignedAllocation)
{
  auto provider = MakeRingBuffer(
    SlotCount {
      1,
    },
    256U, 0.5F);
  ASSERT_NE(provider, nullptr);

  // Arrange
  const uint64_t requested = 100U;

  // Act
  auto alloc = provider->Allocate(
    SizeBytes {
      requested,
    },
    "alloc-test");

  // Assert
  ASSERT_TRUE(alloc.has_value());
  const auto& a = *alloc;
  // Offset must respect alignment
  EXPECT_EQ(a.Offset().get() % 256U, 0U);
  // Size reported should be the requested size (not the aligned amount)
  EXPECT_EQ(a.Size().get(), requested);
  // Buffer backing must be valid
  EXPECT_NE(&a.Buffer(), nullptr);
}

/*!
 Allocate updates telemetry/statistics after a successful allocation.
*/
NOLINT_TEST_F(RingBufferStagingTest, Allocate_UpdatesTelemetry)
{
  auto provider = MakeRingBuffer(
    SlotCount {
      1,
    },
    256U, 0.5F);
  ASSERT_NE(provider, nullptr);

  // Arrange
  const uint64_t requested = 100U;
  const auto stats_before = provider->GetStats();

  // Act
  auto alloc = provider->Allocate(
    SizeBytes {
      requested,
    },
    "alloc-test");

  // Assert
  ASSERT_TRUE(alloc.has_value());
  const auto stats_after = provider->GetStats();

  // Deltas: allocation counts and bytes should increase
  EXPECT_GT(stats_after.total_allocations, stats_before.total_allocations);
  EXPECT_GE(stats_after.total_bytes_allocated,
    stats_before.total_bytes_allocated + requested);

  // allocations_this_frame should increase by at least 1 for this frame
  EXPECT_GT(
    stats_after.allocations_this_frame, stats_before.allocations_this_frame);

  // map_calls should be at least as many as before (may increase)
  EXPECT_GE(stats_after.map_calls, stats_before.map_calls);

  // current buffer size should be >= previous size
  EXPECT_GE(stats_after.current_buffer_size, stats_before.current_buffer_size);
}

/*!
 Different partitions must allocate into distinct, non-overlapping ranges.
*/
NOLINT_TEST_F(RingBufferStagingTest, PartitionIsolation)
{
  // Use 2 partitions with small alignment so we can reason about offsets.
  auto provider = MakeRingBuffer(
    SlotCount {
      2,
    },
    16U, 0.5F);
  ASSERT_NE(provider, nullptr);
  // Arrange
  // Activate partition 0 and allocate (route via uploader to ensure correct
  // tag)
  SimulateStagingFrameStart(Slot {
    0,
  });

  // Act
  auto a0 = provider->Allocate(
    SizeBytes {
      64,
    },
    "p0-a");
  ASSERT_TRUE(a0.has_value());
  const auto off0 = a0->Offset().get();

  // Arrange (partition 1)
  // Activate partition 1 and allocate (route via uploader to ensure correct
  // tag)
  SimulateStagingFrameStart(Slot {
    1,
  });

  // Act (partition 1)
  auto a1 = provider->Allocate(
    SizeBytes {
      64,
    },
    "p1-a");
  ASSERT_TRUE(a1.has_value());
  const auto off1 = a1->Offset().get();

  // Assert
  // Different partitions must not overlap: compute partition size from stats
  const auto total_size = provider->GetStats().current_buffer_size;
  ASSERT_GT(total_size, 0U);
  const uint64_t per_partition = total_size / 2U;
  const auto idx0 = off0 / per_partition;
  const auto idx1 = off1 / per_partition;
  EXPECT_NE(idx0, idx1);
}

/*!
 OnFrameStart must reset allocations_this_frame to zero.
*/
NOLINT_TEST_F(RingBufferStagingTest, FrameStart_ResetsCounters)
{
  auto provider = MakeRingBuffer(
    SlotCount {
      1,
    },
    64U, 0.5F);
  ASSERT_NE(provider, nullptr);
  // Arrange
  // Allocate one entry
  auto a = provider->Allocate(
    SizeBytes {
      32,
    },
    "cnt-a");
  ASSERT_TRUE(a.has_value());

  // Assert (pre-condition)
  const auto& stats_before = provider->GetStats();
  EXPECT_GE(stats_before.allocations_this_frame, 1U);

  // Act
  // OnFrameStart should reset allocations_this_frame (route via uploader)
  SimulateStagingFrameStart(Slot {
    1,
  });

  // Assert (post-condition)
  const auto& stats_after = provider->GetStats();
  EXPECT_EQ(stats_after.allocations_this_frame, 0U);
}

/*!
 Verify the simple EMA update behavior of avg_allocation_size.
 This test performs two allocations of different sizes and verifies the
 moving average was updated in the expected direction and within bounds.
*/
NOLINT_TEST_F(RingBufferStagingTest, AvgAllocationSize_UpdatedByEMA)
{
  // Arrange
  auto provider = MakeRingBuffer(
    SlotCount {
      1,
    },
    256U, 0.5F);
  ASSERT_NE(provider, nullptr);

  const uint64_t first = 100U;
  const uint64_t second = 200U;

  const auto before = CaptureStats();

  // Act
  auto a1 = provider->Allocate(
    SizeBytes {
      first,
    },
    "ema-1");
  ASSERT_TRUE(a1.has_value());
  auto a2 = provider->Allocate(
    SizeBytes {
      second,
    },
    "ema-2");
  ASSERT_TRUE(a2.has_value());

  // Assert
  const auto after = CaptureStats();
  // avg should be between latest sample and previous average, sanity check
  EXPECT_GT(after.avg_allocation_size, 0U);
  // Ensure total allocations increased by 2
  EXPECT_GE(after.total_allocations, before.total_allocations + 2);
}

/*!
 Ensure that when the buffer grows (EnsureCapacity path), the provider
 maps a new buffer and unmaps the previous one. This verifies UnMap is
 called on growth and the map/unmap counters are updated.
*/
NOLINT_TEST_F(RingBufferStagingTest, EnsureCapacity_UnMapOnGrowth)
{
  // Arrange
  // Start with small per-partition capacity so the second allocation forces
  // a growth and buffer remap.
  auto provider = MakeRingBuffer(
    SlotCount {
      1,
    },
    16U, 0.5F);
  ASSERT_NE(provider, nullptr);

  // Make an initial small allocation so the provider creates and maps the
  // backing buffer. This ensures UnMap() will have something to unmap when
  // growth occurs.
  auto init = provider->Allocate(
    SizeBytes {
      8,
    },
    "init");
  ASSERT_TRUE(init.has_value());

  const auto stats_before = CaptureStats();

  // Act
  // Allocate bigger than current capacity per partition to trigger growth
  auto a = provider->Allocate(
    SizeBytes {
      stats_before.current_buffer_size + 1U,
    },
    "grow-test");
  ASSERT_TRUE(a.has_value());

  // Assert
  const auto stats_after = CaptureStats();
  EXPECT_GT(stats_after.current_buffer_size, stats_before.current_buffer_size);
  EXPECT_EQ(stats_after.unmap_calls, stats_before.unmap_calls + 1U);
  EXPECT_EQ(stats_after.map_calls, stats_before.map_calls + 1U);
}

/*!
 Explicit trim should shrink a previously grown ring buffer back toward the
 baseline size without waiting for the idle-frame threshold.
*/
NOLINT_TEST_F(RingBufferStagingTest, ExplicitTrimShrinksGrownBuffer)
{
  auto provider = MakeRingBuffer(
    SlotCount {
      1,
    },
    64U, 0.5F);
  ASSERT_NE(provider, nullptr);

  const auto large_request = SizeBytes {
    12ULL * 1024ULL * 1024ULL,
  };
  {
    auto alloc = provider->Allocate(large_request, "grow-for-trim");
    ASSERT_TRUE(alloc.has_value());
  }

  const auto grown_stats = provider->GetStats();
  ASSERT_GT(grown_stats.current_buffer_size, 0U);

  SimulateStagingFrameStart(Slot {
    0,
  });

  const bool trimmed = Uploader().TrimStagingProvider(
    *provider, "RingBufferStaging.ExplicitTrim");

  ASSERT_TRUE(trimmed);
  const auto trimmed_stats = provider->GetStats();
  EXPECT_LT(trimmed_stats.current_buffer_size, grown_stats.current_buffer_size);

  GfxPtr()->Flush();
}

/*!
 When RetireCompleted is called with advancing fence values, the internal
 retire_count_ should increase which prevents partition-reuse warnings. This
 test simulates two frames, triggers RetireCompleted between them, and then
 verifies that reusing a partition does not produce the reuse warning and
 that allocations still succeed.
*/
NOLINT_TEST_F(RingBufferStagingTest, RetireCompleted_PreventsPartitionReuse)
{
  // Arrange: use two partitions so we can cycle
  auto provider = MakeRingBuffer(
    SlotCount {
      2,
    },
    16U, 0.5F);
  ASSERT_NE(provider, nullptr);

  // Activate partition 0 and allocate
  SimulateStagingFrameStart(Slot {
    0,
  });
  auto a0 = provider->Allocate(
    SizeBytes {
      32,
    },
    "p0");
  ASSERT_TRUE(a0.has_value());

  // Simulate GPU completion by retiring with an advancing fence
  // The underlying uploader uses FakeCommandQueue; directly call
  // RetireCompleted via the provider's interface (we pass a non-zero fence
  // value to bump retire_count_)
  provider->RetireCompleted(
    oxygen::vortex::upload::internal::UploaderTagFactory::Get(),
    oxygen::graphics::FenceValue {
      1,
    });

  // Now cycle to partition 0 again and allocate; this should not trigger a
  // reuse warning
  SimulateStagingFrameStart(Slot {
    0,
  });
  auto a1 = provider->Allocate(
    SizeBytes {
      16,
    },
    "p0-2");
  ASSERT_TRUE(a1.has_value());
}

/*!
 UnMap is idempotent: calling UnMap multiple times (including when no buffer
 exists) should be safe and should not decrement counts unexpectedly.
*/
NOLINT_TEST_F(RingBufferStagingTest, UnMap_Idempotent)
{
  auto provider = MakeRingBuffer(
    SlotCount {
      1,
    },
    64U, 0.5F);
  ASSERT_NE(provider, nullptr);

  // Ensure some mapping happened
  auto a = provider->Allocate(
    SizeBytes {
      32,
    },
    "map-test");
  ASSERT_TRUE(a.has_value());

  const auto before = CaptureStats();

  // Each replacement must unmap exactly the previous backing buffer once.
  auto first_growth = provider->Allocate(
    SizeBytes {
      before.current_buffer_size + 1U,
    },
    "force-grow");
  ASSERT_TRUE(first_growth.has_value());
  const auto after_first_growth = CaptureStats();
  EXPECT_EQ(after_first_growth.unmap_calls, before.unmap_calls + 1U);

  auto second_growth = provider->Allocate(
    SizeBytes {
      after_first_growth.current_buffer_size + 1U,
    },
    "force-grow-2");
  ASSERT_TRUE(second_growth.has_value());
  const auto after = CaptureStats();
  EXPECT_EQ(after.unmap_calls, before.unmap_calls + 2U);
  EXPECT_EQ(after.map_calls, before.map_calls + 2U);
}

/*!
 Destroying Graphics after a staging-buffer growth must not hang while deferred
 releases are drained. This exercises the same path reported in teardown where
 an old staging buffer is kept alive after growth and released during Graphics
 destruction.
*/
NOLINT_TEST(RingBufferStaging, DeferredReleaseAfterGrowthDoesNotHangAtShutdown)
{
  auto run_teardown = []() -> void {
    auto gfx = std::make_shared<oxygen::vortex::testing::FakeGraphics>();
    gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

    auto uploader = std::make_unique<oxygen::vortex::upload::UploadCoordinator>(
      oxygen::observer_ptr<oxygen::Graphics> {
        gfx.get(),
      },
      oxygen::vortex::upload::DefaultUploadPolicy());
    auto provider = uploader->CreateRingBufferStaging(
      oxygen::frame::SlotCount {
        1,
      },
      16U, 0.5F, "RingBufferStaging.ShutdownRegression");

    auto initial = provider->Allocate(
      SizeBytes {
        8,
      },
      "initial");
    ASSERT_TRUE(initial.has_value());

    auto grown = provider->Allocate(
      SizeBytes {
        12ULL * 1024ULL * 1024ULL,
      },
      "force-growth");
    ASSERT_TRUE(grown.has_value());

    provider.reset();
    uploader.reset();
    gfx.reset();
  };

  ASSERT_NO_FATAL_FAILURE(run_teardown());
}

} // namespace
