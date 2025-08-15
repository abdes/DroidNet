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
#include <Oxygen/Scene/api_export.h>

namespace oxygen::data {
class GeometryAsset;
} // namespace oxygen::data

namespace oxygen::scene::detail {

// Renderable component holds a reference to GeometryAsset (preferred) and
// provides a minimal compatibility path for legacy Mesh attachment.
class Renderable final : public Component {
  OXYGEN_COMPONENT(Renderable)

public:
  //! Invariant: LOD 0 is the finest quality. Index i denotes the boundary
  //! between LOD i and LOD i+1. Increasing the LOD index moves to coarser
  //! representations.
  struct FixedPolicy {
    static constexpr std::size_t kFinest = 0;
    std::size_t index { kFinest };
    // Clamp to existing LOD count
    std::size_t Clamp(std::size_t lod_count) const noexcept;
  };
  struct DistancePolicy {
    std::vector<float> thresholds; // boundaries between i and i+1
    float hysteresis_ratio { 0.1f }; // symmetric band around boundary
    // Ensure thresholds are non-decreasing and clamp hysteresis into [0, 0.99]
    void NormalizeThresholds() noexcept;
    // Base selection without hysteresis
    std::size_t SelectBase(
      float normalized_distance, std::size_t lod_count) const noexcept;
    // Apply symmetric hysteresis around the boundary between last and base
    std::size_t ApplyHysteresis(std::optional<std::size_t> current,
      std::size_t base, float normalized_distance,
      std::size_t lod_count) const noexcept;
  };
  struct ScreenSpaceErrorPolicy {
    //! SSE threshold to enter a finer LOD (index decreases) when SSE increases.
    std::vector<float> enter_finer_sse;
    //! SSE threshold to enter a coarser LOD (index increases) when SSE
    //! decreases.
    std::vector<float> exit_coarser_sse;
    // Ensure arrays are non-decreasing
    void NormalizeMonotonic() noexcept;
    // Validate sizes: if provided, expect at least lod_count-1 boundaries
    [[nodiscard]] bool ValidateSizes(std::size_t lod_count) const noexcept;
    // Base selection without hysteresis
    std::size_t SelectBase(float sse, std::size_t lod_count) const noexcept;
    // Apply directional hysteresis using enter/exit arrays
    std::size_t ApplyHysteresis(std::optional<std::size_t> current,
      std::size_t base, float sse, std::size_t lod_count) const noexcept;
  };
  using LodPolicy
    = std::variant<FixedPolicy, DistancePolicy, ScreenSpaceErrorPolicy>;

  // Preferred: full geometry asset with LODs and submeshes
  explicit Renderable(std::shared_ptr<const data::GeometryAsset> geometry)
    : geometry_asset_(std::move(geometry))
  {
  }

  struct NormalizedDistance {
    float value;
    explicit constexpr NormalizedDistance(const float v = 0.0f)
      : value(v)
    {
    }
    constexpr auto operator<=>(const NormalizedDistance&) const = default;
    constexpr explicit operator float() const noexcept { return value; }
  };

  struct ScreenSpaceError {
    float value;
    explicit constexpr ScreenSpaceError(const float v = 0.0f)
      : value(v)
    {
    }
    constexpr auto operator<=>(const ScreenSpaceError&) const = default;
    constexpr explicit operator float() const noexcept { return value; }
  };

  ~Renderable() override = default;

  OXYGEN_DEFAULT_COPYABLE(Renderable)
  OXYGEN_DEFAULT_MOVABLE(Renderable)

  // Geometry API
  OXGN_SCN_API void SetGeometry(
    std::shared_ptr<const data::GeometryAsset> geometry);

  [[nodiscard]] auto GetGeometry() const noexcept
    -> const std::shared_ptr<const data::GeometryAsset>&
  {
    return geometry_asset_;
  }

  [[nodiscard]] OXGN_SCN_API bool UsesFixedPolicy() const noexcept;
  [[nodiscard]] OXGN_SCN_API bool UsesDistancePolicy() const noexcept;
  [[nodiscard]] OXGN_SCN_API bool UsesScreenSpaceErrorPolicy() const noexcept;

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
  [[nodiscard]] OXGN_SCN_NDAPI auto GetActiveLodIndex() const noexcept
    -> std::optional<std::size_t>;
  [[nodiscard]] OXGN_SCN_NDAPI auto EffectiveLodCount() const noexcept
    -> std::size_t;

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
  [[nodiscard]] auto GetWorldSubMeshBoundingBox(
    std::size_t submesh_index) const noexcept
    -> std::optional<std::pair<glm::vec3, glm::vec3>>;

  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    auto copy = std::make_unique<Renderable>(*this);
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
};

} // namespace oxygen::scene::detail
