//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include <glm/common.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Types/ActiveMesh.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/Scene/Types/Strong.h>

using oxygen::scene::detail::RenderableComponent;

auto RenderableComponent::UsesFixedPolicy() const noexcept -> bool
{
  return std::holds_alternative<FixedPolicy>(policy_);
}

auto RenderableComponent::UsesDistancePolicy() const noexcept -> bool
{
  return std::holds_alternative<DistancePolicy>(policy_);
}

auto RenderableComponent::UsesScreenSpaceErrorPolicy() const noexcept -> bool
{
  return std::holds_alternative<ScreenSpaceErrorPolicy>(policy_);
}

void RenderableComponent::SetLodPolicy(FixedPolicy p)
{
  // Clamp against current geometry lod count if available
  if (geometry_asset_) {
    const auto lc = geometry_asset_->LodCount();
    p.index = (lc == 0) ? 0U : p.Clamp(lc);
  }
  policy_ = p;
  current_lod_.reset();
  InvalidateWorldAabbCache();
  RecomputeWorldBoundingSphere();
}

void RenderableComponent::SetLodPolicy(DistancePolicy p)
{
  policy_ = std::move(p);
  current_lod_.reset();
  InvalidateWorldAabbCache();
  RecomputeWorldBoundingSphere();
}

void RenderableComponent::SetLodPolicy(ScreenSpaceErrorPolicy p)
{
  if (!p.ValidateSizes(EffectiveLodCount())) {
    throw std::invalid_argument(
      "Invalid ScreenSpaceErrorPolicy: sizes are not valid");
  }
  policy_ = std::move(p);
  current_lod_.reset();
  InvalidateWorldAabbCache();
  RecomputeWorldBoundingSphere();
}
RenderableComponent::RenderableComponent(
  std::shared_ptr<const data::GeometryAsset> geometry)
  : geometry_asset_(std::move(geometry))
{
  // When constructed with a geometry (AttachGeometry/AddComponent path),
  // eagerly build caches and initialize per-submesh state so queries like
  // IsSubmeshVisible() work immediately without requiring a SetGeometry()
  // call. Clamp fixed LOD policy to the available range as well.
  RebuildLocalBoundsCache();
  RebuildSubmeshStateCache();
  if (auto* fp = std::get_if<FixedPolicy>(&policy_)) {
    const auto lc = geometry_asset_ ? geometry_asset_->LodCount() : 0U;
    fp->index = (lc == 0) ? 0U : fp->Clamp(lc);
  }
  RecomputeWorldBoundingSphere();
  InvalidateWorldAabbCache();
}

/*!
 Behavior:
 - If no GeometryAsset or it has zero LODs -> returns empty.
 - If policy is kFixed -> returns the clamped fixed LOD mesh.
 - If policy is dynamic (kDistance/kScreenSpaceError) and no evaluation has
   been performed yet, returns empty until an evaluation sets a current LOD.

 This avoids exposing raw indices and lets callers work directly with the
 Mesh object (for submeshes, bounds, and draw views).

 @return std::optional with ActiveMesh view; empty if unresolved.
*/
auto RenderableComponent::GetActiveMesh() const noexcept
  -> std::optional<ActiveMesh>
{
  if (!geometry_asset_) {
    return std::nullopt;
  }

  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0) {
    return std::nullopt;
  }

  const auto resolved = ResolveEffectiveLod(lod_count);
  if (!resolved) {
    return std::nullopt;
  }
  const auto lod = *resolved;

  const auto& mesh_ptr = geometry_asset_->MeshAt(lod);
  if (!mesh_ptr) {
    return std::nullopt;
  }

  return ActiveMesh { .mesh = mesh_ptr, .lod = lod };
}

auto RenderableComponent::GetActiveLodIndex() const noexcept
  -> std::optional<std::size_t>
{
  if (!geometry_asset_) {
    return std::nullopt;
  }
  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0) {
    return std::nullopt;
  }
  return ResolveEffectiveLod(lod_count);
}

auto RenderableComponent::EffectiveLodCount() const noexcept -> std::size_t
{
  return geometry_asset_ ? geometry_asset_->LodCount() : 0U;
}

//=== Local bounds cache and world bounds recompute ===--------------------//

