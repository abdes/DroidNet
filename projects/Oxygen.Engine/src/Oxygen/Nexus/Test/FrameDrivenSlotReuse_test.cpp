//===----------------------------------------------------------------------===//
// Tests for Strategy A: FrameDrivenSlotReuse
//===----------------------------------------------------------------------===//

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/BindlessHandle.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Nexus/FrameDrivenSlotReuse.h>
#include <Oxygen/Nexus/Types/Domain.h>

using oxygen::VersionedBindlessHandle;
using oxygen::bindless::Handle;
using oxygen::nexus::DomainKey;

namespace {

//===----------------------------------------------------------------------===//
// Test Fixtures and Helper Classes
//===----------------------------------------------------------------------===//

//! Mock descriptor allocator for testing purposes - provides minimal interface
//! implementation without actual GPU resource allocation.
struct FakeAllocator : oxygen::graphics::DescriptorAllocator {
  using RVT = oxygen::graphics::ResourceViewType;
  using Vis = oxygen::graphics::DescriptorVisibility;

  // Unused in tests
  auto Allocate(RVT, Vis) -> oxygen::graphics::DescriptorHandle override
  {
    return CreateDescriptorHandle(
      oxygen::bindless::Handle { 0 }, RVT::kTexture_SRV, Vis::kShaderVisible);
  }
  void Release(oxygen::graphics::DescriptorHandle&) override { }
  void CopyDescriptor(const oxygen::graphics::DescriptorHandle&,
    const oxygen::graphics::DescriptorHandle&) override
  {
  }

  auto GetRemainingDescriptorsCount(RVT, Vis) const
    -> oxygen::bindless::Count override
  {
    return oxygen::bindless::Count { 0 };
  }
  auto GetDomainBaseIndex(RVT, Vis) const -> oxygen::bindless::Handle override
  {
    return oxygen::bindless::Handle { 0 };
  }
  auto Reserve(RVT, Vis, oxygen::bindless::Count)
    -> std::optional<oxygen::bindless::Handle> override
  {
    return std::nullopt;
  }
  auto Contains(const oxygen::graphics::DescriptorHandle&) const
    -> bool override
  {
    return false;
  }
  auto GetAllocatedDescriptorsCount(RVT, Vis) const
    -> oxygen::bindless::Count override
  {
    return oxygen::bindless::Count { 0 };
  }
  auto GetShaderVisibleIndex(
    const oxygen::graphics::DescriptorHandle&) const noexcept
    -> oxygen::bindless::Handle override
  {
    return oxygen::bindless::Handle { 0 };
  }
};

//! Backend allocator mock that tracks allocations and supports free list reuse.
struct AllocateBackend {
  std::vector<uint32_t> free_list;
  std::atomic<int> alloc_count { 0 };

  Handle operator()(DomainKey)
  {
    ++alloc_count;
    if (!free_list.empty()) {
      auto idx = free_list.back();
      free_list.pop_back();
      return Handle { idx };
    }
    static std::atomic<uint32_t> next { 0 };
    return Handle { next++ };
  }
};

//! Backend free function mock that records freed handle indices.
struct FreeBackend {
  std::vector<uint32_t> freed;
  void operator()(DomainKey, Handle h) { freed.push_back(h.get()); }
};

//===----------------------------------------------------------------------===//
// Frame-Driven Slot Reuse Tests Tests core functionality of deferred
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
  using namespace oxygen;

