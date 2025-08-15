//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Scene/Detail/Renderable.h>

using oxygen::scene::detail::Renderable;

auto Renderable::FixedPolicy::Clamp(std::size_t lod_count) const noexcept
  -> std::size_t
{
  if (lod_count == 0) {
    return 0;
  }
  return (index < lod_count) ? index : (lod_count - 1);
}

void Renderable::DistancePolicy::NormalizeThresholds() noexcept
{
  if (!thresholds.empty()) {
    for (std::size_t i = 1; i < thresholds.size(); ++i) {
      if (thresholds[i] < thresholds[i - 1])
        thresholds[i] = thresholds[i - 1];
    }
  }
  hysteresis_ratio = (std::clamp)(hysteresis_ratio, 0.0f, 0.99f);
}

auto Renderable::DistancePolicy::SelectBase(float normalized_distance,
  std::size_t lod_count) const noexcept -> std::size_t
{
  if (thresholds.empty()) {
    return 0;
  }
  std::size_t lod = 0;
  while (lod < lod_count - 1 && lod < thresholds.size()
    && normalized_distance >= thresholds[lod]) {
    ++lod;
  }
  return lod;
}

auto Renderable::DistancePolicy::ApplyHysteresis(
  std::optional<std::size_t> current, std::size_t base,
  float normalized_distance, std::size_t lod_count) const noexcept
  -> std::size_t
{
  (void)lod_count;
  if (!current.has_value()) {
    return base;
  }
  const auto last = *current;
  if (base == last) {
    return last;
  }
  const auto boundary = (std::min)(last, base);
  if (boundary >= thresholds.size()) {
    return base;
  }
  const float t = thresholds[boundary];
  const float enter = t * (1.0f + hysteresis_ratio);
  const float exit = t * (1.0f - hysteresis_ratio);
  if (base > last) {
    return (normalized_distance >= enter) ? base : last;
  } else {
    return (normalized_distance <= exit) ? base : last;
  }
}

void Renderable::ScreenSpaceErrorPolicy::NormalizeMonotonic() noexcept
{
  if (!enter_finer_sse.empty()) {
    for (std::size_t i = 1; i < enter_finer_sse.size(); ++i) {
      if (enter_finer_sse[i] < enter_finer_sse[i - 1])
        enter_finer_sse[i] = enter_finer_sse[i - 1];
    }
  }
  if (!exit_coarser_sse.empty()) {
    for (std::size_t i = 1; i < exit_coarser_sse.size(); ++i) {
      if (exit_coarser_sse[i] < exit_coarser_sse[i - 1])
        exit_coarser_sse[i] = exit_coarser_sse[i - 1];
    }
  }
}

auto Renderable::ScreenSpaceErrorPolicy::ValidateSizes(
  std::size_t lod_count) const noexcept -> bool
{
  if (lod_count == 0)
    return true;
  const auto need = (lod_count > 0) ? (lod_count - 1) : 0u;
  if (!enter_finer_sse.empty() && enter_finer_sse.size() < need)
    return false;
  if (!exit_coarser_sse.empty() && exit_coarser_sse.size() < need)
    return false;
  return true;
}

auto Renderable::ScreenSpaceErrorPolicy::SelectBase(
  float sse, std::size_t lod_count) const noexcept -> std::size_t
{
  if (enter_finer_sse.empty()) {
    return 0;
  }
  std::size_t lod = 0;
  while (lod < lod_count - 1 && lod < enter_finer_sse.size() && sse < enter_finer_sse[lod]) {
    ++lod;
  }
  return lod;
}

auto Renderable::ScreenSpaceErrorPolicy::ApplyHysteresis(
  std::optional<std::size_t> current, std::size_t base, float sse,
  std::size_t lod_count) const noexcept -> std::size_t
{
  (void)lod_count;
  if (!current.has_value()) {
    return base;
  }
  const auto last = *current;
  if (base == last) {
    return last;
  }
  const auto boundary = (std::min)(last, base);
  if (boundary >= enter_finer_sse.size() || boundary >= exit_coarser_sse.size()) {
    return base;
  }
  const float e_in = enter_finer_sse[boundary];
  const float e_out = exit_coarser_sse[boundary];
  if (base > last) {
    return (sse <= e_out) ? base : last;
  } else {
    return (sse >= e_in) ? base : last;
  }
}

bool Renderable::UsesFixedPolicy() const noexcept
{
  return std::holds_alternative<FixedPolicy>(policy_);
}

bool Renderable::UsesDistancePolicy() const noexcept
{
  return std::holds_alternative<DistancePolicy>(policy_);
}

bool Renderable::UsesScreenSpaceErrorPolicy() const noexcept
{
  return std::holds_alternative<ScreenSpaceErrorPolicy>(policy_);
}

void Renderable::SetLodPolicy(FixedPolicy p)
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

void Renderable::SetLodPolicy(DistancePolicy p)
{
  policy_ = std::move(p);
  current_lod_.reset();
  InvalidateWorldAabbCache();
  RecomputeWorldBoundingSphere();
}

void Renderable::SetLodPolicy(ScreenSpaceErrorPolicy p)
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
auto Renderable::GetActiveMesh() const noexcept -> std::optional<ActiveMesh>
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

auto Renderable::GetActiveLodIndex() const noexcept
  -> std::optional<std::size_t>
{
  if (!geometry_asset_)
    return std::nullopt;
  const auto lod_count = geometry_asset_->LodCount();
  if (lod_count == 0)
    return std::nullopt;
  return ResolveEffectiveLod(lod_count);
}

auto Renderable::EffectiveLodCount() const noexcept -> std::size_t
{
  return geometry_asset_ ? geometry_asset_->LodCount() : 0u;
}

//=== Local bounds cache and world bounds recompute ===--------------------//

void Renderable::SetGeometry(
  std::shared_ptr<const data::GeometryAsset> geometry)
{
  if (geometry_asset_.get() == geometry.get()) {
    return; // no-op
  }

  geometry_asset_ = std::move(geometry);

  // Reset/evaluate caches
  RebuildLocalBoundsCache();

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

void Renderable::RebuildLocalBoundsCache() noexcept
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

void Renderable::RecomputeWorldBoundingSphere() const noexcept
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

void Renderable::InvalidateWorldAabbCache() const noexcept
{
  aabb_cache_lod_.reset();
  submesh_world_aabb_cache_.clear();
}

auto Renderable::GetWorldSubMeshBoundingBox(
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

//=== LOD evaluation with hysteresis ===-----------------------------------//

void Renderable::SelectActiveMesh(NormalizedDistance d) const noexcept
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

void Renderable::SelectActiveMesh(ScreenSpaceError e) const noexcept
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

void Renderable::OnWorldTransformUpdated(const glm::mat4& world)
{
  world_matrix_ = world;
  RecomputeWorldBoundingSphere();
  InvalidateWorldAabbCache();
}

auto Renderable::ResolveEffectiveLod(std::size_t lod_count) const noexcept
  -> std::optional<std::size_t>
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