void RenderableComponent::SetGeometry(
  std::shared_ptr<const data::GeometryAsset> geometry)
{
  if (geometry_asset_.get() == geometry.get()) {
    return; // no-op
  }

  const auto previous_geometry = std::move(geometry_asset_);
  geometry_asset_ = std::move(geometry);

  // Reset/evaluate caches
  RebuildLocalBoundsCache();
  RebuildSubmeshStateCache(previous_geometry.get());

  // Reset dynamic LOD selection when geometry changes
  current_lod_.reset();

  // Clamp fixed LOD index to available range
  if (auto* fp = std::get_if<FixedPolicy>(&policy_)) {
    const auto lc = geometry_asset_ ? geometry_asset_->LodCount() : 0U;
    fp->index = (lc == 0) ? 0U : fp->Clamp(lc);
  }

  // Recompute world bounds for current transform (if available)
  RecomputeWorldBoundingSphere();

  // Invalidate on-demand world AABB cache
  InvalidateWorldAabbCache();
}

void RenderableComponent::RebuildLocalBoundsCache()
{
  lod_bounds_.clear();
  if (!geometry_asset_) {
    return;
  }

  const auto lod_count = geometry_asset_->LodCount();
  lod_bounds_.resize(lod_count);

  for (std::size_t i = 0; i < lod_count; ++i) {
    const auto& mesh_ptr = geometry_asset_->MeshAt(i);
    if (!mesh_ptr) {
      continue;
    }

    auto& lb = lod_bounds_.at(i);
    lb.mesh_bbox_min = mesh_ptr->BoundingBoxMin();
    lb.mesh_bbox_max = mesh_ptr->BoundingBoxMax();
    lb.mesh_sphere = mesh_ptr->BoundingSphere();

    const auto submeshes = mesh_ptr->SubMeshes();
    lb.submesh_aabbs.reserve(submeshes.size());
    for (const auto& sm : submeshes) {
      lb.submesh_aabbs.emplace_back(sm.BoundingBoxMin(), sm.BoundingBoxMax());
    }
    lb.submesh_world_aabbs.resize(submeshes.size());
  }
}

void RenderableComponent::RebuildSubmeshStateCache(
  const data::GeometryAsset* previous_geometry)
{
  auto previous_materials = std::move(slot_materials_);
  slot_materials_.clear();
  submesh_state_.clear();
  if (!geometry_asset_) {
    return;
  }
  const auto& slots = geometry_asset_->MaterialSlots().slots;
  slot_materials_.resize(slots.size());
  submesh_state_.resize(geometry_asset_->LodCount());
  for (std::size_t lod = 0; lod < submesh_state_.size(); ++lod) {
    submesh_state_.at(lod).resize(
      geometry_asset_->MeshAt(lod)->SubMeshes().size());
  }
  if (previous_geometry == nullptr || previous_geometry->GetAssetKey().IsNil()
    || previous_geometry->GetAssetKey() != geometry_asset_->GetAssetKey()) {
    return;
  }
  const auto& previous_slots = previous_geometry->MaterialSlots().slots;
  for (std::size_t index = 0; index < slots.size(); ++index) {
    const auto previous = std::ranges::find(
      previous_slots, slots.at(index).slot_id, &data::MaterialSlot::slot_id);
    if (previous == previous_slots.end()) {
      continue;
    }
    const auto previous_index = static_cast<std::size_t>(
      std::distance(previous_slots.begin(), previous));
    slot_materials_.at(index) = previous_materials.at(previous_index);
    for (const auto& binding : slots.at(index).bindings) {
      submesh_state_.at(binding.lod_index)
        .at(binding.submesh_index)
        .override_material = slot_materials_.at(index);
    }
  }
}

static inline auto MaxScaleFromMatrix(const glm::mat4& m) noexcept -> float
{
  // Columns represent basis vectors (assuming column-major GLM). Compute their
  // lengths and take the maximum as conservative uniform scale for sphere.
  const auto sx = glm::length(glm::vec3(glm::column(m, 0)));
  const auto sy = glm::length(glm::vec3(glm::column(m, 1)));
  const auto sz = glm::length(glm::vec3(glm::column(m, 2)));
  return (std::max)({ sx, sy, sz });
}

static inline auto TransformPoint(
  const glm::mat4& m, const glm::vec3& p) noexcept -> glm::vec3
{
  return { m * glm::vec4(p, 1.0F) };
}

static inline auto SphereFromBounds(const glm::vec3& bounds_min,
  const glm::vec3& bounds_max) noexcept -> glm::vec4
{
  const auto center = 0.5F * (bounds_min + bounds_max);
  const auto radius = 0.5F * glm::length(bounds_max - bounds_min);
  return { center.x, center.y, center.z, radius };
}

