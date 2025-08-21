//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Nexus/TimelineGatedSlotReuse.h>
#include <Oxygen/Nexus/Types/Domain.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

using oxygen::VersionedBindlessHandle;
using oxygen::bindless::Handle;
using oxygen::nexus::DomainKey;

namespace {

// File-level group header for TimelineGatedSlotReuse tests
// Tests follow the AAA pattern and use fixtures for shared setup.

// Common matchers used by tests
using ::testing::SizeIs;
using ::testing::UnorderedElementsAreArray;

// Backend allocator/free mocks used by tests.
struct AllocateBackend {
  std::atomic<uint32_t> next { 0 };
  Handle operator()(DomainKey) { return Handle { next.fetch_add(1u) }; }
};

struct FreeBackend {
  std::vector<uint32_t> freed;
  void operator()(DomainKey, Handle h) { freed.push_back(h.get()); }
};

// Test-only CommandQueue implementation that derives from the real
// `oxygen::graphics::CommandQueue`. This ensures pointer identity and
// virtual dispatch match production code expectations.
struct FakeCommandQueue : public oxygen::graphics::CommandQueue {
  mutable std::atomic<uint64_t> completed { 0 };
  mutable std::atomic<uint64_t> current { 0 };

  FakeCommandQueue()
    : oxygen::graphics::CommandQueue("FakeCommandQueue")
  {
  }

  void Signal(uint64_t value) const override
  {
    completed.store(value);
    current.store(value);
  }

  auto Signal() const -> uint64_t override
  {
    const auto v = current.fetch_add(1u) + 1u;
    current.store(v);
    completed.store(v);
    return v;
  }

  void Wait(uint64_t, std::chrono::milliseconds) const override { }
  void Wait(uint64_t) const override { }

  void QueueSignalCommand(uint64_t value) override { completed.store(value); }
  void QueueWaitCommand(uint64_t) const override { }

  auto GetCompletedValue() const -> uint64_t override
  {
    return completed.load();
  }
  auto GetCurrentValue() const -> uint64_t override { return current.load(); }

  void Submit(oxygen::graphics::CommandList&) override { }
  void Submit(std::span<oxygen::graphics::CommandList*>) override { }

  auto GetQueueRole() const -> oxygen::graphics::QueueRole override
  {
    return oxygen::graphics::QueueRole::kNone;
  }
};

//! Test fixture providing common setup for TimelineGatedSlotReuse tests.
class TimelineGatedSlotReuseTest : public testing::Test {
protected:
  void SetUp() override
  {
    strategy_ = std::make_unique<oxygen::nexus::TimelineGatedSlotReuse>(
      [this](DomainKey d) -> oxygen::bindless::Handle { return alloc_(d); },
      [this](DomainKey d, oxygen::bindless::Handle h) { free_(d, h); });
    domain_ = DomainKey { oxygen::graphics::ResourceViewType::kTexture_SRV,
      oxygen::graphics::DescriptorVisibility::kShaderVisible };
  }

  // Helpers
  auto Allocate() -> VersionedBindlessHandle
  {
    return strategy_->Allocate(domain_);
  }
  void Release(VersionedBindlessHandle h,
    const std::shared_ptr<oxygen::graphics::CommandQueue>& q, uint64_t fence)
  {
    strategy_->Release(domain_, h, q, oxygen::graphics::FenceValue { fence });
  }

  // Helper: assert freed indices match expected set exactly (order-insensitive)
  void ExpectFreedExactly(std::span<const uint32_t> expected)
  {
    EXPECT_THAT(free_.freed, UnorderedElementsAreArray(expected));
  }

  // Helper: assert all handles in a collection are current
  template <typename Collection> void ExpectAllCurrent(const Collection& items)
  {
    for (const auto& kv : items) {
      EXPECT_TRUE(strategy_->IsHandleCurrent(kv.second));
    }
  }

  // Helper: assert no handles in a collection are current
  template <typename Collection> void ExpectNoneCurrent(const Collection& items)
  {
    for (const auto& kv : items) {
      EXPECT_FALSE(strategy_->IsHandleCurrent(kv.second));
    }
  }

