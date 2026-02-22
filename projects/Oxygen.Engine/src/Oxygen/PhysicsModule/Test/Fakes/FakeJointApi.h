//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Physics/PhysicsError.h>
#include <Oxygen/Physics/System/IJointApi.h>
#include <Oxygen/PhysicsModule/Test/Fakes/BackendState.h>

namespace oxygen::physics::test::detail {

class FakeJointApi final : public system::IJointApi {
public:
  explicit FakeJointApi(BackendState& state)
    : state_(&state)
  {
  }

  auto CreateJoint(WorldId world_id, const joint::JointDesc&)
    -> PhysicsResult<JointId> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto joint_id = state_->next_joint_id;
    state_->next_joint_id = JointId { state_->next_joint_id.get() + 1U };
    return PhysicsResult<JointId>::Ok(joint_id);
  }

  auto DestroyJoint(WorldId world_id, JointId) -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

  auto SetJointEnabled(WorldId world_id, JointId, bool)
    -> PhysicsResult<void> override
  {
    if (world_id != state_->world_id || !state_->world_created) {
      return Err(PhysicsError::kWorldNotFound);
    }
    return PhysicsResult<void>::Ok();
  }

private:
  observer_ptr<BackendState> state_;
};

} // namespace oxygen::physics::test::detail
