//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Physics/Handles.h>

namespace oxygen::physics::vehicle {

/*!
 Describes vehicle aggregate creation from existing rigid bodies.

Ownership contract:
 - `chassis_body_id` and `wheels[].body_id` must reference existing bodies in
the target world.
- Vehicle aggregate owns topology/roles, but does not own body lifetime.
*/
enum class VehicleWheelSide : uint8_t {
  kLeft = 0,
  kRight = 1,
};

struct VehicleWheelDesc final {
  BodyId body_id { kInvalidBodyId };
  //! Axle ordering contract:
  //! - Lower index = more forward axle in vehicle local forward direction.
  //! - The leading axle (minimum index) is treated as the steering axle.
  uint16_t axle_index { 0 };
  //! Side is left/right within an axle role; used for topology validation and
  //! backend wheel-role mapping.
  VehicleWheelSide side { VehicleWheelSide::kLeft };
};

struct VehicleDesc final {
  BodyId chassis_body_id { kInvalidBodyId };
  std::span<const VehicleWheelDesc> wheels {};
  std::span<const uint8_t> constraint_settings_blob {};
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
  float hand_brake { 0.0F }; // [0, 1]
};

/*!
 Read-only lightweight vehicle state snapshot.
*/
struct VehicleState final {
  float forward_speed_mps { 0.0F };
  bool grounded { false };
};

} // namespace oxygen::physics::vehicle
