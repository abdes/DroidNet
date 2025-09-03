//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>

namespace oxygen::graphics::internal {

//! Device-level queue manager component used by Common Graphics.
/*!
 The QueueManager owns the canonical mapping from application-visible `QueueKey`
 values to created `CommandQueue` instances. Creation is performed up-front by
 calling `CreateQueues` with a `QueuesStrategy` and a backend-provided `creator`
 callable. The manager records the supplied `QueuesStrategy` (via a clone) and
 the creator for future reference. Lookups are read-only and never perform
 implicit creation.

 The manager is backend-agnostic: the graphics implementation provides a factory
 (the `creator` callable) that knows how to construct concrete `CommandQueue`
 objects for the platform. This separation keeps the common graphics code
 independent from backend details.

 @note Callers must ensure the provided `creator` returns a valid
 `std::shared_ptr<graphics::CommandQueue>` for each specification. If the
 backend fails to create a required queue `CreateQueues` will throw a
 `std::runtime_error` or `std::invalid_argument` for malformed strategies.

 ### Performance Characteristics

 - Time Complexity: Lookups (`GetQueueByName`, `GetQueueByRole`) are O(N) over
   the number of specifications in the worst case where role-based resolution
   requires scanning the map. `ForEachQueue` copies unique pointers under lock
   then invokes the callable outside the lock to avoid holding the mutex during
   user callbacks.

 ### Usage Examples

 ```cpp
 // Create queues using a strategy and a backend creator:
 QueueManager qm;
 qm.CreateQueues(strategy, [&](const QueueKey& key, QueueRole role) {
   return backend->CreateCommandQueue(key, role);
 });

 // Lookup by key:
 auto q = qm.GetQueueByName(QueueKey{"gfx"});
 ```

 @see QueuesStrategy, CommandQueue
 */
class QueueManager final : public Component {
  OXYGEN_COMPONENT(QueueManager)

public:
  QueueManager();

  OXYGEN_MAKE_NON_COPYABLE(QueueManager)
  OXYGEN_DEFAULT_MOVABLE(QueueManager)

  ~QueueManager() override = default;

  //! Create or reuse queues described by the strategy using the provided
  //! creator callable. The creator is expected to throw on error or return a
  //! non-empty shared_ptr.
  auto CreateQueues(const QueuesStrategy& queue_strategy,
    std::function<std::shared_ptr<graphics::CommandQueue>(
      const QueueKey&, QueueRole)>
      creator) -> void;

  //! Get a previously-created queue by application-visible name.
  auto GetQueueByName(const QueueKey& key) const
    -> observer_ptr<graphics::CommandQueue>;

  //! Get a previously-created queue by role. Prefers dedicated then all-in-one
  //! (shared). Does not return queues that were marked kNamed in the strategy;
  //! those are only returned by GetQueueByName.
  auto GetQueueByRole(QueueRole role) const
    -> observer_ptr<graphics::CommandQueue>;

  //! Invoke a callable for every unique CommandQueue.
  template <std::invocable<graphics::CommandQueue&> Fn>
  auto ForEachQueue(Fn&& fn) -> void
  {
    std::vector<std::shared_ptr<graphics::CommandQueue>> queues;
    {
      std::lock_guard lk(queue_cache_mutex_);
      std::unordered_set<graphics::CommandQueue*> seen;

      for (const auto& kv : queues_by_key_) {
        const auto& sp = kv.second.second;
        if (sp && seen.insert(sp.get()).second) {
          queues.push_back(sp);
        }
      }
    }

    for (const auto& sp : queues) {
      std::forward<Fn>(fn)(*sp);
    }
  }

private:
  //! Mutex protecting `queues_by_key_` and related state. This mutex is mutable
  //! to allow read-only accessor functions to lock it.
  mutable std::mutex queue_cache_mutex_;

  //! Canonical map of created queues indexed by `QueueKey`. Each entry stores
  //! the original `QueueSpecification` and the created
  //! `shared_ptr<graphics::CommandQueue>`.
  std::unordered_map<QueueKey,
    std::pair<QueueSpecification, std::shared_ptr<graphics::CommandQueue>>>
    queues_by_key_;

  //! Clone of the `QueuesStrategy` passed to `CreateQueues`. Stored so that
  //! role-based lookups (KeyFor) and subsequent calls can consult the original
  //! policy.
  std::unique_ptr<QueuesStrategy> strategy_ptr_;

  //! Backend creator callable supplied by the graphics implementation. This is
  //! stored for completeness; callers should not invoke it except via
  //! `CreateQueues` which performs creation and validation.
  std::function<std::shared_ptr<graphics::CommandQueue>(
    const QueueKey&, QueueRole)>
    creator_;
};

} // namespace oxygen::graphics::internal
