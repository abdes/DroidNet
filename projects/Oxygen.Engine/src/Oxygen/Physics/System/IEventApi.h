//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Events/PhysicsEvents.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>

namespace oxygen::physics::system {

//! Physics event domain API.
/*!
 Responsibilities now:
 - Report pending event counts.
 - Drain buffered events into caller-owned storage.

 ### Near Future

 - Extend event classes and delivery modes (for example contact phases,
   trigger phases, sleep/wake, break events, and subscription masks).
*/
class IEventApi {
public:
  IEventApi() = default;
  virtual ~IEventApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IEventApi)
  OXYGEN_MAKE_NON_MOVABLE(IEventApi)

  virtual auto GetPendingEventCount(WorldId world_id) const
    -> PhysicsResult<size_t> = 0;
  virtual auto DrainEvents(WorldId world_id,
    std::span<events::PhysicsEvent> out_events) -> PhysicsResult<size_t> = 0;
};

} // namespace oxygen::physics::system
