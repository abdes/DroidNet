//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltAreas.h>

auto oxygen::physics::jolt::JoltAreas::CreateArea(
  WorldId /*world_id*/, const area::AreaDesc& /*desc*/) -> PhysicsResult<AreaId>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltAreas::DestroyArea(
  WorldId /*world_id*/, AreaId /*area_id*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltAreas::GetAreaPosition(
  WorldId /*world_id*/, AreaId /*area_id*/) const -> PhysicsResult<Vec3>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltAreas::GetAreaRotation(
  WorldId /*world_id*/, AreaId /*area_id*/) const -> PhysicsResult<Quat>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltAreas::SetAreaPose(WorldId /*world_id*/,
  AreaId /*area_id*/, const Vec3& /*position*/, const Quat& /*rotation*/)
  -> PhysicsResult<void>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltAreas::AddAreaShape(WorldId /*world_id*/,
  AreaId /*area_id*/, ShapeId /*shape_id*/, const Vec3& /*local_position*/,
  const Quat& /*local_rotation*/) -> PhysicsResult<ShapeInstanceId>
{
  return Err(PhysicsError::kNotImplemented);
}

auto oxygen::physics::jolt::JoltAreas::RemoveAreaShape(WorldId /*world_id*/,
  AreaId /*area_id*/, ShapeInstanceId /*shape_instance_id*/)
  -> PhysicsResult<void>
{
  return Err(PhysicsError::kNotImplemented);
}
