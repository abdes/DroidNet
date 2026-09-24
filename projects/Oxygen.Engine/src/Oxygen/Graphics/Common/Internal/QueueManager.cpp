//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>
#include <stdexcept>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Internal/QueueManager.h>

namespace oxygen::graphics::internal {

QueueManager::QueueManager()
{
  LOG_F(INFO, "Common QueueManager component created");
}

auto QueueManager::ForgetKnownResourceState(
  const NativeResource& resource) noexcept -> void
{
  std::lock_guard lock(queue_cache_mutex_);
  for (const auto& [key, entry] : queues_by_key_) {
    if (entry.second) {
      // Aliased queues may be visited again; erasing absent state is harmless
      // and avoids allocating a deduplication set in retirement.
      entry.second->ForgetKnownResourceState(resource);
    }
  }
}

/*!
 Create or reuse queues described by @p queue_strategy using the provided
 @p creator callable.

 The manager clones the supplied strategy and invokes the @p creator for each
 `QueueSpecification` returned by `queue_strategy.Specifications()`. The creator
 is expected to either return a valid non-empty
 `std::shared_ptr<graphics::CommandQueue>` or throw on failure. If the supplied
 strategy contains duplicate keys the method will throw `std::invalid_argument`.

 @param queue_strategy The queue allocation/sharing strategy. The manager keeps
 a cloned copy for later lookups.
 @param creator A callable invoked as `creator(key, role)` to create the backend
 `CommandQueue`. Must return a `shared_ptr` on success.
 @throw std::invalid_argument If the strategy contains duplicate keys.
 @throw std::runtime_error If the backend creator returns an empty `shared_ptr`
 for a required specification.

 ### Performance

 The method holds an internal mutex while updating the internal cache to ensure
 thread-safety during recreation. Backend creation calls are performed while
 holding the mutex in this implementation; backends that may block for long
 periods should minimize work in the creator or the caller should ensure this is
 performed on an appropriate thread.
*/
auto QueueManager::CreateQueues(const QueuesStrategy& queue_strategy,
  std::function<std::shared_ptr<graphics::CommandQueue>(
    const QueueKey&, QueueRole)>
    creator) -> void
{
  auto strategy = queue_strategy.Clone();
  decltype(queues_by_key_) replacement;
  auto snapshot = std::make_shared<QueueSnapshot>();
  for (const auto& spec : strategy->Specifications()) {
    if (replacement.contains(spec.key)) {
      throw std::invalid_argument("Duplicate queue key");
    }
    auto queue = creator(spec.key, spec.role);
    if (!queue) {
      throw std::runtime_error("Queue factory returned null");
    }
    replacement.emplace(spec.key, std::make_pair(spec, queue));
    if (std::ranges::find(*snapshot, queue) == snapshot->end()) {
      snapshot->push_back(queue);
    }
  }
  std::shared_ptr<const QueueSnapshot> previous;
  {
    std::lock_guard lock(queue_cache_mutex_);
    queues_by_key_.swap(replacement);
    previous = std::move(queue_snapshot_);
    queue_snapshot_ = std::move(snapshot);
    strategy_ptr_ = std::move(strategy);
    creator_ = std::move(creator);
    frames_started_ = false;
    for (auto& fences : frame_fences_) {
      fences.clear();
    }
  }
  // Old queue destruction and native retirement occur outside the cache lock.
}

/*!
 Look up a queue previously created for the exact application-visible
 @p key.

 This lookup is key-based and will return queues that were marked as
 `QueueSharingPreference::kNamed` in the strategy. If the key is empty an
 empty `shared_ptr` is returned and a warning is logged.

 @param key The application-visible queue key to look up.
 @return `std::shared_ptr<graphics::CommandQueue>` owning the queue if found,
 otherwise an empty `shared_ptr`.
*/
auto QueueManager::GetQueueByName(const QueueKey& key) const
  -> observer_ptr<graphics::CommandQueue>
{
  std::lock_guard lk(queue_cache_mutex_);
  if (key.get().empty()) {
    LOG_F(WARNING, "GetQueueByName called with empty key");
    return {};
  }
  const auto it = queues_by_key_.find(key);
  if (it != queues_by_key_.end()) {
    return observer_ptr { it->second.second.get() };
  }
  return {};
}

/*!
 Resolve a queue suitable for @p role using the recorded strategy and created
 queues.

 Resolution rules:
 - Named queues (sharing preference `kNamed`) are not considered by this
   lookup and are only retrievable via `GetQueueByName`.
 - If a queue with `allocation_preference == kDedicated` exists for the
   requested role it is returned (preferred).
 - Otherwise the first `kAllInOne` candidate for the role is returned.

 @param role The queue role to resolve.
 @return A `shared_ptr` to a suitable `CommandQueue`, or empty if none match.
*/
auto QueueManager::GetQueueByRole(QueueRole role) const
  -> observer_ptr<graphics::CommandQueue>
{
  std::lock_guard lk(queue_cache_mutex_);
  DCHECK_LT_F(role, QueueRole::kMax);
  if (role >= QueueRole::kMax) {
    LOG_F(WARNING, "GetQueueByRole called with invalid role: {}",
      nostd::to_underlying(role));
    return {};
  }
  // Scan all created queues. Prefer dedicated over all-in-one. Do NOT return
  // queues that were marked kNamed unless requested by key.
  observer_ptr<graphics::CommandQueue> allinone_candidate;
  for (const auto& kv : queues_by_key_) {
    const auto& spec = kv.second.first;
    const auto& queue = kv.second.second;
    if (spec.sharing_preference == QueueSharingPreference::kNamed) {
      continue; // named queues only returned by key
    }
    if (spec.role != role) {
      continue;
    }
    if (spec.allocation_preference == QueueAllocationPreference::kDedicated) {
      return observer_ptr { queue.get() }; // dedicated preferred
    }
    // remember first all-in-one candidate
    if (!allinone_candidate) {
      allinone_candidate = observer_ptr { queue.get() };
    }
  }
  return allinone_candidate;
}

auto QueueManager::WaitForFrameSlot(const frame::Slot slot) -> void
{
  if (!frames_started_) {
    // Initialization can submit work before the first frame owns a slot.
    // Drain it once before retiring that bootstrap bucket; subsequent frames
    // wait only on the slot they are about to reuse.
    ForEachQueue([](CommandQueue& queue) { queue.Flush(); });
    frames_started_ = true;
  }
  auto& fences = frame_fences_.at(slot.get());
  for (const auto& fence : fences) {
    if (fence.queue->GetCompletedValue()
      == (std::numeric_limits<uint64_t>::max)()) {
      if (auto lifetime = fence.queue->BackendLifetimeState()) {
        lifetime->MarkSubmissionFault();
      }
      throw std::runtime_error("Device loss is not a frame completion proof");
    }
    if (fence.queue->GetCompletedValue() < fence.value) {
      fence.queue->Wait(fence.value);
      if (fence.queue->GetCompletedValue()
        == (std::numeric_limits<uint64_t>::max)()) {
        if (auto lifetime = fence.queue->BackendLifetimeState()) {
          lifetime->MarkSubmissionFault();
        }
        throw std::runtime_error(
          "Device lost while waiting for frame completion");
      }
    }
  }
  fences.clear();
}

auto QueueManager::SignalFrameSlot(const frame::Slot slot) -> void
{
  auto& fences = frame_fences_.at(slot.get());
  fences.clear();
  {
    std::lock_guard lock(queue_cache_mutex_);
    fences.reserve(queues_by_key_.size());
    for (const auto& [key, entry] : queues_by_key_) {
      const auto& queue = entry.second;
      if (queue && std::ranges::none_of(fences, [&](const auto& fence) {
            return fence.queue == queue;
          })) {
        fences.push_back({ .queue = queue });
      }
    }
  }
  for (auto& fence : fences) {
    fence.value = fence.queue->SignalSubmittedWork();
  }
}

} // namespace oxygen::graphics::internal
