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
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>

#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltSoftBodies.h>

namespace {

auto SoftBodyUnknown() -> oxygen::ErrValue<oxygen::physics::PhysicsError>
{
  return oxygen::Err(oxygen::physics::PhysicsError::kInvalidArgument);
}

[[nodiscard]] auto IsMaterialParamsValid(
  const oxygen::physics::softbody::SoftBodyMaterialParams& params) noexcept
  -> bool
{
  return params.stiffness >= 0.0F && params.damping >= 0.0F
    && params.edge_compliance >= 0.0F && params.shear_compliance >= 0.0F
    && params.bend_compliance >= 0.0F
    && params.tether_max_distance_multiplier >= 1.0F;
}

[[nodiscard]] auto ToGridSize(const uint32_t cluster_count) noexcept -> uint32_t
{
  return std::clamp(cluster_count, 2U, 8U);
}

[[nodiscard]] auto ToJoltLRAType(
  const oxygen::physics::softbody::SoftBodyTetherMode mode) noexcept
  -> JPH::SoftBodySharedSettings::ELRAType
{
  switch (mode) {
  case oxygen::physics::softbody::SoftBodyTetherMode::kNone:
    return JPH::SoftBodySharedSettings::ELRAType::None;
  case oxygen::physics::softbody::SoftBodyTetherMode::kEuclidean:
    return JPH::SoftBodySharedSettings::ELRAType::EuclideanDistance;
  case oxygen::physics::softbody::SoftBodyTetherMode::kGeodesic:
    return JPH::SoftBodySharedSettings::ELRAType::GeodesicDistance;
  }
  return JPH::SoftBodySharedSettings::ELRAType::None;
}

constexpr JPH::ObjectLayer kDynamicObjectLayer = 1;

auto BuildSharedSettings(const uint32_t cluster_count,
  const oxygen::physics::softbody::SoftBodyMaterialParams& params)
  -> JPH::Ref<JPH::SoftBodySharedSettings>
{
  const auto grid_size = ToGridSize(cluster_count);
  auto shared_settings = JPH::SoftBodySharedSettings::sCreateCube(
    grid_size, 0.25F / static_cast<float>(grid_size));
  if (shared_settings == nullptr) {
    return nullptr;
  }
  const JPH::SoftBodySharedSettings::VertexAttributes vertex_attributes {
    params.edge_compliance,
    params.shear_compliance,
    params.bend_compliance,
    ToJoltLRAType(params.tether_mode),
    params.tether_max_distance_multiplier,
  };
  shared_settings->CreateConstraints(&vertex_attributes, 1U);
  shared_settings->Optimize();
  return shared_settings;
}

} // namespace

oxygen::physics::jolt::JoltSoftBodies::JoltSoftBodies(JoltWorld& world)
  : world_(&world)
{
}

