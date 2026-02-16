//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>
#include <span>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Nexus/Test/NexusMocks.h>
#include <Oxygen/Nexus/TimelineGatedSlotReuse.h>
#include <Oxygen/Nexus/Types/Domain.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

using oxygen::VersionedBindlessHandle;
using oxygen::nexus::DomainKey;
using oxygen::nexus::testing::AllocateBackend;
using oxygen::nexus::testing::FakeCommandQueue;
using oxygen::nexus::testing::FreeBackend;

namespace b = oxygen::bindless;

namespace {

// File-level group header for TimelineGatedSlotReuse tests
// Tests follow the AAA pattern and use fixtures for shared setup.

// Common matchers used by tests
using testing::SizeIs;
using testing::UnorderedElementsAreArray;

// Common constants to avoid magic numbers
constexpr uint32_t kExpectedFreedCount1 = 1U;
constexpr uint32_t kExpectedFreedCount0 = 0U;

//! Test fixture providing common setup for TimelineGatedSlotReuse tests.
class TimelineGatedSlotReuseTest : public testing::Test {
protected:
  auto SetUp() -> void override
  {
    namespace g = oxygen::graphics;
    strategy_ = std::make_unique<oxygen::nexus::TimelineGatedSlotReuse>(
      [this](DomainKey d) -> b::HeapIndex { return alloc_(d); },
      [this](DomainKey d, b::HeapIndex h) { free_(d, h); });
    domain_ = DomainKey {
      .view_type = g::ResourceViewType::kTexture_SRV,
      .visibility = g::DescriptorVisibility::kShaderVisible,
    };
  }

  // Accessors to avoid protected member warnings if any
  auto GetStrategy() -> auto& { return *strategy_; }
  auto GetStrategy() const -> const auto& { return *strategy_; }
  auto GetFreeBackend() -> auto& { return free_; }
  auto GetFreeBackend() const -> const auto& { return free_; }
  auto GetAllocBackend() -> auto& { return alloc_; }
  auto GetDomain() const -> const auto& { return domain_; }

  // Helpers
  auto Allocate() -> VersionedBindlessHandle
  {
    return GetStrategy().Allocate(domain_);
  }

  auto Release(VersionedBindlessHandle h,
    const std::shared_ptr<oxygen::graphics::CommandQueue>& q, uint64_t fence)
    -> void
  {
    GetStrategy().Release(
      GetDomain(), h, q, oxygen::graphics::FenceValue { fence });
  }

  // Helper: assert freed indices match expected set exactly (order-insensitive)
  auto ExpectFreedExactly(std::span<const uint32_t> expected) const -> void
  {
    EXPECT_THAT(GetFreeBackend().freed, UnorderedElementsAreArray(expected));
  }

  // Helper: assert no handles in a collection are current
  template <typename Collection>
  auto ExpectNoneCurrent(const Collection& items) const -> void
  {
    for (const auto& kv : items) {
      EXPECT_FALSE(GetStrategy().IsHandleCurrent(kv.second));
    }
  }

private:
  AllocateBackend alloc_;
  FreeBackend free_;
  DomainKey domain_ {};
  std::unique_ptr<oxygen::nexus::TimelineGatedSlotReuse> strategy_;
};

//! Happy path test: allocation, release, fence advance, reclamation, generation
//! bump.
NOLINT_TEST_F(TimelineGatedSlotReuseTest,
  AllocateRelease_Process_ReclaimsAndBumpsGeneration)
{
  using oxygen::graphics::CommandQueue;
  using oxygen::graphics::FenceValue;

  // Arrange
  auto h = Allocate();
  ASSERT_TRUE(h.IsValid());
  const auto u_idx = h.ToBindlessHandle().get();

  // Create fake queue
  auto q = std::make_shared<FakeCommandQueue>();
  constexpr uint64_t kTestIndexFence = 1U;

  // Act - release and process before fence reached
  Release(h, q, kTestIndexFence);
  GetStrategy().ProcessFor(q);

  // Assert - not reclaimed yet
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(h));

