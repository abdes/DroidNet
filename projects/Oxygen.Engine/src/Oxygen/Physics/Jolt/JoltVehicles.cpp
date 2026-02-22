//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>

#include <Oxygen/Physics/Jolt/JoltVehicles.h>

namespace {

auto VehicleUnknown() -> oxygen::ErrValue<oxygen::physics::PhysicsError>
{
  return oxygen::Err(oxygen::physics::PhysicsError::kInvalidArgument);
}

[[nodiscard]] auto IsControlInputValid(
  const oxygen::physics::vehicle::VehicleControlInput& input) noexcept -> bool
{
  const auto in_range_01
    = [](const float value) { return value >= 0.0F && value <= 1.0F; };
  return in_range_01(input.throttle) && in_range_01(input.brake)
    && in_range_01(input.handbrake) && input.steering >= -1.0F
    && input.steering <= 1.0F;
}

} // namespace

oxygen::physics::jolt::JoltVehicles::JoltVehicles(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltVehicles::CreateVehicle(const WorldId world_id,
  const vehicle::VehicleDesc& desc) -> PhysicsResult<AggregateId>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!IsBodyKnown(world_id, desc.chassis_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }
  if (desc.wheel_body_ids.empty()) {
    return Err(PhysicsError::kInvalidArgument);
  }

  std::vector<BodyId> wheels;
  wheels.reserve(desc.wheel_body_ids.size());
  for (const auto wheel_id : desc.wheel_body_ids) {
    if (!IsBodyKnown(world_id, wheel_id)) {
      return Err(PhysicsError::kBodyNotFound);
    }
    if (wheel_id == desc.chassis_body_id) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (std::find(wheels.begin(), wheels.end(), wheel_id) != wheels.end()) {
      return Err(PhysicsError::kAlreadyExists);
    }
    wheels.push_back(wheel_id);
  }

  std::scoped_lock lock(mutex_);
  if (next_vehicle_id_ == std::numeric_limits<uint32_t>::max()) {
    return Err(PhysicsError::kNotInitialized);
  }

  const auto vehicle_id = AggregateId { next_vehicle_id_++ };
  vehicles_.emplace(vehicle_id,
    VehicleState {
      .world_id = world_id,
      .chassis_body_id = desc.chassis_body_id,
      .wheel_body_ids = std::move(wheels),
      .authority = aggregate::AggregateAuthority::kCommand,
    });
  NoteStructuralChange(world_id);
  return Ok(vehicle_id);
}

auto oxygen::physics::jolt::JoltVehicles::DestroyVehicle(
  const WorldId world_id, const AggregateId vehicle_id) -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = vehicles_.find(vehicle_id);
  if (it == vehicles_.end()) {
    return VehicleUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  vehicles_.erase(it);
  NoteStructuralChange(world_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltVehicles::SetControlInput(
  const WorldId world_id, const AggregateId vehicle_id,
  const vehicle::VehicleControlInput& input) -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!IsControlInputValid(input)) {
    return Err(PhysicsError::kInvalidArgument);
  }

  std::scoped_lock lock(mutex_);
  const auto it = vehicles_.find(vehicle_id);
  if (it == vehicles_.end()) {
    return VehicleUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  it->second.control_input = input;
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltVehicles::GetState(const WorldId world_id,
  const AggregateId vehicle_id) const -> PhysicsResult<vehicle::VehicleState>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  BodyId chassis_body = kInvalidBodyId;
  std::vector<BodyId> wheel_body_ids;
  {
    std::scoped_lock lock(mutex_);
    const auto it = vehicles_.find(vehicle_id);
    if (it == vehicles_.end()) {
      return VehicleUnknown();
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kWorldNotFound);
    }
    chassis_body = it->second.chassis_body_id;
    wheel_body_ids = it->second.wheel_body_ids;
  }

  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, chassis_body)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  const auto linear_velocity
    = body_interface->GetLinearVelocity(JPH::BodyID(chassis_body.get()));
  const auto speed = static_cast<float>(
    std::sqrt(linear_velocity.GetX() * linear_velocity.GetX()
      + linear_velocity.GetY() * linear_velocity.GetY()
      + linear_velocity.GetZ() * linear_velocity.GetZ()));

  bool grounded = true;
  for (const auto wheel_body_id : wheel_body_ids) {
    if (!world->HasBody(world_id, wheel_body_id)) {
      grounded = false;
      break;
    }
  }

  return Ok(vehicle::VehicleState {
    .forward_speed_mps = speed,
    .grounded = grounded,
  });
}

auto oxygen::physics::jolt::JoltVehicles::GetAuthority(
  const WorldId world_id, const AggregateId vehicle_id) const
  -> PhysicsResult<aggregate::AggregateAuthority>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = vehicles_.find(vehicle_id);
  if (it == vehicles_.end()) {
    return VehicleUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  return Ok(it->second.authority);
}

auto oxygen::physics::jolt::JoltVehicles::FlushStructuralChanges(
  const WorldId world_id) -> PhysicsResult<size_t>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  size_t pending_changes = 0U;
  {
    std::scoped_lock lock(mutex_);
    const auto it = pending_structural_changes_.find(world_id);
    if (it != pending_structural_changes_.end()) {
      pending_changes = it->second;
      it->second = 0U;
    }
  }

  // TODO: Materialize vehicle control/suspension graph state into concrete
  // Jolt vehicle constraints at fixed-simulation boundaries.
  return Ok(pending_changes);
}

auto oxygen::physics::jolt::JoltVehicles::HasWorld(
  const WorldId world_id) const noexcept -> bool
{
  const auto* world = world_.get();
  return world != nullptr && world->TryGetPhysicsSystem(world_id) != nullptr;
}

auto oxygen::physics::jolt::JoltVehicles::IsBodyKnown(
  const WorldId world_id, const BodyId body_id) const noexcept -> bool
{
  const auto* world = world_.get();
  return world != nullptr && world->HasBody(world_id, body_id);
}

auto oxygen::physics::jolt::JoltVehicles::NoteStructuralChange(
  const WorldId world_id, const size_t count) -> void
{
  auto& pending = pending_structural_changes_[world_id];
  pending += count;
}
