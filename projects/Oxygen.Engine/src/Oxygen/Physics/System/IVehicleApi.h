//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Aggregate/AggregateAuthority.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/Vehicle/VehicleDesc.h>

namespace oxygen::physics::system {

//! Vehicle domain API integration point.
/*!
 Responsibilities now:
 - Create and destroy vehicle aggregates keyed by `AggregateId`.
 - Define vehicle control and state query contract.
 - Keep wheel/chassis topology explicit and backend-agnostic.
 - Expose authority policy and structural mutation flush boundaries.

 ### Near Future

 - Add wheel setup, suspension/drivetrain control, and telemetry queries.
*/
class IVehicleApi {
public:
  IVehicleApi() = default;
  virtual ~IVehicleApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IVehicleApi)
  OXYGEN_MAKE_NON_MOVABLE(IVehicleApi)

  virtual auto CreateVehicle(WorldId world_id, const vehicle::VehicleDesc& desc)
    -> PhysicsResult<AggregateId>
    = 0;
  virtual auto DestroyVehicle(WorldId world_id, AggregateId vehicle_id)
    -> PhysicsResult<void>
    = 0;

  virtual auto SetControlInput(WorldId world_id, AggregateId vehicle_id,
    const vehicle::VehicleControlInput& input) -> PhysicsResult<void>
    = 0;
  virtual auto GetState(WorldId world_id, AggregateId vehicle_id) const
    -> PhysicsResult<vehicle::VehicleState>
    = 0;
  virtual auto GetAuthority(WorldId world_id, AggregateId vehicle_id) const
    -> PhysicsResult<aggregate::AggregateAuthority>
    = 0;
  virtual auto FlushStructuralChanges(WorldId world_id) -> PhysicsResult<size_t>
    = 0;
};

} // namespace oxygen::physics::system