  // Act - advance fence and process
  q->Signal(kTestIndexFence);
  GetStrategy().ProcessFor(q);

  // Assert - reclaimed and backend recorded free
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h));
  const std::array<uint32_t, kExpectedFreedCount1> expected { u_idx };
  ExpectFreedExactly(expected);
}

//! Allocate method: provides valid handles with unique indices.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Allocate_ReturnsValidHandle)
{
  // Arrange & Act
  auto h = Allocate();

  // Assert
  EXPECT_TRUE(h.IsValid());
  EXPECT_NE(h.ToBindlessHandle().get(), oxygen::kInvalidBindlessIndex);
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(h));
}

//! Release method: ignores duplicate releases for same handle.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_IgnoresDuplicates)
{
  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();
  constexpr uint64_t kFenceValue = 2U;

  // Act
  Release(h, q, kFenceValue);
  Release(h, q, kFenceValue); // duplicate
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert - only freed once
  const std::array<uint32_t, kExpectedFreedCount1> expected {
    h.ToBindlessHandle().get()
  };
  ExpectFreedExactly(expected);
}

//! Release method: safely ignores invalid handles.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_IgnoresInvalidHandle)
{
  // Arrange
  VersionedBindlessHandle invalid_handle {};
  ASSERT_FALSE(invalid_handle.IsValid());
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  constexpr uint64_t kFenceValue = 5U;
  Release(invalid_handle, q, kFenceValue);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert - no backend operations
  const std::array<uint32_t, kExpectedFreedCount0> none {};
  ExpectFreedExactly(none);
}

//! IsHandleCurrent must reject default invalid handles.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, IsHandleCurrent_InvalidHandleIsFalse)
{
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(VersionedBindlessHandle {}));
}

//! Releasing with null queue must not leave the slot stuck in pending state.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_NullQueue_DoesNotStickPending)
{
  using oxygen::graphics::FenceValue;
  using oxygen::graphics::CommandQueue;

  // Arrange
  auto h = Allocate();
  constexpr uint64_t kFenceValue = 3U;

  // Act - null queue release should be ignored cleanly
  GetStrategy().Release(
    GetDomain(), h, std::shared_ptr<CommandQueue> {}, FenceValue { kFenceValue });

  // Act - valid release must still work for the same handle
  auto q = std::make_shared<FakeCommandQueue>();
  Release(h, q, kFenceValue);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h));
  EXPECT_THAT(GetFreeBackend().freed, SizeIs(1U));
}

//! Stale handles must be ignored by single-item Release.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_StaleHandle_IsIgnored)
{
  // Arrange - make one handle stale
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();
  constexpr uint64_t kFirstFence = 1U;
  constexpr uint64_t kSecondFence = 2U;
  Release(h, q, kFirstFence);
  q->Signal(kFirstFence);
  GetStrategy().ProcessFor(q);
  ASSERT_FALSE(GetStrategy().IsHandleCurrent(h));
  ASSERT_THAT(GetFreeBackend().freed, SizeIs(1U));

  // Act - releasing stale handle again should do nothing
  Release(h, q, kSecondFence);
  q->Signal(kSecondFence);
  GetStrategy().ProcessFor(q);

  // Assert - no duplicate free
  EXPECT_THAT(GetFreeBackend().freed, SizeIs(1U));
}

