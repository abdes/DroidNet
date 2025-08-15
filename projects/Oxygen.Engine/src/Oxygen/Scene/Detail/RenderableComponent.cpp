//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>

using oxygen::scene::detail::RenderableComponent;

bool RenderableComponent::UsesFixedPolicy() const noexcept
{
  return std::holds_alternative<FixedPolicy>(policy_);
}

bool RenderableComponent::UsesDistancePolicy() const noexcept
{
  return std::holds_alternative<DistancePolicy>(policy_);
}

bool RenderableComponent::UsesScreenSpaceErrorPolicy() const noexcept
{
  return std::holds_alternative<ScreenSpaceErrorPolicy>(policy_);
}

void RenderableComponent::SetLodPolicy(FixedPolicy p)
{
  // Clamp against current geometry lod count if available
  if (geometry_asset_) {
    const auto lc = geometry_asset_->LodCount();
    p.index = (lc == 0) ? 0u : p.Clamp(lc);
  }
  policy_ = std::move(p);
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
    const auto lc = geometry_asset_ ? geometry_asset_->LodCount() : 0u;
    fp->index = (lc == 0) ? 0u : fp->Clamp(lc);
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
  if (!geometry_asset_)
    return std::nullopt;

  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0)
    return std::nullopt;

  const auto resolved = ResolveEffectiveLod(lod_count);
  if (!resolved)
    return std::nullopt;
  const auto lod = *resolved;

  const auto& mesh_ptr = geometry_asset_->MeshAt(lod);
  if (!mesh_ptr) {
    return std::nullopt;
  }

  return ActiveMesh { mesh_ptr, lod };
}

auto RenderableComponent::GetActiveLodIndex() const noexcept
  -> std::optional<std::size_t>
{
  if (!geometry_asset_)
    return std::nullopt;
  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0)
    return std::nullopt;
  return ResolveEffectiveLod(lod_count);
}

auto RenderableComponent::EffectiveLodCount() const noexcept -> std::size_t
{
  return geometry_asset_ ? geometry_asset_->LodCount() : 0u;
}

//=== Local bounds cache and world bounds recompute ===--------------------//

void RenderableComponent::SetGeometry(
  std::shared_ptr<const data::GeometryAsset> geometry)
{
  if (geometry_asset_.get() == geometry.get()) {
    return; // no-op
  }

  geometry_asset_ = std::move(geometry);

  // Reset/evaluate caches
  RebuildLocalBoundsCache();
  RebuildSubmeshStateCache();

  // Reset dynamic LOD selection when geometry changes
  current_lod_.reset();

  // Clamp fixed LOD index to available range
  if (auto* fp = std::get_if<FixedPolicy>(&policy_)) {
    const auto lc = geometry_asset_ ? geometry_asset_->LodCount() : 0u;
    fp->index = (lc == 0) ? 0u : fp->Clamp(lc);
  }

  // Recompute world bounds for current transform (if available)
  RecomputeWorldBoundingSphere();

  // Invalidate on-demand world AABB cache
  InvalidateWorldAabbCache();
}

void RenderableComponent::RebuildLocalBoundsCache() noexcept
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

    auto& lb = lod_bounds_[i];
    lb.mesh_bbox_min = mesh_ptr->BoundingBoxMin();
    lb.mesh_bbox_max = mesh_ptr->BoundingBoxMax();
    lb.mesh_sphere = mesh_ptr->BoundingSphere();

    const auto submeshes = mesh_ptr->SubMeshes();
    lb.submesh_aabbs.reserve(submeshes.size());
    for (const auto& sm : submeshes) {
      lb.submesh_aabbs.emplace_back(sm.BoundingBoxMin(), sm.BoundingBoxMax());
    }
  }
}

