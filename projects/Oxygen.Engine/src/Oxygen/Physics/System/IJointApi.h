//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/Joint/JointDesc.h>
#include <Oxygen/Physics/PhysicsError.h>

namespace oxygen::physics::system {

//! Joint/constraint domain API.
/*!
 Responsibilities now:
 - Create and destroy constraints between bodies.
 - Enable/disable joints at runtime.

 ### Near Future

 - Expose per-joint limits, motors, break thresholds, drives, and solver
   tuning.
*/
class IJointApi {
public:
  IJointApi() = default;
  virtual ~IJointApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IJointApi)
  OXYGEN_MAKE_NON_MOVABLE(IJointApi)

  virtual auto CreateJoint(WorldId world_id, const joint::JointDesc& desc)
    -> PhysicsResult<JointId>
    = 0;
  virtual auto DestroyJoint(WorldId world_id, JointId joint_id)
    -> PhysicsResult<void>
    = 0;
  virtual auto SetJointEnabled(WorldId world_id, JointId joint_id, bool enabled)
    -> PhysicsResult<void>
    = 0;
};

} // namespace oxygen::physics::system
