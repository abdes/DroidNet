//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cassert>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Nexus/FrameDrivenSlotReuse.h>

using oxygen::nexus::DomainKey;
using oxygen::nexus::FrameDrivenSlotReuse;

/*!
 Construct the strategy with backend allocation/free functions and frame
 management infrastructure.

 @param allocate Backend function that allocates a bindless slot and returns
                 the absolute handle index
 @param free Backend function that releases a bindless slot
 @param allocator DescriptorAllocator used for domain mapping initialization
 @param per_frame PerFrameResourceManager for deferred cleanup operations

 ### Performance Characteristics

 - Time Complexity: O(1) construction cost
 - Memory: Initializes empty buffers; growth happens lazily on first allocation
 - Optimization: Buffers are sized on-demand to avoid pre-allocating large
   arrays

 ### Usage Examples

 ```cpp
 auto strategy = FrameDrivenSlotReuse(
     [&](DomainKey domain) { return backend.Allocate(domain); },
     [&](DomainKey domain, Handle h) { backend.Free(domain, h); },
     allocator,
     per_frame_manager
 );
 ```

 @note The pending_flags_ buffer is lazily allocated when the first slot is
       requested
 @see Allocate, Release
*/
FrameDrivenSlotReuse::FrameDrivenSlotReuse(AllocateFn allocate, FreeFn free,
  oxygen::graphics::DescriptorAllocator& allocator,
  oxygen::graphics::detail::PerFrameResourceManager& per_frame)
  : allocate_(std::move(allocate))
  , free_(std::move(free))
  , per_frame_(per_frame)
  , generations_(oxygen::bindless::Capacity { 0 })
  , mapper_(allocator /* capture domains lazily at call-sites if needed */)
{
  // pending_flags_ will be lazily sized when we first allocate
}

/*!
 Allocate a bindless slot and return a VersionedBindlessHandle stamped with
 the slot's current generation.

 @param domain DomainKey used by the backend allocate callable
 @return VersionedBindlessHandle containing absolute index and current
         generation

 ### Performance Characteristics

 - Time Complexity: O(1) amortized; O(n) when buffer growth is required
 - Memory: May trigger buffer reallocation if slot index exceeds current
   capacity
 - Optimization: Uses lock-free generation loading after ensuring capacity

 ### Usage Examples

 ```cpp
 auto handle = strategy.Allocate(texture_domain);
 if (handle.IsValid()) {
     // Use handle for resource binding...
 }
 ```

 @note The returned handle is immediately valid and can be used for resource
       binding operations
 @see Release, IsHandleCurrent, EnsureCapacity_
*/
auto FrameDrivenSlotReuse::Allocate(DomainKey domain)
  -> oxygen::VersionedBindlessHandle
{
  const auto idx = allocate_(domain);
  EnsureCapacity_(idx);
  const uint32_t gen = generations_.Load(idx);
  return { idx, oxygen::VersionedBindlessHandle::Generation { gen } };
}

/*!
 Release a previously-allocated VersionedBindlessHandle with deferred
 cleanup and generation bumping for stale handle detection.

 @param domain DomainKey for the backend free callable
 @param h The VersionedBindlessHandle to release; invalid handles are ignored

 ### Performance Characteristics

 - Time Complexity: O(1) for the fast path; RegisterDeferredAction cost is
   backend-dependent
 - Memory: No immediate allocation; deferred action uses lambda capture
 - Optimization: Uses compare-and-swap to prevent double-release without
   blocking

 ### Behavior Details

 The method performs immediate validation and duplicate-release protection,
 then schedules deferred cleanup that will:
 1. Bump the slot's generation counter (using release ordering)
 2. Invoke the backend free function
 3. Clear the pending flag (using release ordering)

 ### Usage Examples

 ```cpp
 // Normal release
 strategy.Release(domain, handle);

 // Safe to call multiple times
 strategy.Release(domain, handle); // No-op on duplicate

 // Invalid handles are ignored
 strategy.Release(domain, {}); // No-op
 ```

 @note The resize mutex is briefly acquired to protect pointer stability
       during flag manipulation
 @warning Once released, the handle should not be used for resource access
 @see Allocate, IsHandleCurrent, OnBeginFrame
*/
void FrameDrivenSlotReuse::Release(
  DomainKey domain, oxygen::VersionedBindlessHandle h)
{
  if (!h.IsValid()) {
    return;
  }
  const auto idx = h.ToBindlessHandle();
  EnsureCapacity_(idx);
  const auto i = static_cast<std::size_t>(idx.get());
  uint8_t expected = 0;
  // Protect pointer stability while we perform the CAS. EnsureCapacity_
  // may reallocate and swap the buffer under the resize mutex; take the
  // same mutex briefly to avoid a use-after-free if a concurrent resize
  // happens during the CAS.
  {
    std::lock_guard<std::mutex> lg(resize_mutex_);
    if (!pending_flags_) {
      // Should not happen because EnsureCapacity_ was called above, but
      // guard for safety.
      return;
    }
    // Use acquire-release on the CAS so we synchronize with the later
    // store(0, release) in the deferred reclamation action. This ensures
    // that once we see pending==1, we also see any writes performed before
    // the CAS by the releasing thread.
    if (!pending_flags_[i].compare_exchange_strong(
          expected, 1, std::memory_order_acq_rel)) {
      // Already pending; ignore duplicate release.
      return;
    }
  }

  per_frame_.RegisterDeferredAction([this, domain, idx]() {
    // Bump generation (publication) then free.
    // Bump the generation with the GenerationTracker which uses
    // acquire/release semantics internally to publish the new generation.
    generations_.Bump(idx);
    // After publication, call backend free.
    free_(domain, idx);
    // Clear pending flag after actual reclamation using release order so
    // any observer that sees pending==0 with acquire will also see the
    // effects of the reclamation (but note callers generally synchronize
    // via generation checks).
    const auto j = static_cast<std::size_t>(idx.get());
    // Protect pointer stability while writing back the flag. The resize
    // mutex is used only briefly here to ensure the buffer isn't replaced
    // concurrently (which could otherwise cause a use-after-free).
    {
      std::lock_guard<std::mutex> lg(resize_mutex_);
      if (pending_flags_) {
        pending_flags_[j].store(0u, std::memory_order_release);
      }
    }
  });
}