void RenderableComponent::RebuildSubmeshStateCache() noexcept
{
  // Preserve existing where possible, clear or default-initialize new slots.
  auto old = std::move(submesh_state_);
  submesh_state_.clear();
  if (!geometry_asset_) {
    return;
  }
  const auto lod_count = geometry_asset_->LodCount();
  submesh_state_.resize(lod_count);
  for (std::size_t i = 0; i < lod_count; ++i) {
    const auto& mesh_ptr = geometry_asset_->MeshAt(i);
    if (!mesh_ptr) {
      continue;
    }
    const auto submeshes = mesh_ptr->SubMeshes();
    auto& lod_states = submesh_state_[i];
    lod_states.resize(submeshes.size());
    // Default initialize all
    for (auto& st : lod_states) {
      st = SubmeshState { .visible = true, .override_material = {} };
    }
    // Copy from old if available
    if (i < old.size()) {
      const auto& prev = old[i];
      const auto count = (std::min)(prev.size(), lod_states.size());
      for (std::size_t s = 0; s < count; ++s) {
        lod_states[s] = prev[s];
      }
    }
  }
}

static inline auto MaxScaleFromMatrix(const glm::mat4& m) noexcept -> float
{
  // Columns represent basis vectors (assuming column-major GLM). Compute their
  // lengths and take the maximum as conservative uniform scale for sphere.
  const auto sx = glm::length(glm::vec3 { m[0][0], m[0][1], m[0][2] });
  const auto sy = glm::length(glm::vec3 { m[1][0], m[1][1], m[1][2] });
  const auto sz = glm::length(glm::vec3 { m[2][0], m[2][1], m[2][2] });
  return (std::max)((std::max)(sx, sy), sz);
}

static inline auto TransformPoint(
  const glm::mat4& m, const glm::vec3& p) noexcept -> glm::vec3
{
  return glm::vec3(m * glm::vec4(p, 1.0f));
}

