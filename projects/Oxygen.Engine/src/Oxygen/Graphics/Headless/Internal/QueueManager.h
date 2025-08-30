//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
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
  QueueManager();
  ~QueueManager() override = default;

  // Non-copyable
  QueueManager(const QueueManager&) = delete;
  QueueManager& operator=(const QueueManager&) = delete;

  //! Create or reuse a command queue according to the requested role and
  //! allocation preference.
  auto CreateCommandQueue(std::string_view queue_name,
    oxygen::graphics::QueueRole role,
    oxygen::graphics::QueueAllocationPreference allocation_preference)
    -> std::shared_ptr<oxygen::graphics::CommandQueue>;

private:
  std::mutex queue_cache_mutex_;
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
};

} // namespace oxygen::graphics::headless::internal