static inline auto IsFiniteVec3(const glm::vec3& v) noexcept -> bool
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

static inline auto IsUsableBounds(
  const glm::vec3& bounds_min, const glm::vec3& bounds_max) noexcept -> bool
{
  return IsFiniteVec3(bounds_min) && IsFiniteVec3(bounds_max)
    && bounds_max.x >= bounds_min.x && bounds_max.y >= bounds_min.y
    && bounds_max.z >= bounds_min.z;
}

static inline auto IsUsableSphere(const glm::vec4& sphere) noexcept -> bool
{
  return std::isfinite(sphere.x) && std::isfinite(sphere.y)
    && std::isfinite(sphere.z) && std::isfinite(sphere.w) && sphere.w > 0.0F;
}

void RenderableComponent::RecomputeWorldBoundingSphere() const noexcept
{
  world_bounding_sphere_ = { 0.0F, 0.0F, 0.0F, 0.0F };
  if (!geometry_asset_) {
    return;
  }

  // Prefer LOD-specific sphere if we have an active LOD (fixed or evaluated)
  std::optional<std::size_t> lod_opt;
  const auto lod_count = geometry_asset_->LodCount();
  lod_opt = ResolveEffectiveLod(lod_count);

  glm::vec4 local_sphere { 0.0F, 0.0F, 0.0F, 0.0F };
  if (lod_opt.has_value()) {
    const auto clamped = (std::min<std::size_t>)(*lod_opt, lod_count - 1);
    const auto& mesh_ptr = geometry_asset_->MeshAt(clamped);
    if (mesh_ptr) {
      local_sphere = mesh_ptr->BoundingSphere();
      if (!IsUsableSphere(local_sphere)) {
        const auto mesh_bounds_min = mesh_ptr->BoundingBoxMin();
        const auto mesh_bounds_max = mesh_ptr->BoundingBoxMax();
        if (IsUsableBounds(mesh_bounds_min, mesh_bounds_max)) {
          local_sphere = SphereFromBounds(mesh_bounds_min, mesh_bounds_max);
        }
      }
    }
  }

  if (!IsUsableSphere(local_sphere)) {
    const auto asset_bounds_min = geometry_asset_->BoundingBoxMin();
    const auto asset_bounds_max = geometry_asset_->BoundingBoxMax();
    if (IsUsableBounds(asset_bounds_min, asset_bounds_max)) {
      local_sphere = SphereFromBounds(asset_bounds_min, asset_bounds_max);
    }
  }

  if (!IsUsableSphere(local_sphere)) {
    return;
  }

  // Transform sphere: center by full transform, radius by max-scale
  const auto world_center = TransformPoint(world_matrix_,
    glm::vec3 { local_sphere.x, local_sphere.y, local_sphere.z });
  const float s = MaxScaleFromMatrix(world_matrix_);
  world_bounding_sphere_
    = { world_center.x, world_center.y, world_center.z, local_sphere.w * s };
}

void RenderableComponent::InvalidateWorldAabbCache() const noexcept
{
  for (const auto& lb : lod_bounds_) {
    std::ranges::fill(lb.submesh_world_aabbs, std::nullopt);
  }
}

auto RenderableComponent::GetWorldSubMeshBoundingBox(
  std::size_t submesh_index) const noexcept
  -> std::optional<std::pair<glm::vec3, glm::vec3>>
{
  if (!geometry_asset_) {
    return std::nullopt;
  }

  auto active = GetActiveMesh();
  if (!active) {
    return std::nullopt;
  }

  const auto lod = active->lod;
  if (lod >= lod_bounds_.size()) {
    return std::nullopt;
  }
  const auto& lb
    = *std::next(lod_bounds_.begin(), static_cast<std::ptrdiff_t>(lod));
  if (submesh_index >= lb.submesh_aabbs.size()
    || submesh_index >= lb.submesh_world_aabbs.size()) {
    return std::nullopt;
  }

  const auto offset = static_cast<std::ptrdiff_t>(submesh_index);
  auto& slot = *std::next(lb.submesh_world_aabbs.begin(), offset);
  if (slot.has_value()) {
    return slot; // cached
  }

  // Compute world AABB by transforming 8 corners of local AABB
  const auto [bmin, bmax] = *std::next(lb.submesh_aabbs.begin(), offset);
  const auto corners = std::array<glm::vec3, 8> {
    glm::vec3 { bmin.x, bmin.y, bmin.z },
    glm::vec3 { bmax.x, bmin.y, bmin.z },
    glm::vec3 { bmin.x, bmax.y, bmin.z },
    glm::vec3 { bmin.x, bmin.y, bmax.z },
    glm::vec3 { bmax.x, bmax.y, bmin.z },
    glm::vec3 { bmax.x, bmin.y, bmax.z },
    glm::vec3 { bmin.x, bmax.y, bmax.z },
    glm::vec3 { bmax.x, bmax.y, bmax.z },
  };

  glm::vec3 wmin { std::numeric_limits<float>::infinity() };
  glm::vec3 wmax { -std::numeric_limits<float>::infinity() };

  for (const auto& corner : corners) {
    const auto wc = TransformPoint(world_matrix_, corner);
    wmin = glm::min(wmin, wc);
    wmax = glm::max(wmax, wc);
  }

  slot = std::make_pair(wmin, wmax);
  return slot;
}

