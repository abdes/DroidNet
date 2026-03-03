//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Core/RTTI.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltVehicles.h>

namespace {

auto VehicleUnknown() -> oxygen::ErrValue<oxygen::physics::PhysicsError>
{
  return oxygen::Err(oxygen::physics::PhysicsError::kInvalidArgument);
}

auto RestoreVehicleConstraintSettings(std::span<const uint8_t> blob)
  -> JPH::Ref<JPH::VehicleConstraintSettings>
{
  if (blob.empty()) {
    return nullptr;
  }

  const std::string serialized(
    reinterpret_cast<const char*>(blob.data()), blob.size());
  std::istringstream stream(serialized, std::ios::in | std::ios::binary);
  JPH::StreamInWrapper wrapped(stream);

  auto settings = JPH::Ref<JPH::VehicleConstraintSettings> {
    new JPH::VehicleConstraintSettings()
  };

  // ConstraintSettings base payload.
  uint32_t settings_hash = 0U;
  wrapped.Read(settings_hash);
  if (wrapped.IsEOF() || wrapped.IsFailed()
    || settings_hash != settings->GetRTTI()->GetHash()) {
    return nullptr;
  }
  wrapped.Read(settings->mEnabled);
  wrapped.Read(settings->mDrawConstraintSize);
  wrapped.Read(settings->mConstraintPriority);
  wrapped.Read(settings->mNumVelocityStepsOverride);
  wrapped.Read(settings->mNumPositionStepsOverride);
  if (wrapped.IsEOF() || wrapped.IsFailed()) {
    return nullptr;
  }

  // VehicleConstraintSettings payload.
  wrapped.Read(settings->mUp);
  wrapped.Read(settings->mForward);
  wrapped.Read(settings->mMaxPitchRollAngle);
  if (wrapped.IsEOF() || wrapped.IsFailed()) {
    return nullptr;
  }

  uint32_t num_anti_rollbars = 0U;
  wrapped.Read(num_anti_rollbars);
  if (wrapped.IsEOF() || wrapped.IsFailed()) {
    return nullptr;
  }
  settings->mAntiRollBars.resize(num_anti_rollbars);
  for (auto& anti_roll_bar : settings->mAntiRollBars) {
    anti_roll_bar.RestoreBinaryState(wrapped);
    if (wrapped.IsEOF() || wrapped.IsFailed()) {
      return nullptr;
    }
  }

  uint32_t num_wheels = 0U;
  wrapped.Read(num_wheels);
  if (wrapped.IsEOF() || wrapped.IsFailed()) {
    return nullptr;
  }
  settings->mWheels.resize(num_wheels);

  for (uint32_t i = 0U; i < num_wheels; ++i) {
    auto wheel_settings
      = JPH::Ref<JPH::WheelSettingsWV> { new JPH::WheelSettingsWV() };
    auto* const wheel_ptr = wheel_settings.GetPtr();
    if (wheel_ptr == nullptr) {
      return nullptr;
    }

    auto* const wheel_base = static_cast<JPH::WheelSettings*>(wheel_ptr);
    wheel_base->RestoreBinaryState(wrapped);
    if (wrapped.IsEOF() || wrapped.IsFailed()) {
      return nullptr;
    }
    settings->mWheels[i] = wheel_ptr;
  }

  uint32_t controller_hash = 0U;
  wrapped.Read(controller_hash);
  if (wrapped.IsEOF() || wrapped.IsFailed()) {
    return nullptr;
  }

  auto wheeled_controller = JPH::Ref<JPH::WheeledVehicleControllerSettings> {
    new JPH::WheeledVehicleControllerSettings()
  };
  if (wheeled_controller == nullptr
    || controller_hash != wheeled_controller->GetRTTI()->GetHash()) {
    return nullptr;
  }
  wheeled_controller->RestoreBinaryState(wrapped);
  if (wrapped.IsEOF() || wrapped.IsFailed()) {
    return nullptr;
  }
  settings->mController = wheeled_controller;

  return settings;
}

[[nodiscard]] auto NormalizeOr(
  const oxygen::Vec3& value, const oxygen::Vec3& fallback) -> oxygen::Vec3
{
  const auto len = glm::length(value);
  if (len <= 1.0e-5F) {
    return fallback;
  }
  return value / len;
}

[[nodiscard]] auto IsControlInputValid(
  const oxygen::physics::vehicle::VehicleControlInput& input) noexcept -> bool
{
  const auto in_range_01
    = [](const float value) { return value >= 0.0F && value <= 1.0F; };
  return input.forward >= -1.0F && input.forward <= 1.0F
    && in_range_01(input.brake) && in_range_01(input.hand_brake)
    && input.steering >= -1.0F && input.steering <= 1.0F;
}

[[nodiscard]] auto ValidateVehicleAttachmentBody(
  const JPH::BodyLockInterface& body_lock_interface, const JPH::BodyID body_id,
  const char* role, const bool require_dynamic) -> bool
{
  JPH::BodyLockRead body_lock(body_lock_interface, body_id);
  if (!body_lock.Succeeded()) {
    LOG_F(ERROR,
      "JoltVehicles: {} body lock failed (body_id={}) while creating vehicle.",
      role, body_id.GetIndexAndSequenceNumber());
    return false;
  }

  const auto& body = body_lock.GetBody();
  const auto is_rigid = body.IsRigidBody();
  const auto is_soft = body.IsSoftBody();
  const auto is_dynamic = body.IsDynamic();
  const auto is_static = body.IsStatic();
  const auto is_kinematic = body.IsKinematic();
  if (!is_rigid || (require_dynamic && !is_dynamic)) {
    LOG_F(ERROR,
      "JoltVehicles: {} body contract violation "
      "(body_id={} rigid={} soft={} dynamic={} static={} kinematic={} "
      "require_dynamic={}).",
      role, body_id.GetIndexAndSequenceNumber(), is_rigid, is_soft, is_dynamic,
      is_static, is_kinematic, require_dynamic);
    return false;
  }
  return true;
}

class VehicleWheelRigidBodyFilter final : public JPH::BodyFilter {
public:
  auto ShouldCollideLocked(const JPH::Body& body) const -> bool override
  {
    if (body.IsRigidBody()) {
      return true;
    }
    const auto is_soft = body.IsSoftBody();
    if (is_soft
      && !soft_body_reject_logged_.exchange(true, std::memory_order_acq_rel)) {
      LOG_F(WARNING,
        "JoltVehicles: wheel collision filtered a non-rigid soft body "
        "(body_id={}).",
        body.GetID().GetIndexAndSequenceNumber());
    }
    return false;
  }

private:
  mutable std::atomic<bool> soft_body_reject_logged_ { false };
};

} // namespace