//! Stale handles must be ignored by ReleaseBatch while valid ones are processed.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ReleaseBatch_StaleHandles_AreIgnored)
{
  using oxygen::graphics::FenceValue;

  // Arrange - create one stale handle and one valid handle
  auto stale = Allocate();
  auto current = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  constexpr uint64_t kFence1 = 4U;
  constexpr uint64_t kFence2 = 5U;

  Release(stale, q, kFence1);
  q->Signal(kFence1);
  GetStrategy().ProcessFor(q);
  ASSERT_FALSE(GetStrategy().IsHandleCurrent(stale));
  ASSERT_TRUE(GetStrategy().IsHandleCurrent(current));
  ASSERT_THAT(GetFreeBackend().freed, SizeIs(1U));

  const std::vector<std::pair<DomainKey, VersionedBindlessHandle>> items {
    { GetDomain(), stale },
    { GetDomain(), current },
  };

  // Act
  GetStrategy().ReleaseBatch(q, FenceValue { kFence2 }, items);
  q->Signal(kFence2);
  GetStrategy().ProcessFor(q);

  // Assert - only current should be newly reclaimed
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(current));
  EXPECT_THAT(GetFreeBackend().freed, SizeIs(2U));
}

//! Release method: accepts first release when called multiple times with
//! different fences.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_FirstFenceWins)
{
  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  // Act - release with fence 5, then again with fence 10
  constexpr uint64_t kFirstFence = 5U;
  constexpr uint64_t kSecondFence = 10U;
  Release(h, q, kFirstFence);
  Release(h, q, kSecondFence);
  q->Signal(kFirstFence);
  GetStrategy().ProcessFor(q);

  // Assert - reclaimed after first fence
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h));
}

//! Release method: handles zero fence value correctly.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_HandlesZeroFence)
{
  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  constexpr uint64_t kZeroFence = 0U;
  Release(h, q, kZeroFence);
  q->Signal(kZeroFence);
  GetStrategy().ProcessFor(q);

  // Assert
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h));
}

//! Release method: handles maximum fence values without overflow.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_HandlesMaxFence)
{
  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();
  constexpr uint64_t kMaxFence = std::numeric_limits<uint64_t>::max();

  // Act
  Release(h, q, kMaxFence);
  q->Signal(kMaxFence);
  GetStrategy().ProcessFor(q);

  // Assert
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h));
}

//! ReleaseBatch method: handles empty collections without error.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ReleaseBatch_HandlesEmptyCollection)
{
  using oxygen::graphics::FenceValue;

  // Arrange
  auto q = std::make_shared<FakeCommandQueue>();
  std::vector<std::pair<DomainKey, VersionedBindlessHandle>> empty_batch;

  // Act
  constexpr uint64_t kFenceValue = 10U;
  GetStrategy().ReleaseBatch(q, FenceValue { kFenceValue }, empty_batch);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert - no operations
  const std::array<uint32_t, kExpectedFreedCount0> none {};
  ExpectFreedExactly(none);
}

//! IsHandleCurrent method: returns false after handle is reclaimed.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, IsHandleCurrent_FalseAfterReclaim)
{
  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  constexpr uint64_t kFenceValue = 3U;
  Release(h, q, kFenceValue);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h));
}

//! ProcessFor method: processes only the specified queue.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ProcessFor_ProcessesSpecificQueue)
{
  // Arrange
  auto qA = std::make_shared<FakeCommandQueue>();
  auto qB = std::make_shared<FakeCommandQueue>();
  auto hA = Allocate();
  auto hB = Allocate();
  constexpr uint64_t kFenceValue = 1U;
  Release(hA, qA, kFenceValue);
  Release(hB, qB, kFenceValue);
  qA->Signal(kFenceValue);
  qB->Signal(kFenceValue);

  // Act - process only qA
  GetStrategy().ProcessFor(qA);

  // Assert - only hA reclaimed
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(hA));
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(hB));
}

//! Process method: processes all queues.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Process_ProcessesAllQueues)
{
  // Arrange
  auto qA = std::make_shared<FakeCommandQueue>();
  auto qB = std::make_shared<FakeCommandQueue>();
  auto hA = Allocate();
  auto hB = Allocate();
  constexpr uint64_t kFenceValue = 1U;
  Release(hA, qA, kFenceValue);
  Release(hB, qB, kFenceValue);
  qA->Signal(kFenceValue);
  qB->Signal(kFenceValue);

  // Act - global process
  GetStrategy().Process();

  // Assert - both reclaimed
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(hA));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(hB));
}

