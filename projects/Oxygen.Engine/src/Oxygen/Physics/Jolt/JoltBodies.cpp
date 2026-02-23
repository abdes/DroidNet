//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>
#include <vector>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

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
    ? desc.gravity_factor
    : 0.0F;
  settings.mLinearDamping = desc.linear_damping;
  settings.mAngularDamping = desc.angular_damping;
  settings.mFriction = desc.friction;
  settings.mRestitution = desc.restitution;
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
  // Store the Oxygen BodyId in the Jolt body's user-data slot so that
  // IQueryApi::Overlap results and PhysicsEvent::user_data_a/b can be
  // reconstructed as BodyId values. Without this, user_data is always 0.
  body_interface->SetUserData(
    jolt_body_id, static_cast<uint64_t>(body_id.get()));
  const auto register_result = world->RegisterBody(world_id, body_id);
  if (register_result.has_error()) {
    body_interface->RemoveBody(jolt_body_id);
    body_interface->DestroyBody(jolt_body_id);
    return Err(register_result.error());
  }

  {
    std::scoped_lock lock(body_state_mutex_);
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

  auto physics_system = world->TryGetPhysicsSystem(world_id);
  if (physics_system == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::unordered_map<ShapeInstanceId, ShapeInstanceState>
    shape_instances_to_remove {};
  {
    std::scoped_lock lock(body_state_mutex_);
    pending_rebuilds_.erase(
      BodyKey { .world_id = world_id, .body_id = body_id });
    const auto body_it
      = body_states_.find(BodyKey { .world_id = world_id, .body_id = body_id });
    if (body_it != body_states_.end()) {
      shape_instances_to_remove = std::move(body_it->second.shape_instances);
      body_states_.erase(body_it);
    }
  }

  // Remove all constraints referencing this body before destroying it.
  // This prevents stale two-body constraints from surviving scene reload
  // teardown and being evaluated against an invalid body pointer.
  const auto target_jolt_body_id = ToJoltBodyId(body_id);
  const auto constraints = physics_system->GetConstraints();
  std::vector<JPH::Constraint*> constraints_to_remove {};
  constraints_to_remove.reserve(constraints.size());
  for (const auto& constraint_ref : constraints) {
    if (constraint_ref == nullptr) {
      continue;
    }
    auto* constraint = constraint_ref.GetPtr();
    if (constraint->GetType() != JPH::EConstraintType::TwoBodyConstraint) {
      continue;
    }
    auto* two_body = static_cast<JPH::TwoBodyConstraint*>(constraint);
    const auto body1 = two_body->GetBody1();
    const auto body2 = two_body->GetBody2();
    const bool matches_body_1
      = body1 != nullptr && body1->GetID() == target_jolt_body_id;
    const bool matches_body_2
      = body2 != nullptr && body2->GetID() == target_jolt_body_id;
    if (matches_body_1 || matches_body_2) {
      constraints_to_remove.push_back(constraint);
    }
  }
  if (!constraints_to_remove.empty()) {
    physics_system->RemoveConstraints(constraints_to_remove.data(),
      static_cast<int>(constraints_to_remove.size()));
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

auto oxygen::physics::jolt::JoltBodies::GetBodyPoses(const WorldId world_id,
  const std::span<const BodyId> body_ids, const std::span<Vec3> out_positions,
  const std::span<Quat> out_rotations) const -> PhysicsResult<size_t>
{
  if (out_positions.size() < body_ids.size()
    || out_rotations.size() < body_ids.size()) {
    return Err(PhysicsError::kBufferTooSmall);
  }

  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  for (size_t i = 0; i < body_ids.size(); ++i) {
    const auto body_id = body_ids[i];
    if (!world->HasBody(world_id, body_id)) {
      return Err(PhysicsError::kBodyNotFound);
    }
    const auto jolt_body_id = ToJoltBodyId(body_id);
    out_positions[i] = ToOxygenVec3(body_interface->GetPosition(jolt_body_id));
    out_rotations[i] = ToOxygenQuat(body_interface->GetRotation(jolt_body_id));
  }

  return Ok(body_ids.size());
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

  const auto jolt_body_id = ToJoltBodyId(body_id);
  body_interface->ActivateBody(jolt_body_id);
  body_interface->SetLinearVelocity(jolt_body_id, ToJoltVec3(velocity));
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

  const auto jolt_body_id = ToJoltBodyId(body_id);
  body_interface->ActivateBody(jolt_body_id);
  body_interface->SetAngularVelocity(jolt_body_id, ToJoltVec3(velocity));
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

  const auto jolt_body_id = ToJoltBodyId(body_id);
  body_interface->ActivateBody(jolt_body_id);
  body_interface->AddForce(jolt_body_id, ToJoltVec3(force));
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

  const auto jolt_body_id = ToJoltBodyId(body_id);
  body_interface->ActivateBody(jolt_body_id);
  body_interface->AddImpulse(jolt_body_id, ToJoltVec3(impulse));
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

  const auto jolt_body_id = ToJoltBodyId(body_id);
  body_interface->ActivateBody(jolt_body_id);
  body_interface->AddTorque(jolt_body_id, ToJoltVec3(torque));
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

auto oxygen::physics::jolt::JoltBodies::MoveKinematicBatch(
  const WorldId world_id, const std::span<const BodyId> body_ids,
  const std::span<const Vec3> target_positions,
  const std::span<const Quat> target_rotations, const float delta_time)
  -> PhysicsResult<size_t>
{
  if (delta_time <= 0.0F) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (target_positions.size() < body_ids.size()
    || target_rotations.size() < body_ids.size()) {
    return Err(PhysicsError::kBufferTooSmall);
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  for (size_t i = 0; i < body_ids.size(); ++i) {
    const auto body_id = body_ids[i];
    if (!world->HasBody(world_id, body_id)) {
      return Err(PhysicsError::kBodyNotFound);
    }
    const auto jolt_body_id = ToJoltBodyId(body_id);
    if (body_interface->GetMotionType(jolt_body_id)
      != JPH::EMotionType::Kinematic) {
      return Err(PhysicsError::kInvalidArgument);
    }

    body_interface->MoveKinematic(jolt_body_id,
      ToJoltRVec3(target_positions[i]), ToJoltQuat(target_rotations[i]),
      delta_time);
  }

  return Ok(body_ids.size());
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
  {
    std::scoped_lock lock(body_state_mutex_);
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
    EnqueueBodyRebuild(world_id, body_id);
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
  {
    std::scoped_lock lock(body_state_mutex_);
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
    EnqueueBodyRebuild(world_id, body_id);
  }

  return shapes->RemoveAttachment(removed_shape_id);
}

auto oxygen::physics::jolt::JoltBodies::FlushStructuralChanges(
  const WorldId world_id) -> PhysicsResult<size_t>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  if (world->TryGetBodyInterface(world_id) == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::vector<BodyKey> body_keys {};
  {
    std::scoped_lock lock(body_state_mutex_);
    for (const auto& key : pending_rebuilds_) {
      if (key.world_id == world_id) {
        body_keys.push_back(key);
      }
    }
  }

  size_t flushed = 0;
  for (const auto& key : body_keys) {
    BodyState snapshot {};
    {
      std::scoped_lock lock(body_state_mutex_);
      const auto it = body_states_.find(key);
      if (it == body_states_.end()) {
        pending_rebuilds_.erase(key);
        continue;
      }
      snapshot = it->second;
    }

    const auto rebuild_result
      = RebuildBodyShape(key.world_id, key.body_id, snapshot);
    if (rebuild_result.has_error()) {
      return Err(rebuild_result.error());
    }
    {
      std::scoped_lock lock(body_state_mutex_);
      pending_rebuilds_.erase(key);
    }
    flushed += 1;
  }
  return Ok(flushed);
}

auto oxygen::physics::jolt::JoltBodies::EnqueueBodyRebuild(
  const WorldId world_id, const BodyId body_id) -> void
{
  pending_rebuilds_.insert(
    BodyKey { .world_id = world_id, .body_id = body_id });
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