  // Arrange
  oxygen::graphics::detail::PerFrameResourceManager per_frame;
  FakeAllocator allocator;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](DomainKey d) { return do_alloc(d); },
    [&do_free](DomainKey d, Handle h) { do_free(d, h); }, allocator, per_frame);

  DomainKey domain { oxygen::graphics::ResourceViewType::kTexture_SRV,
    oxygen::graphics::DescriptorVisibility::kShaderVisible };

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
  per_frame.OnBeginFrame(oxygen::frame::Slot { 0 });
  do_alloc.free_list.push_back(idx.get());
  auto h2 = reuse.Allocate(domain);

  // Assert - Slot reused with incremented generation
  EXPECT_EQ(h2.ToBindlessHandle().get(), idx.get());
  EXPECT_EQ(h2.GenerationValue().get(), gen1 + 1);
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
  using namespace oxygen;

  // Arrange
  oxygen::graphics::detail::PerFrameResourceManager per_frame;
  FakeAllocator allocator;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](DomainKey d) { return do_alloc(d); },
    [&do_free](DomainKey d, Handle h) { do_free(d, h); }, allocator, per_frame);

  DomainKey domain { oxygen::graphics::ResourceViewType::kTexture_SRV,
    oxygen::graphics::DescriptorVisibility::kShaderVisible };

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
  per_frame.OnBeginFrame(oxygen::frame::Slot { 0 });
  do_alloc.free_list.push_back(idx.get());
  auto h_new = reuse.Allocate(domain);

  // Assert - Slot reused with incremented generation, old handle is stale
  EXPECT_EQ(h_new.ToBindlessHandle().get(), idx.get());
  EXPECT_EQ(h_new.GenerationValue().get(), gen + 1);
  EXPECT_FALSE(reuse.IsHandleCurrent(h));
}

