//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

#include <Oxygen/Nexus/TimelineGatedSlotReuse.h>

using oxygen::nexus::TimelineGatedSlotReuse;

// Local aliases to reduce verbosity in implementation
namespace bindless = oxygen::bindless;
using DomainKey = oxygen::nexus::DomainKey;
using VHandle = oxygen::VersionedBindlessHandle;
using CommandQueue = oxygen::graphics::CommandQueue;
using FenceValue = oxygen::graphics::FenceValue;

/*!
 Initialize the timeline-gated slot reuse system with backend hooks.

 @param allocate Function to allocate bindless slots from backend
 @param free Function to deallocate bindless slots in backend

 ### Usage Examples

 ```cpp
 auto reuse_manager = TimelineGatedSlotReuse(
   [](DomainKey domain) { return backend.allocate(domain); },
   [](DomainKey domain, Handle h) { backend.free(domain, h); }
 );
 ```

 @note Backend functions are stored and called for actual
 allocation/deallocation
 @see Allocate, Release
*/
TimelineGatedSlotReuse::TimelineGatedSlotReuse(AllocateFn allocate, FreeFn free)
  : allocate_(std::move(allocate))
  , free_(std::move(free))
{
}

#if !defined(NDEBUG)
namespace {
// Debug-only global config for stall warnings
constexpr int64_t kDefaultWarnBaseMs = 2000; // 2s
constexpr int64_t kDefaultWarnMaxMs = 5000; // 5s
constexpr double kDefaultWarnMultiplier = 2.0; // x2 backoff

struct StallWarningConfig {
  std::atomic<int64_t> base_ms { kDefaultWarnBaseMs };
  std::atomic<int64_t> max_ms { kDefaultWarnMaxMs };
  std::atomic<double> multiplier { kDefaultWarnMultiplier };
};

auto GetStallWarningConfig() -> StallWarningConfig&
{
  static StallWarningConfig config {};
  return config;
}
} // namespace

/*!
 Sets the adaptive backoff parameters for stall warnings when queues appear to
 be stuck. Has no effect in release builds.

 @param base Initial warning interval (minimum 1ms)
 @param multiplier Backoff multiplier per warning (minimum 1.0)
 @param max Maximum warning interval (at least base)

 ### Usage Examples

 ```cpp
 // Configure more aggressive stall detection
 TimelineGatedSlotReuse::SetDebugStallWarningConfig(
   std::chrono::milliseconds(500),  // 500ms base
   1.5,                             // 1.5x multiplier
   std::chrono::milliseconds(8000)  // 8s max
 );
 ```

 @note Only affects debug builds, completely compiled out in release
 @see ProcessFor
*/
void TimelineGatedSlotReuse::SetDebugStallWarningConfig(
  std::chrono::milliseconds base, double multiplier,
  std::chrono::milliseconds max)
{
  auto& config = GetStallWarningConfig();
  if (base.count() <= 0) {
    base = std::chrono::milliseconds(1);
  }
  max = (std::max)(max, base);
  multiplier = (std::max)(multiplier, 1.0);
  config.base_ms.store(
    static_cast<int64_t>(base.count()), std::memory_order_relaxed);
  config.max_ms.store(
    static_cast<int64_t>(max.count()), std::memory_order_relaxed);
  config.multiplier.store(multiplier, std::memory_order_relaxed);
}
#endif // !NDEBUG

/*!
 Grows internal arrays to accommodate the specified bindless handle index.
 Thread-safe and only grows arrays, never shrinks them.

 @param index The bindless handle index that must be covered

 ### Performance Characteristics

 - Time Complexity: O(1) for covered indices, O(n) for resize where n is new
   capacity
 - Memory: Exponential growth strategy (2x) with immediate capacity satisfaction
 - Optimization: Double-checked locking pattern to minimize mutex contention

 ### Usage Examples

 ```cpp
 // Internal usage - called automatically by public methods
 EnsureCapacity(bindless::HeapIndex{1000});
 // Ensures arrays can handle indices up to 1000
 ```

 @note Uses resize_mutex_ to guarantee pointer stability during resizes
 @warning Never shrinks arrays to maintain pointer stability guarantees
 @see Allocate, Release
*/
void TimelineGatedSlotReuse::EnsureCapacity(bindless::HeapIndex index)
{
  // Ensure generation tracker covers index. Resize to at least index+1.
  const std::size_t needed = static_cast<std::size_t>(index.get()) + 1U;

  // First, ensure generations are large enough (only grow, never shrink)
  {
    std::lock_guard<std::mutex> generation_lock(generation_mutex_);
    if (gen_capacity_ < needed) {
      gen_capacity_ = needed;
      generation_tracker_.Resize(
        bindless::Capacity { static_cast<uint32_t>(gen_capacity_) });
    }
  }

  // Early return if pending flags already cover the needed capacity
  if (pending_size_ >= needed) {
    return;
  }

  // Thread-safe resize of pending flags array with pointer stability
  std::lock_guard<std::mutex> lg(resize_mutex_);
  if (pending_size_ < needed) {
    constexpr std::size_t kMinPendingSize = 1U;
    constexpr std::size_t kGrowthFactor = 2U;
    const auto old_size = pending_size_;
    // Exponential growth with immediate satisfaction of needed capacity
    const auto new_size
      = (std::max)((std::max)(kMinPendingSize, old_size * kGrowthFactor),
        needed);
    pending_flags_.resize(new_size);
    pending_size_ = new_size;
  }
}

