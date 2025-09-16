//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#if !defined(NDEBUG)
#  include <chrono>
#endif // !NDEBUG
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Types/FenceValue.h>
#include <Oxygen/Nexus/GenerationTracker.h>
#include <Oxygen/Nexus/Types/Domain.h>
#include <Oxygen/Nexus/api_export.h>

namespace oxygen::nexus {

// This header uses canonical project types from the included headers.
// DomainKey and VersionedBindlessHandle are defined in the engine.

//! Timeline-gated slot reuse strategy for bindless resource management
/*!
  Implements Strategy B for deferred bindless slot reclamation keyed by
  command queue timelines. Handles are released with a queue/fence pair and
  later reclaimed when the queue's completed value passes the fence value.

  This class provides thread-safe, timeline-synchronized resource reclamation
  for bindless rendering systems. It ensures GPU resources are not freed
  until the GPU has finished using them, preventing use-after-free errors.

  @par Thread Safety
  All methods are thread-safe. Internal synchronization uses mutexes for
  queue management and atomic operations for generation tracking.

  @par Performance Characteristics
  - O(1) allocation and release operations
  - O(log n) processing complexity per queue
  - Batch operations for improved cache locality
  - Lock-free generation checking

  @see BindlessDeferredReuseDesign.md (Strategy B)
  @see BindlessArchitecture.md for overall bindless design
*/
class TimelineGatedSlotReuse {
public:
  //! Function signature for backend slot allocation
  using AllocateFn = std::function<bindless::HeapIndex(DomainKey)>;

  //! Function signature for backend slot deallocation
  using FreeFn = std::function<void(DomainKey, bindless::HeapIndex)>;

  //! Construct with backend allocate/free hooks
  OXGN_NXS_API explicit TimelineGatedSlotReuse(
    AllocateFn allocate, FreeFn free);

  //! Allocate a bindless slot in the specified domain with generation stamp
  OXGN_NXS_NDAPI auto Allocate(DomainKey const& domain)
    -> oxygen::VersionedBindlessHandle;

  //! Release a versioned handle for timeline-gated reclamation
  OXGN_NXS_API auto Release(DomainKey const& domain,
    oxygen::VersionedBindlessHandle h,
    const std::shared_ptr<graphics::CommandQueue>& queue,
    graphics::FenceValue fence_value) -> void;

  //! Release multiple handles under the same queue/fence value
  OXGN_NXS_API auto ReleaseBatch(
    const std::shared_ptr<graphics::CommandQueue>& queue,
    graphics::FenceValue fence_value,
    std::span<const std::pair<DomainKey, oxygen::VersionedBindlessHandle>>
      items) -> void;

  //! Process all queues and reclaim eligible handles
  OXGN_NXS_API auto Process() noexcept -> void;

  //! Process pending frees for a specific command queue
  OXGN_NXS_API auto ProcessFor(
    const std::shared_ptr<graphics::CommandQueue>& queue) noexcept -> void;

  //! Check if a versioned handle matches the current generation
  OXGN_NXS_API auto IsHandleCurrent(VersionedBindlessHandle h) const noexcept
    -> bool;

#if !defined(NDEBUG)
  //! Configure debug stall warning parameters (debug builds only)
  OXGN_NXS_API static void SetDebugStallWarningConfig(
    std::chrono::milliseconds base, double multiplier,
    std::chrono::milliseconds max);
#endif // !NDEBUG

private:
  //! Pending free entry for queue-based reclamation
  struct PendingFree {
    //! Domain context for the handle
    DomainKey domain;
    //! Bindless index to reclaim
    bindless::HeapIndex index;
  };

  //! Backend allocation function
  AllocateFn allocate_;
  //! Backend deallocation function
  FreeFn free_;

  //! Generation tracker for stamp/load/bump operations
  GenerationTracker generation_tracker_;

  //! Current generation tracker capacity to prevent accidental shrinking
  /*!
   * Mirrors the generation tracker's capacity to avoid shrinking during
   * EnsureCapacity calls. Only grows when needed for performance.
   */
  std::size_t gen_capacity_ { 0 };

  //! Pending flag per index to prevent double-release races
  /*!
   * Contiguous atomic array with pointer stability guaranteed by resize_mutex_.
   * Each flag indicates whether a handle is pending reclamation. Uses atomic
   * compare-and-swap for race-free double-release detection.
   */
  std::unique_ptr<std::atomic<uint8_t>[]> pending_flags_;
  //! Current pending flags array size
  std::size_t pending_size_ { 0 };
  //! Protects pending array resize operations
  mutable std::mutex resize_mutex_;

  //! Per-queue pending frees organized by fence value
  /*!
   * Maps command queue timelines to ordered buckets of pending frees.
   * Each bucket contains handles waiting for a specific fence value.
   */
  struct QueueData {
    //! Protects buckets map
    std::mutex lock;
    //! Fence-ordered buckets of pending frees
    std::map<graphics::FenceValue, std::vector<PendingFree>> buckets;

#if !defined(NDEBUG)
    //! Debug-only stall detection state
    //! Last observed completed fence
    graphics::FenceValue last_completed {};
    //! Time of last fence progress
    std::chrono::steady_clock::time_point last_progress_time {};
    //! Time of last warning
    std::chrono::steady_clock::time_point last_warn_time {};
    //! Current warning interval with backoff
    std::chrono::steady_clock::duration current_warn_interval {};
#endif // !NDEBUG
  };

  //! Protects pending_per_queue_ map
  std::mutex queues_lock_;

  //! Per-queue data keyed by weak_ptr for automatic cleanup
  /*!
   * Uses weak_ptr keys to avoid extending queue lifetime and automatically
   * detect queue destruction. owner_less comparator ensures proper weak_ptr
   * comparison semantics across different shared_ptr instances.
   */
  std::map<std::weak_ptr<graphics::CommandQueue>, std::shared_ptr<QueueData>,
    std::owner_less<std::weak_ptr<graphics::CommandQueue>>>
    pending_per_queue_;

  //! Ensure generation tracker and pending flags cover the given index
  void EnsureCapacity(bindless::HeapIndex index);
};

} // namespace oxygen::nexus
