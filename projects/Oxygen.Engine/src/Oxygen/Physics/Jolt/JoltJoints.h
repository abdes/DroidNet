//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Physics/System/IJointApi.h>

namespace oxygen::physics::jolt {

//! Jolt implementation of the joint domain.
class JoltJoints final : public system::IJointApi {
public:
  JoltJoints() = default;
  ~JoltJoints() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltJoints)
  OXYGEN_MAKE_NON_MOVABLE(JoltJoints)

  auto CreateJoint(WorldId world_id, const joint::JointDesc& desc)
    -> PhysicsResult<JointId> override;
  auto DestroyJoint(WorldId world_id, JointId joint_id)
    -> PhysicsResult<void> override;
  auto SetJointEnabled(WorldId world_id, JointId joint_id, bool enabled)
    -> PhysicsResult<void> override;
};

} // namespace oxygen::physics::jolt
