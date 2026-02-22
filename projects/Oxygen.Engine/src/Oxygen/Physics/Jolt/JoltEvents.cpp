//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltEvents.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>

oxygen::physics::jolt::JoltEvents::JoltEvents(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltEvents::GetPendingEventCount(
  const WorldId world_id) const -> PhysicsResult<size_t>
{
  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  return world->GetPendingEventCount(world_id);
}

auto oxygen::physics::jolt::JoltEvents::DrainEvents(const WorldId world_id,
  const std::span<events::PhysicsEvent> out_events) -> PhysicsResult<size_t>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  return world->DrainEvents(world_id, out_events);
}