//! Generation isolation: old handles become invalid when slot is reused.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, GenerationIsolation_OldHandleInvalid)
{
  // Arrange - allocate and reclaim handle
  auto first_handle = Allocate();
  const auto u_index = first_handle.ToBindlessHandle().get();
  auto q = std::make_shared<FakeCommandQueue>();
  constexpr uint64_t kFenceValue = 1U;
  Release(first_handle, q, kFenceValue);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Force reuse of same slot
  GetAllocBackend().next.store(u_index);
  auto second_handle = Allocate();

  // Assert - generation isolation maintained
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(first_handle));
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(second_handle));
  EXPECT_EQ(first_handle.ToBindlessHandle().get(),
    second_handle.ToBindlessHandle().get());
  EXPECT_NE(first_handle.GenerationValue().get(),
    second_handle.GenerationValue().get());
}

//! Handle uniqueness: allocated handles have unique indices within domain.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, HandleUniqueness_UniqueIndices)
{
  // Arrange & Act - allocate multiple handles
  constexpr int kNumHandles = 10;
  std::vector<uint32_t> indices;
  indices.reserve(kNumHandles);
  for (int i = 0; i < kNumHandles; ++i) {
    auto h = Allocate();
    ASSERT_TRUE(h.IsValid());
    indices.push_back(h.ToBindlessHandle().get());
  }

  // Assert - all indices unique
  std::sort(indices.begin(), indices.end());
  const auto unique_end = std::unique(indices.begin(), indices.end());
  EXPECT_EQ(
    std::distance(indices.begin(), unique_end), static_cast<long>(kNumHandles));
}

//! Timeline ordering: fences processed in value order regardless of release
//! order.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, TimelineOrdering_FenceValueOrder)
{
  // Arrange - release with fence values 3, 1, 2
  auto q = std::make_shared<FakeCommandQueue>();
  auto h1 = Allocate();
  auto h2 = Allocate();
  auto h3 = Allocate();
  constexpr uint64_t kFence1 = 1U;
  constexpr uint64_t kFence2 = 2U;
  constexpr uint64_t kFence3 = 3U;
  Release(h1, q, kFence3);
  Release(h2, q, kFence1);
  Release(h3, q, kFence2);

  // Act & Assert - process in fence order
  q->Signal(kFence1);
  GetStrategy().ProcessFor(q);
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(h1));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h2));
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(h3));

  q->Signal(kFence2);
  GetStrategy().ProcessFor(q);
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(h1));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h2));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h3));

  q->Signal(kFence3);
  GetStrategy().ProcessFor(q);
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h1));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h2));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h3));
}

//! Domain isolation: handles from different domains processed independently.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, DomainIsolation_IndependentProcessing)
{
  namespace g = oxygen::graphics;

  // Arrange - create handles from different domains
  const auto& domain_a = GetDomain();
  constexpr DomainKey domain_b {
    .view_type = g::ResourceViewType::kRawBuffer_SRV,
    .visibility = g::DescriptorVisibility::kShaderVisible,
  };
  auto h_a = GetStrategy().Allocate(domain_a);
  auto h_b = GetStrategy().Allocate(domain_b);
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  constexpr uint64_t kFenceValue = 1U;
  GetStrategy().Release(domain_a, h_a, q, g::FenceValue { kFenceValue });
  GetStrategy().Release(domain_b, h_b, q, g::FenceValue { kFenceValue });
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert - both domains processed independently
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h_a));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h_b));
}

//! Batch release: all items with same fence reclaimed together.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, BatchRelease_SameFenceReclaimed)
{
  using oxygen::graphics::FenceValue;

  // Arrange
  auto q = std::make_shared<FakeCommandQueue>();
  std::vector<std::pair<DomainKey, VersionedBindlessHandle>> items;
  constexpr size_t kBatchSize = 3U;
  items.reserve(kBatchSize);
  items.emplace_back(GetDomain(), Allocate());
  items.emplace_back(GetDomain(), Allocate());
  items.emplace_back(GetDomain(), Allocate());

  // Act
  constexpr uint64_t kFenceValue = 5U;
  GetStrategy().ReleaseBatch(q, FenceValue { kFenceValue }, items);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert - all freed
  ExpectNoneCurrent(items);
  EXPECT_THAT(GetFreeBackend().freed, SizeIs(kBatchSize));
}

