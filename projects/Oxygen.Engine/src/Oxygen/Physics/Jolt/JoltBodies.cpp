//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltBodies.h>

auto oxygen::physics::jolt::JoltBodies::CreateBody(
  WorldId /*world_id*/, const body::BodyDesc& /*desc*/) -> PhysicsResult<BodyId>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltBodies::DestroyBody(
  WorldId /*world_id*/, BodyId /*body_id*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::GetBodyPosition(
  WorldId /*world_id*/, BodyId /*body_id*/) const -> PhysicsResult<Vec3>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::GetBodyRotation(
  WorldId /*world_id*/, BodyId /*body_id*/) const -> PhysicsResult<Quat>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::SetBodyPosition(WorldId /*world_id*/,
  BodyId /*body_id*/, const Vec3& /*position*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::SetBodyRotation(WorldId /*world_id*/,
  BodyId /*body_id*/, const Quat& /*rotation*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::SetBodyPose(WorldId /*world_id*/,
  BodyId /*body_id*/, const Vec3& /*position*/, const Quat& /*rotation*/)
  -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::GetLinearVelocity(
  WorldId /*world_id*/, BodyId /*body_id*/) const -> PhysicsResult<Vec3>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::GetAngularVelocity(
  WorldId /*world_id*/, BodyId /*body_id*/) const -> PhysicsResult<Vec3>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::SetLinearVelocity(WorldId /*world_id*/,
  BodyId /*body_id*/, const Vec3& /*velocity*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::SetAngularVelocity(WorldId /*world_id*/,
  BodyId /*body_id*/, const Vec3& /*velocity*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::AddForce(WorldId /*world_id*/,
  BodyId /*body_id*/, const Vec3& /*force*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::AddImpulse(WorldId /*world_id*/,
  BodyId /*body_id*/, const Vec3& /*impulse*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::AddTorque(WorldId /*world_id*/,
  BodyId /*body_id*/, const Vec3& /*torque*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}

auto oxygen::physics::jolt::JoltBodies::MoveKinematic(WorldId /*world_id*/,
  BodyId /*body_id*/, const Vec3& /*target_position*/,
  const Quat& /*target_rotation*/, float /*delta_time*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kBodyNotFound);
}
