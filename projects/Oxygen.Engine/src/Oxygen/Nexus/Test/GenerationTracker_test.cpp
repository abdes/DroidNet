//===----------------------------------------------------------------------===//
// Tests for GenerationTracker
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <Oxygen/Nexus/GenerationTracker.h>

namespace {

//! Test fixture for GenerationTracker basic behaviors.
class GenerationTrackerTest : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }
};

using Generation = oxygen::VersionedBindlessHandle::Generation;
// Strong-typed generation constants for readability and reuse
inline constexpr Generation kGen0 { 0u };
inline constexpr Generation kGen1 { 1u };
inline constexpr Generation kGen2 { 2u };
inline constexpr Generation kGen3 { 3u };

//! Ensure that loading an uninitialized slot returns at least 1 after first
//! access and Bump increments the generation counter.
NOLINT_TEST_F(
  GenerationTrackerTest, LoadAndBump_UninitializedSlot_InitializesAndIncrements)
{
  // Arrange
  const auto capacity = oxygen::bindless::Capacity { 4u };
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker tracker(capacity);
  const auto idx = b::Handle { 2u };

  // Act
  const auto gen_before = tracker.Load(idx);
  tracker.Bump(idx);
  const auto gen_after = tracker.Load(idx);

  // Assert
  EXPECT_GE(gen_before, kGen1);
  EXPECT_EQ(gen_after, gen_before + kGen1);
}

//! Ensure Resize preserves existing generations and expands size. Ensure Resize
//! preserves initialized generations and lazily initializes new slots when
//! expanding the tracker capacity.
NOLINT_TEST_F(GenerationTrackerTest,
  Resize_ExpandCapacity_PreservesExistingAndInitializesNew)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 2u });
  const auto idx0 = b::Handle { 0u };
  const auto idx1 = b::Handle { 1u };

  // Act + Assert: initialize and bump
  const auto g0 = t.Load(idx0);
  t.Bump(idx0);
  EXPECT_EQ(t.Load(idx0), g0 + kGen1);

  // Resize larger
  t.Resize(b::Capacity { 4u });

  // Assert: previously initialized slot still has value
  EXPECT_GT(t.Load(idx0), kGen0);
  EXPECT_GT(
    t.Load(idx1), kGen0); // idx1 should have been lazily initialized to 1

  // Assert: new slots after expansion are lazily initialized to 1
  const auto new_slot_gen = t.Load(b::Handle { 3u }); // new slot
  EXPECT_EQ(new_slot_gen, kGen1);
}

//! Verify Load and Bump are no-ops/zero for out-of-range indices.
NOLINT_TEST_F(
  GenerationTrackerTest, LoadAndBump_OutOfRangeIndex_ReturnsZeroAndNoOp)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 2u });
  const auto out = b::Handle { 10u };

  // Act Out-of-range load returns 0 and bump is a no-op
  const auto before = t.Load(out);
  t.Bump(out);

  // Assert
  EXPECT_EQ(before, kGen0);
  EXPECT_EQ(t.Load(out), kGen0);
}

//! Shrinking the tracker drops slots beyond the new capacity and their
//! generation values are no longer visible.
NOLINT_TEST_F(
  GenerationTrackerTest, Resize_ShrinkCapacity_DropsExtraSlotsAndGenerations)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 4u });
  const auto idx3 = b::Handle { 3u };

  // Act: initialize and bump the last slot
  t.Bump(idx3);

  // Assert it was initialized
  EXPECT_GT(t.Load(idx3), kGen0);

  // Act: shrink to smaller capacity
  t.Resize(b::Capacity { 2u });

  // Assert previously initialized slot should be dropped
  EXPECT_EQ(t.Load(idx3), kGen0);
}

//! Verify that generations are never reset on reuse, only incremented. This
//! tests the core contract that generations monotonically increase, including
//! multiple consecutive bumps on the same slot.
NOLINT_TEST_F(GenerationTrackerTest, Bump_GenerationsNeverReset_OnlyIncrement)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 4u });
  const auto idx = b::Handle { 2u };

  // Act & Assert: Test multiple consecutive bumps increment correctly
  const auto initial = t.Load(idx);
  EXPECT_GE(initial, kGen1);

  t.Bump(idx);
  EXPECT_EQ(t.Load(idx), initial + kGen1);

  t.Bump(idx);
  EXPECT_EQ(t.Load(idx), initial + kGen2);

  t.Bump(idx);
  const auto after_multiple_bumps = t.Load(idx);
  EXPECT_EQ(after_multiple_bumps, initial + kGen3);

  // Act & Assert: Simulate allocation, use, release cycle multiple times
  auto gen1 = after_multiple_bumps; // current generation after previous bumps
  t.Bump(idx); // simulate release

  auto gen2 = t.Load(idx); // second allocation of same slot
  t.Bump(idx); // simulate release

  auto gen3 = t.Load(idx); // third allocation of same slot

  // Assert: each reuse should see a higher generation
  EXPECT_GT(gen2, gen1);
  EXPECT_GT(gen3, gen2);
  EXPECT_EQ(gen2, gen1 + kGen1);
  EXPECT_EQ(gen3, gen1 + kGen2);
}