//===----------------------------------------------------------------------===//
// Multithreaded Tests Tests thread safety and concurrent access patterns
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
  using namespace oxygen;

  // Arrange
  oxygen::graphics::detail::PerFrameResourceManager per_frame;
  FakeAllocator allocator;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](DomainKey d) { return do_alloc(d); },
    [&do_free](DomainKey d, Handle h) { do_free(d, h); }, allocator, per_frame);

  DomainKey domain { oxygen::graphics::ResourceViewType::kTexture_SRV,
    oxygen::graphics::DescriptorVisibility::kShaderVisible };

  constexpr int kCount = 256;
  std::vector<VersionedBindlessHandle> handles;
  handles.reserve(kCount);

  // Act - Allocate handles from main thread
  for (int i = 0; i < kCount; ++i) {
    handles.push_back(reuse.Allocate(domain));
  }

  // Act - Release handles concurrently from worker threads
  std::vector<std::thread> workers;
  for (int t = 0; t < 4; ++t) {
    workers.emplace_back([t, &reuse, &handles, kCount]() {
      for (int i = t; i < kCount; i += 4) {
        reuse.Release(
          DomainKey { oxygen::graphics::ResourceViewType::kTexture_SRV,
            oxygen::graphics::DescriptorVisibility::kShaderVisible },
          handles[i]);
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  // Act - Trigger reclamation
  per_frame.OnBeginFrame(oxygen::frame::Slot { 0 });

  // Act - Setup for reuse verification
  for (auto const& idx_val : do_free.freed) {
    do_alloc.free_list.push_back(idx_val);
  }

  // Assert - Handles can be allocated and are valid
  auto h_new = reuse.Allocate(domain);
  ASSERT_TRUE(h_new.IsValid());
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
  using namespace oxygen;

  // Arrange
  oxygen::graphics::detail::PerFrameResourceManager per_frame;
  FakeAllocator allocator;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](DomainKey d) { return do_alloc(d); },
    [&do_free](DomainKey d, Handle h) { do_free(d, h); }, allocator, per_frame);

  DomainKey domain { oxygen::graphics::ResourceViewType::kTexture_SRV,
    oxygen::graphics::DescriptorVisibility::kShaderVisible };

  // Act - Release an explicitly invalid handle
  reuse.Release(domain, {});

  // Act - Advance frame
  per_frame.OnBeginFrame(oxygen::frame::Slot { 0 });

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
  using namespace oxygen;

  // Arrange
  oxygen::graphics::detail::PerFrameResourceManager per_frame;
  FakeAllocator allocator;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](DomainKey d) { return do_alloc(d); },
    [&do_free](DomainKey d, Handle h) { do_free(d, h); }, allocator, per_frame);

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
  using namespace oxygen;

  // Arrange
  oxygen::graphics::detail::PerFrameResourceManager per_frame;
  FakeAllocator allocator;

  // Custom allocator that starts with large indices
  struct CustomAlloc {
    std::vector<uint32_t> free_list;
    std::atomic<uint32_t> next;
    explicit CustomAlloc(uint32_t start)
      : next(start)
    {
    }
    Handle operator()(DomainKey)
    {
      if (!free_list.empty()) {
        auto idx = free_list.back();
        free_list.pop_back();
        return Handle { idx };
      }
      return Handle { next.fetch_add(1u) };
    }
  } do_alloc(10000u);

  FreeBackend do_free;

  nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](DomainKey d) { return do_alloc(d); },
    [&do_free](DomainKey d, Handle h) { do_free(d, h); }, allocator, per_frame);

  DomainKey domain { oxygen::graphics::ResourceViewType::kTexture_SRV,
    oxygen::graphics::DescriptorVisibility::kShaderVisible };

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
  per_frame.OnBeginFrame(oxygen::frame::Slot { 0 });
  do_alloc.free_list.push_back(idx.get());
  auto h2 = reuse.Allocate(domain);

  // Assert - Slot reused with incremented generation
  EXPECT_EQ(h2.ToBindlessHandle().get(), idx.get());
  EXPECT_EQ(h2.GenerationValue().get(), gen1 + 1);
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
  using namespace oxygen;

  // Arrange
  oxygen::graphics::detail::PerFrameResourceManager per_frame;
  FakeAllocator allocator;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](DomainKey d) { return do_alloc(d); },
    [&do_free](DomainKey d, Handle h) { do_free(d, h); }, allocator, per_frame);

  DomainKey domain { oxygen::graphics::ResourceViewType::kTexture_SRV,
    oxygen::graphics::DescriptorVisibility::kShaderVisible };

  // Act - Allocate single handle
  auto h = reuse.Allocate(domain);
  ASSERT_TRUE(h.IsValid());
  const auto idx = h.ToBindlessHandle().get();

  // Act - Spawn many threads attempting to release same handle
  constexpr int kThreads = 32;
  std::atomic<bool> start { false };
  std::vector<std::thread> workers;
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&reuse, &domain, &h, &start]() {
      while (!start.load(std::memory_order_acquire)) { }
      for (int i = 0; i < 1000; ++i) {
        reuse.Release(domain, h);
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (auto& w : workers)
    w.join();

  // Act - Process deferred frees
  per_frame.OnBeginFrame(oxygen::frame::Slot { 0 });

  // Assert - Only one deferred free should have been scheduled
  ASSERT_EQ(do_free.freed.size(), 1u);
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
  using namespace oxygen;

  // Arrange
  oxygen::graphics::detail::PerFrameResourceManager per_frame;
  FakeAllocator allocator;
  AllocateBackend do_alloc;
  FreeBackend do_free;

  nexus::FrameDrivenSlotReuse reuse(
    [&do_alloc](DomainKey d) { return do_alloc(d); },
    [&do_free](DomainKey d, Handle h) { do_free(d, h); }, allocator, per_frame);

  DomainKey domain { oxygen::graphics::ResourceViewType::kTexture_SRV,
    oxygen::graphics::DescriptorVisibility::kShaderVisible };

  constexpr int kThreads = 8;
  constexpr int kPerThread = 512;
  std::atomic<bool> start { false };

  // Act - Create worker threads for high-volume alloc/release
  std::vector<std::thread> workers;
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&reuse, &domain, &start]() {
      while (!start.load(std::memory_order_acquire)) { }
      for (int i = 0; i < kPerThread; ++i) {
        auto h = reuse.Allocate(domain);
        reuse.Release(domain, h);
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (auto& w : workers)
    w.join();

  // Act - Process deferred frees
  per_frame.OnBeginFrame(oxygen::frame::Slot { 0 });

  // Assert - All releases should be processed
  EXPECT_EQ(do_free.freed.size(), static_cast<size_t>(kThreads * kPerThread));
}

} // namespace