void RenderableComponent::RecomputeWorldBoundingSphere() const noexcept
{
  world_bounding_sphere_ = { 0.0f, 0.0f, 0.0f, 0.0f };
  if (!geometry_asset_) {
    return;
  }

  // Prefer LOD-specific sphere if we have an active LOD (fixed or evaluated)
  std::optional<std::size_t> lod_opt;
  const auto lod_count = geometry_asset_->LodCount();
  lod_opt = ResolveEffectiveLod(lod_count);

  glm::vec4 local_sphere { 0.0f, 0.0f, 0.0f, 0.0f };
  if (lod_opt.has_value()) {
    const auto clamped = (std::min<std::size_t>)(*lod_opt, lod_count - 1);
    const auto& mesh_ptr = geometry_asset_->MeshAt(clamped);
    if (mesh_ptr)
      local_sphere = mesh_ptr->BoundingSphere();
  } else {
    // Fallback to asset-level AABB -> sphere approximation
    const auto bb_min = geometry_asset_->BoundingBoxMin();
    const auto bb_max = geometry_asset_->BoundingBoxMax();
    const auto center = 0.5f * (bb_min + bb_max);
    const auto radius = 0.5f * glm::length(bb_max - bb_min);
    local_sphere = { center.x, center.y, center.z, radius };
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
  aabb_cache_lod_.reset();
  submesh_world_aabb_cache_.clear();
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
  if (!aabb_cache_lod_.has_value() || *aabb_cache_lod_ != lod) {
    // Rebuild cache for this LOD
    submesh_world_aabb_cache_.clear();
    const auto count
      = lod_bounds_.size() > lod ? lod_bounds_[lod].submesh_aabbs.size() : 0u;
    submesh_world_aabb_cache_.resize(count);
    aabb_cache_lod_ = lod;
  }

  if (submesh_index >= submesh_world_aabb_cache_.size()) {
    return std::nullopt;
  }

  auto& slot = submesh_world_aabb_cache_[submesh_index];
  if (slot.has_value()) {
    return slot; // cached
  }

  // Compute world AABB by transforming 8 corners of local AABB
  if (lod >= lod_bounds_.size()
    || submesh_index >= lod_bounds_[lod].submesh_aabbs.size()) {
    return std::nullopt;
  }

  const auto [bmin, bmax] = lod_bounds_[lod].submesh_aabbs[submesh_index];
  const glm::vec3 corners[8] = {
    { bmin.x, bmin.y, bmin.z },
    { bmax.x, bmin.y, bmin.z },
    { bmin.x, bmax.y, bmin.z },
    { bmin.x, bmin.y, bmax.z },
    { bmax.x, bmax.y, bmin.z },
    { bmax.x, bmin.y, bmax.z },
    { bmin.x, bmax.y, bmax.z },
    { bmax.x, bmax.y, bmax.z },
  };

  glm::vec3 wmin { std::numeric_limits<float>::infinity() };
  glm::vec3 wmax { -std::numeric_limits<float>::infinity() };

  for (const auto& c : corners) {
    const auto wc = TransformPoint(world_matrix_, c);
    wmin = glm::min(wmin, wc);
    wmax = glm::max(wmax, wc);
  }

  slot = std::make_pair(wmin, wmax);
  return slot;
}

//=== Submesh visibility and material overrides ==========================//

bool RenderableComponent::IsSubmeshVisible(
  std::size_t lod, std::size_t submesh_index) const noexcept
{
  if (lod >= submesh_state_.size())
    return false;
  const auto& lod_states = submesh_state_[lod];
  if (submesh_index >= lod_states.size())
    return false;
  return lod_states[submesh_index].visible;
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
  auto& lod_states = submesh_state_[lod];
  if (submesh_index >= lod_states.size()) {
    LOG_F(WARNING,
      "RenderableComponent::SetSubmeshVisible: Submesh index out of range "
      "(lod={}, "
      "sm={}, sm_count={})",
      lod, submesh_index, lod_states.size());
    return;
  }
  lod_states[submesh_index].visible = visible;
}

void RenderableComponent::SetAllSubmeshesVisible(bool visible) noexcept
{
  for (auto& lod_states : submesh_state_) {
    for (auto& st : lod_states) {
      st.visible = visible;
    }
  }
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
  auto& lod_states = submesh_state_[lod];
  if (submesh_index >= lod_states.size()) {
    LOG_F(WARNING,
      "RenderableComponent::SetMaterialOverride: Submesh index out of range "
      "(lod={}, "
      "sm={}, sm_count={})",
      lod, submesh_index, lod_states.size());
    return;
  }
  lod_states[submesh_index].override_material = std::move(material);
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
  auto& lod_states = submesh_state_[lod];
  if (submesh_index >= lod_states.size()) {
    LOG_F(WARNING,
      "RenderableComponent::ClearMaterialOverride: Submesh index out of range "
      "(lod={}, "
      "sm={}, sm_count={})",
      lod, submesh_index, lod_states.size());
    return;
  }
  lod_states[submesh_index].override_material.reset();
}

auto RenderableComponent::ResolveSubmeshMaterial(
  std::size_t lod, std::size_t submesh_index) const noexcept
  -> std::shared_ptr<const data::MaterialAsset>
{
  bool had_override = false;
  bool had_asset = false;
  // 1) Override if set
  if (lod < submesh_state_.size()) {
    const auto& lod_states = submesh_state_[lod];
    if (submesh_index < lod_states.size()) {
      const auto& ov = lod_states[submesh_index].override_material;
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
        auto mat = submeshes[submesh_index].Material();
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

  const auto base = dp->SelectBase(d.value, lod_count);
  current_lod_ = dp->ApplyHysteresis(current_lod_, base, d.value, lod_count);
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

  const auto base = sp->SelectBase(e.value, lod_count);
  current_lod_ = sp->ApplyHysteresis(current_lod_, base, e.value, lod_count);
  InvalidateWorldAabbCache();
  RecomputeWorldBoundingSphere();
}

void RenderableComponent::OnWorldTransformUpdated(const glm::mat4& world)
{
  world_matrix_ = world;
  RecomputeWorldBoundingSphere();
  InvalidateWorldAabbCache();
}

auto RenderableComponent::ResolveEffectiveLod(
  std::size_t lod_count) const noexcept -> std::optional<std::size_t>
{
  if (lod_count == 0)
    return std::nullopt;
  if (std::holds_alternative<FixedPolicy>(policy_)) {
    return std::get<FixedPolicy>(policy_).Clamp(lod_count);
  }
  if (!current_lod_.has_value())
    return std::nullopt;
  return (std::min<std::size_t>)(*current_lod_, lod_count - 1);
}
