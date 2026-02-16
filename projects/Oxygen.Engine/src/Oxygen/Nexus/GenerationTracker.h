//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::nexus {

//! Thread-safe generation tracker for bindless descriptor slot reuse detection.
/*!
 Manages generation counters for bindless resource slots to prevent stale
 descriptor access after resource deallocation. Each slot maintains an atomic
 generation counter that increments on resource release.

 ### Key Features

 - **Lazy Initialization**: Slots start uninitialized (0) and lazy-initialize
   to generation 1 on first access via either Load() or Bump().
 - **Thread-Safe Operations**: All operations use atomic memory ordering for
   concurrent allocator and reclamation paths.
 - **Zero-Based Invalid**: Generation 0 represents uninitialized/invalid slots,
   while valid generations start at 1.
 - **Dynamic Resizing**: Supports capacity changes while preserving existing
   generation values.

 ### Usage Patterns

 ```cpp
 using namespace oxygen::bindless;
 GenerationTracker tracker(Capacity{1024});

 // Allocate slot and get initial generation
 auto handle = Handle{42};
 auto gen = tracker.Load(handle);  // Returns 1 (lazy init)

 // Release resource and bump generation
 tracker.Bump(handle);
 auto new_gen = tracker.Load(handle);  // Returns 2
 ```

 ### Architecture Notes

 Generation tracking enables safe resource reuse by ensuring that stale
 handles can be detected when a resource slot is reallocated. The bindless
 rendering system combines the slot index and generation into handles that
 become invalid when the generation changes.

 @warning Accessing out-of-bounds indices returns 0 (invalid generation).
 @see oxygen::bindless::HeapIndex, DescriptorAllocator
*/
class GenerationTracker {
public:
  //! Initialize generation tracker with specified capacity.
  /*!
   Creates a generation table with the given capacity, initializing all slots
   to 0 (uninitialized state). Slots will lazy-initialize to generation 1 on
   first access.

   @param capacity Maximum number of bindless slots to track

   ### Performance Characteristics

   - Time Complexity: O(n) where n is capacity
   - Memory: capacity * sizeof(std::atomic<uint32_t>)
   - Optimization: Uses relaxed memory ordering for initialization

   ### Usage Examples

   ```cpp
   using namespace oxygen::bindless;
   GenerationTracker tracker(Capacity{1024});
   ```

   @warning capacity must be greater than 0 for meaningful operation.
   @see Load, Bump
  */
  explicit GenerationTracker(oxygen::bindless::Capacity capacity)
  {
    const auto u_capacity = capacity.get();
    size_ = u_capacity;
    table_.resize(u_capacity);
    // zero means "never initialized"; lazy init to 1 on first load
    for (std::size_t i = 0; i < size_; ++i) {
      table_[i].value.store(0U, std::memory_order_relaxed);
    }
  }

  //! Default constructor creates an empty tracker.
  GenerationTracker() = default;

  OXYGEN_MAKE_NON_COPYABLE(GenerationTracker)
  OXYGEN_DEFAULT_MOVABLE(GenerationTracker)
  ~GenerationTracker() = default;

  //! Load current generation value for the specified slot.
  /*!
   Retrieves the current generation value for a bindless slot with acquire
   memory ordering. Uninitialized slots (value 0) are lazy-initialized to
   generation 1 atomically.

   @param index Bindless handle index to query
   @return Current generation value as a strong type (>= 1 for valid slots,
     0 for out-of-bounds)

   ### Performance Characteristics

   - Time Complexity: O(1)
   - Memory: No allocation, one atomic load + optional store
   - Optimization: Uses acquire/release ordering for initialization safety

   ### Usage Examples

   ```cpp
   auto handle = oxygen::bindless::HeapIndex{42};
  auto generation = tracker.Load(handle);
  if (generation.get() == 0) {
     // Handle is out of bounds or invalid
   }
   ```

   @note Multiple concurrent Load() calls on uninitialized slots safely
         initialize to 1 (idempotent operation).
   @see Bump, Resize
  */
  [[nodiscard]] auto Load(oxygen::bindless::HeapIndex index) const noexcept
    -> bindless::Generation
  {
    const auto u_index = index.get();
    if (u_index >= size_) {
      return bindless::Generation { 0U };
    }
    uint32_t v = table_[u_index].value.load(std::memory_order_acquire);
    if (v == 0U) {
      // Lazily initialize to 1 only if the slot is still zero. Use
      // compare_exchange to avoid overwriting concurrent bumps which could
      // otherwise increase the generation (e.g., Bump() racing with Load()).
      uint32_t expected = 0U;
      table_[u_index].value.compare_exchange_strong(
        expected, 1U, std::memory_order_acq_rel, std::memory_order_acquire);
      // Return the up-to-date value (either observed bumped value or 1).
      return bindless::Generation { table_[u_index].value.load(
        std::memory_order_acquire) };
    }
    return bindless::Generation { v };
  }

