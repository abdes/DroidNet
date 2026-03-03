//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <utility>
#include <vector>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
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
  return input.forward >= -1.0F && input.forward <= 1.0F
    && in_range_01(input.brake) && in_range_01(input.handbrake)
    && input.steering >= -1.0F && input.steering <= 1.0F;
}

auto AppendDifferential(JPH::WheeledVehicleControllerSettings& settings,
  const int left_wheel, const int right_wheel, const float engine_torque_ratio)
  -> void
{
  auto differential = JPH::VehicleDifferentialSettings {};
  differential.mLeftWheel = left_wheel;
  differential.mRightWheel = right_wheel;
  differential.mEngineTorqueRatio = engine_torque_ratio;
  settings.mDifferentials.push_back(differential);
}

[[nodiscard]] auto ConfigureDefaultDifferentials(
  JPH::WheeledVehicleControllerSettings& settings,
  const size_t wheel_count) noexcept -> bool
{
  settings.mDifferentials.clear();
  if (wheel_count < 2U) {
    return false;
  }

  const auto pair_count = wheel_count / 2U;
  const auto single_wheel_count = wheel_count % 2U;
  const auto differential_count = pair_count + single_wheel_count;
  if (differential_count == 0U) {
    return false;
  }

  const auto base_torque_ratio = 1.0F / static_cast<float>(differential_count);
  auto assigned_torque_ratio = 0.0F;
  auto emitted_differentials = size_t { 0 };

  for (auto pair_index = size_t { 0 }; pair_index < pair_count; ++pair_index) {
    const auto left_wheel = static_cast<int>(pair_index * 2U);
    const auto right_wheel = static_cast<int>(pair_index * 2U + 1U);
    const auto is_last = (++emitted_differentials == differential_count);
    const auto torque_ratio
      = is_last ? (1.0F - assigned_torque_ratio) : base_torque_ratio;
    AppendDifferential(settings, left_wheel, right_wheel, torque_ratio);
    assigned_torque_ratio += torque_ratio;
  }

  if (single_wheel_count == 1U) {
    const auto wheel_index = static_cast<int>(wheel_count - 1U);
    const auto remaining = 1.0F - assigned_torque_ratio;
    AppendDifferential(settings, wheel_index, -1, remaining);
  }

  return !settings.mDifferentials.empty();
}

} // namespace

struct oxygen::physics::jolt::JoltVehicles::Impl final {
  struct RuntimeVehicleState final {
    JPH::Ref<JPH::VehicleConstraint> constraint;
    JPH::Ref<JPH::VehicleCollisionTester> collision_tester;
    // The collision tester holds a raw pointer to this filter, so it must
    // outlive the collision tester. Stored per-vehicle to avoid file-scope
    // globals with non-trivial lifetime.
    JPH::SpecifiedObjectLayerFilter static_only_filter {
      kNonMovingObjectLayer,
    };
  };

  std::unordered_map<AggregateId, RuntimeVehicleState> runtime_vehicles;
};

oxygen::physics::jolt::JoltVehicles::JoltVehicles(JoltWorld& world)
  : world_(&world)
  , impl_(std::make_unique<Impl>())
{
}

