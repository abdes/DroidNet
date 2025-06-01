//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Types/Queues.h>

namespace oxygen::graphics {

//! A specification of a queue for a certain role in the graphics system.
/*!
 QueueSpecification defines the properties of a queue that the application
 requires. The backend implementation is responsible for mapping these
 specifications to API-specific queue types or families.
*/
struct QueueSpecification {
    // A unique name to identify the queue in the application domain, and which
    // will also be used to obtain the queue from the device at any time later.
    std::string name;

    QueueRole role;
    QueueAllocationPreference allocation_preference;
    QueueSharingPreference sharing_preference;
};

//! Abstract interface for queue selection strategies.
/*!
 QueueStrategy defines how command queues are specified and selected for
 different roles (graphics, compute, transfer, present) in the graphics system.
 Implementations of this interface provide the set of queue specifications
 required by the application and map each operation type to a named queue. This
 abstraction decouples application code from the physical queue topology of the
 device and allows flexible queue management strategies.

 The backend implementation is responsible for mapping the requested queue roles
 and preferences to API-specific queue types, families, or indices.
*/
class QueueStrategy {
public:
    QueueStrategy() = default;
    virtual ~QueueStrategy() = default;

    OXYGEN_DEFAULT_COPYABLE(QueueStrategy);
    OXYGEN_DEFAULT_MOVABLE(QueueStrategy);

    [[nodiscard]] virtual auto Specifications() const
        -> std::vector<QueueSpecification>
        = 0;

    // Get the queue name for a certain type of operation from the queue
    // strategy and then use it to request the corresponding queue from the
    // device. This way, the application code is decoupled from the topology of
    // queues and their roles, whether the physical device offers a single
    // family or not or many queues per family or not.

    [[nodiscard]] virtual auto GraphicsQueueName() const -> std::string_view = 0;
    [[nodiscard]] virtual auto PresentQueueName() const -> std::string_view = 0;
    [[nodiscard]] virtual auto ComputeQueueName() const -> std::string_view = 0;
    [[nodiscard]] virtual auto TransferQueueName() const -> std::string_view = 0;
};

//! A queue strategy that provides a single queue from the all-in-one queue
//! family. This is the default strategy for most devices and is the most common
//! configuration for graphics applications.
/*!
 In practice, graphics implies compute support. Compute implies transfer
 support. Every device supports presenting from a graphics queue. In general
 there is no need to present from a dedicated queue unless you are trying to
 reach very high frame rate per second.

 The interface still abstracts this design choice through the use of the
 <Family>QueueName() methods. Using these methods to get a suitable queue for
 the operations that need to be submitted allows easy move to a different
 strategy later.
*/
class SingleQueueStrategy final : public QueueStrategy {
public:
    SingleQueueStrategy() = default;
    ~SingleQueueStrategy() override = default;

    OXYGEN_DEFAULT_COPYABLE(SingleQueueStrategy);
    OXYGEN_DEFAULT_MOVABLE(SingleQueueStrategy);

    [[nodiscard]] auto Specifications() const
        -> std::vector<QueueSpecification> override
    {
        return { {
            .name = kSingleQueueName,
            .role = QueueRole::kGraphics,
            .allocation_preference = QueueAllocationPreference::kAllInOne,
            .sharing_preference = QueueSharingPreference::kShared,
        } };
    }
    [[nodiscard]] auto GraphicsQueueName() const -> std::string_view override
    {
        return kSingleQueueName;
    }
    [[nodiscard]] auto PresentQueueName() const -> std::string_view override
    {
        return kSingleQueueName;
    }
    [[nodiscard]] auto ComputeQueueName() const -> std::string_view override
    {
        return kSingleQueueName;
    }
    [[nodiscard]] auto TransferQueueName() const -> std::string_view override
    {
        return kSingleQueueName;
    }

private:
    inline static constexpr const char* kSingleQueueName = "universal";
};

} // namespace oxygen::graphics
