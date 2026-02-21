//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Handles.h>
#include <Oxygen/Physics/PhysicsError.h>

namespace oxygen::physics::system {

//! Rigid-body domain API.
/*!
 Responsibilities now:
 - Create and destroy bodies in a world.
 - Read and write body pose state.

 ### Near Future

 - Extend with velocity, force/impulse, damping, sleep/awake, materials,
   collision filtering, and mass property controls.
*/
class IBodyApi {
public:
  IBodyApi() = default;
  virtual ~IBodyApi() = default;

  OXYGEN_MAKE_NON_COPYABLE(IBodyApi)
  OXYGEN_MAKE_NON_MOVABLE(IBodyApi)

  virtual auto CreateBody(WorldId world_id, const body::BodyDesc& desc)
    -> PhysicsResult<BodyId>
    = 0;
  virtual auto DestroyBody(WorldId world_id, BodyId body_id)
    -> PhysicsResult<void>
    = 0;

  virtual auto GetBodyPosition(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3>
    = 0;
  virtual auto GetBodyRotation(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Quat>
    = 0;
  virtual auto SetBodyPosition(WorldId world_id, BodyId body_id,
    const Vec3& position) -> PhysicsResult<void>
    = 0;
  virtual auto SetBodyRotation(WorldId world_id, BodyId body_id,
    const Quat& rotation) -> PhysicsResult<void>
    = 0;
  virtual auto SetBodyPose(WorldId world_id, BodyId body_id,
    const Vec3& position, const Quat& rotation) -> PhysicsResult<void>
    = 0;

  virtual auto GetLinearVelocity(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3>
    = 0;
  virtual auto GetAngularVelocity(WorldId world_id, BodyId body_id) const
    -> PhysicsResult<Vec3>
    = 0;
  virtual auto SetLinearVelocity(WorldId world_id, BodyId body_id,
    const Vec3& velocity) -> PhysicsResult<void>
    = 0;
  virtual auto SetAngularVelocity(WorldId world_id, BodyId body_id,
    const Vec3& velocity) -> PhysicsResult<void>
    = 0;

  virtual auto AddForce(WorldId world_id, BodyId body_id, const Vec3& force)
    -> PhysicsResult<void>
    = 0;
  virtual auto AddImpulse(WorldId world_id, BodyId body_id, const Vec3& impulse)
    -> PhysicsResult<void>
    = 0;
  virtual auto AddTorque(WorldId world_id, BodyId body_id, const Vec3& torque)
    -> PhysicsResult<void>
    = 0;

  virtual auto MoveKinematic(WorldId world_id, BodyId body_id,
    const Vec3& target_position, const Quat& target_rotation, float delta_time)
    -> PhysicsResult<void>
    = 0;
};

} // namespace oxygen::physics::system