  //! Increment generation value for resource reclamation.
  /*!
   Atomically increments the generation counter for a bindless slot using
   release memory ordering. Safe to call from resource reclamation paths
   without explicit synchronization.

   @param index Bindless handle index to increment

   ### Performance Characteristics

   - Time Complexity: O(1)
   - Memory: No allocation, one atomic fetch_add
   - Optimization: Uses release ordering for visibility to other threads

   ### Usage Examples

   ```cpp
   // Resource is being deallocated, bump generation
   tracker.Bump(oxygen::bindless::HeapIndex{slot_index});

   // Future allocations to this slot will see new generation
   auto new_gen = tracker.Load(oxygen::bindless::HeapIndex{slot_index});
   ```

   @note For uninitialized slots (0), this increments to 1. For initialized
         slots, this increments to the next generation value.
   @warning Out-of-bounds indices are silently ignored.
   @see Load
  */
  void Bump(oxygen::bindless::HeapIndex index) noexcept
  {
    const auto u_index = index.get();
    if (u_index >= size_) {
      return;
    }
    table_[u_index].value.fetch_add(1U, std::memory_order_release);
  }

  //! Resize generation table while preserving existing values.
  /*!
   Changes the tracker capacity, copying existing generation values when
   growing and initializing new slots to 0. When shrinking, values beyond
   the new capacity are discarded.

   @param capacity New maximum number of slots to track

   ### Performance Characteristics

   - Time Complexity: O(min(old_size, new_size)) for value copying
   - Memory: Allocates new capacity * sizeof(std::atomic<uint32_t>)
   - Optimization: No-op if capacity unchanged, uses relaxed ordering for copy

   ### Usage Examples

   ```cpp
   GenerationTracker tracker(oxygen::bindless::Capacity{512});

   // Later, need more capacity
   tracker.Resize(oxygen::bindless::Capacity{1024});

   // Existing generations preserved, new slots start at 0
   ```

   @note Existing generation values are preserved during growth operations.
   @warning Shrinking discards generation values beyond new capacity.
   @see Load, GenerationTracker constructor
  */
  void Resize(oxygen::bindless::Capacity capacity)
  {
    const auto u_capacity = capacity.get();
    if (u_capacity == size_) {
      return;
    }
    std::vector<AtomicGeneration> new_table;
    new_table.resize(u_capacity);
    // copy existing values (by load/store) and initialize new slots to 0
    const auto old_size = size_;
    for (std::size_t i = 0; i < u_capacity; ++i) {
      if (i < old_size) {
        const uint32_t val = table_[i].value.load(std::memory_order_relaxed);
        new_table[i].value.store(val, std::memory_order_relaxed);
      } else {
        new_table[i].value.store(0U, std::memory_order_relaxed);
      }
    }
    table_ = std::move(new_table);
    size_ = u_capacity;
  }

private:
  struct AtomicGeneration {
    mutable std::atomic<uint32_t> value { 0U };

    AtomicGeneration() = default;
    AtomicGeneration(const AtomicGeneration& other)
      : value(other.value.load(std::memory_order_relaxed))
    {
    }
    AtomicGeneration(AtomicGeneration&& other) noexcept
      : value(other.value.load(std::memory_order_relaxed))
    {
    }
    auto operator=(const AtomicGeneration& other) -> AtomicGeneration&
    {
      if (this != &other) {
        value.store(other.value.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
      }
      return *this;
    }
    auto operator=(AtomicGeneration&& other) noexcept -> AtomicGeneration&
    {
      if (this != &other) {
        value.store(other.value.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
      }
      return *this;
    }
    ~AtomicGeneration() = default;
  };

  std::vector<AtomicGeneration> table_;
  std::size_t size_ { 0 };
};

} // namespace oxygen::nexus