//! Multi-timeline: only eligible queues reclaim their buckets.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, MultiTimeline_EligibleQueuesOnly)
{
  // Arrange
  auto qA = std::make_shared<FakeCommandQueue>();
  auto qB = std::make_shared<FakeCommandQueue>();
  auto hA = Allocate();
  auto hB = Allocate();
  constexpr uint64_t kFenceA = 10U;
  constexpr uint64_t kFenceB = 20U;
  Release(hA, qA, kFenceA);
  Release(hB, qB, kFenceB);

  // Act - advance only A
  qA->Signal(kFenceA);
  GetStrategy().Process();

  // Assert - A reclaimed, B pending
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(hA));
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(hB));

  // Act - advance B
  qB->Signal(kFenceB);
  GetStrategy().Process();

  // Assert - both reclaimed
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(hA));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(hB));
}

//! Capacity growth: large indices trigger safe expansion.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, CapacityGrowth_LargeIndicesSafe)
{
  // Arrange - force large index allocation
  constexpr uint32_t kLargeIndex = 1024U;
  GetAllocBackend().next.store(kLargeIndex);
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  constexpr uint64_t kFenceValue = 1U;
  Release(h, q, kFenceValue);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert - handled without crashes
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h));
}

//! Expired queue: processing prunes expired keys safely.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ExpiredQueue_PrunesKeysSafely)
{
  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();
  constexpr uint64_t kFenceValue = 7U;
  Release(h, q, kFenceValue);
  q.reset(); // expire queue

  // Act - processing should not crash
  GetStrategy().Process();

  // Assert - handle not reclaimed (no queue to signal)
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(h));
}

//! Queue reuse: same queue works across multiple fence cycles.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, QueueReuse_MultipleFenceCycles)
{
  // Arrange
  auto q = std::make_shared<FakeCommandQueue>();
  auto h1 = Allocate();
  auto h2 = Allocate();
  auto h3 = Allocate();
  constexpr uint64_t kFence1 = 1U;
  constexpr uint64_t kFence2 = 2U;
  constexpr uint64_t kFence3 = 3U;
  Release(h1, q, kFence1);
  Release(h2, q, kFence2);
  Release(h3, q, kFence3);

  // Act - signal incrementally
  q->Signal(kFence1);
  GetStrategy().ProcessFor(q);
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h1));
  EXPECT_TRUE(GetStrategy().IsHandleCurrent(h2));

  q->Signal(kFence3); // skip fence 2
  GetStrategy().ProcessFor(q);

  // Assert - all reclaimed
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h1));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h2));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h3));
}

//! Large batch: many items processed efficiently.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, LargeBatch_ManyItemsProcessed)
{
  using oxygen::graphics::FenceValue;

  // Arrange - create large batch
  constexpr size_t kBatchSize = 100U;
  std::vector<std::pair<DomainKey, VersionedBindlessHandle>> large_batch;
  large_batch.reserve(kBatchSize);
  for (size_t i = 0; i < kBatchSize; ++i) {
    large_batch.emplace_back(GetDomain(), Allocate());
  }
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  constexpr uint64_t kFenceValue = 1U;
  GetStrategy().ReleaseBatch(q, FenceValue { kFenceValue }, large_batch);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert - all processed
  ExpectNoneCurrent(large_batch);
  EXPECT_THAT(GetFreeBackend().freed, SizeIs(kBatchSize));
}

