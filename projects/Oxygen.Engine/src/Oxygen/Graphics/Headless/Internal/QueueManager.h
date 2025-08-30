//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>

namespace oxygen::graphics::headless::internal {

//!  Centralized manager for headless backend `CommandQueue` instances.
/*!
 This component provides a deterministic, thread-safe mapping from higher-level
 queue specifications (name, role, allocation preference) to concrete
 `CommandQueue` instances used by the headless backend.

 ### Key Features

 - **Name-authoritative lookup**: if a non-empty name is supplied and a queue
   was previously created using that name, the previously created instance is
   returned ("first-creation-wins").
 - **All-in-one semantics**: requests with `kAllInOne` allocation preference
   produce a single universal queue (created with role `QueueRole::kGraphics`)
   which is reused for subsequent AllInOne requests and can be registered under
   an application-visible name.
 - **Per-role caches for dedicated queues**: `kDedicated` requests are resolved
   to per-role cached instances so repeated requests for the same role reuse the
   same queue.
 - **Thread-safety**: all cache accesses and creations are serialized by an
   internal mutex to make concurrent `CreateCommandQueue` calls safe.

 ### Usage Patterns

 - Backends should call `CreateCommandQueue` with the strategy-provided name (if
   any), role, and allocation preference. Prefer using names for explicit
   control; otherwise rely on deterministic role-based fallbacks.

 ### Example

 ```cpp
 // Request a universal queue by name (created as Graphics on first use)
 auto q = qm.CreateCommandQueue("universal", QueueRole::kGraphics,
   QueueAllocationPreference::kAllInOne);
 ```

 @note The component intentionally sets the universal queue role to Graphics for
 AllInOne allocations to preserve predictable execution semantics across
 backends.

 @warning Do not rely on pointer identity across separate process runs or after
 backend reinitialization; the manager guarantees deterministic mapping only
 within a single composition lifetime.

 @see oxygen::graphics::CommandQueue,
 oxygen::graphics::QueueAllocationPreference
*/
class QueueManager final : public Component {
  OXYGEN_COMPONENT(QueueManager)

public:
  explicit QueueManager();
  ~QueueManager() override = default;

  // Non-copyable
  QueueManager(const QueueManager&) = delete;
  QueueManager& operator=(const QueueManager&) = delete;

  void CreateQueues(const graphics::QueueStrategy& queue_strategy);

  //! Get a previously-created queue by application-visible name.
  auto GetQueueByName(std::string_view name) const
    -> std::shared_ptr<oxygen::graphics::CommandQueue>;

  //! Invoke a callable for every unique CommandQueue.
  /*! This method snapshots the set of unique queues while holding the
      internal lock, then releases the lock and invokes `fn` for each
      queue. This avoids deadlocks when the callable may reenter the
      QueueManager or other subsystems that interact with the manager.
  */
  template <std::invocable<oxygen::graphics::CommandQueue&> Fn>
  void ForEachQueue(Fn&& fn)
  {
    std::vector<std::shared_ptr<oxygen::graphics::CommandQueue>> queues;
    {
      std::lock_guard lk(queue_cache_mutex_);
      std::unordered_set<oxygen::graphics::CommandQueue*> seen;

      auto try_collect
        = [&](const std::shared_ptr<oxygen::graphics::CommandQueue>& sp) {
            if (sp && seen.insert(sp.get()).second) {
              queues.push_back(sp);
            }
          };

      try_collect(universal_queue_);

      for (const auto& kv : role_queues_) {
        try_collect(kv.second);
      }

      for (const auto& kv : name_queues_) {
        try_collect(kv.second);
      }
    }

    for (const auto& sp : queues) {
      std::forward<Fn>(fn)(*sp);
    }
  }

private:
  //! Create or reuse a command queue according to the requested role and
  //! allocation preference. This API is intended for backend callers that hold
  //! a reference to the manager (for example, headless Graphics) and therefore
  //! is public.
  auto CreateCommandQueue(std::string_view queue_name,
    oxygen::graphics::QueueRole role,
    oxygen::graphics::QueueAllocationPreference allocation_preference)
    -> std::shared_ptr<oxygen::graphics::CommandQueue>;

  mutable std::mutex queue_cache_mutex_;
  std::shared_ptr<oxygen::graphics::CommandQueue> universal_queue_;
  // Map application-visible queue names to created queues. This preserves the
  // expectation that different named QueueSpecification entries produce
  // distinct queues even if they share the same role.
  std::unordered_map<std::string,
    std::shared_ptr<oxygen::graphics::CommandQueue>>
    name_queues_;
  std::unordered_map<oxygen::graphics::QueueRole,
    std::shared_ptr<oxygen::graphics::CommandQueue>>
    role_queues_;
  // Optional stored strategy provided at construction time.
  // Store a copy of the strategy so the QueueManager owns its configuration
  // and is not dependent on the caller's lifetime.
  // We use a std::unique_ptr to allow storing polymorphic concrete
  // implementations of QueueStrategy.
  std::unique_ptr<oxygen::graphics::QueueStrategy> strategy_ptr_;
};

} // namespace oxygen::graphics::headless::internal
