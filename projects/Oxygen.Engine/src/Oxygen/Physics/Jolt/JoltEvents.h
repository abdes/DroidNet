//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/System/IEventApi.h>

namespace oxygen::physics::jolt {

//! Jolt implementation of the event domain.
class JoltEvents final : public system::IEventApi {
public:
  JoltEvents() = default;
  ~JoltEvents() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltEvents)
  OXYGEN_MAKE_NON_MOVABLE(JoltEvents)

  auto GetPendingEventCount(WorldId world_id) const -> PhysicsResult<size_t> override;
  auto DrainEvents(WorldId world_id, std::span<events::PhysicsEvent> out_events)
    -> PhysicsResult<size_t> override;
};

} // namespace oxygen::physics::jolt