//! Many pending buckets: multiple fence values handled simultaneously.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ManyPendingBuckets_SimultaneousFences)
{
  // Arrange - create handles with different fence values
  constexpr size_t kNumFences = 20U;
  std::vector<VersionedBindlessHandle> handles;
  handles.reserve(kNumFences);
  auto q = std::make_shared<FakeCommandQueue>();
  for (size_t i = 1; i <= kNumFences; ++i) {
    auto h = Allocate();
    handles.push_back(h);
    Release(h, q, static_cast<uint64_t>(i));
  }

  // Act - signal all fences at once
  q->Signal(static_cast<uint64_t>(kNumFences));
  GetStrategy().ProcessFor(q);

  // Assert - all reclaimed
  for (const auto& h : handles) {
    EXPECT_FALSE(GetStrategy().IsHandleCurrent(h));
  }
  EXPECT_THAT(GetFreeBackend().freed, SizeIs(kNumFences));
}

//! Mixed operations: individual and batch releases work together.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, MixedOperations_IndividualAndBatch)
{
  using oxygen::graphics::FenceValue;

  // Arrange
  auto q = std::make_shared<FakeCommandQueue>();
  auto h1 = Allocate();
  auto h2 = Allocate();
  std::vector<std::pair<DomainKey, VersionedBindlessHandle>> batch;
  constexpr size_t kBatchSize = 2U;
  batch.reserve(kBatchSize);
  batch.emplace_back(GetDomain(), Allocate());
  batch.emplace_back(GetDomain(), Allocate());

  // Act - mix individual and batch operations
  constexpr uint64_t kFenceValue = 1U;
  Release(h1, q, kFenceValue);
  Release(h2, q, kFenceValue);
  GetStrategy().ReleaseBatch(q, FenceValue { kFenceValue }, batch);
  q->Signal(kFenceValue);
  GetStrategy().ProcessFor(q);

  // Assert - all reclaimed
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h1));
  EXPECT_FALSE(GetStrategy().IsHandleCurrent(h2));
  ExpectNoneCurrent(batch);
  constexpr uint32_t kExpectedTotalFreed = 4U;
  EXPECT_THAT(GetFreeBackend().freed, SizeIs(kExpectedTotalFreed));
}

#if !defined(NDEBUG)
//! Debug stall warning: adaptive backoff throttles subsequent logs.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, StallWarning_AdaptiveBackoff)
{
  using namespace std::chrono_literals;

  oxygen::testing::ScopedLogCapture const capture(
    "Nexus_TimelineGatedSlotReuse_StallWarning", loguru::Verbosity_9);

  // Configure short intervals for testing
  constexpr auto kInitialThreshold = 20ms;
  constexpr auto kMaxThreshold = 80ms;
  constexpr double kGrowthFactor = 2.0;
  oxygen::nexus::TimelineGatedSlotReuse::SetDebugStallWarningConfig(
    kInitialThreshold, kGrowthFactor, kMaxThreshold);

  // Arrange - stalled queue
  auto q = std::make_shared<FakeCommandQueue>();
  auto h = Allocate();
  constexpr uint64_t kFenceValue = 42U;
  Release(h, q, kFenceValue);

  // Act & Assert - warning emitted after threshold and throttled
  GetStrategy().ProcessFor(q); // Initialize timers

  constexpr auto kSleepTime = kInitialThreshold + 5ms;
  std::this_thread::sleep_for(kSleepTime);

  GetStrategy().ProcessFor(q); // Should warn now
  EXPECT_GE(capture.Count("appears stalled"), 1);

  const int after_first = capture.Count("appears stalled");
  GetStrategy().ProcessFor(q);
  EXPECT_EQ(capture.Count("appears stalled"), after_first); // throttled

  // Restore defaults to avoid affecting other tests in the same process
  constexpr auto kDefaultBase = 2000ms;
  constexpr auto kDefaultMax = 5000ms;
  constexpr double kDefaultMult = 2.0;
  oxygen::nexus::TimelineGatedSlotReuse::SetDebugStallWarningConfig(
    kDefaultBase, kDefaultMult, kDefaultMax);
}
#endif // !NDEBUG

} // namespace
