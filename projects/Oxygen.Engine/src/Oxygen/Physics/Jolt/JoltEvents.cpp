//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Physics/Jolt/JoltEvents.h>

auto oxygen::physics::jolt::JoltEvents::GetPendingEventCount(
  WorldId /*world_id*/) const -> PhysicsResult<size_t>
{
  return Err(PhysicsError::kWorldNotFound);
}

auto oxygen::physics::jolt::JoltEvents::DrainEvents(
  WorldId /*world_id*/, std::span<events::PhysicsEvent> /*out_events*/)
  -> PhysicsResult<size_t>
{
  return Err(PhysicsError::kWorldNotFound);
}
