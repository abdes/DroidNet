//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Headless/CommandQueue.h>
#include <Oxygen/Graphics/Headless/Internal/QueueManager.h>

namespace oxygen::graphics::headless::internal {

QueueManager::QueueManager() { LOG_F(INFO, "QueueManager component created"); }

auto QueueManager::CreateCommandQueue(std::string_view queue_name,
  oxygen::graphics::QueueRole role,
  oxygen::graphics::QueueAllocationPreference allocation_preference)
  -> std::shared_ptr<oxygen::graphics::CommandQueue>
{
  std::lock_guard lk(queue_cache_mutex_);

  if (role == oxygen::graphics::QueueRole::kNone) {
    role = oxygen::graphics::QueueRole::kGraphics;
  }
  // If a name was supplied and we already created a queue with that name,
  // reuse it (name is authoritative for application-visible queues).
  if (!queue_name.empty()) {
    const std::string key(queue_name);
    auto nit = name_queues_.find(key);
    if (nit != name_queues_.end()) {
      LOG_F(INFO, "Reusing named queue '{}'", key);
      return nit->second;
    }
  }

  if (allocation_preference
    == oxygen::graphics::QueueAllocationPreference::kAllInOne) {
    // Create or reuse a single universal queue. By design the universal
    // queue represents a Graphics-capable queue family; requests that ask
    // for an AllInOne queue are normalized to a Graphics role. We do NOT
    // attempt to preserve a caller-provided role for the universal queue.
    if (!universal_queue_) {
      const auto name = queue_name.empty() ? std::string("headless-universal")
                                           : std::string(queue_name);
      // Universal queue role is explicitly Graphics
      universal_queue_ = std::make_shared<CommandQueue>(
        name, oxygen::graphics::QueueRole::kGraphics);
      // If caller provided a name, register it so further lookups by the same
      // name reuse the queue.
      if (!queue_name.empty()) {
        name_queues_.emplace(std::string(queue_name), universal_queue_);
      }
      LOG_F(INFO, "Created universal queue '{}' role={}", name,
        nostd::to_string(oxygen::graphics::QueueRole::kGraphics));
    } else {
      LOG_F(INFO, "Reusing universal queue role={}",
        nostd::to_string(oxygen::graphics::QueueRole::kGraphics));
    }
    return universal_queue_;
  }

  // Dedicated allocation: first check if a queue already exists for the
  // requested role and name.
  auto rit = role_queues_.find(role);
  if (rit != role_queues_.end()) {
    LOG_F(INFO, "Reusing cached queue for role {}", nostd::to_string(role));
    // If a name was provided, ensure the name maps to this instance for
    // subsequent lookups.
    if (!queue_name.empty()) {
      name_queues_.emplace(std::string(queue_name), rit->second);
    }
    return rit->second;
  }

  // No existing role queue — create one. Use the supplied name if present,
  // otherwise generate a deterministic name based on role.
  const auto name = queue_name.empty()
    ? fmt::format("headless-queue-{}", static_cast<int>(role))
    : std::string(queue_name);
  auto q = std::make_shared<CommandQueue>(name, role);
  role_queues_.emplace(role, q);
  if (!queue_name.empty()) {
    name_queues_.emplace(name, q);
  }
  LOG_F(
    INFO, "Created per-role queue '{}' role={}", name, nostd::to_string(role));
  return q;
}

} // namespace oxygen::graphics::headless::internal