  AllocateBackend alloc_;
  FreeBackend free_;
  DomainKey domain_ {};
  std::unique_ptr<oxygen::nexus::TimelineGatedSlotReuse> strategy_;
};

//! Happy path test: allocation, release, fence advance, reclamation, generation
//! bump.
/*! Arrange: create strategy, allocate a handle.
    Act: release the handle on a fake queue, advance fence and ProcessFor.
    Assert: backend free recorded and generation is no longer current.
*/
NOLINT_TEST_F(TimelineGatedSlotReuseTest,
  AllocateRelease_Process_ReclaimsAndBumpsGeneration)
{
  using namespace oxygen;

  // Arrange
  auto h = Allocate();
  ASSERT_TRUE(h.IsValid());
  const auto u_idx = h.ToBindlessHandle().get();

  // Create fake queue
  auto q = std::make_shared<FakeCommandQueue>();

  // Act - release and process before fence reached
  const uint64_t fence = 1;
  Release(h, q, fence);
  strategy_->ProcessFor(q);

  // Assert - not reclaimed yet
  EXPECT_TRUE(strategy_->IsHandleCurrent(h));

  // Act - advance fence and process
  q->Signal(fence);
  strategy_->ProcessFor(q);

  // Assert - reclaimed and backend recorded free
  EXPECT_FALSE(strategy_->IsHandleCurrent(h));
  const uint32_t expected[] { u_idx };
  TRACE_GCHECK_F(ExpectFreedExactly(expected), "freed-one");
}

//! Allocate method: provides valid handles with unique indices.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Allocate_ReturnsValidHandle)
{
  // Arrange & Act
  auto h = Allocate();

  // Assert
  EXPECT_TRUE(h.IsValid());
  EXPECT_NE(h.ToBindlessHandle().get(), oxygen::kInvalidBindlessIndex);
  EXPECT_TRUE(strategy_->IsHandleCurrent(h));
}

//! Release method: ignores duplicate releases for same handle.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_IgnoresDuplicates)
{
  using namespace oxygen;

  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();
  const uint64_t fence = 2;

  // Act
  Release(h, q, fence);
  Release(h, q, fence); // duplicate
  q->Signal(fence);
  strategy_->ProcessFor(q);

  // Assert - only freed once
  const uint32_t expected[] { h.ToBindlessHandle().get() };
  TRACE_GCHECK_F(ExpectFreedExactly(expected), "freed-once");
}

//! Release method: safely ignores invalid handles.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_IgnoresInvalidHandle)
{
  using namespace oxygen;

  // Arrange
  VersionedBindlessHandle invalid_handle {};
  ASSERT_FALSE(invalid_handle.IsValid());
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  Release(invalid_handle, q, 5);
  q->Signal(5);
  strategy_->ProcessFor(q);

  // Assert - no backend operations
  std::array<uint32_t, 0> none {};
  TRACE_GCHECK_F(ExpectFreedExactly(none), "no-operations");
}

//! Release method: accepts first release when called multiple times with
//! different fences.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_FirstFenceWins)
{
  using namespace oxygen;

  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  // Act - release with fence 5, then again with fence 10
  Release(h, q, 5);
  Release(h, q, 10);
  q->Signal(5);
  strategy_->ProcessFor(q);

  // Assert - reclaimed after first fence
  EXPECT_FALSE(strategy_->IsHandleCurrent(h));
}

//! Release method: handles zero fence value correctly.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_HandlesZeroFence)
{
  using namespace oxygen;

  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  Release(h, q, 0);
  q->Signal(0);
  strategy_->ProcessFor(q);

  // Assert
  EXPECT_FALSE(strategy_->IsHandleCurrent(h));
}

//! Release method: handles maximum fence values without overflow.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Release_HandlesMaxFence)
{
  using namespace oxygen;

  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();
  const uint64_t max_fence = std::numeric_limits<uint64_t>::max();

  // Act
  Release(h, q, max_fence);
  q->Signal(max_fence);
  strategy_->ProcessFor(q);

  // Assert
  EXPECT_FALSE(strategy_->IsHandleCurrent(h));
}

//! ReleaseBatch method: handles empty collections without error.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ReleaseBatch_HandlesEmptyCollection)
{
  using namespace oxygen;

  // Arrange
  auto q = std::make_shared<FakeCommandQueue>();
  std::vector<std::pair<DomainKey, VersionedBindlessHandle>> empty_batch;

  // Act
  strategy_->ReleaseBatch(q, graphics::FenceValue { 10 }, empty_batch);
  q->Signal(10);
  strategy_->ProcessFor(q);

  // Assert - no operations
  std::array<uint32_t, 0> none {};
  TRACE_GCHECK_F(ExpectFreedExactly(none), "no-operations");
}

//! IsHandleCurrent method: returns false after handle is reclaimed.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, IsHandleCurrent_FalseAfterReclaim)
{
  using namespace oxygen;

  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  Release(h, q, 3);
  q->Signal(3);
  strategy_->ProcessFor(q);

  // Assert
  EXPECT_FALSE(strategy_->IsHandleCurrent(h));
}

//! ProcessFor method: processes only the specified queue.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ProcessFor_ProcessesSpecificQueue)
{
  using namespace oxygen;

  // Arrange
  auto qA = std::make_shared<FakeCommandQueue>();
  auto qB = std::make_shared<FakeCommandQueue>();
  auto hA = Allocate();
  auto hB = Allocate();
  Release(hA, qA, 1);
  Release(hB, qB, 1);
  qA->Signal(1);
  qB->Signal(1);

  // Act - process only qA
  strategy_->ProcessFor(qA);

  // Assert - only hA reclaimed
  EXPECT_FALSE(strategy_->IsHandleCurrent(hA));
  EXPECT_TRUE(strategy_->IsHandleCurrent(hB));
}

//! Process method: processes all queues.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, Process_ProcessesAllQueues)
{
  using namespace oxygen;

  // Arrange
  auto qA = std::make_shared<FakeCommandQueue>();
  auto qB = std::make_shared<FakeCommandQueue>();
  auto hA = Allocate();
  auto hB = Allocate();
  Release(hA, qA, 1);
  Release(hB, qB, 1);
  qA->Signal(1);
  qB->Signal(1);

  // Act - global process
  strategy_->Process();

  // Assert - both reclaimed
  EXPECT_FALSE(strategy_->IsHandleCurrent(hA));
  EXPECT_FALSE(strategy_->IsHandleCurrent(hB));
}

//! Generation isolation: old handles become invalid when slot is reused.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, GenerationIsolation_OldHandleInvalid)
{
  using namespace oxygen;

  // Arrange - allocate and reclaim handle
  auto first_handle = Allocate();
  const auto u_index = first_handle.ToBindlessHandle().get();
  auto q = std::make_shared<FakeCommandQueue>();
  Release(first_handle, q, 1);
  q->Signal(1);
  strategy_->ProcessFor(q);

  // Force reuse of same slot
  alloc_.next.store(u_index);
  auto second_handle = Allocate();

  // Assert - generation isolation maintained
  EXPECT_FALSE(strategy_->IsHandleCurrent(first_handle));
  EXPECT_TRUE(strategy_->IsHandleCurrent(second_handle));
  EXPECT_EQ(first_handle.ToBindlessHandle().get(),
    second_handle.ToBindlessHandle().get());
  EXPECT_NE(first_handle.GenerationValue(), second_handle.GenerationValue());
}

//! Handle uniqueness: allocated handles have unique indices within domain.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, HandleUniqueness_UniqueIndices)
{
  // Arrange & Act - allocate multiple handles
  std::vector<uint32_t> indices;
  for (int i = 0; i < 10; ++i) {
    auto h = Allocate();
    ASSERT_TRUE(h.IsValid());
    indices.push_back(h.ToBindlessHandle().get());
  }

  // Assert - all indices unique
  std::sort(indices.begin(), indices.end());
  const auto unique_end = std::unique(indices.begin(), indices.end());
  EXPECT_EQ(std::distance(indices.begin(), unique_end), 10);
}

//! Timeline ordering: fences processed in value order regardless of release
//! order.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, TimelineOrdering_FenceValueOrder)
{
  using namespace oxygen;

  // Arrange - release with fence values 3, 1, 2
  auto q = std::make_shared<FakeCommandQueue>();
  auto h1 = Allocate();
  auto h2 = Allocate();
  auto h3 = Allocate();
  Release(h1, q, 3);
  Release(h2, q, 1);
  Release(h3, q, 2);

  // Act & Assert - process in fence order
  q->Signal(1);
  strategy_->ProcessFor(q);
  EXPECT_TRUE(strategy_->IsHandleCurrent(h1));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h2));
  EXPECT_TRUE(strategy_->IsHandleCurrent(h3));

  q->Signal(2);
  strategy_->ProcessFor(q);
  EXPECT_TRUE(strategy_->IsHandleCurrent(h1));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h2));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h3));

  q->Signal(3);
  strategy_->ProcessFor(q);
  EXPECT_FALSE(strategy_->IsHandleCurrent(h1));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h2));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h3));
}

