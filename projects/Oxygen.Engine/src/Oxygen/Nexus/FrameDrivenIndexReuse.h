//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <functional>

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Nexus/GenerationTracker.h>

namespace oxygen::nexus {

//! Concept defining types that can serve as indices for reuse strategies.
/*!
 Types must be integral, convertible to size_t (for array indexing),
 and equality comparable.
*/
template <typename T>
concept IndexLike = std::integral<T> || (requires(T t) {
  { t.get() } -> std::convertible_to<std::size_t>;
} && std::equality_comparable<T>);

namespace detail {
  template <typename T> constexpr auto GetIndexValue(T idx) -> std::size_t
  {
    if constexpr (std::integral<T>) {
      return static_cast<std::size_t>(idx);
    } else {
      return static_cast<std::size_t>(idx.get());
    }
  }
} // namespace detail

//! A handle that carries a generation stamp for verification.
/*!
 Similar to VersionedBindlessHandle but generic for any IndexType.
*/
template <IndexLike IndexType> struct VersionedIndex {
  IndexType index {};
  oxygen::bindless::Generation generation { 0 };

  auto operator<=>(const VersionedIndex&) const = default;
};

//! Generic frame-driven index reuse strategy.
/*!
 Manages the lifecycle of reusable indices (slots) with:
 - Generation tracking for stale reference detection
 - Deferred reclamation via DeferredReclaimer
 - Thread-safe state usage (allocation/release)

 This class is the generic core logic, decoupled from specific resource types.
 Usage:
   1. Initialize with a reclamation backend and a callback for when indices
      are finally recycled.
   2. When allocating a slot, call `ActivateSlot(index)`.
   3. When freeing a slot, call `Release(handle, context)`.
   4. Call `OnBeginFrame(slot)` to process deferred reclamations for that slot.

 @tparam IndexType The type of index being managed (e.g. uint32_t,
         BindlessHeapIndex).
 @tparam ContextType Optional context data passed to the recycle callback (e.g.
         DomainKey). Defaults to std::monostate.
*/
template <IndexLike IndexType, typename ContextType = std::monostate>
class FrameDrivenIndexReuse {
public:
  using RecycleFn = std::function<void(IndexType, ContextType)>;
  struct TelemetrySnapshot {
    uint64_t allocate_calls { 0 };
    uint64_t release_calls { 0 };
    uint64_t stale_reject_count { 0 };
    uint64_t duplicate_reject_count { 0 };
    uint64_t reclaimed_count { 0 };
    uint64_t pending_count { 0 };
  };

  //! Construct with reclaimer and recycle callback.
  /*!
   @param reclaimer Reference to the per-frame deferred action manager.
   @param on_recycle Callback invoked when an index is safe to reuse.
  */
  FrameDrivenIndexReuse(oxygen::graphics::detail::DeferredReclaimer& reclaimer,
    RecycleFn on_recycle)
    : reclaimer_(&reclaimer)
    , on_recycle_(std::move(on_recycle))
  {
  }

  // Destructor needed for correct cleanup
  ~FrameDrivenIndexReuse() = default;

  OXYGEN_MAKE_NON_COPYABLE(FrameDrivenIndexReuse)
  OXYGEN_DEFAULT_MOVABLE(FrameDrivenIndexReuse)

  //! Mark an index as active and return a versioned handle.
  /*!
   Call this typically after obtaining a fresh index from a free-list.
   @param index The index being allocated.
   @return A versioned handle containing the index and its current generation.
  */
  auto ActivateSlot(IndexType index) -> VersionedIndex<IndexType>
  {
    telemetry_->allocate_calls.fetch_add(1, std::memory_order_relaxed);

    const auto raw_index = detail::GetIndexValue(index);
    EnsureCapacity_(raw_index);

    // Load current generation under shared lock to avoid racing with resize.
    std::shared_lock lock(storage_mutex_);
    const auto gen = generations_.Load(
      oxygen::bindless::HeapIndex { static_cast<uint32_t>(raw_index) });

    return VersionedIndex<IndexType> { index, gen };
  }

