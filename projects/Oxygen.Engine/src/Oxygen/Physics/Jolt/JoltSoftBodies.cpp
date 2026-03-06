//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstdint>
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

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Physics/Body/BodyDesc.h>
#include <Oxygen/Physics/Jolt/Converters.h>
#include <Oxygen/Physics/Jolt/JoltSoftBodies.h>

namespace {

constexpr float kMinVolumeCompliance = 1.0e-6F;
constexpr double kMinAbsSurfaceRestVolume = 1.0e-8;

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
    && std::isfinite(params.volume_compliance)
    && std::isfinite(params.pressure_coefficient)
    && std::isfinite(params.tether_max_distance_multiplier)
    && params.stiffness >= 0.0F && params.damping >= 0.0F
    && params.damping <= kMaxDamping && params.edge_compliance >= 0.0F
    && params.shear_compliance >= 0.0F && params.bend_compliance >= 0.0F
    && params.volume_compliance >= 0.0F && params.pressure_coefficient >= 0.0F
    && params.tether_max_distance_multiplier >= 1.0F;
}

[[nodiscard]] auto IsSettingsScaleValid(
  const oxygen::Vec3& settings_scale) noexcept -> bool
{
  return std::isfinite(settings_scale.x) && std::isfinite(settings_scale.y)
    && std::isfinite(settings_scale.z) && settings_scale.x > 0.0F
    && settings_scale.y > 0.0F && settings_scale.z > 0.0F;
}