//! Domain isolation: handles from different domains processed independently.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, DomainIsolation_IndependentProcessing)
{
  using namespace oxygen;

  // Arrange - create handles from different domains
  const DomainKey domain_a = domain_;
  const DomainKey domain_b { graphics::ResourceViewType::kRawBuffer_SRV,
    graphics::DescriptorVisibility::kShaderVisible };
  auto h_a = strategy_->Allocate(domain_a);
  auto h_b = strategy_->Allocate(domain_b);
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  strategy_->Release(domain_a, h_a, q, graphics::FenceValue { 1 });
  strategy_->Release(domain_b, h_b, q, graphics::FenceValue { 1 });
  q->Signal(1);
  strategy_->ProcessFor(q);

  // Assert - both domains processed independently
  EXPECT_FALSE(strategy_->IsHandleCurrent(h_a));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h_b));
}

//! Batch release: all items with same fence reclaimed together.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, BatchRelease_SameFenceReclaimed)
{
  using namespace oxygen;

  // Arrange
  auto q = std::make_shared<FakeCommandQueue>();
  std::vector<std::pair<DomainKey, VersionedBindlessHandle>> items;
  items.emplace_back(domain_, Allocate());
  items.emplace_back(domain_, Allocate());
  items.emplace_back(domain_, Allocate());

  // Act
  strategy_->ReleaseBatch(q, graphics::FenceValue { 5 }, items);
  q->Signal(5);
  strategy_->ProcessFor(q);

  // Assert - all freed
  TRACE_GCHECK_F(ExpectNoneCurrent(items), "all-reclaimed");
  EXPECT_THAT(free_.freed, SizeIs(3));
}