oxygen::physics::jolt::JoltVehicles::~JoltVehicles()
{
  UnregisterAllConstraints();
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
  auto controller_settings = JPH::Ref<JPH::WheeledVehicleControllerSettings> {
    new JPH::WheeledVehicleControllerSettings()
  };
  settings.mController = controller_settings;
  settings.mUp = ToJoltVec3(oxygen::space::move::Up);
  settings.mForward = ToJoltVec3(oxygen::space::move::Forward);

  // Only the first two wheels (assumed to be the front axle) can steer.
  // This is a simplification; per-wheel steering configuration can be
  // exposed through VehicleDesc if more complex setups are needed.
  constexpr size_t kFrontAxleWheelCount = 2U;
  for (size_t i = 0; i < wheels.size(); ++i) {
    const auto wheel_world_position = ToOxygenVec3(
      body_interface->GetCenterOfMassPosition(ToJoltBodyId(wheels[i])));
    const auto local_position
      = inv_chassis_rotation * (wheel_world_position - chassis_position);

    auto wheel_settings
      = JPH::Ref<JPH::WheelSettingsWV> { new JPH::WheelSettingsWV() };
    wheel_settings->mPosition = ToJoltVec3(local_position);
    wheel_settings->mSuspensionDirection
      = ToJoltVec3(oxygen::space::move::Down);
    wheel_settings->mSteeringAxis = ToJoltVec3(oxygen::space::move::Up);
    wheel_settings->mWheelUp = ToJoltVec3(oxygen::space::move::Up);
    wheel_settings->mWheelForward = ToJoltVec3(oxygen::space::move::Forward);
    constexpr float kMaxSteeringAngleDeg = 35.0F;
    wheel_settings->mMaxSteerAngle = i < kFrontAxleWheelCount
      ? JPH::DegreesToRadians(kMaxSteeringAngleDeg)
      : 0.0F;
    settings.mWheels.push_back(
      JPH::Ref<JPH::WheelSettings> { wheel_settings.GetPtr() });
  }
  if (!ConfigureDefaultDifferentials(*controller_settings, wheels.size())) {
    return Err(PhysicsError::kInvalidArgument);
  }

  JPH::Ref<JPH::VehicleConstraint> constraint {};
  {
    JPH::BodyLockWrite chassis_lock(*body_lock_interface, chassis_jolt_id);
    if (!chassis_lock.Succeeded()) {
      return Err(PhysicsError::kBodyNotFound);
    }
    constraint = new JPH::VehicleConstraint(chassis_lock.GetBody(), settings);
  }

  // Use engine Z-up vector for collision tester slope filtering.
  auto collision_tester = JPH::Ref<JPH::VehicleCollisionTester> {
    new JPH::VehicleCollisionTesterRay(
      kNonMovingObjectLayer, ToJoltVec3(oxygen::space::move::Up)),
  };

  // Check ID availability BEFORE registering constraint/step listener to avoid
  // a window where the constraint is active but untracked.
  std::scoped_lock lock(mutex_);
  if (next_vehicle_id_ > kVehicleAggregateIdMax) {
    return Err(PhysicsError::kResourceExhausted);
  }

  const auto vehicle_id = AggregateId { next_vehicle_id_++ };

  // Store the per-vehicle filter in the RuntimeVehicleState so its lifetime
  // is tied to the vehicle, not to a file-scope global.
  auto& runtime_state = impl_->runtime_vehicles[vehicle_id];
  runtime_state.constraint = constraint;
  runtime_state.collision_tester = collision_tester;
  // Set the filter pointer AFTER the RuntimeVehicleState is emplaced so the
  // address is stable. The collision tester stores a raw pointer to it.
  collision_tester->SetObjectLayerFilter(&runtime_state.static_only_filter);
  constraint->SetVehicleCollisionTester(collision_tester.GetPtr());

  physics_system->AddConstraint(constraint.GetPtr());
  physics_system->AddStepListener(constraint.GetPtr());

  vehicles_.emplace(vehicle_id,
    VehicleState {
      .world_id = world_id,
      .chassis_body_id = desc.chassis_body_id,
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

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto physics_system = world->TryGetPhysicsSystem(world_id);
  if (physics_system == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  // Validate and remove from Jolt BEFORE erasing state, so we never lose the
  // ability to clean up.
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
    if (runtime_it != impl_->runtime_vehicles.end()) {
      constraint = runtime_it->second.constraint;
    }

    // Unregister from Jolt while we still own the state.
    if (constraint != nullptr) {
      physics_system->RemoveStepListener(constraint.GetPtr());
      physics_system->RemoveConstraint(constraint.GetPtr());
    }

    // Now safe to erase.
    if (runtime_it != impl_->runtime_vehicles.end()) {
      impl_->runtime_vehicles.erase(runtime_it);
    }
    vehicles_.erase(it);
    NoteStructuralChange(world_id);
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

  // The VehicleConstraint is a PhysicsStepListener. SetDriverInput must NOT
  // be called concurrently with PhysicsSystem::Update(). Callers must
  // ensure this method is invoked outside of the physics step window
  // (typically before Step() on the game thread).
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  auto* controller
    = static_cast<JPH::WheeledVehicleController*>(constraint->GetController());
  JPH_ASSERT(controller != nullptr);
  if (controller == nullptr) {
    return Err(PhysicsError::kBackendInitFailed);
  }
  controller->SetDriverInput(
    input.forward, input.steering, input.brake, input.handbrake);

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

auto oxygen::physics::jolt::JoltVehicles::UnregisterAllConstraints() -> void
{
  auto* world = world_.get();
  if (world == nullptr || impl_ == nullptr) {
    return;
  }

  // Best-effort cleanup: attempt to remove all constraints from every world.
  for (auto& [vehicle_id, runtime_state] : impl_->runtime_vehicles) {
    const auto vehicle_it = vehicles_.find(vehicle_id);
    if (vehicle_it == vehicles_.end()) {
      continue;
    }
    auto physics_system
      = world->TryGetPhysicsSystem(vehicle_it->second.world_id);
    if (physics_system == nullptr || runtime_state.constraint == nullptr) {
      continue;
    }
    physics_system->RemoveStepListener(runtime_state.constraint.GetPtr());
    physics_system->RemoveConstraint(runtime_state.constraint.GetPtr());
  }
  impl_->runtime_vehicles.clear();
  vehicles_.clear();
}
