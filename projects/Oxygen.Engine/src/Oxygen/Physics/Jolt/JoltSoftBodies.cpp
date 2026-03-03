//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <Jolt/Jolt.h> // Must always be first (keep separate)

#include <Jolt/Core/StreamWrapper.h>
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
  // Damping upper bound: Jolt's mLinearDamping is a velocity decay coefficient
  // (dv/dt = -c*v). Values above ~10 cause numerical instability.
  constexpr float kMaxDamping = 10.0F;
  return std::isfinite(params.stiffness) && std::isfinite(params.damping)
    && std::isfinite(params.edge_compliance)
    && std::isfinite(params.shear_compliance)
    && std::isfinite(params.bend_compliance)
    && std::isfinite(params.tether_max_distance_multiplier)
    && params.stiffness >= 0.0F && params.damping >= 0.0F
    && params.damping <= kMaxDamping && params.edge_compliance >= 0.0F
    && params.shear_compliance >= 0.0F && params.bend_compliance >= 0.0F
    && params.tether_max_distance_multiplier >= 1.0F;
}

// Clamps cluster_count to valid Jolt grid range [2, 8].
// Values outside this range are silently clamped. Consider returning an error
// from CreateSoftBody if the original cluster_count is outside [2, 8].
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

// Prototype-only fallback path. Shipping/runtime-authored content must rely on
// cooked kJoltSoftBodySharedSettingsBinary payloads.
#if defined(NDEBUG)
constexpr bool kEnablePrototypeProceduralSoftBodyFallback = false;
#else
constexpr bool kEnablePrototypeProceduralSoftBodyFallback = true;
#endif

auto BuildPrototypeSharedSettings(const uint32_t cluster_count,
  const oxygen::physics::softbody::SoftBodyMaterialParams& params)
  -> JPH::Ref<JPH::SoftBodySharedSettings>
{
  const auto grid_size = ToGridSize(cluster_count);
  auto shared_settings = JPH::SoftBodySharedSettings::sCreateCube(
    grid_size, 0.25F / static_cast<float>(grid_size));
  if (shared_settings == nullptr) {
    return nullptr;
  }
  // Stiffness in Oxygen is an authored rigidity bias, not gas pressure.
  // We apply it by lowering compliance values (lower compliance => stiffer).
  const auto compliance_scale
    = params.stiffness > 0.0F ? (1.0F / (1.0F + params.stiffness)) : 1.0F;
  // Use the named constructor to avoid fragility if Jolt ever reorders
  // the VertexAttributes fields.
  const JPH::SoftBodySharedSettings::VertexAttributes vertex_attributes(
    params.edge_compliance * compliance_scale,
    params.shear_compliance * compliance_scale,
    params.bend_compliance * compliance_scale,
    ToJoltLRAType(params.tether_mode), params.tether_max_distance_multiplier);
  shared_settings->CreateConstraints(&vertex_attributes, 1U);
  shared_settings->Optimize();
  return shared_settings;
}

auto RestoreSharedSettingsFromBlob(const std::span<const uint8_t> blob)
  -> JPH::Ref<JPH::SoftBodySharedSettings>
{
  if (blob.empty()) {
    return nullptr;
  }

  const std::string serialized(
    reinterpret_cast<const char*>(blob.data()), blob.size());
  std::istringstream stream(serialized, std::ios::in | std::ios::binary);
  JPH::StreamInWrapper wrapped(stream);

  auto shared_settings = JPH::Ref<JPH::SoftBodySharedSettings> {
    new JPH::SoftBodySharedSettings()
  };
  shared_settings->RestoreBinaryState(wrapped);
  if (wrapped.IsEOF() || wrapped.IsFailed()) {
    return nullptr;
  }
  return shared_settings;
}

[[nodiscard]] auto AreMaterialParamsNearlyEqual(
  const oxygen::physics::softbody::SoftBodyMaterialParams& lhs,
  const oxygen::physics::softbody::SoftBodyMaterialParams& rhs) noexcept -> bool
{
  return oxygen::physics::jolt::IsNearlyEqual(lhs.stiffness, rhs.stiffness)
    && oxygen::physics::jolt::IsNearlyEqual(lhs.damping, rhs.damping)
    && oxygen::physics::jolt::IsNearlyEqual(
      lhs.edge_compliance, rhs.edge_compliance)
    && oxygen::physics::jolt::IsNearlyEqual(
      lhs.shear_compliance, rhs.shear_compliance)
    && oxygen::physics::jolt::IsNearlyEqual(
      lhs.bend_compliance, rhs.bend_compliance)
    && lhs.tether_mode == rhs.tether_mode
    && oxygen::physics::jolt::IsNearlyEqual(
      lhs.tether_max_distance_multiplier, rhs.tether_max_distance_multiplier);
}