//! Multi-timeline: only eligible queues reclaim their buckets.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, MultiTimeline_EligibleQueuesOnly)
{
  using namespace oxygen;

  // Arrange
  auto qA = std::make_shared<FakeCommandQueue>();
  auto qB = std::make_shared<FakeCommandQueue>();
  auto hA = Allocate();
  auto hB = Allocate();
  Release(hA, qA, 10);
  Release(hB, qB, 20);

  // Act - advance only A
  qA->Signal(10);
  strategy_->Process();

  // Assert - A reclaimed, B pending
  EXPECT_FALSE(strategy_->IsHandleCurrent(hA));
  EXPECT_TRUE(strategy_->IsHandleCurrent(hB));

  // Act - advance B
  qB->Signal(20);
  strategy_->Process();

  // Assert - both reclaimed
  EXPECT_FALSE(strategy_->IsHandleCurrent(hA));
  EXPECT_FALSE(strategy_->IsHandleCurrent(hB));
}

//! Capacity growth: large indices trigger safe expansion.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, CapacityGrowth_LargeIndicesSafe)
{
  using namespace oxygen;

  // Arrange - force large index allocation
  alloc_.next.store(1024);
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  Release(h, q, 1);
  q->Signal(1);
  strategy_->ProcessFor(q);

  // Assert - handled without crashes
  EXPECT_FALSE(strategy_->IsHandleCurrent(h));
}

//! Expired queue: processing prunes expired keys safely.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ExpiredQueue_PrunesKeysSafely)
{
  using namespace oxygen;

  // Arrange
  auto h = Allocate();
  auto q = std::make_shared<FakeCommandQueue>();
  Release(h, q, 7);
  q.reset(); // expire queue

  // Act - processing should not crash
  strategy_->Process();

  // Assert - handle not reclaimed (no queue to signal)
  EXPECT_TRUE(strategy_->IsHandleCurrent(h));
}

//! Queue reuse: same queue works across multiple fence cycles.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, QueueReuse_MultipleFenceCycles)
{
  using namespace oxygen;

  // Arrange
  auto q = std::make_shared<FakeCommandQueue>();
  auto h1 = Allocate();
  auto h2 = Allocate();
  auto h3 = Allocate();
  Release(h1, q, 1);
  Release(h2, q, 2);
  Release(h3, q, 3);

  // Act - signal incrementally
  q->Signal(1);
  strategy_->ProcessFor(q);
  EXPECT_FALSE(strategy_->IsHandleCurrent(h1));
  EXPECT_TRUE(strategy_->IsHandleCurrent(h2));

  q->Signal(3); // skip fence 2
  strategy_->ProcessFor(q);

  // Assert - all reclaimed
  EXPECT_FALSE(strategy_->IsHandleCurrent(h1));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h2));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h3));
}

