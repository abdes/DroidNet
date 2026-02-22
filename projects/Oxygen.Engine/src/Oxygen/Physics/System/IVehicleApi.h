//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>

namespace oxygen::physics::system {

//! Vehicle domain API integration point.
/*!
 Responsibilities now:
 - Define a backend-agnostic vehicle entry point keyed by `AggregateId`.
 - Reserve contract surface for vehicle aggregate lifecycle and body membership.

 ### Near Future

 - Add wheel setup, suspension/drivetrain control, and telemetry queries.
*/
class IVehicleApi {
public:
  IVehicleApi() = default;
  virtual ~IVehicleApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IVehicleApi)
  OXYGEN_MAKE_NON_MOVABLE(IVehicleApi)

  virtual auto CreateVehicle(WorldId world_id, BodyId chassis_body_id,
    std::span<const BodyId> wheel_body_ids) -> PhysicsResult<AggregateId>
    = 0;
  virtual auto DestroyVehicle(WorldId world_id, AggregateId vehicle_id)
    -> PhysicsResult<void>
    = 0;
};

} // namespace oxygen::physics::system
