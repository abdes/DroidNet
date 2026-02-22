//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Jolt/Jolt.h> // Used - Must always be first (keep separate)

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltBodies.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>

oxygen::physics::jolt::JoltBodies::JoltBodies(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltBodies::CreateBody(
  const WorldId world_id, const body::BodyDesc& desc) -> PhysicsResult<BodyId>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto shape_result = MakeShape(desc.shape);
  if (shape_result.has_error()) {
    return Err(shape_result.error());
  }
  const auto motion_type = ToMotionType(desc.type);
  JPH::BodyCreationSettings settings(shape_result.value(),
    ToJoltRVec3(desc.initial_position), ToJoltQuat(desc.initial_rotation),
    motion_type, ToObjectLayer(desc.type));
  settings.mIsSensor
    = (desc.flags & body::BodyFlags::kIsTrigger) != body::BodyFlags::kNone;
  settings.mGravityFactor
    = (desc.flags & body::BodyFlags::kEnableGravity) != body::BodyFlags::kNone
    ? 1.0F
    : 0.0F;
  settings.mMotionQuality
    = (desc.flags & body::BodyFlags::kEnableContinuousCollisionDetection)
      != body::BodyFlags::kNone
    ? JPH::EMotionQuality::LinearCast
    : JPH::EMotionQuality::Discrete;

  const auto jolt_body_id
    = body_interface->CreateAndAddBody(settings, ToActivation(desc.type));
  if (jolt_body_id.IsInvalid()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto body_id = BodyId { jolt_body_id.GetIndexAndSequenceNumber() };
  const auto register_result = world->RegisterBody(world_id, body_id);
  if (register_result.has_error()) {
    body_interface->RemoveBody(jolt_body_id);
    body_interface->DestroyBody(jolt_body_id);
    return Err(register_result.error());
  }
  return Ok(body_id);
}

auto oxygen::physics::jolt::JoltBodies::DestroyBody(
  const WorldId world_id, const BodyId body_id) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  const auto jolt_body_id = ToJoltBodyId(body_id);
  body_interface->RemoveBody(jolt_body_id);
  body_interface->DestroyBody(jolt_body_id);
  return world->UnregisterBody(world_id, body_id);
}

auto oxygen::physics::jolt::JoltBodies::GetBodyPosition(
  const WorldId world_id, const BodyId body_id) const -> PhysicsResult<Vec3>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  return Ok(ToOxygenVec3(body_interface->GetPosition(ToJoltBodyId(body_id))));
}

auto oxygen::physics::jolt::JoltBodies::GetBodyRotation(
  const WorldId world_id, const BodyId body_id) const -> PhysicsResult<Quat>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  return Ok(ToOxygenQuat(body_interface->GetRotation(ToJoltBodyId(body_id))));
}

auto oxygen::physics::jolt::JoltBodies::SetBodyPosition(const WorldId world_id,
  const BodyId body_id, const Vec3& position) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  body_interface->SetPosition(ToJoltBodyId(body_id), ToJoltRVec3(position),
    JPH::EActivation::DontActivate);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::SetBodyRotation(const WorldId world_id,
  const BodyId body_id, const Quat& rotation) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  body_interface->SetRotation(ToJoltBodyId(body_id), ToJoltQuat(rotation),
    JPH::EActivation::DontActivate);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::SetBodyPose(const WorldId world_id,
  const BodyId body_id, const Vec3& position, const Quat& rotation)
  -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  body_interface->SetPositionAndRotation(ToJoltBodyId(body_id),
    ToJoltRVec3(position), ToJoltQuat(rotation),
    JPH::EActivation::DontActivate);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::GetLinearVelocity(
  const WorldId world_id, const BodyId body_id) const -> PhysicsResult<Vec3>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  return Ok(
    ToOxygenVec3(body_interface->GetLinearVelocity(ToJoltBodyId(body_id))));
}

auto oxygen::physics::jolt::JoltBodies::GetAngularVelocity(
  const WorldId world_id, const BodyId body_id) const -> PhysicsResult<Vec3>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  return Ok(
    ToOxygenVec3(body_interface->GetAngularVelocity(ToJoltBodyId(body_id))));
}

auto oxygen::physics::jolt::JoltBodies::SetLinearVelocity(
  const WorldId world_id, const BodyId body_id, const Vec3& velocity)
  -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  body_interface->SetLinearVelocity(
    ToJoltBodyId(body_id), ToJoltVec3(velocity));
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::SetAngularVelocity(
  const WorldId world_id, const BodyId body_id, const Vec3& velocity)
  -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  body_interface->SetAngularVelocity(
    ToJoltBodyId(body_id), ToJoltVec3(velocity));
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::AddForce(const WorldId world_id,
  const BodyId body_id, const Vec3& force) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  body_interface->AddForce(ToJoltBodyId(body_id), ToJoltVec3(force));
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::AddImpulse(const WorldId world_id,
  const BodyId body_id, const Vec3& impulse) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  body_interface->AddImpulse(ToJoltBodyId(body_id), ToJoltVec3(impulse));
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::AddTorque(const WorldId world_id,
  const BodyId body_id, const Vec3& torque) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  body_interface->AddTorque(ToJoltBodyId(body_id), ToJoltVec3(torque));
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::MoveKinematic(const WorldId world_id,
  const BodyId body_id, const Vec3& target_position,
  const Quat& target_rotation, const float delta_time) -> PhysicsResult<void>
{
  if (delta_time <= 0.0F) {
    return Err(PhysicsError::kInvalidArgument);
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  const auto jolt_body_id = ToJoltBodyId(body_id);
  if (body_interface->GetMotionType(jolt_body_id)
    != JPH::EMotionType::Kinematic) {
    return Err(PhysicsError::kInvalidArgument);
  }

  body_interface->MoveKinematic(jolt_body_id, ToJoltRVec3(target_position),
    ToJoltQuat(target_rotation), delta_time);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltBodies::AddBodyShape(WorldId /*world_id*/,
  BodyId /*body_id*/, ShapeId /*shape_id*/, const Vec3& /*local_position*/,
  const Quat& /*local_rotation*/) -> PhysicsResult<ShapeInstanceId>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltBodies::RemoveBodyShape(WorldId /*world_id*/,
  BodyId /*body_id*/, ShapeInstanceId /*shape_instance_id*/)
  -> PhysicsResult<void>
{
  return Err(PhysicsError::kNotImplemented);
}
