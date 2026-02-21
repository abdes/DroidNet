//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltJoints.h>

auto oxygen::physics::jolt::JoltJoints::CreateJoint(WorldId /*world_id*/,
  const joint::JointDesc& /*desc*/) -> PhysicsResult<JointId>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltJoints::DestroyJoint(
  WorldId /*world_id*/, JointId /*joint_id*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltJoints::SetJointEnabled(WorldId /*world_id*/,
  JointId /*joint_id*/, bool /*enabled*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kNotImplemented);
}