[[nodiscard]] auto NeedsTopologyRebuild(
  const oxygen::physics::softbody::SoftBodyMaterialParams& current_params,
  const oxygen::physics::softbody::SoftBodyMaterialParams& next_params) noexcept
  -> bool
{
  return !oxygen::physics::jolt::IsNearlyEqual(
           current_params.stiffness, next_params.stiffness)
    || !oxygen::physics::jolt::IsNearlyEqual(
      current_params.edge_compliance, next_params.edge_compliance)
    || !oxygen::physics::jolt::IsNearlyEqual(
      current_params.shear_compliance, next_params.shear_compliance)
    || !oxygen::physics::jolt::IsNearlyEqual(
      current_params.bend_compliance, next_params.bend_compliance)
    || current_params.tether_mode != next_params.tether_mode
    || !oxygen::physics::jolt::IsNearlyEqual(
      current_params.tether_max_distance_multiplier,
      next_params.tether_max_distance_multiplier);
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
  if (desc.settings_blob.empty()) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (!IsMaterialParamsValid(desc.material_params)) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (!IsFiniteTranslation(desc.initial_position)
    || !IsValidRotation(desc.initial_rotation)) {
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

  auto shared_settings = RestoreSharedSettingsFromBlob(desc.settings_blob);
  if (shared_settings == nullptr) {
    return Err(PhysicsError::kInvalidArgument);
  }

  const auto normalized_rotation = NormalizeRotation(desc.initial_rotation);
  JPH::SoftBodyCreationSettings creation_settings(shared_settings.GetPtr(),
    ToJoltRVec3(desc.initial_position), ToJoltQuat(normalized_rotation),
    kDynamicObjectLayer);
  creation_settings.mLinearDamping = desc.material_params.damping;
  creation_settings.mPressure = 0.0F;

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
  if (next_soft_body_id_ > kSoftBodyAggregateIdMax) {
    (void)world->UnregisterBody(world_id, registered_body_id);
    body_interface->RemoveBody(soft_body_jolt_id);
    body_interface->DestroyBody(soft_body_jolt_id);
    return Err(PhysicsError::kResourceExhausted);
  }

  const auto soft_body_id = AggregateId { next_soft_body_id_++ };
  soft_bodies_.emplace(soft_body_id,
    SoftBodyState {
      .world_id = world_id,
      .jolt_body_id = soft_body_jolt_id,
      .registered_body_id = registered_body_id,
      .cluster_count = desc.cluster_count,
      .material_params = desc.material_params,
      .settings_blob = std::vector<uint8_t> {
        desc.settings_blob.begin(),
        desc.settings_blob.end(),
      },
      .authority = aggregate::AggregateAuthority::kSimulation,
    });
  NoteStructuralChange(world_id);
  return Ok(soft_body_id);
}

auto oxygen::physics::jolt::JoltSoftBodies::DestroySoftBody(
  const WorldId world_id, const AggregateId soft_body_id) -> PhysicsResult<void>
{
  if (!HasWorld(world_id)) {
    return Err(PhysicsError::kWorldNotFound);
  }

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
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

  const auto jolt_body_id = it->second.jolt_body_id;
  const auto registered_body_id = it->second.registered_body_id;
  pending_material_rebuilds_.erase(soft_body_id);

  if (body_interface->IsAdded(jolt_body_id)) {
    body_interface->RemoveBody(jolt_body_id);
  }
  if (world->HasBody(world_id, registered_body_id)) {
    body_interface->DestroyBody(jolt_body_id);
    (void)world->UnregisterBody(world_id, registered_body_id);
  }

  soft_bodies_.erase(it);
  NoteStructuralChange(world_id);
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

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
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

  const auto current_params = it->second.material_params;
  if (NeedsTopologyRebuild(current_params, params)) {
    const auto first_queued_rebuild_for_body
      = pending_material_rebuilds_.find(soft_body_id)
      == pending_material_rebuilds_.end();
    pending_material_rebuilds_.insert_or_assign(soft_body_id, params);
    if (first_queued_rebuild_for_body) {
      NoteStructuralChange(world_id);
    }
    return PhysicsResult<void>::Ok();
  }

  if (!AreMaterialParamsNearlyEqual(current_params, params)) {
    const auto apply_result
      = ApplyMaterialParams(world_id, it->second.jolt_body_id, params);
    if (!apply_result.has_value()) {
      return Err(apply_result.error());
    }
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
    soft_motion->SetPressure(0.0F);
  }
  // BodyLockWrite releases via RAII. We intentionally let it release here
  // so that ActivateBody (threadsafe in Jolt) runs after the write lock.
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

  auto* world = world_.get();
  if (world == nullptr) {
    return Err(PhysicsError::kNotInitialized);
  }
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
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

  const auto old_jolt_body_id = it->second.jolt_body_id;
  const auto old_registered_body_id = it->second.registered_body_id;
  const auto cluster_count = it->second.cluster_count;
  const auto& settings_blob = it->second.settings_blob;
  if (!body_interface->IsAdded(old_jolt_body_id)) {
    return Err(PhysicsError::kBodyNotFound);
  }

  const auto current_position
    = body_interface->GetCenterOfMassPosition(old_jolt_body_id);
  // Note: if the body was created with mMakeRotationIdentity = true (default),
  // the rotation baked into vertices is already accounted for and the body's
  // rotation will be identity. Passing identity here is correct — the rebuilt
  // body will receive the same baking treatment.
  const auto current_rotation = body_interface->GetRotation(old_jolt_body_id);
  const auto was_active = body_interface->IsActive(old_jolt_body_id);

  auto shared_settings
    = RestoreSharedSettingsFromBlob(std::span<const uint8_t> { settings_blob });
  if (shared_settings == nullptr
    && kEnablePrototypeProceduralSoftBodyFallback) {
    shared_settings = BuildPrototypeSharedSettings(cluster_count, params);
  }
  if (shared_settings == nullptr) {
    return Err(PhysicsError::kInvalidArgument);
  }

  JPH::SoftBodyCreationSettings creation_settings(shared_settings.GetPtr(),
    current_position, current_rotation, kDynamicObjectLayer);
  creation_settings.mLinearDamping = params.damping;
  creation_settings.mPressure = 0.0F;

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
  if (world->HasBody(world_id, old_registered_body_id)) {
    body_interface->DestroyBody(old_jolt_body_id);
    (void)world->UnregisterBody(world_id, old_registered_body_id);
  }

  if (was_active) {
    body_interface->ActivateBody(new_jolt_body_id);
  }

  it->second.jolt_body_id = new_jolt_body_id;
  it->second.registered_body_id = new_registered_body_id;
  it->second.material_params = params;
  return PhysicsResult<void>::Ok();
}

auto oxygen::physics::jolt::JoltSoftBodies::FlushPendingMaterialRebuilds(
  const WorldId world_id) -> PhysicsResult<size_t>
{
  std::vector<std::pair<AggregateId, softbody::SoftBodyMaterialParams>>
    pending_for_world {};
  {
    std::scoped_lock lock(mutex_);
    pending_for_world.reserve(pending_material_rebuilds_.size());
    for (auto it = pending_material_rebuilds_.begin();
      it != pending_material_rebuilds_.end();) {
      const auto state_it = soft_bodies_.find(it->first);
      if (state_it == soft_bodies_.end()) {
        it = pending_material_rebuilds_.erase(it);
        continue;
      }
      if (state_it->second.world_id == world_id) {
        pending_for_world.push_back(*it);
      }
      ++it;
    }
  }

  size_t applied_count = 0U;
  for (const auto& pending : pending_for_world) {
    const auto rebuild_result = RebuildSoftBodyForMaterialParams(
      world_id, pending.first, pending.second);
    if (!rebuild_result.has_value()) {
      return Err(rebuild_result.error());
    }
    std::scoped_lock lock(mutex_);
    const auto queued_it = pending_material_rebuilds_.find(pending.first);
    if (queued_it != pending_material_rebuilds_.end()
      && AreMaterialParamsNearlyEqual(queued_it->second, pending.second)) {
      pending_material_rebuilds_.erase(queued_it);
      ++applied_count;
    }
  }
  return Ok(applied_count);
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