//=== Submesh visibility and material overrides ==========================//

auto RenderableComponent::IsSubmeshVisible(
  std::size_t lod, std::size_t submesh_index) const noexcept -> bool
{
  if (lod >= submesh_state_.size()) {
    return false;
  }
  const auto& lod_states = submesh_state_.at(lod);
  if (submesh_index >= lod_states.size()) {
    return false;
  }
  return lod_states.at(submesh_index).visible;
}

void RenderableComponent::SetSubmeshVisible(
  std::size_t lod, std::size_t submesh_index, bool visible) noexcept
{
  if (lod >= submesh_state_.size()) {
    LOG_F(WARNING,
      "RenderableComponent::SetSubmeshVisible: LOD index out of range (lod={}, "
      "lod_count={})",
      lod, submesh_state_.size());
    return;
  }
  auto& lod_states = submesh_state_.at(lod);
  if (submesh_index >= lod_states.size()) {
    LOG_F(WARNING,
      "RenderableComponent::SetSubmeshVisible: Submesh index out of range "
      "(lod={}, "
      "sm={}, sm_count={})",
      lod, submesh_index, lod_states.size());
    return;
  }
  lod_states.at(submesh_index).visible = visible;
}

void RenderableComponent::SetAllSubmeshesVisible(bool visible) noexcept
{
  for (auto& lod_states : submesh_state_) {
    for (auto& st : lod_states) {
      st.visible = visible;
    }
  }
}

auto RenderableComponent::SetMaterialOverride(const data::MaterialSlotId slot,
  std::shared_ptr<const data::MaterialAsset> material) noexcept -> bool
{
  if (!geometry_asset_) {
    return false;
  }
  const auto& slots = geometry_asset_->MaterialSlots().slots;
  const auto declaration
    = std::ranges::find(slots, slot, &data::MaterialSlot::slot_id);
  if (declaration == slots.end()) {
    return false;
  }
  const auto index
    = static_cast<std::size_t>(std::distance(slots.begin(), declaration));
  for (const auto& binding : declaration->bindings) {
    submesh_state_.at(binding.lod_index)
      .at(binding.submesh_index)
      .override_material = material;
  }
  slot_materials_.at(index) = std::move(material);
  return true;
}

auto RenderableComponent::ClearMaterialOverride(
  const data::MaterialSlotId slot) noexcept -> bool
{
  return SetMaterialOverride(slot, nullptr);
}

void RenderableComponent::SetMaterialOverride(std::size_t lod,
  std::size_t submesh_index,
  std::shared_ptr<const data::MaterialAsset> material) noexcept
{
  if (lod >= submesh_state_.size()) {
    LOG_F(WARNING,
      "RenderableComponent::SetMaterialOverride: LOD index out of range "
      "(lod={}, "
      "lod_count={})",
      lod, submesh_state_.size());
    return;
  }
  auto& lod_states = submesh_state_.at(lod);
  if (submesh_index >= lod_states.size()) {
    LOG_F(WARNING,
      "RenderableComponent::SetMaterialOverride: Submesh index out of range "
      "(lod={}, "
      "sm={}, sm_count={})",
      lod, submesh_index, lod_states.size());
    return;
  }
  lod_states.at(submesh_index).override_material = std::move(material);
}