/*!
 Check whether a VersionedBindlessHandle's recorded generation matches the
 current generation for its slot, indicating the handle is still valid.

 @param h Handle to validate
 @return true if the handle's generation equals the current slot generation;
         false if handle is invalid or generations differ

 ### Performance Characteristics

 - Time Complexity: O(1) lock-free operation
 - Memory: No allocation; uses atomic load with acquire ordering
 - Optimization: Early return for invalid handles avoids generation lookup

 ### Usage Examples

 ```cpp
 if (strategy.IsHandleCurrent(handle)) {
     // Safe to use handle for resource access
     UseResource(handle);
 } else {
     // Handle is stale, must re-allocate
     handle = strategy.Allocate(domain);
 }
 ```

 @note This check provides CPU-side validation before GPU operations
 @see Allocate, Release, GenerationTracker::Load
*/
auto FrameDrivenSlotReuse::IsHandleCurrent(
  oxygen::VersionedBindlessHandle h) const noexcept -> bool
{
  if (!h.IsValid()) {
    return false;
  }
  const auto idx = h.ToBindlessHandle();
  const uint32_t current = generations_.Load(idx);
  return current == h.GenerationValue().get();
}

/*!
 Forward frame-begin event to the PerFrameResourceManager to execute
 deferred cleanup actions scheduled for the specified frame slot.

 @param fi Frame slot identifier passed to the per-frame manager

 ### Performance Characteristics

 - Time Complexity: O(k) where k is the number of deferred actions for this
   frame slot
 - Memory: No allocation in this method; cleanup actions may deallocate
 - Optimization: Batch execution of all deferred actions for the frame

 ### Usage Examples

 ```cpp
 // Called once per frame by the render coordinator
 for (auto slot = frame::Slot{0}; slot < frame::kMaxFramesInFlight; ++slot) {
     strategy.OnBeginFrame(slot);
 }
 ```

 @note This triggers generation bumping and backend resource cleanup for
       handles released in previous frames
 @see Release, PerFrameResourceManager::OnBeginFrame
*/
void FrameDrivenSlotReuse::OnBeginFrame(oxygen::frame::Slot fi)
{
  per_frame_.OnBeginFrame(fi);
}

/*!
 Ensure internal buffers have capacity for the provided bindless index,
 growing and copying existing state if necessary.

 @param idx Bindless handle index that must be addressable by internal buffers

 ### Performance Characteristics

 - Time Complexity: O(1) fast path when capacity is sufficient; O(n) when
   growth occurs where n is the new buffer size
 - Memory: Allocates new atomic<uint8_t> array and copies existing flags
 - Optimization: Uses relaxed memory ordering for copying since resize is
   protected by mutex

 ### Implementation Details

 When growth occurs:
 1. Calculates required size based on the input index
 2. Allocates new pending flags buffer with relaxed atomic initialization
 3. Copies existing flags using relaxed ordering under mutex protection
 4. Resizes the GenerationTracker to match the new capacity
 5. Swaps buffers atomically

 ### Usage Examples

 ```cpp
 // Called internally before accessing slot arrays
 EnsureCapacity_(handle.ToBindlessHandle());
 // Now safe to access pending_flags_[index] and generations_[index]
 ```

 @note The resize mutex provides synchronization between concurrent resize
       operations and flag access
 @see Allocate, Release
*/
void FrameDrivenSlotReuse::EnsureCapacity_(oxygen::bindless::Handle idx)
{
  const auto needed = static_cast<std::size_t>(idx.get()) + 1u;
  if (pending_size_ >= needed) {
    return;
  }
  std::lock_guard<std::mutex> lg(resize_mutex_);
  if (pending_size_ < needed) {
    const auto old = pending_size_;
    auto new_size = needed;
    auto new_buf = std::make_unique<std::atomic<uint8_t>[]>(new_size);
    // Copy existing pending flags. We can copy with relaxed ordering
    // because the flags are only used as a simple in-flight marker and
    // we already hold the resize mutex to prevent concurrent resizes.
    for (std::size_t i = 0; i < new_size; ++i) {
      if (i < old && pending_flags_) {
        new_buf[i].store(pending_flags_[i].load(std::memory_order_relaxed),
          std::memory_order_relaxed);
      } else {
        new_buf[i].store(0u, std::memory_order_relaxed);
      }
    }
    pending_flags_.swap(new_buf);
    pending_size_ = new_size;
    // Grow generations to at least this index
    generations_.Resize(
      oxygen::bindless::Capacity { static_cast<uint32_t>(pending_size_) });
  }
}
