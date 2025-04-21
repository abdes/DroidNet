//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! The intended use of the command queue.
/*!
 QueueRole expresses the intended use of a queue. The backend implementation
 is responsible for mapping these roles to API-specific queue types or families.
 For example, in D3D12, kPresent maps to a graphics queue.
*/
enum class QueueRole : int8_t {
    kGraphics = 0, //!< Graphics command queue.
    kCompute = 1, //!< Compute command queue.
    kTransfer = 2, //!< Copy command queue.
    kPresent = 3, //!< Presentation command queue.

    kNone = -1 //!< Invalid command queue.
};

//! String representation of enum values in `QueueFamilyType`.
OXYGEN_GFX_API auto to_string(QueueRole value) -> const char*;

//! The preferred allocation of strategy of queues for their intended role.
/*!
 QueueAllocationPreference expresses whether the application prefers to use a single queue
 for all roles (graphics, compute, transfer, present), or to use dedicated queues for each
 role if the hardware supports it. The backend implementation will map this preference to
 the underlying API's queue family or type system.
*/
enum class QueueAllocationPreference {
    // In practice, all devices offer a graphics family, which implies compute,
    // which in turn implies transfer. All devices support presentation from q
    // queue in the the graphics family.
    kAllInOne,
    // Use a dedicated family for the operation type if present.
    kDedicated
};

//! String representation of enum values in `CommandListType`.
OXYGEN_GFX_API auto to_string(QueueAllocationPreference value) -> const char*;

//! The preferred sharing strategy of queues.
/*!
 QueueSharingPreference indicates whether the application prefers to share a queue among
 multiple roles (if allowed by the driver and hardware), or to use a separate queue for
 each role. If a separate queue cannot be created due to hardware or driver limits, the
 backend may fall back to a shared queue.
*/
enum class QueueSharingPreference {
    // Use a shared queue from the requested role. If no queue has previously
    // been created, request a new one within the limits of the driver (number
    // of queues for a particular role is limited).
    kShared,
    // Prefer a separate queue created within the limits of the driver (number
    // of queues for a particular role is limited). If not possible, fall back
    // to a shared queue.
    kSeparate
};

//! String representation of enum values in `QueueSharingPreference`.
OXYGEN_GFX_API auto to_string(QueueSharingPreference value) -> const char*;

} // namespace oxygen::graphics
