//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Internal/QueueManager.h>

namespace oxygen::graphics::internal {

QueueManager::QueueManager()
{
  LOG_F(INFO, "Common QueueManager component created");
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
  LOG_SCOPE_FUNCTION(INFO);
  strategy_ptr_ = queue_strategy.Clone();
  creator_ = std::move(creator);

  // If queues are already present treat this as a device reset/recovery and
  // recreate them. Clear existing caches and recreate from the new
  // strategy. Emit a warning to aid debugging.
  std::lock_guard lk(queue_cache_mutex_);
  if (!queues_by_key_.empty()) {
    LOG_F(WARNING, "Recreating all CommandQueues...");
    queues_by_key_.clear();
  }

  for (const auto& s : strategy_ptr_->Specifications()) {
    if (queues_by_key_.find(s.key) != queues_by_key_.end()) {
      LOG_F(ERROR, "duplicate key detected: '{}'", s.key.get());
      throw std::invalid_argument(
        fmt::format("duplicate key in queues strategy: '{}'", s.key.get()));
    }

    auto q = creator_(s.key, s.role);
    if (!q) {
      throw std::runtime_error(
        fmt::format("CreateCommandQueue returned nullptr for key='{}' role={}",
          s.key.get(), nostd::to_string(s.role)));
    }
    LOG_F(INFO, "CommandQueue key='{}' role={}", s.key.get(),
      nostd::to_string(s.role));
    queues_by_key_.emplace(s.key, std::make_pair(s, q));
  }
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
      std::to_underlying(role));
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

} // namespace oxygen::graphics::internal
