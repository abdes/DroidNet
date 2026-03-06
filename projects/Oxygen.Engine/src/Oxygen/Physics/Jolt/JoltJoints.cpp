//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <unordered_set>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <array>

#include <Jolt/Core/RTTI.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltJoints.h>
#include <Oxygen/Physics/Jolt/JoltWorld.h>

namespace {

auto IsZero(const oxygen::Vec3& value) noexcept -> bool
{
  return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

auto MakeConstraint(const oxygen::physics::joint::JointDesc& desc,
  JPH::Body& body_a, JPH::Body& body_b) -> JPH::Ref<JPH::TwoBodyConstraint>
{
  switch (desc.type) {
  case oxygen::physics::joint::JointType::kFixed: {
    JPH::FixedConstraintSettings settings {};
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mAutoDetectPoint = IsZero(desc.anchor_a) && IsZero(desc.anchor_b);
    settings.mPoint1 = oxygen::physics::jolt::ToJoltRVec3(desc.anchor_a);
    settings.mPoint2 = oxygen::physics::jolt::ToJoltRVec3(desc.anchor_b);
    return settings.Create(body_a, body_b);
  }
  case oxygen::physics::joint::JointType::kDistance: {
    JPH::DistanceConstraintSettings settings {};
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = IsZero(desc.anchor_a)
      ? body_a.GetCenterOfMassPosition()
      : oxygen::physics::jolt::ToJoltRVec3(desc.anchor_a);
    settings.mPoint2 = IsZero(desc.anchor_b)
      ? body_b.GetCenterOfMassPosition()
      : oxygen::physics::jolt::ToJoltRVec3(desc.anchor_b);
    if (desc.stiffness > 0.0F || desc.damping > 0.0F) {
      settings.mLimitsSpringSettings.mMode
        = JPH::ESpringMode::FrequencyAndDamping;
      settings.mLimitsSpringSettings.mFrequency = desc.stiffness;
      settings.mLimitsSpringSettings.mDamping = desc.damping;
    }
    return settings.Create(body_a, body_b);
  }
  case oxygen::physics::joint::JointType::kHinge: {
    JPH::HingeConstraintSettings settings {};
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = IsZero(desc.anchor_a)
      ? body_a.GetCenterOfMassPosition()
      : oxygen::physics::jolt::ToJoltRVec3(desc.anchor_a);
    settings.mPoint2 = IsZero(desc.anchor_b)
      ? body_b.GetCenterOfMassPosition()
      : oxygen::physics::jolt::ToJoltRVec3(desc.anchor_b);
    return settings.Create(body_a, body_b);
  }
  case oxygen::physics::joint::JointType::kSlider: {
    JPH::SliderConstraintSettings settings {};
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mAutoDetectPoint = IsZero(desc.anchor_a) && IsZero(desc.anchor_b);
    settings.mPoint1 = oxygen::physics::jolt::ToJoltRVec3(desc.anchor_a);
    settings.mPoint2 = oxygen::physics::jolt::ToJoltRVec3(desc.anchor_b);
    return settings.Create(body_a, body_b);
  }
  case oxygen::physics::joint::JointType::kSpherical: {
    JPH::PointConstraintSettings settings {};
    settings.mSpace = JPH::EConstraintSpace::WorldSpace;
    settings.mPoint1 = IsZero(desc.anchor_a)
      ? body_a.GetCenterOfMassPosition()
      : oxygen::physics::jolt::ToJoltRVec3(desc.anchor_a);
    settings.mPoint2 = IsZero(desc.anchor_b)
      ? body_b.GetCenterOfMassPosition()
      : oxygen::physics::jolt::ToJoltRVec3(desc.anchor_b);
    return settings.Create(body_a, body_b);
  }
  }
  return {};
}

auto RestoreTwoBodyConstraintSettings(std::span<const uint8_t> blob)
  -> JPH::Ref<JPH::TwoBodyConstraintSettings>
{
  if (blob.empty()) {
    return nullptr;
  }

  const std::string serialized(
    reinterpret_cast<const char*>(blob.data()), blob.size());
  std::istringstream stream(serialized, std::ios::in | std::ios::binary);
  JPH::StreamInWrapper wrapped(stream);
  auto restored = JPH::ConstraintSettings::sRestoreFromBinaryState(wrapped);
  if (wrapped.IsFailed() || !restored.IsValid()) {
    return nullptr;
  }

  const auto& restored_settings = restored.Get();
  auto* two_body_settings
    = JPH::DynamicCast<JPH::TwoBodyConstraintSettings>(restored_settings);
  if (two_body_settings == nullptr) {
    return nullptr;
  }
  return JPH::Ref<JPH::TwoBodyConstraintSettings> { two_body_settings };
}

} // namespace

oxygen::physics::jolt::JoltJoints::JoltJoints(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltJoints::CreateJoint(const WorldId world_id,
  const joint::JointDesc& desc) -> PhysicsResult<JointId>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto physics_system = world->TryGetPhysicsSystem(world_id);
  if (physics_system == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  // Purge stale joint table entries for constraints that are no longer
  // registered in the physics system (e.g. removed during body teardown).
  {
    const auto live_constraints = physics_system->GetConstraints();
    std::unordered_set<const JPH::Constraint*> live_constraint_ptrs {};
    live_constraint_ptrs.reserve(live_constraints.size());
    for (const auto& constraint_ref : live_constraints) {
      if (constraint_ref != nullptr) {
        live_constraint_ptrs.insert(constraint_ref.GetPtr());
      }
    }

    std::scoped_lock lock(mutex_);
    for (auto it = joints_.begin(); it != joints_.end();) {
      const auto* constraint_ptr = (it->second.constraint != nullptr)
        ? it->second.constraint.GetPtr()
        : nullptr;
      if (constraint_ptr == nullptr
        || !live_constraint_ptrs.contains(constraint_ptr)) {
        it = joints_.erase(it);
      } else {
        ++it;
      }
    }
  }

  const auto body_lock_interface = world->TryGetBodyLockInterface(world_id);
  if (body_lock_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (desc.body_a == kInvalidBodyId || desc.body_b == kInvalidBodyId) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (desc.body_a == desc.body_b) {
    return Err(PhysicsError::kInvalidArgument);
  }

  const auto body_ids = std::array<JPH::BodyID, 2> {
    ToJoltBodyId(desc.body_a),
    ToJoltBodyId(desc.body_b),
  };
  JPH::BodyLockMultiWrite body_locks(
    *body_lock_interface, body_ids.data(), static_cast<int>(body_ids.size()));
  auto* body_a = body_locks.GetBody(0);
  if (body_a == nullptr) {
    return Err(PhysicsError::kBodyNotFound);
  }
  auto* body_b = body_locks.GetBody(1);
  if (body_b == nullptr) {
    return Err(PhysicsError::kBodyNotFound);
  }

  auto constraint = JPH::Ref<JPH::TwoBodyConstraint> {};
  if (!desc.constraint_settings_blob.empty()) {
    auto settings
      = RestoreTwoBodyConstraintSettings(desc.constraint_settings_blob);
    if (settings == nullptr) {
      return Err(PhysicsError::kInvalidArgument);
    }
    constraint = settings->Create(*body_a, *body_b);
  } else {
    constraint = MakeConstraint(desc, *body_a, *body_b);
  }
  if (constraint == nullptr) {
    return Err(PhysicsError::kBackendInitFailed);
  }
  constraint->SetEnabled(true);
  physics_system->AddConstraint(constraint.GetPtr());

  std::scoped_lock lock(mutex_);
  if (next_joint_id_ == std::numeric_limits<uint32_t>::max()) {
    physics_system->RemoveConstraint(constraint.GetPtr());
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto joint_id = JointId { next_joint_id_++ };
  joints_.insert_or_assign(joint_id,
    JointState {
      .world_id = world_id,
      .constraint = constraint,
    });
  return Ok(joint_id);
}

auto oxygen::physics::jolt::JoltJoints::DestroyJoint(
  const WorldId world_id, const JointId joint_id) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto physics_system = world->TryGetPhysicsSystem(world_id);
  if (physics_system == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  JPH::Ref<JPH::TwoBodyConstraint> constraint {};
  {
    std::scoped_lock lock(mutex_);
    const auto it = joints_.find(joint_id);
    if (it == joints_.end()) {
      return Err(PhysicsError::kInvalidArgument);
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kInvalidArgument);
    }
    constraint = it->second.constraint;
    joints_.erase(it);
  }

  physics_system->RemoveConstraint(constraint.GetPtr());
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltJoints::SetJointEnabled(const WorldId world_id,
  const JointId joint_id, const bool enabled) -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  if (world->TryGetPhysicsSystem(world_id) == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = joints_.find(joint_id);
  if (it == joints_.end()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kInvalidArgument);
  }
  it->second.constraint->SetEnabled(enabled);
  return PhysicsResult<void>::Ok();
}
