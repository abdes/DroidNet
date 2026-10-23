//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
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
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceInventory.h>
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

auto RenderableComponent::HasLodPolicyOverride() const noexcept -> bool
{
  return lod_policy_override_.has_value();
}

void RenderableComponent::SetLodPolicyOverride(LodPolicy policy)
{
  lod_policy_override_ = std::move(policy);
  ResolveLodPolicy();
}

void RenderableComponent::ClearLodPolicyOverride()
{
  lod_policy_override_.reset();
  ResolveLodPolicy();
}

void RenderableComponent::ResolveLodPolicy()
{
  if (lod_policy_override_.has_value()) {
    policy_ = *lod_policy_override_;
  } else if (geometry_asset_ && geometry_asset_->DefaultLodPolicy()) {
    policy_ = MakeLodPolicy(*geometry_asset_->DefaultLodPolicy());
  } else {
    policy_ = MakeLodPolicy(data::schema::geometry::kLodPolicyDefault);
  }
  current_lod_.reset();
  InvalidateWorldAabbCache();
  RecomputeWorldBoundingSphere();
}

RenderableComponent::RenderableComponent(
  std::shared_ptr<const data::GeometryAsset> geometry)
  : geometry_asset_(std::move(geometry))
{
  // Build the caches now, so queries like IsSubmeshVisible() work without a
  // SetGeometry() call.
  RebuildLocalBoundsCache();
  RebuildSubmeshStateCache();
  ResolveLodPolicy();
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
  -> std::optional<data::LodIndex>
{
  if (!geometry_asset_) {
    return std::nullopt;
  }
  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0) {
    return std::nullopt;
  }
  if (const auto lod = ResolveEffectiveLod(lod_count)) {
    return lod;
  }
  return std::nullopt;
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

  RebuildLocalBoundsCache();
  RebuildSubmeshStateCache(previous_geometry.get());
  ResolveLodPolicy();
}