auto TimelineGatedSlotReuse::TrySetPending(bindless::HeapIndex index) -> bool
{
  const std::size_t idx = index.get();

  std::unique_lock<std::mutex> lock(resize_mutex_);
  if (idx >= pending_size_) {
    lock.unlock();
    EnsureCapacity(index);
    lock.lock();
  }

  uint8_t expected = 0U;
  return pending_flags_[idx].value.compare_exchange_strong(
    expected, 1U, std::memory_order_acq_rel);
}

auto TimelineGatedSlotReuse::GetOrCreateQueueData(
  const std::shared_ptr<CommandQueue>& queue) -> std::shared_ptr<QueueData>
{
  std::lock_guard<std::mutex> lock(queues_lock_);
  const auto key = std::weak_ptr<CommandQueue>(queue);
  auto it = pending_per_queue_.find(key);
  if (it == pending_per_queue_.end()) {
    auto created = std::make_shared<QueueData>();
    pending_per_queue_.emplace(key, created);
    return created;
  }
  return it->second;
}

auto TimelineGatedSlotReuse::FindQueueData(
  const std::shared_ptr<CommandQueue>& queue) -> std::shared_ptr<QueueData>
{
  std::lock_guard<std::mutex> lock(queues_lock_);
  const auto it = pending_per_queue_.find(std::weak_ptr<CommandQueue>(queue));
  if (it == pending_per_queue_.end()) {
    return {};
  }
  return it->second;
}

/*!
 Creates a new versioned handle by allocating from the backend and stamping with
 the current generation value.

 @param domain The allocation domain context
 @return New versioned bindless handle with current generation

 ### Performance Characteristics

 - Time Complexity: O(1) with potential O(log n) for capacity expansion
 - Memory: May trigger exponential growth of internal arrays
 - Optimization: Generation stamping prevents stale handle reuse

 ### Usage Examples

 ```cpp
 // Basic allocation
 auto handle = reuse_manager.Allocate(domain_key);
 // Use handle for GPU resource binding
 ```

 @note Always pair with Release() calls to prevent memory leaks
 @see Release, EnsureCapacity
*/
auto TimelineGatedSlotReuse::Allocate(DomainKey const& domain) -> VHandle
{
  const bindless::HeapIndex handle = allocate_(domain);

  // Ensure generation tracker and pending flags cover index
  EnsureCapacity(handle);

  bindless::Generation generation {};
  {
    std::lock_guard<std::mutex> generation_lock(generation_mutex_);
    generation = generation_tracker_.Load(handle);
  }
  return VHandle(handle, generation);
}

/*!
 Enqueues the handle for deferred reclamation when the specified queue reaches
 the given fence value. Prevents double-release via internal pending flags.

 @param domain The domain context for the handle
 @param h The versioned handle to release
 @param queue Command queue timeline for synchronization
 @param fence_value Fence value that must be reached for reclamation

 ### Performance Characteristics

 - Time Complexity: O(1) for enqueue, O(log n) for queue bucket insertion
 - Memory: Constant memory allocation for pending free entry
 - Optimization: Atomic compare-exchange prevents double-release races

 ### Usage Examples

 ```cpp
 // Release after GPU work submission
 reuse_manager.Release(domain, handle, command_queue, fence_value);
 // Handle will be reclaimed when queue reaches fence_value
 ```

 @warning Invalid handles are silently ignored to prevent crashes
 @note Call Process() regularly to actually reclaim released handles
 @see ReleaseBatch, ProcessFor
*/
auto TimelineGatedSlotReuse::Release(DomainKey const& domain, VHandle const h,
  const std::shared_ptr<CommandQueue>& queue, FenceValue fence_value) -> void
{
  // Early return for invalid handles - prevent massive memory allocation
  if (!h.IsValid() || !queue || !IsHandleCurrent(h)) {
    return;
  }

  const bindless::HeapIndex idx = h.ToBindlessHandle();
  if (!TrySetPending(idx)) {
    return;
  }

  PendingFree pending_free { domain, idx };

  // Insert into per-queue buckets with thread-safe queue management
  {
    auto qd = GetOrCreateQueueData(queue);
    std::lock_guard<std::mutex> lk(qd->lock);
    qd->buckets[fence_value].push_back(pending_free);
  }
}

