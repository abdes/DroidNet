//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Types/ActiveMesh.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/Scene/Types/Strong.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::data {
class GeometryAsset;
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::scene::detail {

//! Draws one geometry asset, with this instance's overrides of its defaults.
/*!
 The overrides are the scene schema's asset-sourced properties: per surface,
 `Material`, `Visibility` and `CastsShadows`; per renderable, the LOD policy.
 Without an override a surface takes the geometry's default, and a geometry
 that declares none takes the schema's default.

 Surface overrides are kept by SurfaceId. Replacing the geometry with one of
 the same identity keeps those of the surfaces it still declares; any other
 geometry drops them. The LOD policy override is kept across replacement.
 Edits and replacement resolve the overrides into a per-submesh state, which
 drawing reads without identity lookups.
*/
class RenderableComponent final : public Component {
  OXYGEN_COMPONENT(RenderableComponent)
  OXYGEN_COMPONENT_REQUIRES(TransformComponent)

public:
  using LodPolicy = scene::LodPolicy;

  // Preferred: full geometry asset with LODs and submeshes
  OXGN_SCN_API explicit RenderableComponent(
    std::shared_ptr<const data::GeometryAsset> geometry);

  ~RenderableComponent() override = default;

  OXYGEN_DEFAULT_COPYABLE(RenderableComponent)
  OXYGEN_DEFAULT_MOVABLE(RenderableComponent)

  // Geometry API
  OXGN_SCN_API void SetGeometry(
    std::shared_ptr<const data::GeometryAsset> geometry);

  [[nodiscard]] auto GetGeometry() const noexcept
    -> const std::shared_ptr<const data::GeometryAsset>&
  {
    return geometry_asset_;
  }

  OXGN_SCN_NDAPI auto UsesFixedPolicy() const noexcept -> bool;
  OXGN_SCN_NDAPI auto UsesDistancePolicy() const noexcept -> bool;
  OXGN_SCN_NDAPI auto UsesScreenSpaceErrorPolicy() const noexcept -> bool;

  //! The LOD policy in effect: the override, else the geometry's default,
  //! else the schema's default.
  [[nodiscard]] auto GetLodPolicy() const noexcept -> const LodPolicy&
  {
    return policy_;
  }

  //! Selects LODs with `policy` instead of the geometry's default.
  OXGN_SCN_API void SetLodPolicyOverride(LodPolicy policy);
  //! Returns to the geometry's default LOD policy.
  OXGN_SCN_API void ClearLodPolicyOverride();
  OXGN_SCN_NDAPI auto HasLodPolicyOverride() const noexcept -> bool;

  // Select active LOD based on current policy using strong types.
  // For distance policy, pass NormalizedDistance (||cam-center|| / radius).
  // For SSE policy, pass ScreenSpaceError (projected radius in pixels).
  OXGN_SCN_API void SelectActiveMesh(NormalizedDistance d) const noexcept;
  OXGN_SCN_API void SelectActiveMesh(ScreenSpaceError e) const noexcept;

  //! Returns the currently active Mesh and its LOD index when available.
  OXGN_SCN_NDAPI auto GetActiveMesh() const noexcept
    -> std::optional<ActiveMesh>;

  // Utilities
  OXGN_SCN_NDAPI auto GetActiveLodIndex() const noexcept
    -> std::optional<data::LodIndex>;
  OXGN_SCN_NDAPI auto EffectiveLodCount() const noexcept -> std::size_t;

  // Bounds and transform hook
  void OnWorldTransformUpdated(const Mat4& world);

  // Aggregated world bounding sphere (center.xyz, radius.w). Returns (0,0,0,0)
  // if not available (e.g., no geometry or unresolved LOD).
  [[nodiscard]] auto GetWorldBoundingSphere() const noexcept -> Vec4
  {
    return world_bounding_sphere_;
  }

  // On-demand per-submesh world AABB for the current LOD.
  // Returns nullopt if unavailable (no geometry, LOD unresolved, or index OOB).
  OXGN_SCN_NDAPI auto GetWorldSubMeshBoundingBox(
    data::SubmeshIndex submesh) const noexcept
    -> std::optional<std::pair<Vec3, Vec3>>;

  //=== Surface overrides ==================================================//

  //! Binds `material` to every submesh of `surface`; nullptr clears the
  //! override. False when the geometry does not declare `surface`.
  OXGN_SCN_NDAPI auto SetMaterialOverride(data::SurfaceId surface,
    std::shared_ptr<const data::MaterialAsset> material) noexcept -> bool;
  OXGN_SCN_NDAPI auto ClearMaterialOverride(data::SurfaceId surface) noexcept
    -> bool;

  //! Shows or hides `surface`. False when the geometry does not declare it.
  OXGN_SCN_NDAPI auto SetVisibilityOverride(
    data::SurfaceId surface, bool visible) noexcept -> bool;
  OXGN_SCN_NDAPI auto ClearVisibilityOverride(data::SurfaceId surface) noexcept
    -> bool;

  //! Sets whether `surface` casts shadows. False when the geometry does not
  //! declare it.
  OXGN_SCN_NDAPI auto SetCastsShadowsOverride(
    data::SurfaceId surface, bool casts_shadows) noexcept -> bool;
  OXGN_SCN_NDAPI auto ClearCastsShadowsOverride(
    data::SurfaceId surface) noexcept -> bool;

  //! Whether the surface bound to the submesh is visible; false for a submesh
  //! the geometry does not have.
  OXGN_SCN_NDAPI auto IsSubmeshVisible(
    data::LodIndex lod, data::SubmeshIndex submesh) const noexcept -> bool;

  //! Whether the surface bound to the submesh casts shadows: it is visible and
  //! its CastsShadows is on. The node's own shadow flag is not included.
  OXGN_SCN_NDAPI auto SubmeshCastsShadows(
    data::LodIndex lod, data::SubmeshIndex submesh) const noexcept -> bool;

  //! Resolves the effective material applying override → submesh → default.
  OXGN_SCN_NDAPI auto ResolveSubmeshMaterial(
    data::LodIndex lod, data::SubmeshIndex submesh) const noexcept
    -> std::shared_ptr<const data::MaterialAsset>;

  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    auto copy = std::make_unique<RenderableComponent>(*this);
    return copy;
  }

protected:
  //! Takes the node's world transform when it is already resolved, so bounds
  //! are right for geometry attached to a placed node. A dirty transform
  //! reaches the renderable through the next scene update.
  OXGN_SCN_API auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override;

private:
  [[nodiscard]] auto ResolveEffectiveLod(std::size_t lod_count) const noexcept
    -> std::optional<data::LodIndex>;
  struct LodBounds {
    Vec3 mesh_bbox_min { 0.0F, 0.0F, 0.0F };
    Vec3 mesh_bbox_max { 0.0F, 0.0F, 0.0F };
    Vec4 mesh_sphere { 0.0F, 0.0F, 0.0F, 0.0F };
    std::vector<std::pair<Vec3, Vec3>> submesh_aabbs; // local
    //! World AABB per submesh, computed on demand and reset when the world
    //! transform or LOD changes. Sized with the local bounds, so reading it
    //! never allocates.
    mutable std::vector<std::optional<std::pair<Vec3, Vec3>>>
      submesh_world_aabbs;
  };

  //! This instance's overrides of one surface's asset defaults.
  struct SurfaceOverrides {
    std::shared_ptr<const data::MaterialAsset> material;
    std::optional<bool> visible;
    std::optional<bool> casts_shadows;
  };

  //! What drawing reads for one submesh, resolved from its surface.
  struct SubmeshState {
    bool visible { true };
    bool casts_shadows { true };
    std::shared_ptr<const data::MaterialAsset> override_material;
  };

  void RebuildLocalBoundsCache();
  void RecomputeWorldBoundingSphere() const noexcept;
  void InvalidateWorldAabbCache() const noexcept;
  void RebuildSubmeshStateCache(
    const data::GeometryAsset* previous_geometry = nullptr);
  void ResolveLodPolicy();
  void ResolveSurface(std::size_t index) noexcept;
  [[nodiscard]] auto FindSubmeshState(data::LodIndex lod,
    data::SubmeshIndex submesh) const noexcept -> const SubmeshState*;
  template <typename Edit>
  [[nodiscard]] auto EditSurface(data::SurfaceId surface, Edit edit) noexcept
    -> bool;

  // Preferred data
  std::shared_ptr<const data::GeometryAsset> geometry_asset_;

  // This instance's LOD policy, replacing the geometry's default when set.
  std::optional<LodPolicy> lod_policy_override_;

  // LOD policy in effect, resolved on geometry and override changes.
  LodPolicy policy_ { FixedPolicy {} };

  // Cached dynamic LOD result (updated during updates/submission)
  mutable std::optional<data::LodIndex> current_lod_;

  // Per-LOD and per-submesh local bounds cache (rebuilt on SetGeometry)
  std::vector<LodBounds> lod_bounds_;

  // Surface overrides in the current inventory's declaration order.
  // Resolved into submesh_state_ only on edits and geometry replacement.
  std::vector<SurfaceOverrides> surface_overrides_;

  // World transform state and derived bounds
  Mat4 world_matrix_ { 1.0F };
  mutable Vec4 world_bounding_sphere_ { 0.0F, 0.0F, 0.0F, 0.0F };

  // Per-LOD submesh state resolved from the surface overrides and defaults.
  std::vector<std::vector<SubmeshState>> submesh_state_;
};

} // namespace oxygen::scene::detail
