//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltQueries.h>

auto oxygen::physics::jolt::JoltQueries::Raycast(
  WorldId /*world_id*/, const query::RaycastDesc& /*desc*/) const
  -> PhysicsResult<query::OptionalRaycastHit>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltQueries::Sweep(WorldId /*world_id*/,
  const query::SweepDesc& /*desc*/,
  std::span<query::SweepHit> /*out_hits*/) const -> PhysicsResult<size_t>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltQueries::Overlap(WorldId /*world_id*/,
  const query::OverlapDesc& /*desc*/,
  std::span<uint64_t> /*out_user_data*/) const -> PhysicsResult<size_t>
{
  return Err(PhysicsError::kWorldNotFound);
}