void RenderableComponent::ClearMaterialOverride(
  std::size_t lod, std::size_t submesh_index) noexcept
{
  if (lod >= submesh_state_.size()) {
    LOG_F(WARNING,
      "RenderableComponent::ClearMaterialOverride: LOD index out of range "
      "(lod={}, "
      "lod_count={})",
      lod, submesh_state_.size());
    return;
  }
  auto& lod_states = submesh_state_.at(lod);
  if (submesh_index >= lod_states.size()) {
    LOG_F(WARNING,
      "RenderableComponent::ClearMaterialOverride: Submesh index out of range "
      "(lod={}, "
      "sm={}, sm_count={})",
      lod, submesh_index, lod_states.size());
    return;
  }
  lod_states.at(submesh_index).override_material.reset();
}

auto RenderableComponent::ResolveSubmeshMaterial(
  std::size_t lod, std::size_t submesh_index) const noexcept
  -> std::shared_ptr<const data::MaterialAsset>
{
  bool had_override = false;
  bool had_asset = false;
  // 1) Override if set
  if (lod < submesh_state_.size()) {
    const auto& lod_states = submesh_state_.at(lod);
    if (submesh_index < lod_states.size()) {
      const auto& ov = lod_states.at(submesh_index).override_material;
      if (ov) {
        had_override = true;
        return ov;
      }
    }
  }

  // 2) Submesh material from asset
  if (geometry_asset_) {
    const auto& mesh_ptr = geometry_asset_->MeshAt(lod);
    if (mesh_ptr) {
      const auto submeshes = mesh_ptr->SubMeshes();
      if (submesh_index < submeshes.size()) {
        auto mat = std::next(
          submeshes.begin(), static_cast<std::ptrdiff_t>(submesh_index))
                     ->Material();
        if (mat) {
          had_asset = true;
          return mat;
        }
      }
    }
  }

  // 3) Fallback to default (or debug) material
  if (!had_override && !had_asset) {
    LOG_F(WARNING,
      "RenderableComponent::ResolveSubmeshMaterial: Missing material (lod={}, "
      "sm={}). "
      "Using default material.",
      lod, submesh_index);
  }
  auto fallback = data::MaterialAsset::CreateDefault();
  if (fallback) {
    return fallback;
  }
  LOG_F(ERROR,
    "RenderableComponent::ResolveSubmeshMaterial: Failed to create default "
    "material "
    "(lod={}, sm={}). Using debug material.",
    lod, submesh_index);
  return data::MaterialAsset::CreateDebug();
}

//=== LOD evaluation with hysteresis ===-----------------------------------//

void RenderableComponent::SelectActiveMesh(NormalizedDistance d) const noexcept
{
  const auto* dp = std::get_if<DistancePolicy>(&policy_);
  if (!dp) {
    return;
  }
  if (!geometry_asset_) {
    return;
  }
  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0) {
    return;
  }

  const auto base = dp->SelectBase(d, lod_count);
  current_lod_ = dp->ApplyHysteresis(current_lod_, base, d, lod_count);
  InvalidateWorldAabbCache();
  RecomputeWorldBoundingSphere();
}

void RenderableComponent::SelectActiveMesh(ScreenSpaceError e) const noexcept
{
  const auto* sp = std::get_if<ScreenSpaceErrorPolicy>(&policy_);
  if (!sp) {
    return;
  }
  if (!geometry_asset_) {
    return;
  }
  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0) {
    return;
  }

  const auto base = sp->SelectBase(e, lod_count);
  current_lod_ = sp->ApplyHysteresis(current_lod_, base, e, lod_count);
  InvalidateWorldAabbCache();
  RecomputeWorldBoundingSphere();
}

void RenderableComponent::OnWorldTransformUpdated(const glm::mat4& world)
{
  world_matrix_ = world;
  RecomputeWorldBoundingSphere();
  InvalidateWorldAabbCache();
}

auto RenderableComponent::UpdateDependencies(
  const std::function<Component&(TypeId)>& get_component) noexcept -> void
{
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  const auto& transform = static_cast<const TransformComponent&>(
    get_component(TransformComponent::ClassTypeId()));
  if (!transform.IsDirty()) {
    OnWorldTransformUpdated(transform.GetWorldMatrix());
  }
}

auto RenderableComponent::ResolveEffectiveLod(
  std::size_t lod_count) const noexcept -> std::optional<std::size_t>
{
  if (lod_count == 0) {
    return std::nullopt;
  }
  if (const auto* fixed = std::get_if<FixedPolicy>(&policy_)) {
    return fixed->Clamp(lod_count);
  }
  if (!current_lod_.has_value()) {
    return std::nullopt;
  }
  return (std::min<std::size_t>)(*current_lod_, lod_count - 1);
}