/*!
 Optimized batch release operation for improved performance when releasing
 multiple handles with the same synchronization point.

 @param queue Command queue timeline for synchronization
 @param fence_value Fence value that must be reached for reclamation
 @param items Span of domain/handle pairs to release

 ### Performance Characteristics

 - Time Complexity: O(n) where n is items.size()
 - Memory: Single vector allocation, batch insertion
 - Optimization: Reduces lock contention vs individual Release calls

 ### Usage Examples

 ```cpp
 // Batch release after frame submission
 std::vector<std::pair<DomainKey, VersionedBindlessHandle>> to_release;
 // ... populate to_release ...
 reuse_manager.ReleaseBatch(queue, fence, to_release);
 ```

 @note More efficient than multiple Release() calls for same fence
 @see Release, ProcessFor
*/
auto TimelineGatedSlotReuse::ReleaseBatch(
  const std::shared_ptr<CommandQueue>& queue, FenceValue fence_value,
  std::span<const std::pair<DomainKey, VHandle>> items) -> void
{
  if (!queue) {
    return;
  }

  // Reserve vector of pending frees
  std::vector<PendingFree> local;
  local.reserve(items.size());

  for (const auto& it : items) {
    const auto& domain = it.first;
    const auto& vh = it.second;

    // Skip invalid handles - prevent massive memory allocation
    if (!vh.IsValid() || !IsHandleCurrent(vh)) {
      continue;
    }

    const bindless::HeapIndex idx = vh.ToBindlessHandle();
    if (TrySetPending(idx)) {
      local.push_back(PendingFree { domain, idx });
    }
  }

  if (local.empty()) {
    return;
  }

  {
    auto qd = GetOrCreateQueueData(queue);
    std::lock_guard<std::mutex> lk(qd->lock);
    auto& bucket = qd->buckets[fence_value];
    bucket.insert(bucket.end(), local.begin(), local.end());
  }
}

/*!
 Targeted processing for a single queue's timeline. More efficient than
 Process() when you know which queue to check.

 @param queue The command queue to process

 ### Performance Characteristics

 - Time Complexity: O(k log k) where k is pending buckets for this queue
 - Memory: Temporary vector for completed fence buckets
 - Optimization: Processes single queue without global iteration

 ### Usage Examples

 ```cpp
 // Process specific queue after fence completion
 reuse_manager.ProcessFor(graphics_queue);
 // Only processes pending frees for graphics_queue
 ```

 @note Includes debug stall detection in debug builds
 @warning Null queue pointer is safely handled with early return
 @see Process, Release
*/
auto TimelineGatedSlotReuse::ProcessFor(
  const std::shared_ptr<CommandQueue>& queue) noexcept -> void
{
  // find per-queue data
  if (!queue) {
    return;
  }
  auto qdptr = FindQueueData(queue);
  if (!qdptr) {
    return;
  }

  const FenceValue completed { queue->GetCompletedValue() };

  std::vector<std::pair<FenceValue, std::vector<PendingFree>>> to_process;
  {
    std::lock_guard<std::mutex> lk(qdptr->lock);
#if !defined(NDEBUG)
    auto& config = GetStallWarningConfig();
    // Progress tracking for throttled stuck-fence warnings (debug-only)
    const auto now = std::chrono::steady_clock::now();
    if (qdptr->last_progress_time == std::chrono::steady_clock::time_point {}) {
      qdptr->last_progress_time = now;
      qdptr->last_warn_time = now;
      // Initialize interval if unset
      qdptr->current_warn_interval = std::chrono::milliseconds(
        config.base_ms.load(std::memory_order_relaxed));
    }

    if (completed.get() > qdptr->last_completed.get()) {
      // Fence advanced – record progress
      qdptr->last_completed = completed;
      qdptr->last_progress_time = now;
      qdptr->last_warn_time = now;
      // Reset backoff on progress
      qdptr->current_warn_interval = std::chrono::milliseconds(
        config.base_ms.load(std::memory_order_relaxed));
    } else {
      // No progress: if there are pending frees, consider warning
      const bool has_pending = !qdptr->buckets.empty();
      if (has_pending) {
        const auto interval = qdptr->current_warn_interval;
        if (now - qdptr->last_warn_time >= interval) {
          DLOG_F(WARNING,
            "TimelineGatedSlotReuse: queue '{}' appears stalled: completed={} "
            "(pending buckets={})",
            queue->GetName(), completed.get(), qdptr->buckets.size());
          qdptr->last_warn_time = now;
          // Backoff interval: interval = min(interval * multiplier, max)
          const auto max_ms = config.max_ms.load(std::memory_order_relaxed);
          const auto mult = config.multiplier.load(std::memory_order_relaxed);
          const auto cur_ms = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(interval)
              .count());
          const auto next_ms = static_cast<int64_t>(cur_ms * mult);
          const auto clamped
            = (std::min)(next_ms, (std::max<int64_t>)(max_ms, 1));
          qdptr->current_warn_interval = std::chrono::milliseconds(clamped);
        }
      }
    }
#endif // !NDEBUG

    auto it = qdptr->buckets.begin();
    while (it != qdptr->buckets.end() && it->first.get() <= completed.get()) {
      to_process.emplace_back(it->first, std::move(it->second));
      it = qdptr->buckets.erase(it);
    }
  }

  // Process reclaim outside queue locks to minimize contention
  for (auto& entry : to_process) {
    for (auto& pending_free : entry.second) {
      // Bump generation to invalidate existing handles, then free backend slot
      {
        EnsureCapacity(pending_free.index);
        {
          std::lock_guard<std::mutex> generation_lock(generation_mutex_);
          generation_tracker_.Bump(pending_free.index);
        }

        // Clear pending flag using release ordering for proper synchronization.
        // Protect pointer stability during flag clear using resize_mutex_.
        const auto j = static_cast<std::size_t>(pending_free.index.get());
        std::lock_guard<std::mutex> lg(resize_mutex_);
        if (j < pending_size_) {
          pending_flags_[j].value.store(0U, std::memory_order_release);
        }
      }
      // Call backend free function to actually reclaim the slot
      free_(pending_free.domain, pending_free.index);
    }
  }
}

