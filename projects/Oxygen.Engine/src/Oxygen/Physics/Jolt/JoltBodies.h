//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Physics/System/IBodyApi.h>

namespace oxygen::physics::jolt {

class JoltWorld;

//! Jolt implementation of the body domain.
class JoltBodies final : public system::IBodyApi {
public:
  explicit JoltBodies(JoltWorld& world);
  ~JoltBodies() override = default;

  OXYGEN_MAKE_NON_COPYABLE(JoltBodies)
  OXYGEN_MAKE_NON_MOVABLE(JoltBodies)

  auto CreateBody(WorldId world_id, const body::BodyDesc& desc)
    -> PhysicsResult<BodyId> override;
  auto DestroyBody(WorldId world_id, BodyId body_id)
    -> PhysicsResult<void> override;
  auto GetBodyPosition(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override;
  auto GetBodyRotation(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Quat> override;
  auto SetBodyPosition(WorldId world_id, BodyId body_id, const Vec3& position)
    -> PhysicsResult<void> override;
  auto SetBodyRotation(WorldId world_id, BodyId body_id, const Quat& rotation)
    -> PhysicsResult<void> override;
  auto SetBodyPose(WorldId world_id, BodyId body_id, const Vec3& position,
    const Quat& rotation) -> PhysicsResult<void> override;

  auto GetLinearVelocity(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override;
  auto GetAngularVelocity(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3> override;
  auto SetLinearVelocity(WorldId world_id, BodyId body_id, const Vec3& velocity)
    -> PhysicsResult<void> override;
  auto SetAngularVelocity(WorldId world_id, BodyId body_id,
    const Vec3& velocity) -> PhysicsResult<void> override;

  auto AddForce(WorldId world_id, BodyId body_id, const Vec3& force)
    -> PhysicsResult<void> override;
  auto AddImpulse(WorldId world_id, BodyId body_id, const Vec3& impulse)
    -> PhysicsResult<void> override;
  auto AddTorque(WorldId world_id, BodyId body_id, const Vec3& torque)
    -> PhysicsResult<void> override;

  auto MoveKinematic(WorldId world_id, BodyId body_id,
    const Vec3& target_position, const Quat& target_rotation, float delta_time)
    -> PhysicsResult<void> override;
  auto AddBodyShape(WorldId world_id, BodyId body_id, ShapeId shape_id,
    const Vec3& local_position, const Quat& local_rotation)
    -> PhysicsResult<ShapeInstanceId> override;
  auto RemoveBodyShape(WorldId world_id, BodyId body_id,
    ShapeInstanceId shape_instance_id) -> PhysicsResult<void> override;

private:
  observer_ptr<JoltWorld> world_ {};
};

} // namespace oxygen::physics::jolt
