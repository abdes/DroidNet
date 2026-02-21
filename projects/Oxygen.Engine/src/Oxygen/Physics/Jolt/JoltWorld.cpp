//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltWorld.h>

auto oxygen::physics::jolt::JoltWorld::CreateWorld(
  const world::WorldDesc& /*desc*/) -> PhysicsResult<WorldId>
{
  return Err(PhysicsError::kBackendInitFailed);
}

auto oxygen::physics::jolt::JoltWorld::DestroyWorld(WorldId /*world_id*/)
  -> PhysicsResult<void>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltWorld::Step(WorldId /*world_id*/,
  float /*delta_time*/, int /*max_sub_steps*/, float /*fixed_dt_seconds*/)
  -> PhysicsResult<void>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltWorld::GetActiveBodyTransforms(
  WorldId /*world_id*/,
  std::span<system::ActiveBodyTransform> /*out_transforms*/) const
  -> PhysicsResult<size_t>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltWorld::GetGravity(WorldId /*world_id*/) const
  -> PhysicsResult<Vec3>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltWorld::SetGravity(
  WorldId /*world_id*/, const Vec3& /*gravity*/) -> PhysicsResult<void>
{
  return Err(PhysicsError::kWorldNotFound);
}