void RenderableComponent::RebuildLocalBoundsCache()
{
  lod_bounds_.clear();
  if (!geometry_asset_) {
    return;
  }

  const auto meshes = geometry_asset_->Meshes();
  lod_bounds_.resize(meshes.size());

  for (auto&& [lb, mesh_ptr] : std::views::zip(lod_bounds_, meshes)) {
    if (!mesh_ptr) {
      continue;
    }

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
  auto previous_overrides = std::move(surface_overrides_);
  surface_overrides_.clear();
  submesh_state_.clear();
  if (!geometry_asset_) {
    return;
  }
  const auto& surfaces = geometry_asset_->Surfaces().surfaces;
  surface_overrides_.resize(surfaces.size());
  const auto meshes = geometry_asset_->Meshes();
  submesh_state_.resize(meshes.size());
  for (auto&& [lod_states, mesh] : std::views::zip(submesh_state_, meshes)) {
    lod_states.resize(mesh->SubMeshes().size());
  }
  const auto same_geometry = previous_geometry != nullptr
    && !previous_geometry->GetAssetKey().IsNil()
    && previous_geometry->GetAssetKey() == geometry_asset_->GetAssetKey();
  const auto& previous_surfaces = same_geometry
    ? previous_geometry->Surfaces().surfaces
    : std::vector<data::Surface> {};
  for (std::size_t index = 0; index < surfaces.size(); ++index) {
    const auto previous = std::ranges::find(previous_surfaces,
      surfaces.at(index).surface_id, &data::Surface::surface_id);
    if (previous != previous_surfaces.end()) {
      surface_overrides_.at(index)
        = std::move(previous_overrides.at(static_cast<std::size_t>(
          std::distance(previous_surfaces.begin(), previous))));
    }
    ResolveSurface(index);
  }
}

void RenderableComponent::ResolveSurface(const std::size_t index) noexcept
{
  const auto& surface = geometry_asset_->Surfaces().surfaces.at(index);
  const auto& overrides = surface_overrides_.at(index);
  const auto defaults = surface.defaults.value_or(data::SurfaceDefaults {
    .visible = data::schema::geometry::surface::kVisibilityDefault,
    .casts_shadows = data::schema::geometry::surface::kCastsShadowsDefault,
  });
  const auto visible = overrides.visible.value_or(defaults.visible);
  const auto casts_shadows
    = visible && overrides.casts_shadows.value_or(defaults.casts_shadows);
  for (const auto& binding : surface.bindings) {
    submesh_state_.at(binding.lod_index.get()).at(binding.submesh_index.get())
      = SubmeshState {
          .visible = visible,
          .casts_shadows = casts_shadows,
          .override_material = overrides.material,
        };
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
  const auto lod_opt = ResolveEffectiveLod(geometry_asset_->LodCount());

  glm::vec4 local_sphere { 0.0F, 0.0F, 0.0F, 0.0F };
  if (lod_opt.has_value()) {
    const auto& mesh_ptr = geometry_asset_->MeshAt(*lod_opt);
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
  const data::SubmeshIndex submesh) const noexcept
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
  if (lod.get() >= lod_bounds_.size()) {
    return std::nullopt;
  }
  const auto& lb
    = *std::next(lod_bounds_.begin(), static_cast<std::ptrdiff_t>(lod.get()));
  if (submesh.get() >= lb.submesh_aabbs.size()
    || submesh.get() >= lb.submesh_world_aabbs.size()) {
    return std::nullopt;
  }

  const auto offset = static_cast<std::ptrdiff_t>(submesh.get());
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

auto RenderableComponent::FindSubmeshState(const data::LodIndex lod,
  const data::SubmeshIndex submesh) const noexcept -> const SubmeshState*
{
  if (lod.get() >= submesh_state_.size()) {
    return nullptr;
  }
  const auto& lod_states = submesh_state_.at(lod.get());
  if (submesh.get() >= lod_states.size()) {
    return nullptr;
  }
  return &lod_states.at(submesh.get());
}

auto RenderableComponent::IsSubmeshVisible(const data::LodIndex lod,
  const data::SubmeshIndex submesh) const noexcept -> bool
{
  const auto* state = FindSubmeshState(lod, submesh);
  return state != nullptr && state->visible;
}

auto RenderableComponent::SubmeshCastsShadows(const data::LodIndex lod,
  const data::SubmeshIndex submesh) const noexcept -> bool
{
  const auto* state = FindSubmeshState(lod, submesh);
  return state != nullptr && state->casts_shadows;
}

template <typename Edit>
auto RenderableComponent::EditSurface(
  const oxygen::data::SurfaceId surface, Edit edit) noexcept -> bool
{
  if (!geometry_asset_) {
    return false;
  }
  const auto& surfaces = geometry_asset_->Surfaces().surfaces;
  const auto declaration
    = std::ranges::find(surfaces, surface, &data::Surface::surface_id);
  if (declaration == surfaces.end()) {
    return false;
  }
  const auto index
    = static_cast<std::size_t>(std::distance(surfaces.begin(), declaration));
  edit(surface_overrides_.at(index));
  ResolveSurface(index);
  return true;
}

auto RenderableComponent::SetMaterialOverride(const data::SurfaceId surface,
  std::shared_ptr<const data::MaterialAsset> material) noexcept -> bool
{
  return EditSurface(surface, [&material](SurfaceOverrides& overrides) {
    overrides.material = std::move(material);
  });
}

auto RenderableComponent::ClearMaterialOverride(
  const data::SurfaceId surface) noexcept -> bool
{
  return SetMaterialOverride(surface, nullptr);
}

auto RenderableComponent::SetVisibilityOverride(
  const data::SurfaceId surface, const bool visible) noexcept -> bool
{
  return EditSurface(surface,
    [visible](SurfaceOverrides& overrides) { overrides.visible = visible; });
}

auto RenderableComponent::ClearVisibilityOverride(
  const data::SurfaceId surface) noexcept -> bool
{
  return EditSurface(
    surface, [](SurfaceOverrides& overrides) { overrides.visible.reset(); });
}

auto RenderableComponent::SetCastsShadowsOverride(
  const data::SurfaceId surface, const bool casts_shadows) noexcept -> bool
{
  return EditSurface(surface, [casts_shadows](SurfaceOverrides& overrides) {
    overrides.casts_shadows = casts_shadows;
  });
}

auto RenderableComponent::ClearCastsShadowsOverride(
  const data::SurfaceId surface) noexcept -> bool
{
  return EditSurface(surface,
    [](SurfaceOverrides& overrides) { overrides.casts_shadows.reset(); });
}

auto RenderableComponent::ResolveSubmeshMaterial(
  const data::LodIndex lod, const data::SubmeshIndex submesh) const noexcept
  -> std::shared_ptr<const data::MaterialAsset>
{
  bool had_override = false;
  bool had_asset = false;
  // 1) Override if set
  if (lod.get() < submesh_state_.size()) {
    const auto& lod_states = submesh_state_.at(lod.get());
    if (submesh.get() < lod_states.size()) {
      const auto& ov = lod_states.at(submesh.get()).override_material;
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
      if (submesh.get() < submeshes.size()) {
        auto mat = std::next(
          submeshes.begin(), static_cast<std::ptrdiff_t>(submesh.get()))
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
      lod, submesh);
  }
  auto fallback = data::MaterialAsset::CreateDefault();
  if (fallback) {
    return fallback;
  }
  LOG_F(ERROR,
    "RenderableComponent::ResolveSubmeshMaterial: Failed to create default "
    "material "
    "(lod={}, sm={}). Using debug material.",
    lod, submesh);
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
  const auto& transform = static_cast<const TransformComponent&>(
    get_component(TransformComponent::ClassTypeId()));
  if (!transform.IsDirty()) {
    OnWorldTransformUpdated(transform.GetWorldMatrix());
  }
}

auto RenderableComponent::ResolveEffectiveLod(
  std::size_t lod_count) const noexcept -> std::optional<data::LodIndex>
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
  return (std::min)(*current_lod_,
    data::LodIndex { static_cast<std::uint32_t>(lod_count - 1) });
}