  //! Schedule an index for deferred release with context.
  /*!
   Bumps the generation immediately to invalidate current handles, then
   schedules the recycle callback to run when the current frame slot cycles
   back.
   @param handle The versioned handle to release.
   @param context Context data to pass to the recycle callback.
  */
  auto Release(VersionedIndex<IndexType> handle, ContextType context) -> void
  {
    telemetry_->release_calls.fetch_add(1, std::memory_order_relaxed);

    if (!IsHandleCurrent(handle)) {
      // Stale handle or double release - ignore.
      telemetry_->stale_reject_count.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    const auto raw_index = detail::GetIndexValue(handle.index);

    // CAS loop to set pending flag.
    // Prevents multiple threads from scheduling the same slot for release
    // in the same frame/lifecycle period.
    {
      std::shared_lock lock(storage_mutex_);
      // Re-check bounds under lock just in case
      if (raw_index >= pending_size_) {
        return;
      }

      uint8_t expected = 0;
      if (!pending_flags_[raw_index].value.compare_exchange_strong(
            expected, 1, std::memory_order_acq_rel)) {
        // Already pending release.
        telemetry_->duplicate_reject_count.fetch_add(
          1, std::memory_order_relaxed);
        return;
      }
    }
    telemetry_->pending_count.fetch_add(1, std::memory_order_relaxed);

    // Bump generation immediately to invalidate outstanding handles.
    // Keep shared lock while bumping to avoid races with resize.
    {
      std::shared_lock lock(storage_mutex_);
      generations_.Bump(
        oxygen::bindless::HeapIndex { static_cast<uint32_t>(raw_index) });
    }

    // Schedule deferred reclamation.
    // Safety: The lambda captures the index and context by value.
    reclaimer_->RegisterDeferredAction(
      [this, idx = handle.index, ctx = std::move(context)]() {
        // 1. Invoke user callback to return index to free-list.
        if (on_recycle_) {
          on_recycle_(idx, ctx);
        }

        // 2. Clear pending flag so it can be re-allocated and released again.
        const auto raw = detail::GetIndexValue(idx);

        // We must lock here too because this runs later, potentially during a
        // resize.
        {
          std::shared_lock lock(storage_mutex_);
          if (raw < pending_size_) {
            // Relaxed is sufficient; the sequence is enforced by the frame
            // synchronization.
            pending_flags_[raw].value.store(0, std::memory_order_release);
          }
        }

        telemetry_->reclaimed_count.fetch_add(1, std::memory_order_relaxed);
        telemetry_->pending_count.fetch_sub(1, std::memory_order_relaxed);
      });
  }

  //! Schedule an index for deferred release (default context).
  auto Release(VersionedIndex<IndexType> handle) -> void
    requires(std::is_default_constructible_v<ContextType>)
  {
    Release(handle, ContextType {});
  }

  //! Validate if a handle matches the current generation.
  [[nodiscard]] auto IsHandleCurrent(VersionedIndex<IndexType> handle) const
    -> bool
  {
    const auto raw_index = detail::GetIndexValue(handle.index);
    // Bounds check
    std::shared_lock lock(storage_mutex_);
    if (raw_index >= pending_size_) {
      return false;
    }
    const auto current_gen = generations_.Load(
      oxygen::bindless::HeapIndex { static_cast<uint32_t>(raw_index) });
    return current_gen == handle.generation;
  }

  // Hook for frame processing.
  auto OnBeginFrame(frame::Slot slot) -> void
  {
    // The reclaimer handles the execution of the callbacks registered in
    // Release.
    reclaimer_->OnBeginFrame(slot);
  }

  [[nodiscard]] auto GetTelemetrySnapshot() const noexcept -> TelemetrySnapshot
  {
    return {
      .allocate_calls
      = telemetry_->allocate_calls.load(std::memory_order_relaxed),
      .release_calls
      = telemetry_->release_calls.load(std::memory_order_relaxed),
      .stale_reject_count
      = telemetry_->stale_reject_count.load(std::memory_order_relaxed),
      .duplicate_reject_count
      = telemetry_->duplicate_reject_count.load(std::memory_order_relaxed),
      .reclaimed_count
      = telemetry_->reclaimed_count.load(std::memory_order_relaxed),
      .pending_count
      = telemetry_->pending_count.load(std::memory_order_relaxed),
    };
  }

private:
  struct TelemetryData {
    std::atomic<uint64_t> allocate_calls { 0 };
    std::atomic<uint64_t> release_calls { 0 };
    std::atomic<uint64_t> stale_reject_count { 0 };
    std::atomic<uint64_t> duplicate_reject_count { 0 };
    std::atomic<uint64_t> reclaimed_count { 0 };
    std::atomic<uint64_t> pending_count { 0 };
  };

  // Wrapper for atomic<uint8_t> to make it copyable/movable for vector.
  // The copy/move operations are NOT atomic, but this is safe because
  // we only resize (copy/move) under an exclusive writer lock.
  struct AtomicFlag {
    std::atomic<uint8_t> value { 0 };

    AtomicFlag() = default;

    // Copy constructor (relaxed load)
    AtomicFlag(const AtomicFlag& other)
      : value(other.value.load(std::memory_order_relaxed))
    {
    }

    // Move constructor (effectively copy)
    AtomicFlag(AtomicFlag&& other) noexcept
      : value(other.value.load(std::memory_order_relaxed))
    {
    }

    // Assignment operator (relaxed load/store)
    AtomicFlag& operator=(const AtomicFlag& other)
    {
      if (this != &other) {
        value.store(other.value.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
      }
      return *this;
    }

    // Move assignment operator (effectively copy)
    AtomicFlag& operator=(AtomicFlag&& other) noexcept
    {
      if (this != &other) {
        value.store(other.value.load(std::memory_order_relaxed),
          std::memory_order_relaxed);
      }
      return *this;
    }

    ~AtomicFlag() = default;
  };

  void EnsureCapacity_(std::size_t raw_index)
  {
    // Use shared_mutex writer lock for resizing which protects against
    // concurrent reads in Release/IsHandleCurrent and concurrent writes
    // from other threads trying to grow the capacity.
    std::unique_lock lock(storage_mutex_);
    if (raw_index < pending_size_) {
      return;
    }

    const auto new_size = (std::max)(pending_size_ * 2,
      (std::max)(std::size_t { 64 }, raw_index + 1));

    // Grow generation tracker
    generations_.Resize(
      oxygen::bindless::Capacity { static_cast<uint32_t>(new_size) });

    // Grow pending flags vector.
    // std::vector handles allocation and copying via AtomicFlag copy ctor.
    // We are under exclusive lock, so copying atomics is safe.
    pending_flags_.resize(new_size);

    pending_size_ = new_size;
  }

  oxygen::graphics::detail::DeferredReclaimer* reclaimer_;
  RecycleFn on_recycle_;

  GenerationTracker generations_;
  std::shared_ptr<TelemetryData> telemetry_ {
    std::make_shared<TelemetryData>()
  };

  mutable std::shared_mutex storage_mutex_;
  std::vector<AtomicFlag> pending_flags_;
  std::size_t pending_size_ { 0 };
};

} // namespace oxygen::nexus
