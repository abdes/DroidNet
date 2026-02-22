//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/System/IEventApi.h>

namespace oxygen::physics::jolt {

class JoltWorld;

//! Jolt implementation of the event domain.
class JoltEvents final : public system::IEventApi {
public:
  explicit JoltEvents(JoltWorld& world);
  ~JoltEvents() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltEvents)
  OXYGEN_MAKE_NON_MOVABLE(JoltEvents)

  auto GetPendingEventCount(WorldId world_id) const
    -> PhysicsResult<size_t> override;
  auto DrainEvents(WorldId world_id, std::span<events::PhysicsEvent> out_events)
    -> PhysicsResult<size_t> override;

private:
  observer_ptr<JoltWorld> world_ {};
};

} // namespace oxygen::physics::jolt
