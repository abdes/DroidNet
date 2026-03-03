//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Physics/Handles.h>

namespace oxygen::physics::vehicle {

/*!
 Describes vehicle aggregate creation from existing rigid bodies.

 Ownership contract:
 - `chassis_body_id` and `wheel_body_ids` must reference existing bodies in the
   target world.
 - Vehicle aggregate owns topology/roles, but does not own body lifetime.
*/
struct VehicleDesc final {
  BodyId chassis_body_id { kInvalidBodyId };
  std::span<const BodyId> wheel_body_ids {};
};

/*!
 Control input for one simulation step.

 Contract:
 - Value domains are backend-agnostic normalized ranges.
 - Backends may clamp and map to backend-specific control limits.
 - `forward` sign convention: positive = accelerate forward,
   negative = reverse (for auto-transmission backends).
 - `steering` sign convention: positive = turn right.
*/
struct VehicleControlInput final {
  float forward { 0.0F }; // [-1, 1]  (negative = reverse)
  float brake { 0.0F }; // [0, 1]
  float steering { 0.0F }; // [-1, 1]  (positive = right)
  float handbrake { 0.0F }; // [0, 1]
};

/*!
 Read-only lightweight vehicle state snapshot.
*/
struct VehicleState final {
  float forward_speed_mps { 0.0F };
  bool grounded { false };
};

} // namespace oxygen::physics::vehicle