//! Large batch: many items processed efficiently.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, LargeBatch_ManyItemsProcessed)
{
  using namespace oxygen;

  // Arrange - create large batch
  constexpr size_t batch_size = 100;
  std::vector<std::pair<DomainKey, VersionedBindlessHandle>> large_batch;
  large_batch.reserve(batch_size);
  for (size_t i = 0; i < batch_size; ++i) {
    large_batch.emplace_back(domain_, Allocate());
  }
  auto q = std::make_shared<FakeCommandQueue>();

  // Act
  strategy_->ReleaseBatch(q, graphics::FenceValue { 1 }, large_batch);
  q->Signal(1);
  strategy_->ProcessFor(q);

  // Assert - all processed
  TRACE_GCHECK_F(ExpectNoneCurrent(large_batch), "all-reclaimed");
  EXPECT_THAT(free_.freed, SizeIs(batch_size));
}

//! Many pending buckets: multiple fence values handled simultaneously.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, ManyPendingBuckets_SimultaneousFences)
{
  using namespace oxygen;

  // Arrange - create handles with different fence values
  constexpr size_t num_fences = 20;
  std::vector<VersionedBindlessHandle> handles;
  auto q = std::make_shared<FakeCommandQueue>();
  for (size_t i = 1; i <= num_fences; ++i) {
    auto h = Allocate();
    handles.push_back(h);
    Release(h, q, i);
  }

  // Act - signal all fences at once
  q->Signal(num_fences);
  strategy_->ProcessFor(q);

  // Assert - all reclaimed
  for (const auto& h : handles) {
    EXPECT_FALSE(strategy_->IsHandleCurrent(h));
  }
  EXPECT_THAT(free_.freed, SizeIs(num_fences));
}

//! Mixed operations: individual and batch releases work together.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, MixedOperations_IndividualAndBatch)
{
  using namespace oxygen;

  // Arrange
  auto q = std::make_shared<FakeCommandQueue>();
  auto h1 = Allocate();
  auto h2 = Allocate();
  std::vector<std::pair<DomainKey, VersionedBindlessHandle>> batch;
  batch.emplace_back(domain_, Allocate());
  batch.emplace_back(domain_, Allocate());

  // Act - mix individual and batch operations
  Release(h1, q, 1);
  Release(h2, q, 1);
  strategy_->ReleaseBatch(q, graphics::FenceValue { 1 }, batch);
  q->Signal(1);
  strategy_->ProcessFor(q);

  // Assert - all reclaimed
  EXPECT_FALSE(strategy_->IsHandleCurrent(h1));
  EXPECT_FALSE(strategy_->IsHandleCurrent(h2));
  TRACE_GCHECK_F(ExpectNoneCurrent(batch), "batch-reclaimed");
  EXPECT_THAT(free_.freed, SizeIs(4));
}

#if !defined(NDEBUG)
//! Debug stall warning: adaptive backoff throttles subsequent logs.
NOLINT_TEST_F(TimelineGatedSlotReuseTest, StallWarning_AdaptiveBackoff)
{
  using namespace std::chrono_literals;

  oxygen::testing::ScopedLogCapture capture(
    "Nexus_TimelineGatedSlotReuse_StallWarning", loguru::Verbosity_9);

  // Configure short intervals for testing
  oxygen::nexus::TimelineGatedSlotReuse::SetDebugStallWarningConfig(
    20ms, 2.0, 80ms);

  // Arrange - stalled queue
  auto q = std::make_shared<FakeCommandQueue>();
  auto h = Allocate();
  Release(h, q, 42);

  // Act & Assert - warning emitted and throttled
  strategy_->ProcessFor(q);
  EXPECT_GE(capture.Count("appears stalled"), 1);

  const int after_first = capture.Count("appears stalled");
  strategy_->ProcessFor(q);
  EXPECT_EQ(capture.Count("appears stalled"), after_first); // throttled
}
#endif // !NDEBUG

} // namespace