auto oxygen::physics::jolt::JoltSoftBodies::CreateSoftBody(
  const WorldId world_id, const softbody::SoftBodyDesc& desc)
  -> PhysicsResult<AggregateId>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (desc.cluster_count == 0U) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (!IsMaterialParamsValid(desc.material_params)) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (desc.anchor_body_id != kInvalidBodyId) {
    if (!IsBodyKnown(world_id, desc.anchor_body_id)) {
      return Err(PhysicsError::kBodyNotFound);
    }
    // Jolt two-body constraints are rigid-body-only in solver path.
    // Soft-body anchor-to-body needs skin/tether integration.
    return Err(PhysicsError::kNotImplemented);
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const Vec3 spawn_position { 0.0F, 0.0F, 0.0F };

  auto shared_settings
    = BuildSharedSettings(desc.cluster_count, desc.material_params);
  if (shared_settings == nullptr) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  JPH::SoftBodyCreationSettings creation_settings(shared_settings.GetPtr(),
    ToJoltRVec3(spawn_position), JPH::Quat::sIdentity(), kDynamicObjectLayer);
  creation_settings.mLinearDamping = desc.material_params.damping;
  creation_settings.mPressure = desc.material_params.stiffness;

  const auto soft_body_jolt_id = body_interface->CreateAndAddSoftBody(
    creation_settings, JPH::EActivation::Activate);
  if (soft_body_jolt_id.IsInvalid()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto registered_body_id
    = BodyId { soft_body_jolt_id.GetIndexAndSequenceNumber() };
  const auto register_result
    = world->RegisterBody(world_id, registered_body_id);
  if (!register_result.has_value()) {
    body_interface->RemoveBody(soft_body_jolt_id);
    body_interface->DestroyBody(soft_body_jolt_id);
    return Err(register_result.error());
  }

  std::scoped_lock lock(mutex_);
  if (next_soft_body_id_ == std::numeric_limits<uint32_t>::max()) {
    (void)world->UnregisterBody(world_id, registered_body_id);
    body_interface->RemoveBody(soft_body_jolt_id);
    body_interface->DestroyBody(soft_body_jolt_id);
    return Err(PhysicsError::kNotInitialized);
  }

  const auto soft_body_id = AggregateId { next_soft_body_id_++ };
  soft_bodies_.emplace(soft_body_id,
    SoftBodyState {
      .world_id = world_id,
      .jolt_body_id = soft_body_jolt_id,
      .registered_body_id = registered_body_id,
      .cluster_count = desc.cluster_count,
      .material_params = desc.material_params,
      .authority = aggregate::AggregateAuthority::kSimulation,
    });
  NoteStructuralChange(world_id);
  return Ok(soft_body_id);
}

auto oxygen::physics::jolt::JoltSoftBodies::DestroySoftBody(
  const WorldId world_id, const AggregateId soft_body_id) -> PhysicsResult<void>
{
  WorldId owned_world_id = kInvalidWorldId;
  JPH::BodyID jolt_body_id {};
  BodyId registered_body_id { kInvalidBodyId };
  {
    std::scoped_lock lock(mutex_);
    const auto it = soft_bodies_.find(soft_body_id);
    if (it == soft_bodies_.end()) {
      return SoftBodyUnknown();
    }
    owned_world_id = it->second.world_id;
    if (owned_world_id != world_id) {
      return Err(PhysicsError::kWorldNotFound);
    }
    jolt_body_id = it->second.jolt_body_id;
    registered_body_id = it->second.registered_body_id;
    soft_bodies_.erase(it);
    NoteStructuralChange(owned_world_id);
  }

  if (!HasWorld(owned_world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(owned_world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }

  if (body_interface->IsAdded(jolt_body_id)) {
    body_interface->RemoveBody(jolt_body_id);
  }
  body_interface->DestroyBody(jolt_body_id);
  (void)world->UnregisterBody(owned_world_id, registered_body_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltSoftBodies::SetMaterialParams(
  const WorldId world_id, const AggregateId soft_body_id,
  const softbody::SoftBodyMaterialParams& params) -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!IsMaterialParamsValid(params)) {
    return Err(PhysicsError::kInvalidArgument);
  }

  JPH::BodyID jolt_body_id {};
  bool requires_structural_rebuild = false;
  bool first_queued_rebuild_for_body = false;
  softbody::SoftBodyMaterialParams current_params {};
  {
    std::scoped_lock lock(mutex_);
    const auto it = soft_bodies_.find(soft_body_id);
    if (it == soft_bodies_.end()) {
      return SoftBodyUnknown();
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kWorldNotFound);
    }
    current_params = it->second.material_params;
    if (current_params.edge_compliance != params.edge_compliance
      || current_params.shear_compliance != params.shear_compliance
      || current_params.bend_compliance != params.bend_compliance
      || current_params.tether_mode != params.tether_mode
      || current_params.tether_max_distance_multiplier
        != params.tether_max_distance_multiplier) {
      requires_structural_rebuild = true;
      first_queued_rebuild_for_body
        = pending_material_rebuilds_.find(soft_body_id)
        == pending_material_rebuilds_.end();
      pending_material_rebuilds_.insert_or_assign(soft_body_id, params);
    }
    jolt_body_id = it->second.jolt_body_id;
  }
  if (requires_structural_rebuild) {
    if (first_queued_rebuild_for_body) {
      NoteStructuralChange(world_id);
    }
    return PhysicsResult<void>::Ok();
  }
  const auto apply_result = ApplyMaterialParams(world_id, jolt_body_id, params);
  if (!apply_result.has_value()) {
    return Err(apply_result.error());
  }
  std::scoped_lock lock(mutex_);
  const auto it = soft_bodies_.find(soft_body_id);
  if (it == soft_bodies_.end()) {
    return SoftBodyUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  it->second.material_params = params;
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltSoftBodies::GetState(
  const WorldId world_id, const AggregateId soft_body_id) const
  -> PhysicsResult<softbody::SoftBodyState>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  JPH::BodyID jolt_body_id {};
  {
    std::scoped_lock lock(mutex_);
    const auto it = soft_bodies_.find(soft_body_id);
    if (it == soft_bodies_.end()) {
      return SoftBodyUnknown();
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kWorldNotFound);
    }
    jolt_body_id = it->second.jolt_body_id;
  }

  const auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!body_interface->IsAdded(jolt_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  const auto center
    = ToOxygenVec3(body_interface->GetCenterOfMassPosition(jolt_body_id));
  const auto is_active = body_interface->IsActive(jolt_body_id);
  return Ok(softbody::SoftBodyState {
    .center_of_mass = center,
    .sleeping = !is_active,
  });
}

auto oxygen::physics::jolt::JoltSoftBodies::GetAuthority(
  const WorldId world_id, const AggregateId soft_body_id) const
  -> PhysicsResult<aggregate::AggregateAuthority>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  std::scoped_lock lock(mutex_);
  const auto it = soft_bodies_.find(soft_body_id);
  if (it == soft_bodies_.end()) {
    return SoftBodyUnknown();
  }
  if (it->second.world_id != world_id) {
    return Err(PhysicsError::kWorldNotFound);
  }
  return Ok(it->second.authority);
}

auto oxygen::physics::jolt::JoltSoftBodies::FlushStructuralChanges(
  const WorldId world_id) -> PhysicsResult<size_t>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  const auto flush_rebuild_result = FlushPendingMaterialRebuilds(world_id);
  if (!flush_rebuild_result.has_value()) {
    return Err(flush_rebuild_result.error());
  }
  const auto pending_changes = ConsumePendingStructuralChangeCount(world_id);
  return Ok(pending_changes);
}

auto oxygen::physics::jolt::JoltSoftBodies::HasWorld(
  const WorldId world_id) const noexcept -> bool
{
  const auto* world = world_.get();
  return world != nullptr && world->TryGetPhysicsSystem(world_id) != nullptr;
}

auto oxygen::physics::jolt::JoltSoftBodies::IsBodyKnown(
  const WorldId world_id, const BodyId body_id) const noexcept -> bool
{
  const auto* world = world_.get();
  return world != nullptr && world->HasBody(world_id, body_id);
}

auto oxygen::physics::jolt::JoltSoftBodies::NoteStructuralChange(
  const WorldId world_id, const size_t count) -> void
{
  auto& pending = pending_structural_changes_[world_id];
  pending += count;
}

auto oxygen::physics::jolt::JoltSoftBodies::ApplyMaterialParams(
  const WorldId world_id, const JPH::BodyID soft_body_id,
  const softbody::SoftBodyMaterialParams& params) const -> PhysicsResult<void>
{
  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  const auto body_lock_interface = world->TryGetBodyLockInterface(world_id);
  const auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_lock_interface == nullptr || body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!body_interface->IsAdded(soft_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  JPH::BodyLockWrite body_lock(*body_lock_interface, soft_body_id);
  if (!body_lock.Succeeded()) {
    return Err(PhysicsError::kBodyNotFound);
  }
  {
    auto& body = body_lock.GetBody();
    if (!body.IsSoftBody()) {
      return Err(PhysicsError::kInvalidArgument);
    }

    auto* motion_properties = body.GetMotionProperties();
    if (motion_properties == nullptr) {
      return Err(PhysicsError::kBackendInitFailed);
    }
    motion_properties->SetLinearDamping(params.damping);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    auto* soft_motion
      = static_cast<JPH::SoftBodyMotionProperties*>(motion_properties);
    soft_motion->SetPressure(params.stiffness);
  }

  body_lock.ReleaseLock();
  body_interface->ActivateBody(soft_body_id);
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltSoftBodies::RebuildSoftBodyForMaterialParams(
  const WorldId world_id, const AggregateId soft_body_id,
  const softbody::SoftBodyMaterialParams& params) -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  JPH::BodyID old_jolt_body_id {};
  BodyId old_registered_body_id { kInvalidBodyId };
  uint32_t cluster_count = 0U;
  {
    std::scoped_lock lock(mutex_);
    const auto it = soft_bodies_.find(soft_body_id);
    if (it == soft_bodies_.end()) {
      return SoftBodyUnknown();
    }
    if (it->second.world_id != world_id) {
      return Err(PhysicsError::kWorldNotFound);
    }
    old_jolt_body_id = it->second.jolt_body_id;
    old_registered_body_id = it->second.registered_body_id;
    cluster_count = it->second.cluster_count;
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  if (!body_interface->IsAdded(old_jolt_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  const auto current_position
    = body_interface->GetCenterOfMassPosition(old_jolt_body_id);
  const auto current_rotation = body_interface->GetRotation(old_jolt_body_id);
  const auto was_active = body_interface->IsActive(old_jolt_body_id);

  auto shared_settings = BuildSharedSettings(cluster_count, params);
  if (shared_settings == nullptr) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  JPH::SoftBodyCreationSettings creation_settings(shared_settings.GetPtr(),
    current_position, current_rotation, kDynamicObjectLayer);
  creation_settings.mLinearDamping = params.damping;
  creation_settings.mPressure = params.stiffness;

  const auto new_jolt_body_id = body_interface->CreateAndAddSoftBody(
    creation_settings, JPH::EActivation::DontActivate);
  if (new_jolt_body_id.IsInvalid()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto new_registered_body_id
    = BodyId { new_jolt_body_id.GetIndexAndSequenceNumber() };
  const auto register_result
    = world->RegisterBody(world_id, new_registered_body_id);
  if (!register_result.has_value()) {
    if (body_interface->IsAdded(new_jolt_body_id)) {
      body_interface->RemoveBody(new_jolt_body_id);
    }
    body_interface->DestroyBody(new_jolt_body_id);
    return Err(register_result.error());
  }

  if (body_interface->IsAdded(old_jolt_body_id)) {
    body_interface->RemoveBody(old_jolt_body_id);
  }
  body_interface->DestroyBody(old_jolt_body_id);
  (void)world->UnregisterBody(world_id, old_registered_body_id);

  if (was_active) {
    body_interface->ActivateBody(new_jolt_body_id);
  }

  {
    std::scoped_lock lock(mutex_);
    const auto it = soft_bodies_.find(soft_body_id);
    if (it == soft_bodies_.end()) {
      (void)world->UnregisterBody(world_id, new_registered_body_id);
      if (body_interface->IsAdded(new_jolt_body_id)) {
        body_interface->RemoveBody(new_jolt_body_id);
      }
      body_interface->DestroyBody(new_jolt_body_id);
      return SoftBodyUnknown();
    }
    it->second.jolt_body_id = new_jolt_body_id;
    it->second.registered_body_id = new_registered_body_id;
    it->second.material_params = params;
  }

  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltSoftBodies::FlushPendingMaterialRebuilds(
  const WorldId world_id) -> PhysicsResult<size_t>
{
  std::vector<std::pair<AggregateId, softbody::SoftBodyMaterialParams>>
    pending_for_world {};
  {
    std::scoped_lock lock(mutex_);
    for (auto it = pending_material_rebuilds_.begin();
      it != pending_material_rebuilds_.end();) {
      const auto state_it = soft_bodies_.find(it->first);
      if (state_it == soft_bodies_.end()) {
        it = pending_material_rebuilds_.erase(it);
        continue;
      }
      if (state_it->second.world_id == world_id) {
        pending_for_world.push_back(
          std::pair<AggregateId, softbody::SoftBodyMaterialParams> {
            it->first,
            it->second,
          });
        it = pending_material_rebuilds_.erase(it);
        continue;
      }
      ++it;
    }
  }

  for (const auto& pending : pending_for_world) {
    const auto rebuild_result = RebuildSoftBodyForMaterialParams(
      world_id, pending.first, pending.second);
    if (!rebuild_result.has_value()) {
      return Err(rebuild_result.error());
    }
  }
  return Ok(pending_for_world.size());
}

auto oxygen::physics::jolt::JoltSoftBodies::ConsumePendingStructuralChangeCount(
  const WorldId world_id) -> size_t
{
  std::scoped_lock lock(mutex_);
  const auto it = pending_structural_changes_.find(world_id);
  if (it == pending_structural_changes_.end()) {
    return 0U;
  }
  const auto pending_changes = it->second;
  it->second = 0U;
  return pending_changes;
}