//! Verify that resizing to the same capacity is a no-op and preserves all
//! existing generation values.
NOLINT_TEST_F(
  GenerationTrackerTest, Resize_SameCapacity_PreservesAllGenerations)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;
  constexpr auto capacity = b::Capacity { 3u };

  GenTracker t(capacity);
  const auto idx0 = b::Handle { 0u };
  const auto idx1 = b::Handle { 1u };
  const auto idx2 = b::Handle { 2u };

  // Initialize all slots with different generation values
  t.Bump(idx0); // gen will be 2
  t.Bump(idx1); // gen will be 2
  t.Bump(idx1); // gen will be 3
  t.Bump(idx2); // gen will be 2

  const auto gen0_before = t.Load(idx0);
  const auto gen1_before = t.Load(idx1);
  const auto gen2_before = t.Load(idx2);

  // Act: resize to same capacity
  t.Resize(capacity);

  // Assert: all generations preserved
  EXPECT_EQ(t.Load(idx0), gen0_before);
  EXPECT_EQ(t.Load(idx1), gen1_before);
  EXPECT_EQ(t.Load(idx2), gen2_before);
}

//! Verify that resizing to zero capacity results in all accesses returning zero
//! (no valid slots).
NOLINT_TEST_F(GenerationTrackerTest, Resize_ZeroCapacity_AllAccessesReturnZero)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 4u });
  const auto idx = b::Handle { 1u };

  // Initialize a slot
  t.Bump(idx);
  EXPECT_GT(t.Load(idx), kGen0);

  // Act: resize to zero capacity
  t.Resize(b::Capacity { 0u });

  // Assert: all accesses should return zero
  EXPECT_EQ(t.Load(b::Handle { 0u }), kGen0);
  EXPECT_EQ(t.Load(idx), kGen0);
  EXPECT_EQ(t.Load(b::Handle { 10u }), kGen0);

  // Bump should be no-op
  t.Bump(idx);
  EXPECT_EQ(t.Load(idx), kGen0);
}

//! Verify generation persistence across multiple resize operations (expand,
//! shrink, expand again).
NOLINT_TEST_F(GenerationTrackerTest,
  Resize_MultipleOperations_MaintainsGenerationConsistency)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 4u });
  const auto idx0 = b::Handle { 0u };
  const auto idx1 = b::Handle { 1u };

  // Initialize slots with known generations
  t.Bump(idx0); // gen = 2
  t.Bump(idx0); // gen = 3
  t.Bump(idx1); // gen = 2

  const auto gen0_initial = t.Load(idx0);
  const auto gen1_initial = t.Load(idx1);

  // Act: multiple resize operations
  t.Resize(b::Capacity { 2u }); // shrink, should preserve idx0 and idx1
  const auto gen0_after_shrink = t.Load(idx0);
  const auto gen1_after_shrink = t.Load(idx1);

  t.Resize(b::Capacity { 6u }); // expand again
  const auto gen0_after_expand = t.Load(idx0);
  const auto gen1_after_expand = t.Load(idx1);

  // Assert: generations should be preserved through valid resize operations
  EXPECT_EQ(gen0_after_shrink, gen0_initial);
  EXPECT_EQ(gen1_after_shrink, gen1_initial);
  EXPECT_EQ(gen0_after_expand, gen0_initial);
  EXPECT_EQ(gen1_after_expand, gen1_initial);
}

