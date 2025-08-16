//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Scene/Types/ActiveMesh.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>
#include <Oxygen/Scene/Types/Strong.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::data {
class GeometryAsset;
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::scene::detail {

// Renderable component holds a reference to GeometryAsset (preferred) and
// provides a minimal compatibility path for legacy Mesh attachment.
class RenderableComponent final : public Component {
  OXYGEN_COMPONENT(RenderableComponent)

public:
  using LodPolicy
    = std::variant<FixedPolicy, DistancePolicy, ScreenSpaceErrorPolicy>;

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

  OXGN_SCN_NDAPI bool UsesFixedPolicy() const noexcept;
  OXGN_SCN_NDAPI bool UsesDistancePolicy() const noexcept;
  OXGN_SCN_NDAPI bool UsesScreenSpaceErrorPolicy() const noexcept;

  OXGN_SCN_API void SetLodPolicy(FixedPolicy p);
  OXGN_SCN_API void SetLodPolicy(DistancePolicy p);
  OXGN_SCN_API void SetLodPolicy(ScreenSpaceErrorPolicy p);

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
    -> std::optional<std::size_t>;
  OXGN_SCN_NDAPI auto EffectiveLodCount() const noexcept -> std::size_t;

  // Bounds and transform hook
  void OnWorldTransformUpdated(const glm::mat4& world);

  // Aggregated world bounding sphere (center.xyz, radius.w). Returns (0,0,0,0)
  // if not available (e.g., no geometry or unresolved LOD).
  [[nodiscard]] auto GetWorldBoundingSphere() const noexcept -> glm::vec4
  {
    return world_bounding_sphere_;
  }

  // On-demand per-submesh world AABB for the current LOD.
  // Returns nullopt if unavailable (no geometry, LOD unresolved, or index OOB).
  OXGN_SCN_NDAPI auto GetWorldSubMeshBoundingBox(
    std::size_t submesh_index) const noexcept
    -> std::optional<std::pair<glm::vec3, glm::vec3>>;

  //=== Submesh visibility and material overrides ========================//

  //! Returns whether the given submesh (by LOD and index) is visible.
  OXGN_SCN_NDAPI bool IsSubmeshVisible(
    std::size_t lod, std::size_t submesh_index) const noexcept;

  //! Sets visibility for the given submesh (by LOD and index).
  OXGN_SCN_API void SetSubmeshVisible(
    std::size_t lod, std::size_t submesh_index, bool visible) noexcept;

  //! Sets visibility for all submeshes across all LODs.
  OXGN_SCN_API void SetAllSubmeshesVisible(bool visible) noexcept;

  //! Sets a material override for a submesh (by LOD and index). Pass nullptr
  //! to clear the override and fall back to the submesh material.
  OXGN_SCN_API void SetMaterialOverride(std::size_t lod,
    std::size_t submesh_index,
    std::shared_ptr<const data::MaterialAsset> material) noexcept;

  //! Clears the material override for the given submesh.
  OXGN_SCN_API void ClearMaterialOverride(
    std::size_t lod, std::size_t submesh_index) noexcept;

  //! Resolves the effective material applying override → submesh → default.
  OXGN_SCN_NDAPI auto ResolveSubmeshMaterial(
    std::size_t lod, std::size_t submesh_index) const noexcept
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

private:
  [[nodiscard]] auto ResolveEffectiveLod(std::size_t lod_count) const noexcept
    -> std::optional<std::size_t>;
  struct LodBounds {
    glm::vec3 mesh_bbox_min { 0.0f, 0.0f, 0.0f };
    glm::vec3 mesh_bbox_max { 0.0f, 0.0f, 0.0f };
    glm::vec4 mesh_sphere { 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<std::pair<glm::vec3, glm::vec3>> submesh_aabbs; // local
  };

  void RebuildLocalBoundsCache() noexcept;
  void RecomputeWorldBoundingSphere() const noexcept;
  void InvalidateWorldAabbCache() const noexcept;
  void RebuildSubmeshStateCache() noexcept;

  // Preferred data
  std::shared_ptr<const data::GeometryAsset> geometry_asset_ {};

  // LOD policy (runtime variant)
  LodPolicy policy_ { FixedPolicy {} };

  // Cached dynamic LOD result (updated during updates/submission)
  mutable std::optional<std::size_t> current_lod_ {};

  // Per-LOD and per-submesh local bounds cache (rebuilt on SetGeometry)
  std::vector<LodBounds> lod_bounds_ {};

  // World transform state and derived bounds
  glm::mat4 world_matrix_ { 1.0f };
  mutable glm::vec4 world_bounding_sphere_ { 0.0f, 0.0f, 0.0f, 0.0f };

  // On-demand world AABB cache for current LOD (invalidated on transform/LOD)
  mutable std::optional<std::size_t> aabb_cache_lod_ {};
  mutable std::vector<std::optional<std::pair<glm::vec3, glm::vec3>>>
    submesh_world_aabb_cache_ {};

  // Policy parameters live inside the variant

  struct SubmeshState {
    bool visible { true };
    std::shared_ptr<const data::MaterialAsset> override_material;
  };
  // Per-LOD submesh state (visibility + override). Rebuilt on SetGeometry.
  std::vector<std::vector<SubmeshState>> submesh_state_ {};
};

} // namespace oxygen::scene::detail
