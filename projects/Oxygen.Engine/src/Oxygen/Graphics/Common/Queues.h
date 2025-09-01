//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>

namespace oxygen::graphics {

using QueueKey = NamedType<std::string,
  // clang-format off
  struct QueueKeyTag,
  MethodCallable,
  Comparable,
  Hashable,
  Printable>; // clang-format on

constexpr auto to_string(const QueueKey& key) -> const auto&
{
  return key.get();
}

//! How command queues should be provisioned for specified roles.
/*!
 Encodes an allocation *preference* used by queue-management code and
 higher-level strategies. This is an advisory hint; it does not guarantee a
 particular hardware mapping. Backends or the manager may alias or fall back
 depending on device capabilities.

 @see QueueSpecification
 */
enum class QueueAllocationPreference : uint8_t {
  //! Prefer using a single universal queue for all roles.
  kAllInOne,

  //! Prefer using distinct queues per logical role when possible.
  kDedicated
};

OXGN_GFX_API auto to_string(QueueAllocationPreference value) -> const char*;

//! How command queues should be provided when requested.
/*!
 Encodes a sharing *preference* used by queue-management code and higher-level
 strategies. Indicates whether this command queue prefers to be returned only
 when specifically requested by its QueueKey, or whether it can be used for
 requests for a role it can satisfy.

 @see QueueSpecification
 */
enum class QueueSharingPreference : uint8_t {
  //! Can be returned for generic requests by role, as long as the requested
  //! role is compatible with the queue roles.
  kShared,

  //! Prefers being returned only for specific requests by QueueKey.
  kNamed
};

//! Convert a QueueSharingPreference value to a textual name.
OXGN_GFX_API auto to_string(QueueSharingPreference value) -> const char*;

//! Properties describing a command queue.
/*!
 Describes the application-visible properties of a command queue. Concrete
 QueuesStrategy specify one or more QueueSpecification entries; backend
 implementations consumes these to create or select CommandQueue instances.

 @see QueuesStrategy, headless::internal::QueueManager
 */
struct QueueSpecification {
  //! Application-visible name used for named lookup and reuse.
  QueueKey key;

  //! Logical role requested for this queue.
  QueueRole role;

  //! Allocation preference for universal vs per-role provisioning.
  QueueAllocationPreference allocation_preference;

  //! Advisory hint whether this spec should be shared or kept separate.
  QueueSharingPreference sharing_preference;
};

//! Strategy interface that produces queue specifications and canonical keys for
//! commonly used queues.
/*!
 Implementations of QueuesStrategy declare which queues the application requires
 and provide canonical keys for each logical role. QueueManager consumes the
 returned specifications and names to create or lookup CommandQueue instances.

 Implementations are expected to be copyable (Clone()) and lightweight. The
 strategy separates policy (which queues the app wants) from backend mapping
 (how to create or assign native queues).
 */
class QueuesStrategy {
public:
  QueuesStrategy() = default;

  OXYGEN_DEFAULT_COPYABLE(QueuesStrategy)
  OXYGEN_DEFAULT_MOVABLE(QueuesStrategy)

  virtual ~QueuesStrategy() = default;

  //! Clone the concrete strategy for polymorphic copying.
  [[nodiscard]] virtual auto Clone() const -> std::unique_ptr<QueuesStrategy>
    = 0;

  //! Return the list of QueueSpecification entries defined by this strategy.
  [[nodiscard]] virtual auto Specifications() const
    -> std::vector<QueueSpecification>
    = 0;

  //! Canonical name to request for graphics submissions.
  [[nodiscard]] virtual auto KeyFor(QueueRole role) const -> QueueKey = 0;
};

//! Simple strategy that requests a single universal graphics queue.
/*!
 SingleQueueStrategy constructs a single QueueSpecification for an all-in-one,
 sharable graphics queue with key "universal".

 Use this strategy on platforms where a single graphics-capable queue should
 service all workloads.
 */
class SingleQueueStrategy final : public QueuesStrategy {
public:
  SingleQueueStrategy() = default;

  OXYGEN_DEFAULT_COPYABLE(SingleQueueStrategy)
  OXYGEN_DEFAULT_MOVABLE(SingleQueueStrategy)

  ~SingleQueueStrategy() override = default;

  [[nodiscard]] auto Specifications() const
    -> std::vector<QueueSpecification> override
  {
    return {
      QueueSpecification {
        .key = QueueKey { kSingleQueueName },
        .role = QueueRole::kGraphics,
        .allocation_preference = QueueAllocationPreference::kAllInOne,
        .sharing_preference = QueueSharingPreference::kShared,
      },
    };
  }

  [[nodiscard]] auto KeyFor(const QueueRole role) const -> QueueKey override
  {
    // Returns the same key for all roles.
    (void)role;
    return QueueKey { kSingleQueueName };
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<SingleQueueStrategy>(*this);
  }

private:
  static constexpr auto kSingleQueueName = "universal";
};

} // namespace oxygen::graphics
