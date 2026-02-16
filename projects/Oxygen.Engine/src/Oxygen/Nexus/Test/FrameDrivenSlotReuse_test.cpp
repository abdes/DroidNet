//===----------------------------------------------------------------------===//
// Tests for Strategy A: FrameDrivenSlotReuse
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Nexus/FrameDrivenSlotReuse.h>
#include <Oxygen/Nexus/Test/NexusMocks.h>
#include <Oxygen/Nexus/Types/Domain.h>

using oxygen::VersionedBindlessHandle;
using oxygen::nexus::DomainKey;
using oxygen::nexus::testing::AllocateBackend;
using oxygen::nexus::testing::FreeBackend;

namespace b = oxygen::bindless;

namespace {

// Common constants to avoid magic numbers
constexpr oxygen::frame::Slot kFrameSlot0 { 0U };
constexpr uint32_t kGen1 = 1U;
constexpr int kNumIters = 1000;
constexpr int kHighVolumeThreads = 8;
constexpr int kHighVolumePerThread = 512;

//===----------------------------------------------------------------------===//
// Frame-Driven Slot Reuse Tests core functionality of deferred
// reclamation with generation tracking
//===----------------------------------------------------------------------===//

//! Tests that slots are properly reused after frame cycle with incremented
//! generation.
/*!
 Verifies the core deferred reclamation behavior: handles released in one frame
 are not immediately reused but become available after the frame cycle
 completes, with generation counters incremented to detect stale handles.
*/
NOLINT_TEST(FrameDrivenSlotReuse,
  Allocate_AfterFrameCycleReclamation_ReusesSlotWithIncrementedGeneration)
{
  // Arrange
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain { .view_type
    = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible };

  // Act - Allocate initial handle
  auto h1 = reuse.Allocate(domain);
  ASSERT_TRUE(h1.IsValid());
  const auto idx = h1.ToBindlessHandle();
  const auto gen1 = h1.GenerationValue().get();

  // Act - Release and verify not immediately reused
  reuse.Release(domain, h1);
  auto h_before = reuse.Allocate(domain);

  // Assert - Must not reuse before frame cycle
  EXPECT_NE(h_before.ToBindlessHandle().get(), idx.get())
    << "Must not reuse before frame cycle";

  // Act - Complete frame cycle and enable reuse
  per_frame.OnBeginFrame(kFrameSlot0);
  auto h2 = reuse.Allocate(domain);

  // Assert - Slot reused with incremented generation
  EXPECT_EQ(h2.ToBindlessHandle().get(), idx.get());
  EXPECT_EQ(h2.GenerationValue().get(), gen1 + kGen1);
}

//! Tests stale handle detection and safe double-release behavior.
/*!
 Verifies that released handles become stale after generation increment and that
 multiple release calls on the same handle are safely ignored without causing
 crashes or duplicate state changes.
*/
NOLINT_TEST(
  FrameDrivenSlotReuse, Release_StaleHandleDetection_IgnoresDoubleRelease)
{
  // Arrange
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain { .view_type
    = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible };

  // Act - Allocate handle and verify it's current
  auto h = reuse.Allocate(domain);
  const auto idx = h.ToBindlessHandle();
  const auto gen = h.GenerationValue().get();

  // Assert - Handle is initially current
  EXPECT_TRUE(reuse.IsHandleCurrent(h));

  // Act - Release handle twice (test double-release safety)
  reuse.Release(domain, h);
  reuse.Release(domain, h); // Double release should be ignored

  // Act - Complete frame cycle and reuse slot
  per_frame.OnBeginFrame(kFrameSlot0);
  auto h_new = reuse.Allocate(domain);

  // Assert - Slot reused with incremented generation, old handle is stale
  EXPECT_EQ(h_new.ToBindlessHandle().get(), idx.get());
  EXPECT_EQ(h_new.GenerationValue().get(), gen + kGen1);
  EXPECT_FALSE(reuse.IsHandleCurrent(h));
}

//! Tests telemetry counters for allocate/release/reclaim and stale rejection.
NOLINT_TEST(FrameDrivenSlotReuse, TelemetrySnapshot_TracksLifecycleCounters)
{
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain { .view_type
    = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible };

  const auto h = reuse.Allocate(domain);
  reuse.Release(domain, h);
  reuse.Release(domain, h); // stale on second call

  const auto before_frame = reuse.GetTelemetrySnapshot();
  EXPECT_EQ(before_frame.allocate_calls, 1U);
  EXPECT_EQ(before_frame.release_calls, 2U);
  EXPECT_EQ(before_frame.stale_reject_count, 1U);
  EXPECT_EQ(before_frame.duplicate_reject_count, 0U);
  EXPECT_EQ(before_frame.reclaimed_count, 0U);
  EXPECT_EQ(before_frame.pending_count, 1U);

  per_frame.OnBeginFrame(kFrameSlot0);
  const auto after_frame = reuse.GetTelemetrySnapshot();
  EXPECT_EQ(after_frame.reclaimed_count, 1U);
  EXPECT_EQ(after_frame.pending_count, 0U);
}

//===----------------------------------------------------------------------===//
// Multithreaded Tests thread safety and concurrent access patterns
//===----------------------------------------------------------------------===//

//! Tests concurrent handle release and reclamation from multiple threads.
/*!
 Verifies that the strategy correctly handles concurrent release operations from
 multiple worker threads without data races or corruption, and that deferred
 reclamation processes all handles correctly.
*/
NOLINT_TEST(
  FrameDrivenSlotReuse, Release_ConcurrentMultipleThreads_HandlesAllDeferred)
{
  // Arrange
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain {
    .view_type = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
  };

  constexpr int kTotalHandles = 256;
  std::vector<VersionedBindlessHandle> handles;
  handles.reserve(kTotalHandles);

  // Act - Allocate handles from main thread
  for (int i = 0; i < kTotalHandles; ++i) {
    handles.push_back(reuse.Allocate(domain));
  }

  // Act - Release handles concurrently from worker threads
  constexpr int kNumWorkers = 4;
  std::vector<std::thread> workers;
  workers.reserve(kNumWorkers);
  for (int t = 0; t < kNumWorkers; ++t) {
    workers.emplace_back([t, &reuse, &handles]() {
      for (int i = t; i < kTotalHandles; i += kNumWorkers) {
        reuse.Release(
          DomainKey {
            .view_type = oxygen::graphics::ResourceViewType::kTexture_SRV,
            .visibility
            = oxygen::graphics::DescriptorVisibility::kShaderVisible },
          handles[static_cast<size_t>(i)]);
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  // Act - Trigger reclamation
  per_frame.OnBeginFrame(kFrameSlot0);

  // Assert - Every released handle was reclaimed exactly once
  ASSERT_EQ(do_free.freed.size(), static_cast<size_t>(kTotalHandles));
  std::sort(do_free.freed.begin(), do_free.freed.end());
  const auto unique_end
    = std::unique(do_free.freed.begin(), do_free.freed.end());
  EXPECT_EQ(std::distance(do_free.freed.begin(), unique_end), kTotalHandles);
}

//===----------------------------------------------------------------------===//
// Edge Case Tests Tests boundary conditions and error handling
//===----------------------------------------------------------------------===//

//! Tests that releasing invalid handles is safely ignored without side effects.
/*!
 Verifies that attempting to release default-constructed (invalid) handles does
 not trigger any deferred actions or cause crashes, ensuring robust error
 handling for invalid input.
*/
NOLINT_TEST(FrameDrivenSlotReuse, Release_InvalidHandle_IsNoOp)
{
  // Arrange
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain { .view_type
    = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible };

  // Act - Release an explicitly invalid handle
  reuse.Release(domain, {});

  // Act - Advance frame
  per_frame.OnBeginFrame(kFrameSlot0);

  // Assert - Nothing should have been freed
  EXPECT_TRUE(do_free.freed.empty());
}

//! Tests that IsHandleCurrent returns false for invalid handles.
/*!
 Verifies that the handle validation correctly identifies default-constructed
 (invalid) handles as not current, ensuring proper boundary condition handling
  for stale handle detection.
*/
NOLINT_TEST(FrameDrivenSlotReuse, IsHandleCurrent_InvalidHandle_ReturnsFalse)
{
  // Arrange
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  // Act & Assert - Default-constructed VersionedBindlessHandle is invalid
  EXPECT_FALSE(reuse.IsHandleCurrent(VersionedBindlessHandle {}));
}

//! Tests buffer growth for large indices and subsequent reuse behavior.
/*!
 Verifies that the strategy correctly handles large bindless handle indices by
 growing internal buffers as needed, and that reuse behavior remains correct
 even with non-contiguous index allocations.
*/
NOLINT_TEST(
  FrameDrivenSlotReuse, EnsureCapacity_LargeIndex_GrowsBuffersAndEnablesReuse)
{
  // Arrange
  oxygen::graphics::detail::DeferredReclaimer per_frame;

  // Custom allocator that starts with large indices
  AllocateBackend do_alloc;
  constexpr uint32_t kLargeIndex = 10000U;
  do_alloc.next.store(kLargeIndex);

  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain {
    .view_type = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
  };

  // Act - Allocate large-index handle forcing EnsureCapacity_ growth
  auto h1 = reuse.Allocate(domain);
  ASSERT_TRUE(h1.IsValid());
  const auto idx = h1.ToBindlessHandle();
  const auto gen1 = h1.GenerationValue().get();

  // Act - Release and verify no immediate reuse
  reuse.Release(domain, h1);
  auto h_before = reuse.Allocate(domain);

  // Assert - Must not reuse before frame cycle
  EXPECT_NE(h_before.ToBindlessHandle().get(), idx.get())
    << "Must not reuse before frame cycle (large-index case)";

  // Act - Complete frame cycle and enable reuse
  per_frame.OnBeginFrame(kFrameSlot0);
  auto h2 = reuse.Allocate(domain);

  // Assert - Slot reused with incremented generation
  EXPECT_EQ(h2.ToBindlessHandle().get(), idx.get());
  EXPECT_EQ(h2.GenerationValue().get(), gen1 + kGen1);
}

//! Tests concurrent double-release protection for a single handle.
/*!
 Verifies that when multiple threads attempt to release the same handle
 simultaneously, only one deferred free action is scheduled, preventing
 duplicate cleanup operations.
*/
NOLINT_TEST(FrameDrivenSlotReuse,
  Release_ConcurrentDoubleReleaseSingleHandle_SchedulesOnlyOneDeferred)
{
  // Arrange
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain { .view_type
    = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible };

  // Act - Allocate single handle
  auto h = reuse.Allocate(domain);
  ASSERT_TRUE(h.IsValid());
  const auto idx = h.ToBindlessHandle().get();

  // Act - Spawn many threads attempting to release same handle
  constexpr int kNumThreads = 32;
  const auto start = std::make_shared<std::atomic<bool>>(false);
  std::vector<std::thread> workers;
  workers.reserve(kNumThreads);
  for (int t = 0; t < kNumThreads; ++t) {
    workers.emplace_back([&reuse, domain, h, start]() {
      while (!start->load(std::memory_order_acquire)) { }
      for (int i = 0; i < kNumIters; ++i) {
        reuse.Release(domain, h);
      }
    });
  }

  start->store(true, std::memory_order_release);
  for (auto& w : workers) {
    w.join();
  }

  // Act - Process deferred frees
  per_frame.OnBeginFrame(kFrameSlot0);

  // Assert - Only one deferred free should have been scheduled
  ASSERT_EQ(do_free.freed.size(), 1U);
  EXPECT_EQ(do_free.freed.front(), idx);
}

//! Tests high-volume concurrent allocation and release operations.
/*!
 Verifies that the strategy correctly handles high-throughput scenarios with
 many threads simultaneously allocating and releasing handles, ensuring all
 operations complete successfully without data corruption.
*/
NOLINT_TEST(FrameDrivenSlotReuse,
  AllocateRelease_ConcurrentHighVolume_HandlesAllOperations)
{
  // Arrange
  oxygen::graphics::detail::DeferredReclaimer per_frame;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  oxygen::nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](const DomainKey d) { return do_alloc(d); },
    [&do_alloc, &do_free](const DomainKey d, const b::HeapIndex h) {
      do_free(d, h);
      do_alloc.free_list.push_back(h.get());
    },
    per_frame);

  constexpr DomainKey domain {
    .view_type = oxygen::graphics::ResourceViewType::kTexture_SRV,
    .visibility = oxygen::graphics::DescriptorVisibility::kShaderVisible,
  };

  constexpr int kHighVolumeTotalExpected
    = kHighVolumeThreads * kHighVolumePerThread;
  const auto start = std::make_shared<std::atomic<bool>>(false);

  // Act - Create worker threads for high-volume alloc/release
  std::vector<std::thread> workers;
  workers.reserve(kHighVolumeThreads);
  for (int t = 0; t < kHighVolumeThreads; ++t) {
    workers.emplace_back([&reuse, domain, start]() {
      while (!start->load(std::memory_order_acquire)) { }
      for (int i = 0; i < kHighVolumePerThread; ++i) {
        const auto h = reuse.Allocate(domain);
        reuse.Release(domain, h);
      }
    });
  }

  start->store(true, std::memory_order_release);
  for (auto& w : workers) {
    w.join();
  }

  // Act - Process deferred frees
  per_frame.OnBeginFrame(kFrameSlot0);

  // Assert - All releases should be processed
  EXPECT_EQ(
    do_free.freed.size(), static_cast<size_t>(kHighVolumeTotalExpected));
}

} // namespace
