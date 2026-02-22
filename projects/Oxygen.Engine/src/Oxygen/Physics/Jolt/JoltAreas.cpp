//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>
#include <utility>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltAreas.h>
#include <Oxygen/Physics/Jolt/JoltShapes.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>

oxygen::physics::jolt::JoltAreas::JoltAreas(
  JoltWorld& world, JoltShapes& shapes)
  : world_(&world)
  , shapes_(&shapes)
{
}

auto oxygen::physics::jolt::JoltAreas::CreateArea(
  const WorldId world_id, const area::AreaDesc& desc) -> PhysicsResult<AreaId>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  if (next_area_id_ == std::numeric_limits<uint32_t>::max()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  JPH::BodyCreationSettings settings(
    JPH::RefConst<JPH::Shape> { new JPH::EmptyShape() },
    ToJoltRVec3(desc.initial_position), ToJoltQuat(desc.initial_rotation),
    JPH::EMotionType::Static, JPH::ObjectLayer { 0 });
  settings.mIsSensor = true;
  settings.mGravityFactor = 0.0F;
  const auto jolt_body_id = body_interface->CreateAndAddBody(
    settings, JPH::EActivation::DontActivate);
  if (jolt_body_id.IsInvalid()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto area_id = AreaId { next_area_id_++ };
  std::scoped_lock lock(mutex_);
  areas_.insert_or_assign(AreaKey { .world_id = world_id, .area_id = area_id },
    AreaState {
      .jolt_body_id = jolt_body_id,
    });
  return Ok(area_id);
}

auto oxygen::physics::jolt::JoltAreas::DestroyArea(
  const WorldId world_id, const AreaId area_id) -> PhysicsResult<void>
{
  auto* world = world_.get();
  auto* shapes = shapes_.get();
  if (world == nullptr || shapes == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  AreaState removed_state {};
  {
    std::scoped_lock lock(mutex_);
    const auto area_it
      = areas_.find(AreaKey { .world_id = world_id, .area_id = area_id });
    if (area_it == areas_.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    removed_state = std::move(area_it->second);
    areas_.erase(area_it);
  }

  for (const auto& [instance_id, instance] : removed_state.shape_instances) {
    static_cast<void>(instance_id);
    const auto detach_result = shapes->RemoveAttachment(instance.shape_id);
    if (detach_result.has_error()) {
      return Err(detach_result.error());
    }
  }

  body_interface->RemoveBody(removed_state.jolt_body_id);
  body_interface->DestroyBody(removed_state.jolt_body_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltAreas::GetAreaPosition(
  const WorldId world_id, const AreaId area_id) const -> PhysicsResult<Vec3>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  JPH::BodyID jolt_body_id {};
  {
    std::scoped_lock lock(mutex_);
    const auto area_it
      = areas_.find(AreaKey { .world_id = world_id, .area_id = area_id });
    if (area_it == areas_.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    jolt_body_id = area_it->second.jolt_body_id;
  }
  return Ok(ToOxygenVec3(body_interface->GetPosition(jolt_body_id)));
}

auto oxygen::physics::jolt::JoltAreas::GetAreaRotation(
  const WorldId world_id, const AreaId area_id) const -> PhysicsResult<Quat>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  JPH::BodyID jolt_body_id {};
  {
    std::scoped_lock lock(mutex_);
    const auto area_it
      = areas_.find(AreaKey { .world_id = world_id, .area_id = area_id });
    if (area_it == areas_.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    jolt_body_id = area_it->second.jolt_body_id;
  }
  return Ok(ToOxygenQuat(body_interface->GetRotation(jolt_body_id)));
}

auto oxygen::physics::jolt::JoltAreas::SetAreaPose(const WorldId world_id,
  const AreaId area_id, const Vec3& position, const Quat& rotation)
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

  JPH::BodyID jolt_body_id {};
  {
    std::scoped_lock lock(mutex_);
    const auto area_it
      = areas_.find(AreaKey { .world_id = world_id, .area_id = area_id });
    if (area_it == areas_.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    jolt_body_id = area_it->second.jolt_body_id;
  }
  body_interface->SetPositionAndRotation(jolt_body_id, ToJoltRVec3(position),
    ToJoltQuat(rotation), JPH::EActivation::DontActivate);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltAreas::AddAreaShape(const WorldId world_id,
  const AreaId area_id, const ShapeId shape_id, const Vec3& local_position,
  const Quat& local_rotation) -> PhysicsResult<ShapeInstanceId>
{
  auto* world = world_.get();
  auto* shapes = shapes_.get();
  if (world == nullptr || shapes == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto shape_result = shapes->TryGetShape(shape_id);
  if (shape_result.has_error()) {
    return Err(shape_result.error());
  }
  const auto attach_result = shapes->AddAttachment(shape_id);
  if (attach_result.has_error()) {
    return Err(attach_result.error());
  }

  AreaState updated_state {};
  ShapeInstanceId shape_instance_id { kInvalidShapeInstanceId };
  {
    std::scoped_lock lock(mutex_);
    const auto area_it
      = areas_.find(AreaKey { .world_id = world_id, .area_id = area_id });
    if (area_it == areas_.end()) {
      static_cast<void>(shapes->RemoveAttachment(shape_id));
      return Err(PhysicsError::kInvalidArgument);
    }
    if (next_shape_instance_id_ == std::numeric_limits<uint32_t>::max()) {
      static_cast<void>(shapes->RemoveAttachment(shape_id));
      return Err(PhysicsError::kBackendInitFailed);
    }
    shape_instance_id = ShapeInstanceId { next_shape_instance_id_++ };
    area_it->second.shape_instances.emplace(shape_instance_id,
      ShapeInstanceState {
        .shape_id = shape_id,
        .shape = shape_result.value(),
        .local_position = local_position,
        .local_rotation = local_rotation,
      });
    updated_state = area_it->second;
  }

  const auto rebuilt_shape = RebuildAreaShape(updated_state);
  if (rebuilt_shape.has_error()) {
    std::scoped_lock lock(mutex_);
    auto area_it
      = areas_.find(AreaKey { .world_id = world_id, .area_id = area_id });
    if (area_it != areas_.end()) {
      area_it->second.shape_instances.erase(shape_instance_id);
    }
    static_cast<void>(shapes->RemoveAttachment(shape_id));
    return Err(rebuilt_shape.error());
  }

  body_interface->SetShape(updated_state.jolt_body_id,
    rebuilt_shape.value().GetPtr(), false, JPH::EActivation::DontActivate);
  return Ok(shape_instance_id);
}

auto oxygen::physics::jolt::JoltAreas::RemoveAreaShape(const WorldId world_id,
  const AreaId area_id, const ShapeInstanceId shape_instance_id)
  -> PhysicsResult<void>
{
  auto* world = world_.get();
  auto* shapes = shapes_.get();
  if (world == nullptr || shapes == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  AreaState updated_state {};
  ShapeInstanceState removed_instance {};
  {
    std::scoped_lock lock(mutex_);
    const auto area_it
      = areas_.find(AreaKey { .world_id = world_id, .area_id = area_id });
    if (area_it == areas_.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    const auto instance_it
      = area_it->second.shape_instances.find(shape_instance_id);
    if (instance_it == area_it->second.shape_instances.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    removed_instance = instance_it->second;
    area_it->second.shape_instances.erase(instance_it);
    updated_state = area_it->second;
  }

  const auto rebuilt_shape = RebuildAreaShape(updated_state);
  if (rebuilt_shape.has_error()) {
    std::scoped_lock lock(mutex_);
    auto area_it
      = areas_.find(AreaKey { .world_id = world_id, .area_id = area_id });
    if (area_it != areas_.end()) {
      area_it->second.shape_instances.emplace(
        shape_instance_id, removed_instance);
    }
    return Err(rebuilt_shape.error());
  }

  body_interface->SetShape(updated_state.jolt_body_id,
    rebuilt_shape.value().GetPtr(), false, JPH::EActivation::DontActivate);
  return shapes->RemoveAttachment(removed_instance.shape_id);
}

auto oxygen::physics::jolt::JoltAreas::RebuildAreaShape(const AreaState& state)
  -> PhysicsResult<JPH::RefConst<JPH::Shape>>
{
  if (state.shape_instances.empty()) {
    return Ok(JPH::RefConst<JPH::Shape> { new JPH::EmptyShape() });
  }

  JPH::StaticCompoundShapeSettings settings {};
  for (const auto& [id, instance] : state.shape_instances) {
    static_cast<void>(id);
    settings.AddShape(ToJoltVec3(instance.local_position),
      ToJoltQuat(instance.local_rotation), instance.shape.GetPtr());
  }
  const auto shape_result = settings.Create();
  if (!shape_result.IsValid()) {
    return Err(PhysicsError::kBackendInitFailed);
  }
  return Ok(JPH::RefConst<JPH::Shape> { shape_result.Get().GetPtr() });
}