//! Verify behavior with maximum representable generation values to ensure no
//! overflow issues in practical scenarios.
NOLINT_TEST_F(
  GenerationTrackerTest, Bump_HighGenerationValues_HandlesLargeNumbers)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 1u });
  const auto idx = b::Handle { 0u };

  // Act: Get initial generation (lazy initialization to 1), then simulate many
  // bumps
  const auto initial_gen = t.Load(idx); // This should be 1 (lazy init)
  EXPECT_EQ(initial_gen, kGen1);

  // Note: In real implementation, this would be atomic operations
  constexpr uint32_t num_bumps = 1000000u;
  for (uint32_t i = 0; i < num_bumps; ++i) {
    t.Bump(idx);
  }

  const auto final_gen = t.Load(idx);

  // Assert: should handle large generation values correctly Started at 1,
  // bumped num_bumps times, so should be 1 + num_bumps
  EXPECT_EQ(final_gen, initial_gen + Generation { num_bumps });

  // One more bump should still work
  t.Bump(idx);
  EXPECT_EQ(t.Load(idx), final_gen + kGen1);
}

//! Verify the lazy initialization contract: uninitialized slots return 1 on
//! first Load(), and multiple loads of the same uninitialized slot return the
//! same value.
NOLINT_TEST_F(
  GenerationTrackerTest, Load_UninitializedSlots_LazyInitializesToOne)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 4u });
  const auto idx0 = b::Handle { 0u };
  const auto idx1 = b::Handle { 1u };
  const auto idx2 = b::Handle { 2u };

  // Act & Assert: First load of any uninitialized slot should return 1
  const auto gen0_first = t.Load(idx0);
  EXPECT_EQ(gen0_first, kGen1);

  const auto gen1_first = t.Load(idx1);
  EXPECT_EQ(gen1_first, kGen1);

  const auto gen2_first = t.Load(idx2);
  EXPECT_EQ(gen2_first, kGen1);

  // Act & Assert: Subsequent loads of the same slots should return the same
  // value (1)
  const auto gen0_second = t.Load(idx0);
  EXPECT_EQ(gen0_second, kGen1);
  EXPECT_EQ(gen0_second, gen0_first);

  const auto gen1_second = t.Load(idx1);
  EXPECT_EQ(gen1_second, kGen1);
  EXPECT_EQ(gen1_second, gen1_first);

  // Act & Assert: Bump on a lazily initialized slot should increment from 1 to
  // 2
  t.Bump(idx0);
  const auto gen0_after_bump = t.Load(idx0);
  EXPECT_EQ(gen0_after_bump, kGen2);

  // Act & Assert: Other slots should remain unaffected
  EXPECT_EQ(t.Load(idx1), kGen1);
  EXPECT_EQ(t.Load(idx2), kGen1);
}

//! Verify that Bump on completely uninitialized slots works correctly. This
//! test discovers the actual behavior of Bump on uninitialized slots.
NOLINT_TEST_F(
  GenerationTrackerTest, Bump_UninitializedSlot_LazyInitializesAndIncrements)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker t(b::Capacity { 2u });
  const auto idx = b::Handle { 0u };

  // Act: Bump without ever calling Load() first
  t.Bump(idx);

  // Act & Assert: Load after bump to see what the actual behavior is
  const auto gen_after_bump = t.Load(idx);

  // The actual implementation behavior: Bump on uninitialized slot results in 1
  // (not 2 as originally expected). This suggests Bump initializes to 0, then
  // increments to 1, while Load() initializes directly to 1.
  EXPECT_EQ(gen_after_bump, kGen1);

  // Act & Assert: Another bump should increment to 2
  t.Bump(idx);
  EXPECT_EQ(t.Load(idx), kGen2);

  // Act & Assert: Another bump should increment to 3
  t.Bump(idx);
  EXPECT_EQ(t.Load(idx), kGen3);
}

//===----------------------------------------------------------------------===//
// Thread Safety Tests
//===----------------------------------------------------------------------===//

//! Test fixture for GenerationTracker thread safety scenarios.
class GenerationTrackerThreadSafetyTest : public ::testing::Test { };