[[nodiscard]] auto IsCollisionResponseValid(const float restitution,
  const float friction, const float vertex_radius) noexcept -> bool
{
  return std::isfinite(restitution) && restitution >= 0.0F
    && std::isfinite(friction) && friction >= 0.0F
    && std::isfinite(vertex_radius) && vertex_radius >= 0.0F;
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

[[nodiscard]] auto IsFiniteFloat3(const JPH::Float3& value) noexcept -> bool
{
  return std::isfinite(value.x) && std::isfinite(value.y)
    && std::isfinite(value.z);
}

[[nodiscard]] auto ValidateSharedSettings(
  const JPH::SoftBodySharedSettings& settings, std::string& reason) -> bool
{
  constexpr float kAbsVertexBound = 1.0e6F;
  constexpr float kMinAbsSixRestVolume = 1.0e-8F;

  if (settings.mVertices.empty()) {
    reason = "mVertices is empty";
    return false;
  }

  const auto vertex_count = settings.mVertices.size();
  bool has_dynamic_vertex = false;
  for (size_t i = 0; i < vertex_count; ++i) {
    const auto& vertex = settings.mVertices[i];
    if (!IsFiniteFloat3(vertex.mPosition)) {
      reason = "vertex position is non-finite at index=" + std::to_string(i);
      return false;
    }
    if (!IsFiniteFloat3(vertex.mVelocity)) {
      reason = "vertex velocity is non-finite at index=" + std::to_string(i);
      return false;
    }
    if (!std::isfinite(vertex.mInvMass) || vertex.mInvMass < 0.0F) {
      reason = "vertex inv_mass is invalid at index=" + std::to_string(i);
      return false;
    }
    if (std::abs(vertex.mPosition.x) > kAbsVertexBound
      || std::abs(vertex.mPosition.y) > kAbsVertexBound
      || std::abs(vertex.mPosition.z) > kAbsVertexBound) {
      reason
        = "vertex position exceeds safety bound at index=" + std::to_string(i);
      return false;
    }
    has_dynamic_vertex = has_dynamic_vertex || vertex.mInvMass > 0.0F;
  }
  if (!has_dynamic_vertex) {
    reason = "all vertices are kinematic (inv_mass=0)";
    return false;
  }

  for (size_t i = 0; i < settings.mFaces.size(); ++i) {
    const auto& face = settings.mFaces[i];
    if (face.IsDegenerate()) {
      reason = "degenerate face at index=" + std::to_string(i);
      return false;
    }
    for (const auto vertex_index : face.mVertex) {
      if (vertex_index >= vertex_count) {
        reason = "face vertex index out of range at face=" + std::to_string(i);
        return false;
      }
    }
  }

  for (size_t i = 0; i < settings.mVolumeConstraints.size(); ++i) {
    const auto& volume = settings.mVolumeConstraints[i];
    const auto v0 = volume.mVertex[0];
    const auto v1 = volume.mVertex[1];
    const auto v2 = volume.mVertex[2];
    const auto v3 = volume.mVertex[3];
    if (v0 == v1 || v0 == v2 || v0 == v3 || v1 == v2 || v1 == v3 || v2 == v3) {
      reason
        = "volume has duplicate vertex indices at volume=" + std::to_string(i);
      return false;
    }
    for (const auto vertex_index : volume.mVertex) {
      if (vertex_index >= vertex_count) {
        reason
          = "volume vertex index out of range at volume=" + std::to_string(i);
        return false;
      }
    }
    if (!std::isfinite(volume.mCompliance)
      || volume.mCompliance < kMinVolumeCompliance) {
      reason = "volume compliance invalid at volume=" + std::to_string(i)
        + " (must be finite and >= " + std::to_string(kMinVolumeCompliance)
        + ")";
      return false;
    }
    if (!std::isfinite(volume.mSixRestVolume)
      || std::abs(volume.mSixRestVolume) < kMinAbsSixRestVolume) {
      reason = "volume rest volume invalid at volume=" + std::to_string(i);
      return false;
    }
  }

  return true;
}

auto RecomputeDerivedConstraintState(JPH::SoftBodySharedSettings& settings,
  const oxygen::physics::softbody::SoftBodyMaterialParams& params) -> void
{
  settings.CalculateEdgeLengths();
  settings.CalculateBendConstraintConstants();
  settings.CalculateVolumeConstraintVolumes();
  settings.CalculateLRALengths(params.tether_max_distance_multiplier);
}

[[nodiscard]] auto ComputeSignedSurfaceVolume(
  const JPH::SoftBodySharedSettings& settings) noexcept -> double
{
  double signed_six_volume = 0.0;
  for (const auto& face : settings.mFaces) {
    const auto& p0 = settings.mVertices[face.mVertex[0]].mPosition;
    const auto& p1 = settings.mVertices[face.mVertex[1]].mPosition;
    const auto& p2 = settings.mVertices[face.mVertex[2]].mPosition;
    signed_six_volume += static_cast<double>(p0.x)
        * (static_cast<double>(p1.y) * static_cast<double>(p2.z)
          - static_cast<double>(p1.z) * static_cast<double>(p2.y))
      + static_cast<double>(p0.y)
        * (static_cast<double>(p1.z) * static_cast<double>(p2.x)
          - static_cast<double>(p1.x) * static_cast<double>(p2.z))
      + static_cast<double>(p0.z)
        * (static_cast<double>(p1.x) * static_cast<double>(p2.y)
          - static_cast<double>(p1.y) * static_cast<double>(p2.x));
  }
  return signed_six_volume / 6.0;
}

auto ValidatePressureRestVolume(const JPH::SoftBodySharedSettings& settings,
  const oxygen::physics::softbody::SoftBodyMaterialParams& params,
  std::string& reason) -> bool
{
  if (params.pressure_coefficient <= 0.0F) {
    return true;
  }
  if (settings.mFaces.empty()) {
    reason = "pressure_coefficient > 0 requires non-empty soft-body faces";
    return false;
  }
  const auto signed_volume = ComputeSignedSurfaceVolume(settings);
  if (!std::isfinite(signed_volume)
    || std::abs(signed_volume) < kMinAbsSurfaceRestVolume) {
    reason = "pressure_coefficient > 0 requires non-zero finite rest volume";
    return false;
  }
  return true;
}

auto ApplySettingsScale(JPH::SoftBodySharedSettings& settings,
  const oxygen::Vec3& settings_scale) -> void
{
  for (auto& vertex : settings.mVertices) {
    vertex.mPosition.x *= settings_scale.x;
    vertex.mPosition.y *= settings_scale.y;
    vertex.mPosition.z *= settings_scale.z;
  }
}

auto PrepareSharedSettingsForRuntime(JPH::SoftBodySharedSettings& settings,
  const oxygen::physics::softbody::SoftBodyMaterialParams& params,
  const oxygen::Vec3& settings_scale, std::string& error_reason) -> bool
{
  if (!IsSettingsScaleValid(settings_scale)) {
    error_reason = "settings_scale must be finite and > 0";
    return false;
  }

  if (!ValidateSharedSettings(settings, error_reason)) {
    return false;
  }

  ApplySettingsScale(settings, settings_scale);
  RecomputeDerivedConstraintState(settings, params);
  if (!ValidatePressureRestVolume(settings, params, error_reason)) {
    return false;
  }

  settings.Optimize();
  if (!ValidateSharedSettings(settings, error_reason)) {
    return false;
  }

  return true;
}

[[nodiscard]] auto HasVolumeConstraints(
  const JPH::SoftBodySharedSettings& settings) noexcept -> bool
{
  return !settings.mVolumeConstraints.empty();
}

[[nodiscard]] auto ResolveRuntimePressure(
  const oxygen::physics::softbody::SoftBodyMaterialParams& params,
  const bool /*has_volume_constraints*/) noexcept -> float
{
  return std::max(params.pressure_coefficient, 0.0F);
}

struct SettingsBounds final {
  JPH::Float3 min { 0.0F, 0.0F, 0.0F };
  JPH::Float3 max { 0.0F, 0.0F, 0.0F };
};

[[nodiscard]] auto ComputeSettingsBounds(
  const JPH::SoftBodySharedSettings& settings) noexcept -> SettingsBounds
{
  auto bounds = SettingsBounds {};
  if (settings.mVertices.empty()) {
    return bounds;
  }

  bounds.min = settings.mVertices[0].mPosition;
  bounds.max = settings.mVertices[0].mPosition;
  for (size_t i = 1; i < settings.mVertices.size(); ++i) {
    const auto& p = settings.mVertices[i].mPosition;
    bounds.min.x = std::min(bounds.min.x, p.x);
    bounds.min.y = std::min(bounds.min.y, p.y);
    bounds.min.z = std::min(bounds.min.z, p.z);
    bounds.max.x = std::max(bounds.max.x, p.x);
    bounds.max.y = std::max(bounds.max.y, p.y);
    bounds.max.z = std::max(bounds.max.z, p.z);
  }
  return bounds;
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
    && oxygen::physics::jolt::IsNearlyEqual(
      lhs.volume_compliance, rhs.volume_compliance)
    && oxygen::physics::jolt::IsNearlyEqual(
      lhs.pressure_coefficient, rhs.pressure_coefficient)
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
    || !oxygen::physics::jolt::IsNearlyEqual(
      current_params.volume_compliance, next_params.volume_compliance)
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
  if (!IsSettingsScaleValid(desc.settings_scale)
    || !IsCollisionResponseValid(
      desc.restitution, desc.friction, desc.vertex_radius)) {
    return Err(PhysicsError::kInvalidArgument);
  }
  if (desc.solver_iteration_count == 0U
    || !std::isfinite(desc.gravity_factor)) {
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
  const auto simulation_lock = world->LockSimulationApi();
  auto body_interface = world->TryGetBodyInterface(world_id);
  if (body_interface == nullptr) {
    return Err(PhysicsError::kWorldNotFound);
  }
  const auto object_layer_result = world->ResolveBodyObjectLayer(world_id,
    body::BodyType::kDynamic, desc.collision_layer, desc.collision_mask, false);
  if (object_layer_result.has_error()) {
    return Err(object_layer_result.error());
  }

  auto shared_settings = RestoreSharedSettingsFromBlob(desc.settings_blob);
  std::string validation_error {};
  if (shared_settings != nullptr
    && !PrepareSharedSettingsForRuntime(*shared_settings, desc.material_params,
      desc.settings_scale, validation_error)) {
    LOG_F(ERROR,
      "JoltSoftBodies: invalid shared settings blob for create "
      "(cluster_count={}, reason='{}').",
      desc.cluster_count, validation_error);
    shared_settings = nullptr;
  }
  if (shared_settings == nullptr) {
    return Err(PhysicsError::kInvalidArgument);
  }

  const auto has_volume_constraints = HasVolumeConstraints(*shared_settings);
  const auto runtime_pressure
    = ResolveRuntimePressure(desc.material_params, has_volume_constraints);
  const auto bounds = ComputeSettingsBounds(*shared_settings);
  LOG_F(INFO,
    "JoltSoftBodies: create topology summary (cluster_count={} vertices={} "
    "faces={} edges={} volumes={} lra={} pressure={:.4f} "
    "solver_iterations={} gravity_factor={:.3f} "
    "scale=[{:.3f},{:.3f},{:.3f}] restitution={:.3f} friction={:.3f} "
    "vertex_radius={:.3f} "
    "bounds_min=[{:.3f},{:.3f},{:.3f}] bounds_max=[{:.3f},{:.3f},{:.3f}]).",
    desc.cluster_count, shared_settings->mVertices.size(),
    shared_settings->mFaces.size(), shared_settings->mEdgeConstraints.size(),
    shared_settings->mVolumeConstraints.size(),
    shared_settings->mLRAConstraints.size(), runtime_pressure,
    desc.solver_iteration_count, desc.gravity_factor, desc.settings_scale.x,
    desc.settings_scale.y, desc.settings_scale.z, desc.restitution,
    desc.friction, desc.vertex_radius, bounds.min.x, bounds.min.y, bounds.min.z,
    bounds.max.x, bounds.max.y, bounds.max.z);
  if (!has_volume_constraints && runtime_pressure <= 0.0F) {
    LOG_F(WARNING,
      "JoltSoftBodies: non-volumetric soft body has zero pressure; collapse "
      "under gravity/contact is expected.");
  }

  const auto normalized_rotation = NormalizeRotation(desc.initial_rotation);
  JPH::SoftBodyCreationSettings creation_settings(shared_settings.GetPtr(),
    ToJoltRVec3(desc.initial_position), ToJoltQuat(normalized_rotation),
    static_cast<JPH::ObjectLayer>(object_layer_result.value()));
  creation_settings.mLinearDamping = desc.material_params.damping;
  creation_settings.mNumIterations = desc.solver_iteration_count;
  creation_settings.mPressure = runtime_pressure;
  creation_settings.mRestitution = desc.restitution;
  creation_settings.mFriction = desc.friction;
  creation_settings.mGravityFactor = desc.gravity_factor;
  creation_settings.mVertexRadius = desc.vertex_radius;

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
  body_interface->SetUserData(
    soft_body_jolt_id, static_cast<uint64_t>(soft_body_id.get()));
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
      .settings_scale = desc.settings_scale,
      .collision_layer = desc.collision_layer,
      .collision_mask = desc.collision_mask,
      .restitution = desc.restitution,
      .friction = desc.friction,
      .vertex_radius = desc.vertex_radius,
      .solver_iteration_count = desc.solver_iteration_count,
      .gravity_factor = desc.gravity_factor,
      .authority = aggregate::AggregateAuthority::kSimulation,
    });
  LOG_F(INFO,
    "JoltSoftBodies: created soft body (world_id={} aggregate_id={} "
    "jolt_body_id={} registered_body_id={} cluster_count={}).",
    world_id.get(), soft_body_id.get(),
    soft_body_jolt_id.GetIndexAndSequenceNumber(), registered_body_id.get(),
    desc.cluster_count);
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
  const auto simulation_lock = world->LockSimulationApi();
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

  LOG_F(INFO,
    "JoltSoftBodies: destroyed soft body (world_id={} aggregate_id={} "
    "jolt_body_id={} registered_body_id={}).",
    world_id.get(), soft_body_id.get(),
    jolt_body_id.GetIndexAndSequenceNumber(), registered_body_id.get());
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
  const auto simulation_lock = world->LockSimulationApi();
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
  const auto simulation_lock = world->LockSimulationApi();
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
    const auto* settings = soft_motion->GetSettings();
    const auto has_volume_constraints
      = settings != nullptr && HasVolumeConstraints(*settings);
    soft_motion->SetPressure(
      ResolveRuntimePressure(params, has_volume_constraints));
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
  const auto simulation_lock = world->LockSimulationApi();
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
  const auto settings_scale = it->second.settings_scale;
  const auto collision_layer = it->second.collision_layer;
  const auto collision_mask = it->second.collision_mask;
  const auto restitution = it->second.restitution;
  const auto friction = it->second.friction;
  const auto vertex_radius = it->second.vertex_radius;
  const auto solver_iteration_count = it->second.solver_iteration_count;
  const auto gravity_factor = it->second.gravity_factor;
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
  std::string validation_error {};
  if (shared_settings != nullptr
    && !PrepareSharedSettingsForRuntime(
      *shared_settings, params, settings_scale, validation_error)) {
    LOG_F(ERROR,
      "JoltSoftBodies: invalid shared settings blob for rebuild "
      "(aggregate_id={}, reason='{}').",
      soft_body_id.get(), validation_error);
    shared_settings = nullptr;
  }
  if (shared_settings == nullptr) {
    return Err(PhysicsError::kInvalidArgument);
  }
  const auto object_layer_result = world->ResolveBodyObjectLayer(
    world_id, body::BodyType::kDynamic, collision_layer, collision_mask, false);
  if (object_layer_result.has_error()) {
    return Err(object_layer_result.error());
  }

  const auto has_volume_constraints = HasVolumeConstraints(*shared_settings);
  const auto runtime_pressure
    = ResolveRuntimePressure(params, has_volume_constraints);
  const auto bounds = ComputeSettingsBounds(*shared_settings);
  LOG_F(INFO,
    "JoltSoftBodies: rebuild topology summary (aggregate_id={} vertices={} "
    "faces={} edges={} volumes={} lra={} pressure={:.4f} "
    "solver_iterations={} gravity_factor={:.3f} "
    "scale=[{:.3f},{:.3f},{:.3f}] restitution={:.3f} friction={:.3f} "
    "vertex_radius={:.3f} "
    "bounds_min=[{:.3f},{:.3f},{:.3f}] bounds_max=[{:.3f},{:.3f},{:.3f}]).",
    soft_body_id.get(), shared_settings->mVertices.size(),
    shared_settings->mFaces.size(), shared_settings->mEdgeConstraints.size(),
    shared_settings->mVolumeConstraints.size(),
    shared_settings->mLRAConstraints.size(), runtime_pressure,
    solver_iteration_count, gravity_factor, settings_scale.x, settings_scale.y,
    settings_scale.z, restitution, friction, vertex_radius, bounds.min.x,
    bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z);
  if (!has_volume_constraints && runtime_pressure <= 0.0F) {
    LOG_F(WARNING,
      "JoltSoftBodies: rebuild produced non-volumetric soft body with zero "
      "pressure; collapse under gravity/contact is expected.");
  }

  JPH::SoftBodyCreationSettings creation_settings(shared_settings.GetPtr(),
    current_position, current_rotation,
    static_cast<JPH::ObjectLayer>(object_layer_result.value()));
  creation_settings.mLinearDamping = params.damping;
  creation_settings.mNumIterations = solver_iteration_count;
  creation_settings.mPressure = runtime_pressure;
  creation_settings.mRestitution = restitution;
  creation_settings.mFriction = friction;
  creation_settings.mGravityFactor = gravity_factor;
  creation_settings.mVertexRadius = vertex_radius;

  const auto new_jolt_body_id = body_interface->CreateAndAddSoftBody(
    creation_settings, JPH::EActivation::DontActivate);
  if (new_jolt_body_id.IsInvalid()) {
    return Err(PhysicsError::kBackendInitFailed);
  }

  const auto new_registered_body_id
    = BodyId { new_jolt_body_id.GetIndexAndSequenceNumber() };
  body_interface->SetUserData(
    new_jolt_body_id, static_cast<uint64_t>(soft_body_id.get()));
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
  LOG_F(INFO,
    "JoltSoftBodies: rebuilt soft body (world_id={} aggregate_id={} "
    "old_jolt_body_id={} new_jolt_body_id={}).",
    world_id.get(), soft_body_id.get(),
    old_jolt_body_id.GetIndexAndSequenceNumber(),
    new_jolt_body_id.GetIndexAndSequenceNumber());
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
