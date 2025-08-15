//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

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
  enum class LODPolicy { kFixed, kDistance, kScreenSpaceError };

  // Preferred: full geometry asset with LODs and submeshes
  explicit Renderable(std::shared_ptr<const data::GeometryAsset> geometry)
    : geometry_asset_(std::move(geometry))
  {
  }

  ~Renderable() override = default;

  OXYGEN_DEFAULT_COPYABLE(Renderable)
  OXYGEN_DEFAULT_MOVABLE(Renderable)

  // Geometry API
  void SetGeometry(std::shared_ptr<const data::GeometryAsset> geometry) noexcept
  {
    geometry_asset_ = std::move(geometry);
    // TODO: rebuild caches, clamp visibility, recompute bounds
  }

  [[nodiscard]] auto GetGeometry() const noexcept
    -> const std::shared_ptr<const data::GeometryAsset>&
  {
    return geometry_asset_;
  }

  // LOD controls (stubs for now)
  void SetLODFixed(size_t index) noexcept
  {
    lod_policy_ = LODPolicy::kFixed;
    fixed_lod_index_ = index;
  }
  void SetLODPolicy(LODPolicy policy) noexcept { lod_policy_ = policy; }

  //! Returns the currently active Mesh and its LOD index when available.
  OXGN_SCN_NDAPI auto GetActiveMesh() const noexcept
    -> std::optional<ActiveMesh>;

  // Bounds and transform hook (placeholders; to be implemented later)
  void OnWorldTransformUpdated(const glm::mat4& /*world*/)
  { /* compute world bounds */ }

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
  // Preferred data
  std::shared_ptr<const data::GeometryAsset> geometry_asset_ {};

  // LOD selection state (minimal for now)
  LODPolicy lod_policy_ { LODPolicy::kFixed };
  size_t fixed_lod_index_ { 0 };

  // Cached dynamic LOD result (set by evaluation code during
  // updates/submission)
  mutable std::optional<std::size_t> current_lod_ {};
};

} // namespace oxygen::scene::detail