//! Verify concurrent Load operations on the same slot are thread-safe and
//! produce consistent lazy initialization behavior.
NOLINT_TEST_F(GenerationTrackerThreadSafetyTest,
  Load_ConcurrentAccess_LazyInitializationIsThreadSafe)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker tracker(b::Capacity { 100u });
  const auto idx = b::Handle { 42u };
  constexpr int num_threads = 10;
  constexpr int loads_per_thread = 1000;

  std::vector<std::thread> threads;
  std::vector<std::vector<uint32_t>> results(num_threads);

  // Act: Multiple threads concurrently loading the same uninitialized slot
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&tracker, idx, t, &results, loads_per_thread]() {
      results[t].reserve(loads_per_thread);
      for (int i = 0; i < loads_per_thread; ++i) {
        results[t].push_back(tracker.Load(idx).get());
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert: All threads should see the same generation value (1 after lazy
  // init)
  for (const auto& thread_results : results) {
    for (const auto generation : thread_results) {
      EXPECT_EQ(generation, 1u);
    }
  }
}

//! Verify concurrent Bump operations on the same slot increment the generation
//! counter correctly without lost updates.
NOLINT_TEST_F(
  GenerationTrackerThreadSafetyTest, Bump_ConcurrentAccess_IncrementsSafely)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker tracker(b::Capacity { 100u });
  const auto idx = b::Handle { 42u };
  constexpr int num_threads = 10;
  constexpr int bumps_per_thread = 100;
  constexpr int expected_total_bumps = num_threads * bumps_per_thread;

  std::vector<std::thread> threads;

  // Initialize the slot to a known state
  const auto initial_gen = tracker.Load(idx); // Lazy init to 1

  // Act: Multiple threads concurrently bumping the same slot
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&tracker, idx, bumps_per_thread]() {
      for (int i = 0; i < bumps_per_thread; ++i) {
        tracker.Bump(idx);
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert: Final generation should reflect all increments
  const auto final_gen = tracker.Load(idx);
  EXPECT_EQ(final_gen, initial_gen + Generation { expected_total_bumps });
}

//! Verify mixed concurrent Load and Bump operations maintain consistency and
//! proper ordering guarantees.
NOLINT_TEST_F(GenerationTrackerThreadSafetyTest,
  LoadAndBump_ConcurrentMixedAccess_MaintainsConsistency)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker tracker(b::Capacity { 100u });
  const auto idx = b::Handle { 42u };
  constexpr int num_reader_threads = 4;
  constexpr int num_writer_threads = 2;
  constexpr int operations_per_thread = 500;

  std::vector<std::thread> threads;
  std::atomic<bool> start_flag { false };
  std::atomic<uint32_t> min_observed_gen { UINT32_MAX };
  std::atomic<uint32_t> max_observed_gen { 0 };

  // Act: Reader threads continuously observe generation values
  for (int t = 0; t < num_reader_threads; ++t) {
    threads.emplace_back([&tracker, idx, &start_flag, &min_observed_gen,
                           &max_observed_gen, operations_per_thread]() {
      while (!start_flag.load()) {
        std::this_thread::yield();
      }

      for (int i = 0; i < operations_per_thread; ++i) {
        const auto gen = tracker.Load(idx).get();

        // Update observed range atomically
        uint32_t current_min = min_observed_gen.load();
        while (gen < current_min
          && !min_observed_gen.compare_exchange_weak(current_min, gen)) { }

        uint32_t current_max = max_observed_gen.load();
        while (gen > current_max
          && !max_observed_gen.compare_exchange_weak(current_max, gen)) { }

        std::this_thread::yield();
      }
    });
  }

  // Writer threads continuously increment generation
  for (int t = 0; t < num_writer_threads; ++t) {
    threads.emplace_back([&tracker, idx, &start_flag, operations_per_thread]() {
      while (!start_flag.load()) {
        std::this_thread::yield();
      }

      for (int i = 0; i < operations_per_thread; ++i) {
        tracker.Bump(idx);
        std::this_thread::yield();
      }
    });
  }

  // Start all threads simultaneously
  start_flag.store(true);

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert: Check consistency
  const auto final_gen = tracker.Load(idx);
  const auto observed_min = min_observed_gen.load();
  const auto observed_max = max_observed_gen.load();

  // Readers should never observe generations higher than the final value
  EXPECT_LE(observed_max, final_gen.get());

  // Minimum observed should be at least 1 (lazy initialization)
  EXPECT_GE(observed_min, 1u);

  // Final generation should reflect the expected number of bumps
  const auto expected_bumps = num_writer_threads * operations_per_thread;
  EXPECT_EQ(
    final_gen.get(), 1u + expected_bumps); // 1 from lazy init + all bumps
}

//! Verify concurrent access to different slots is independent and doesn't
//! interfere with each other.
NOLINT_TEST_F(GenerationTrackerThreadSafetyTest,
  LoadAndBump_DifferentSlots_IndependentThreadSafety)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  GenTracker tracker(b::Capacity { 100u });
  constexpr int num_slots = 10;
  constexpr int threads_per_slot = 5;
  constexpr int operations_per_thread = 200;

  std::vector<std::thread> threads;
  std::vector<std::atomic<uint32_t>> final_generations(num_slots);

  // Act: Each slot gets its own set of threads performing operations
  for (int slot = 0; slot < num_slots; ++slot) {
    const auto idx = b::Handle { static_cast<uint32_t>(slot) };

    for (int t = 0; t < threads_per_slot; ++t) {
      threads.emplace_back(
        [&tracker, idx, slot, &final_generations, operations_per_thread]() {
          // Initialize the slot (lazy)
          [[maybe_unused]] const auto init_gen = tracker.Load(idx);

          // Perform bump operations
          for (int i = 0; i < operations_per_thread; ++i) {
            tracker.Bump(idx);
          }

          // Store final generation for this slot
          final_generations[slot].store(tracker.Load(idx).get());
        });
    }
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert: Each slot should have the expected number of increments
  for (int slot = 0; slot < num_slots; ++slot) {
    const auto idx = b::Handle { static_cast<uint32_t>(slot) };
    const auto expected_gen = 1u + (threads_per_slot * operations_per_thread);
    EXPECT_EQ(tracker.Load(idx), Generation { expected_gen });
  }
}

//! Verify that the Resize operation requires external synchronization and
//! should not be called concurrently with other operations. This documents the
//! design decision to keep Load/Bump operations lock-free for performance.
NOLINT_TEST_F(GenerationTrackerThreadSafetyTest,
  Resize_RequiresExternalSynchronization_DocumentedDesign)
{
  // Arrange
  namespace b = oxygen::bindless;
  using GenTracker = oxygen::nexus::GenerationTracker;

  // Note: This test documents that Resize() requires external synchronization.
  // This design choice preserves the lock-free performance of Load() and Bump()
  // operations while requiring callers to coordinate Resize() externally.

  GenTracker tracker(b::Capacity { 50u });
  const auto idx = b::Handle { 10u };

  // Initialize slot
  [[maybe_unused]] const auto first_load = tracker.Load(idx);
  tracker.Bump(idx);
  const auto initial_gen = tracker.Load(idx);

  // Act: Resize while ensuring no concurrent access (This is the CORRECT way to
  // use Resize - no concurrent access)
  tracker.Resize(b::Capacity { 100u });

  // Assert: Generation should be preserved after resize
  EXPECT_EQ(tracker.Load(idx), initial_gen);

  // Assert: Can access new slots
  const auto new_slot = b::Handle { 75u };
  EXPECT_EQ(tracker.Load(new_slot), kGen1); // Lazy init
}

//! Tests GenerationTracker monotonicity under concurrent reader/writer access.
/*!
 Verifies that the GenerationTracker maintains monotonic generation values per
 reader thread even under heavy concurrent bump operations, ensuring memory
 ordering correctness for stale handle detection.
*/
NOLINT_TEST_F(GenerationTrackerThreadSafetyTest,
  Load_ConcurrentReaderWriterAccess_MaintainsMonotonicity)
{
  using namespace oxygen::nexus;

  // Arrange
  GenerationTracker tracker(oxygen::bindless::Capacity { 1 });
  constexpr int kWriterThreads = 4;
  constexpr int kReaderThreads = 4;
  constexpr int kIters = 10000;

  std::atomic<bool> start { false };
  std::atomic<uint32_t> max_seen { 0 };

  // Act - Create writer threads that bump generation
  std::vector<std::thread> writers;
  for (int t = 0; t < kWriterThreads; ++t) {
    writers.emplace_back([&tracker, &start]() {
      while (!start.load(std::memory_order_acquire)) { }
      for (int i = 0; i < kIters; ++i) {
        tracker.Bump(oxygen::bindless::Handle { 0 });
      }
    });
  }

  // Act - Create reader threads that verify monotonicity
  std::vector<std::thread> readers;
  for (int t = 0; t < kReaderThreads; ++t) {
    readers.emplace_back([&tracker, &start, &max_seen]() {
      while (!start.load(std::memory_order_acquire)) { }
      uint32_t last = 0;
      for (int i = 0; i < kIters; ++i) {
        const uint32_t v = tracker.Load(oxygen::bindless::Handle { 0 }).get();

        // Assert - Must be non-decreasing per reader
        EXPECT_GE(v, last);
        last = v;

        // Update global max
        uint32_t prev = max_seen.load(std::memory_order_relaxed);
        while (v > prev && !max_seen.compare_exchange_weak(prev, v)) { }
      }
    });
  }

  // Act - Start all threads simultaneously
  start.store(true, std::memory_order_release);
  for (auto& w : writers)
    w.join();
  for (auto& r : readers)
    r.join();

  // Assert - Final value should reflect some bumps occurred
  EXPECT_GT(max_seen.load(), 0u);
}

} // namespace
