//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Core/Reference.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/System/IJointApi.h>

namespace oxygen::physics::jolt {

class JoltWorld;

//! Jolt implementation of the joint domain.
class JoltJoints final : public system::IJointApi {
public:
  explicit JoltJoints(JoltWorld& world);
  ~JoltJoints() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltJoints)
  OXYGEN_MAKE_NON_MOVABLE(JoltJoints)

  auto CreateJoint(WorldId world_id, const joint::JointDesc& desc)
    -> PhysicsResult<JointId> override;
  auto DestroyJoint(WorldId world_id, JointId joint_id)
    -> PhysicsResult<void> override;
  auto SetJointEnabled(WorldId world_id, JointId joint_id, bool enabled)
    -> PhysicsResult<void> override;

private:
  struct JointState final {
    WorldId world_id { kInvalidWorldId };
    JPH::Ref<JPH::TwoBodyConstraint> constraint {};
  };

  observer_ptr<JoltWorld> world_ {};
  mutable std::mutex mutex_ {};
  uint32_t next_joint_id_ { 1U };
  std::unordered_map<JointId, JointState> joints_ {};
};

} // namespace oxygen::physics::jolt