struct oxygen::physics::jolt::JoltVehicles::Impl final {
  struct RuntimeVehicleState final {
    JPH::Ref<JPH::VehicleConstraint> constraint;
    JPH::Ref<JPH::VehicleCollisionTester> collision_tester;
    std::unique_ptr<VehicleWheelRigidBodyFilter> wheel_body_filter;
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
  if (desc.wheels.size() < 2U) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (desc.constraint_settings_blob.empty()) {
    return Err(PhysicsError::kInvalidArgument);
  }

  std::vector<vehicle::VehicleWheelDesc> wheels {};
  wheels.reserve(desc.wheels.size());
  for (const auto& wheel : desc.wheels) {
    if (!IsBodyKnown(world_id, wheel.body_id)) {
      return Err(PhysicsError::kBodyNotFound);
    }
    if (wheel.body_id == desc.chassis_body_id) {
      return Err(PhysicsError::kInvalidArgument);
    }
    const auto duplicate = std::find_if(wheels.begin(), wheels.end(),
      [&](const auto& existing) { return existing.body_id == wheel.body_id; });
    if (duplicate != wheels.end()) {
      return Err(PhysicsError::kAlreadyExists);
    }
    wheels.push_back(wheel);
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto simulation_lock = world->LockSimulationApi();
  auto body_interface = world->TryGetBodyInterface(world_id);
  auto body_lock_interface = world->TryGetBodyLockInterface(world_id);
  auto physics_system = world->TryGetPhysicsSystem(world_id);
  if (body_interface == nullptr || body_lock_interface == nullptr
    || physics_system == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto chassis_jolt_id = ToJoltBodyId(desc.chassis_body_id);
  if (!ValidateVehicleAttachmentBody(
        *body_lock_interface, chassis_jolt_id, "chassis", true)) {
    return Err(PhysicsError::kInvalidArgument);
  }
  for (size_t i = 0; i < wheels.size(); ++i) {
    const auto wheel_jolt_id = ToJoltBodyId(wheels[i].body_id);
    if (!ValidateVehicleAttachmentBody(
          *body_lock_interface, wheel_jolt_id, "wheel", false)) {
      LOG_F(ERROR,
        "JoltVehicles: invalid wheel attachment at wheel_index={} "
        "(body_id={}).",
        i, wheels[i].body_id.get());
      return Err(PhysicsError::kInvalidArgument);
    }
  }

  const auto chassis_position
    = ToOxygenVec3(body_interface->GetCenterOfMassPosition(chassis_jolt_id));
  const auto chassis_rotation
    = ToOxygenQuat(body_interface->GetRotation(chassis_jolt_id));
  const auto inv_chassis_rotation = glm::inverse(chassis_rotation);

  auto settings
    = RestoreVehicleConstraintSettings(desc.constraint_settings_blob);
  if (settings == nullptr) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (settings->mWheels.size() != wheels.size()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (settings->mController == nullptr
    || JPH::DynamicCast<JPH::WheeledVehicleControllerSettings>(
         settings->mController.GetPtr())
      == nullptr) {
    return Err(PhysicsError::kInvalidArgument);
  }

  settings->mUp = ToJoltVec3(oxygen::space::move::Up);
  settings->mForward = ToJoltVec3(oxygen::space::move::Forward);

  const auto engine_up
    = NormalizeOr(oxygen::space::move::Up, oxygen::Vec3 { 0.0F, 0.0F, 1.0F });
  const auto engine_down = -engine_up;
  const auto engine_forward = NormalizeOr(
    oxygen::space::move::Forward, oxygen::Vec3 { 0.0F, -1.0F, 0.0F });
  auto steering_axle_index = std::numeric_limits<uint16_t>::max();
  for (const auto& wheel : wheels) {
    steering_axle_index = std::min(steering_axle_index, wheel.axle_index);
  }

  for (size_t i = 0; i < settings->mWheels.size(); ++i) {
    auto* const wheel_settings = settings->mWheels[i].GetPtr();
    if (wheel_settings == nullptr) {
      return Err(PhysicsError::kInvalidArgument);
    }

    // Engine contract: Oxygen physics space is Z-up / -Y forward.
    // Wheel axes are canonicalized here so binary blobs cannot inject a
    // mismatched basis and silently break traction/contact.
    wheel_settings->mSuspensionDirection = ToJoltVec3(engine_down);
    wheel_settings->mSteeringAxis = ToJoltVec3(engine_up);
    wheel_settings->mWheelUp = ToJoltVec3(engine_up);
    wheel_settings->mWheelForward = ToJoltVec3(engine_forward);

    auto* const wheel_wv
      = JPH::DynamicCast<JPH::WheelSettingsWV>(wheel_settings);
    if (wheel_wv == nullptr) {
      return Err(PhysicsError::kInvalidArgument);
    }

    // Steering contract: only the leading axle (lowest axle_index) steers.
    // Other axles are constrained to zero steering angle.
    if (wheels[i].axle_index != steering_axle_index) {
      wheel_wv->mMaxSteerAngle = 0.0F;
    }

    const auto steer_enabled = wheel_wv->mMaxSteerAngle > 0.0F;
    LOG_F(INFO,
      "JoltVehicles: canonicalized wheel axes to engine basis "
      "(wheel_index={}, axle_index={}, steer_enabled={}, "
      "up=[{:.3f},{:.3f},{:.3f}], "
      "forward=[{:.3f},{:.3f},{:.3f}])",
      i, wheels[i].axle_index, steer_enabled, engine_up.x, engine_up.y,
      engine_up.z, engine_forward.x, engine_forward.y, engine_forward.z);
  }

  for (size_t i = 0; i < wheels.size(); ++i) {
    if (settings->mWheels[i] == nullptr) {
      return Err(PhysicsError::kInvalidArgument);
    }
    const auto wheel_world_position = ToOxygenVec3(
      body_interface->GetCenterOfMassPosition(ToJoltBodyId(wheels[i].body_id)));
    const auto local_position
      = inv_chassis_rotation * (wheel_world_position - chassis_position);
    settings->mWheels[i]->mPosition = ToJoltVec3(local_position);
  }

  JPH::Ref<JPH::VehicleConstraint> constraint {};
  {
    JPH::BodyLockWrite chassis_lock(*body_lock_interface, chassis_jolt_id);
    if (!chassis_lock.Succeeded()) {
      return Err(PhysicsError::kBodyNotFound);
    }
    constraint = new JPH::VehicleConstraint(chassis_lock.GetBody(), *settings);
  }

  const auto chassis_object_layer
    = body_interface->GetObjectLayer(chassis_jolt_id);

  // Use engine Z-up vector for collision tester slope filtering.
  auto collision_tester = JPH::Ref<JPH::VehicleCollisionTester> {
    new JPH::VehicleCollisionTesterRay(
      chassis_object_layer, ToJoltVec3(oxygen::space::move::Up)),
  };
  auto wheel_body_filter = std::make_unique<VehicleWheelRigidBodyFilter>();
  collision_tester->SetBodyFilter(wheel_body_filter.get());

  // Check ID availability BEFORE registering constraint/step listener to avoid
  // a window where the constraint is active but untracked.
  std::scoped_lock lock(mutex_);
  if (next_vehicle_id_ > kVehicleAggregateIdMax) {
    return Err(PhysicsError::kResourceExhausted);
  }

  const auto vehicle_id = AggregateId { next_vehicle_id_++ };

  auto& runtime_state = impl_->runtime_vehicles[vehicle_id];
  runtime_state.constraint = constraint;
  runtime_state.collision_tester = collision_tester;
  runtime_state.wheel_body_filter = std::move(wheel_body_filter);
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
  const auto simulation_lock = world->LockSimulationApi();
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

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto simulation_lock = world->LockSimulationApi();

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
    input.forward, input.steering, input.brake, input.hand_brake);

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
  const auto simulation_lock = world->LockSimulationApi();
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
