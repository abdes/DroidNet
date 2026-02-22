//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <Oxygen/Core/Constants.h>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltVehicles.h>

namespace {

constexpr JPH::ObjectLayer kNonMovingObjectLayer = 0;

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

struct oxygen::physics::jolt::JoltVehicles::Impl final {
  struct RuntimeVehicleState final {
    JPH::Ref<JPH::VehicleConstraint> constraint;
    JPH::RefConst<JPH::VehicleCollisionTester> collision_tester;
  };

  std::unordered_map<AggregateId, RuntimeVehicleState> runtime_vehicles;
};

oxygen::physics::jolt::JoltVehicles::JoltVehicles(JoltWorld& world)
  : world_(&world)
  , impl_(std::make_unique<Impl>())
{
}

oxygen::physics::jolt::JoltVehicles::~JoltVehicles() = default;

auto oxygen::physics::jolt::JoltVehicles::CreateVehicle(const WorldId world_id,
  const vehicle::VehicleDesc& desc) -> PhysicsResult<AggregateId>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!IsBodyKnown(world_id, desc.chassis_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }
  if (desc.wheel_body_ids.size() < 2U) {
    return Err(PhysicsError::kInvalidArgument);
  }

  std::vector<BodyId> wheels {};
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

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  auto body_lock_interface = world->TryGetBodyLockInterface(world_id);
  auto physics_system = world->TryGetPhysicsSystem(world_id);
  if (body_interface == nullptr || body_lock_interface == nullptr
    || physics_system == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto chassis_jolt_id = ToJoltBodyId(desc.chassis_body_id);
  const auto chassis_position
    = ToOxygenVec3(body_interface->GetCenterOfMassPosition(chassis_jolt_id));
  const auto chassis_rotation
    = ToOxygenQuat(body_interface->GetRotation(chassis_jolt_id));
  const auto inv_chassis_rotation = glm::inverse(chassis_rotation);

  JPH::VehicleConstraintSettings settings {};
  settings.mController = new JPH::WheeledVehicleControllerSettings();
  settings.mUp = ToJoltVec3(oxygen::space::move::Up);
  settings.mForward = ToJoltVec3(oxygen::space::move::Forward);

  for (size_t i = 0; i < wheels.size(); ++i) {
    const auto wheel_world_position = ToOxygenVec3(
      body_interface->GetCenterOfMassPosition(ToJoltBodyId(wheels[i])));
    const auto local_position
      = inv_chassis_rotation * (wheel_world_position - chassis_position);

    auto wheel_settings
      = JPH::Ref<JPH::WheelSettingsWV> { new JPH::WheelSettingsWV() };
    wheel_settings->mPosition = ToJoltVec3(local_position);
    wheel_settings->mSuspensionDirection = ToJoltVec3(oxygen::space::move::Down);
    wheel_settings->mWheelUp = ToJoltVec3(oxygen::space::move::Up);
    wheel_settings->mWheelForward = ToJoltVec3(oxygen::space::move::Forward);
    constexpr float kMaxSteeringAngleDeg = 35.0F;
    wheel_settings->mMaxSteerAngle = i < (wheels.size() + 1U) / 2U
      ? JPH::DegreesToRadians(kMaxSteeringAngleDeg)
      : 0.0F;
    settings.mWheels.push_back(
      JPH::Ref<JPH::WheelSettings> { wheel_settings.GetPtr() });
  }

  JPH::Ref<JPH::VehicleConstraint> constraint {};
  {
    JPH::BodyLockWrite chassis_lock(*body_lock_interface, chassis_jolt_id);
    if (!chassis_lock.Succeeded()) {
      return Err(PhysicsError::kBodyNotFound);
    }
    constraint = new JPH::VehicleConstraint(chassis_lock.GetBody(), settings);
  }

  auto collision_tester = JPH::RefConst<JPH::VehicleCollisionTester> {
    new JPH::VehicleCollisionTesterRay(kNonMovingObjectLayer),
  };
  constraint->SetVehicleCollisionTester(collision_tester.GetPtr());

  physics_system->AddConstraint(constraint.GetPtr());
  physics_system->AddStepListener(constraint.GetPtr());

  std::scoped_lock lock(mutex_);
  if (next_vehicle_id_ == std::numeric_limits<uint32_t>::max()) {
    physics_system->RemoveStepListener(constraint.GetPtr());
    physics_system->RemoveConstraint(constraint.GetPtr());
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
  impl_->runtime_vehicles.emplace(vehicle_id,
    Impl::RuntimeVehicleState {
      .constraint = std::move(constraint),
      .collision_tester = std::move(collision_tester),
    });
  NoteStructuralChange(world_id);
  return Ok(vehicle_id);
}

auto oxygen::physics::jolt::JoltVehicles::DestroyVehicle(
  const WorldId world_id, const AggregateId vehicle_id) -> PhysicsResult<void>
{
  WorldId owned_world_id = kInvalidWorldId;
  JPH::Ref<JPH::VehicleConstraint> constraint {};
  {
    std::scoped_lock lock(mutex_);
    const auto it = vehicles_.find(vehicle_id);
    if (it == vehicles_.end()) {
      return VehicleUnknown();
    }
    owned_world_id = it->second.world_id;
    if (owned_world_id != world_id) {
      return Err(PhysicsError::kWorldNotFound);
    }

    const auto runtime_it = impl_->runtime_vehicles.find(vehicle_id);
    if (runtime_it != impl_->runtime_vehicles.end()) {
      constraint = runtime_it->second.constraint;
      impl_->runtime_vehicles.erase(runtime_it);
    }
    vehicles_.erase(it);
    NoteStructuralChange(owned_world_id);
  }

  if (!HasWorld(owned_world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  if (constraint != nullptr) {
    auto* world = world_.get();
    if (world == nullptr) {
      return Err(PhysicsError::kNotInitialized);
    }
    auto physics_system = world->TryGetPhysicsSystem(owned_world_id);
    if (physics_system == nullptr) {
      return Err(PhysicsError::kWorldNotFound);
    }
    physics_system->RemoveStepListener(constraint.GetPtr());
    physics_system->RemoveConstraint(constraint.GetPtr());
  }

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

  BodyId chassis_body_id = kInvalidBodyId;
  JPH::Ref<JPH::VehicleConstraint> constraint {};
  {
    std::scoped_lock lock(mutex_);
    const auto it = vehicles_.find(vehicle_id);
    if (it == vehicles_.end()) {
      return VehicleUnknown();
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kWorldNotFound);
    }

    const auto runtime_it = impl_->runtime_vehicles.find(vehicle_id);
    if (runtime_it == impl_->runtime_vehicles.end()
      || runtime_it->second.constraint == nullptr) {
      return Err(PhysicsError::kBackendInitFailed);
    }
    it->second.control_input = input;
    chassis_body_id = it->second.chassis_body_id;
    constraint = runtime_it->second.constraint;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* controller
    = static_cast<JPH::WheeledVehicleController*>(constraint->GetController());
  if (controller == nullptr) {
    return Err(PhysicsError::kBackendInitFailed);
  }
  controller->SetDriverInput(
    input.throttle, input.steering, input.brake, input.handbrake);

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  body_interface->ActivateBody(ToJoltBodyId(chassis_body_id));
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltVehicles::GetState(const WorldId world_id,
  const AggregateId vehicle_id) const -> PhysicsResult<vehicle::VehicleState>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  BodyId chassis_body_id = kInvalidBodyId;
  JPH::Ref<JPH::VehicleConstraint> constraint {};
  {
    std::scoped_lock lock(mutex_);
    const auto it = vehicles_.find(vehicle_id);
    if (it == vehicles_.end()) {
      return VehicleUnknown();
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kWorldNotFound);
    }
    const auto runtime_it = impl_->runtime_vehicles.find(vehicle_id);
    if (runtime_it == impl_->runtime_vehicles.end()
      || runtime_it->second.constraint == nullptr) {
      return Err(PhysicsError::kBackendInitFailed);
    }
    chassis_body_id = it->second.chassis_body_id;
    constraint = runtime_it->second.constraint;
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!world->HasBody(world_id, chassis_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  const auto chassis_jolt_id = ToJoltBodyId(chassis_body_id);
  const auto chassis_rotation = body_interface->GetRotation(chassis_jolt_id);
  const auto world_forward = chassis_rotation * constraint->GetLocalForward();
  const auto linear_velocity
    = body_interface->GetLinearVelocity(chassis_jolt_id);
  const auto forward_speed = linear_velocity.Dot(world_forward);

  bool grounded = false;
  for (const auto* wheel : constraint->GetWheels()) {
    if (wheel != nullptr && wheel->HasContact()) {
      grounded = true;
      break;
    }
  }

  return Ok(vehicle::VehicleState {
    .forward_speed_mps = forward_speed,
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
