//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltBodies.h>
#include <Oxygen/Physics/Jolt/JoltShapes.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>

oxygen::physics::jolt::JoltBodies::JoltBodies(
  JoltWorld& world, JoltShapes& shapes)
  : world_(&world)
  , shapes_(&shapes)
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

  {
    std::scoped_lock lock(shape_instance_mutex_);
    body_states_.insert_or_assign(
      BodyKey {
        .world_id = world_id,
        .body_id = body_id,
      },
      BodyState {
        .base_shape = shape_result.value(),
      });
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

  std::unordered_map<ShapeInstanceId, ShapeInstanceState>
    shape_instances_to_remove {};
  {
    std::scoped_lock lock(shape_instance_mutex_);
    const auto body_it
      = body_states_.find(BodyKey { .world_id = world_id, .body_id = body_id });
    if (body_it != body_states_.end()) {
      shape_instances_to_remove = std::move(body_it->second.shape_instances);
      body_states_.erase(body_it);
    }
  }

  auto* shapes = shapes_.get();
  if (shapes == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  for (const auto& [instance_id, instance] : shape_instances_to_remove) {
    static_cast<void>(instance_id);
    const auto detach_result = shapes->RemoveAttachment(instance.shape_id);
    if (detach_result.has_error()) {
      return Err(detach_result.error());
    }
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

auto oxygen::physics::jolt::JoltBodies::AddBodyShape(const WorldId world_id,
  const BodyId body_id, const ShapeId shape_id, const Vec3& local_position,
  const Quat& local_rotation) -> PhysicsResult<ShapeInstanceId>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto* shapes = shapes_.get();
  if (shapes == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  const auto shape_result = shapes->TryGetShape(shape_id);
  if (shape_result.has_error()) {
    return Err(shape_result.error());
  }

  const auto attach_result = shapes->AddAttachment(shape_id);
  if (attach_result.has_error()) {
    return Err(attach_result.error());
  }

  ShapeInstanceId shape_instance_id { kInvalidShapeInstanceId };
  PhysicsResult<void> rebuild_result = PhysicsResult<void>::Ok();
  {
    std::scoped_lock lock(shape_instance_mutex_);
    auto body_it
      = body_states_.find(BodyKey { .world_id = world_id, .body_id = body_id });
    if (body_it == body_states_.end()) {
      static_cast<void>(shapes->RemoveAttachment(shape_id));
      return Err(PhysicsError::kBodyNotFound);
    }

    if (next_shape_instance_id_ == std::numeric_limits<uint32_t>::max()) {
      static_cast<void>(shapes->RemoveAttachment(shape_id));
      return Err(PhysicsError::kBackendInitFailed);
    }
    shape_instance_id = ShapeInstanceId { next_shape_instance_id_++ };
    body_it->second.shape_instances.emplace(shape_instance_id,
      ShapeInstanceState {
        .shape_id = shape_id,
        .shape = shape_result.value(),
        .local_position = local_position,
        .local_rotation = local_rotation,
      });
    rebuild_result = RebuildBodyShape(world_id, body_id, body_it->second);
    if (rebuild_result.has_error()) {
      body_it->second.shape_instances.erase(shape_instance_id);
    }
  }

  if (rebuild_result.has_error()) {
    static_cast<void>(shapes->RemoveAttachment(shape_id));
    return Err(rebuild_result.error());
  }
  return Ok(shape_instance_id);
}

auto oxygen::physics::jolt::JoltBodies::RemoveBodyShape(const WorldId world_id,
  const BodyId body_id, const ShapeInstanceId shape_instance_id)
  -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto* shapes = shapes_.get();
  if (shapes == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  if (!world->HasBody(world_id, body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  ShapeId removed_shape_id { kInvalidShapeId };
  PhysicsResult<void> rebuild_result = PhysicsResult<void>::Ok();
  {
    std::scoped_lock lock(shape_instance_mutex_);
    auto body_it
      = body_states_.find(BodyKey { .world_id = world_id, .body_id = body_id });
    if (body_it == body_states_.end()) {
      return Err(PhysicsError::kBodyNotFound);
    }

    auto instance_it = body_it->second.shape_instances.find(shape_instance_id);
    if (instance_it == body_it->second.shape_instances.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }

    const auto removed_instance = instance_it->second;
    removed_shape_id = removed_instance.shape_id;
    body_it->second.shape_instances.erase(instance_it);
    rebuild_result = RebuildBodyShape(world_id, body_id, body_it->second);
    if (rebuild_result.has_error()) {
      body_it->second.shape_instances.emplace(
        shape_instance_id, removed_instance);
    }
  }

  if (rebuild_result.has_error()) {
    return Err(rebuild_result.error());
  }
  return shapes->RemoveAttachment(removed_shape_id);
}

auto oxygen::physics::jolt::JoltBodies::RebuildBodyShape(const WorldId world_id,
  const BodyId body_id, const BodyState& state) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto jolt_body_id = ToJoltBodyId(body_id);
  const auto motion_type = body_interface->GetMotionType(jolt_body_id);
  if (state.shape_instances.empty()) {
    body_interface->SetShape(jolt_body_id, state.base_shape.GetPtr(),
      motion_type == JPH::EMotionType::Dynamic, JPH::EActivation::DontActivate);
    return PhysicsResult<void>::Ok();
  }

  JPH::StaticCompoundShapeSettings settings {};
  settings.AddShape(
    JPH::Vec3::sZero(), JPH::Quat::sIdentity(), state.base_shape.GetPtr());
  for (const auto& [id, instance] : state.shape_instances) {
    static_cast<void>(id);
    settings.AddShape(ToJoltVec3(instance.local_position),
      ToJoltQuat(instance.local_rotation), instance.shape.GetPtr());
  }
  const auto compound_result = settings.Create();
  if (!compound_result.IsValid()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  body_interface->SetShape(jolt_body_id, compound_result.Get().GetPtr(),
    motion_type == JPH::EMotionType::Dynamic, JPH::EActivation::DontActivate);
  return PhysicsResult<void>::Ok();
}