/*!
 Opportunistically checks all registered command queues and reclaims handles
 whose fence values have been reached. Call regularly to prevent memory leaks.

 ### Performance Characteristics

 - Time Complexity: O(n * k log k) where n is active queues, k is buckets per
   queue
 - Memory: Snapshot of queue shared_ptrs to avoid lock contention
 - Optimization: Automatic cleanup of destroyed queues via weak_ptr

 ### Usage Examples

 ```cpp
 // Regular processing in main loop
 while (running) {
   // ... render frame ...
   reuse_manager.Process(); // Reclaim completed handles
 }
 ```

 @note Should be called regularly to prevent unbounded memory growth
 @see ProcessFor, Release
*/
auto TimelineGatedSlotReuse::Process() noexcept -> void
{
  // snapshot keys to process to avoid holding the queues_lock_ while iterating
  std::vector<std::shared_ptr<CommandQueue>> keys;
  {
    std::lock_guard<std::mutex> qlk(queues_lock_);
    keys.reserve(pending_per_queue_.size());
    for (auto it = pending_per_queue_.begin();
      it != pending_per_queue_.end();) {
      if (auto sp = it->first.lock()) {
        keys.push_back(std::move(sp));
        ++it;
      } else {
        it = pending_per_queue_.erase(it);
      }
    }
  }
  for (auto& k : keys) {
    ProcessFor(k);
  }
}

/*!
 Validates that the provided handle's generation matches the current generation
 for its index, indicating the handle is still valid.

 @param h The versioned handle to validate
 @return true if handle generation matches current, false otherwise

 ### Performance Characteristics

 - Time Complexity: O(1) generation lookup
 - Memory: No allocation, read-only operation
 - Optimization: Lock-free generation comparison

 ### Usage Examples

 ```cpp
 // Validate handle before use
 if (reuse_manager.IsHandleCurrent(handle)) {
   // Safe to use handle for GPU operations
   bind_resource(handle);
 }
 ```

 @note Returns false for invalid handles or mismatched generations
 @see Allocate, GenerationTracker
*/
auto TimelineGatedSlotReuse::IsHandleCurrent(
  oxygen::VersionedBindlessHandle h) const noexcept -> bool
{
  if (!h.IsValid()) {
    return false;
  }
  const bindless::HeapIndex idx = h.ToBindlessHandle();
  bindless::Generation cur {};
  {
    std::lock_guard<std::mutex> generation_lock(generation_mutex_);
    cur = generation_tracker_.Load(idx);
  }
  return cur.get() == h.GenerationValue().get();
}
