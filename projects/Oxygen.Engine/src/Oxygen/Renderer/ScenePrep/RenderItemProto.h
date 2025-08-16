//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/SceneNodeImpl.h>

namespace oxygen::data {
class Mesh;
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::engine::sceneprep {

namespace scn = oxygen::scene;
namespace scn_detail = oxygen::scene::detail;

//! Facade for the node's RenderableComponent used during ScenePrep collection.
/*!
 Provides a minimal, view of the RenderableComponent used by the ScenePrep
 extractors. The facade forwards a small set of operations (LOD selection,
 visibility queries, material resolution and geometry access) without exposing
 the full component API. It holds a non-owning pointer to the underlying
 component and is safe to use for the duration of the collection phase where the
 owning SceneNodeImpl outlives the facade.

 Usage: Obtain from RenderItemProto by calling its `Renderable()` method.
*/
struct RenderableFacade {
  explicit RenderableFacade(const scn_detail::RenderableComponent& c) noexcept
    : comp_(&c)
  {
  }

  bool UsesDistancePolicy() const noexcept
  {
    return comp_->UsesDistancePolicy();
  }
  bool UsesScreenSpaceErrorPolicy() const noexcept
  {
    return comp_->UsesScreenSpaceErrorPolicy();
  }

  void SelectActiveMesh(scn::NormalizedDistance d) const noexcept
  {
    comp_->SelectActiveMesh(d);
  }
  void SelectActiveMesh(scn::ScreenSpaceError e) const noexcept
  {
    comp_->SelectActiveMesh(e);
  }

  auto GetActiveLodIndex() const noexcept -> std::optional<std::size_t>
  {
    return comp_->GetActiveLodIndex();
  }

  bool IsSubmeshVisible(std::size_t lod, std::size_t submesh) const noexcept
  {
    return comp_->IsSubmeshVisible(lod, submesh);
  }

  auto ResolveSubmeshMaterial(
    std::size_t lod, std::size_t submesh) const noexcept
    -> std::shared_ptr<const oxygen::data::MaterialAsset>
  {
    return comp_->ResolveSubmeshMaterial(lod, submesh);
  }

  // Geometry access (used by initial filter to populate WorkItem::proto)
  auto GetGeometry() const noexcept
    -> const std::shared_ptr<const oxygen::data::GeometryAsset>&
  {
    return comp_->GetGeometry();
  }

  auto GetWorldBoundingSphere() const noexcept -> glm::vec4
  {
    return comp_->GetWorldBoundingSphere();
  }

  // On-demand world-space AABB for a submesh of the current LOD.
  // Returns nullopt if unavailable (no geometry, unresolved LOD, or OOB).
  auto GetWorldSubMeshBoundingBox(std::size_t submesh_index) const noexcept
    -> std::optional<std::pair<glm::vec3, glm::vec3>>
  {
    return comp_->GetWorldSubMeshBoundingBox(submesh_index);
  }

private:
  const scn_detail::RenderableComponent* comp_ { nullptr };
};

//! Facade for the node's TransformComponent used during ScenePrep collection.
/*!
 Provides a tiny, read-only interface to the node's TransformComponent used by
 the ScenePrep extractors and tests. The facade exposes only the world matrix
 accessor required during extraction and holds a non-owning pointer to the
 underlying component. It is safe to use while the owning `SceneNodeImpl`
 outlives the facade (collection phase lifetime).

 Usage: Obtain from RenderItemProto by calling its `Transform()` method.
 */
struct TransformFacade {
  explicit TransformFacade(const scn_detail::TransformComponent& c) noexcept
    : comp_(&c)
  {
  }

  auto GetWorldMatrix() const -> const glm::mat4&
  {
    return comp_->GetWorldMatrix();
  }

private:
  const scn_detail::TransformComponent* comp_ { nullptr };
};

//! Per-node collection-phase carrier for ScenePrep.
/*!
 Holds cached component facades and ephemeral selection state while extracting
 data from a scene node. It accumulates a stable `RenderItemData` snapshot in
 `proto`, which becomes the input to the finalization phase.

 ### Key Features

 - Fast access to Renderable/Transform via facades; debug-checked getters.
 - Ephemeral LOD/submesh state (`pending_lod`, `selected_submesh`,
   `visible_submeshes`).
 - Optional resolved mesh pointer for the chosen LOD; null until resolved.
 - Early-reject support via `dropped` for collection filters.

 ### Usage Patterns

 - Construct from a `scn::SceneNodeImpl` during collection traversal.
 - Fill `proto` (transform/material/geometry), then decide LOD/submeshes.
 - Emit one or many `RenderItemData` derived from `proto` to the output list.

 ### Architecture Notes

 - Lifetime is limited to the Collection phase; Finalization operates on the
   emitted `RenderItemData` and does not depend on this carrier object.
 - Contract: RenderItemProto can only be created with a SceneNodeImpl that has
   both Renderable and Transform components. Will throw std::runtime_error if
   either component is missing.

 @see RenderItemData, ScenePrepState, CollectionConfig, FinalizationConfig
*/
class RenderItemProto {
public:
  //! Construct from a scene node and cache component facades.
  /*!
   Initializes the collection-phase carrier for a specific scene node. The
   constructor acquires non-owning facades to the node's Renderable and
   Transform components and records the owning node pointer. Component
   presence is mandatory for ScenePrep and enforced by the accessors.

   @param impl Scene node providing Renderable and Transform components.
   @throw std::runtime_error if either component is missing on the node.

   @see RenderItemData, Renderable(), Transform()
  */
  explicit RenderItemProto(const scn::SceneNodeImpl& impl)
    : node_(&impl)
    , renderable_facade_(impl.GetComponent<scn_detail::RenderableComponent>())
    , transform_facade_(impl.GetComponent<scn_detail::TransformComponent>())
  {
  }

  OXYGEN_MAKE_NON_COPYABLE(RenderItemProto)
  OXYGEN_MAKE_NON_MOVABLE(RenderItemProto)

  //! Destructor, defaulted because none of the pointers is owned.
  ~RenderItemProto() = default;

  auto Renderable() noexcept -> RenderableFacade& { return renderable_facade_; }

  auto Renderable() const noexcept -> const RenderableFacade&
  {
    return renderable_facade_;
  }

  auto Transform() noexcept -> TransformFacade& { return transform_facade_; }

  auto Transform() const noexcept -> const TransformFacade&
  {
    return transform_facade_;
  }

  auto& Flags() const noexcept { return node_->GetFlags(); }

  void SetVisibleSubmeshes(std::vector<uint32_t> indices) noexcept
  {
    visible_submeshes_ = std::move(indices);
  }
  [[nodiscard]] auto VisibleSubmeshes() const noexcept
    -> std::span<const uint32_t>
  {
    return visible_submeshes_;
  }
  void SetVisible() noexcept { visibility_flag_ = true; }
  [[nodiscard]] auto IsVisible() const noexcept -> bool
  {
    return visibility_flag_;
  }
  [[nodiscard]] auto IsCulled() const noexcept -> bool
  {
    return !visibility_flag_ || visible_submeshes_.empty();
  }

  void SetCastShadows(bool cast) noexcept { cast_shadows = cast; }
  [[nodiscard]] auto CastsShadows() const noexcept { return cast_shadows; }
  void SetReceiveShadows(bool receive) noexcept { receive_shadows = receive; }
  [[nodiscard]] auto ReceivesShadows() const noexcept
  {
    return receive_shadows;
  }

  // Allow tests and extractors to seed geometry during collection.
  void SetGeometry(
    std::shared_ptr<const oxygen::data::GeometryAsset> g) noexcept
  {
    // Only allow setting geometry if mesh is not resolved
    DCHECK_EQ_F(mesh_, nullptr);
    DCHECK_EQ_F(mesh_lod_, 0U);

    geometry_ = std::move(g);
  }
  [[nodiscard]] auto& Geometry() const noexcept { return geometry_; }

  void SetWorldTransform(const glm::mat4& transform) noexcept
  {
    world_transform = transform;
  }
  [[nodiscard]] auto& GetWorldTransform() const noexcept
  {
    return world_transform;
  }

  void ResolveMesh(std::shared_ptr<const oxygen::data::Mesh> mesh, uint32_t lod)
  {
    mesh_ = std::move(mesh);
    mesh_lod_ = lod;
  }
  [[nodiscard]] auto& ResolvedMesh() const noexcept { return mesh_; }
  [[nodiscard]] auto ResolvedMeshIndex() const noexcept
  {
    if (!mesh_) {
      DLOG_F(WARNING, "Mesh not resolved yet -> using first LOD (index {})",
        mesh_lod_);
    }
    return mesh_lod_;
  }

  void MarkDropped() noexcept { dropped_ = true; }
  [[nodiscard]] auto IsDropped() const noexcept { return dropped_; }

private:
  // Drop flag set by filters
  bool dropped_ = false;

  // Node data

  // Visibility flag of the entire node
  bool visibility_flag_ { false };

  // Rendering flags
  bool cast_shadows = true;
  bool receive_shadows = true;

  // Geometry asset seeded during collection phase
  std::shared_ptr<const oxygen::data::GeometryAsset> geometry_ {};

  // -- Mesh data (resolved LOD)

  // Resolved mesh LOD (index into the geometry meshes). Defaults to the first
  // LOD (index 0) until resolved.
  uint32_t mesh_lod_ { 0 };

  // Resolved mesh pointer (single canonical resolved LOD). Null until resolved.
  std::shared_ptr<const oxygen::data::Mesh> mesh_;

  // Transform and bounds
  glm::mat4 world_transform { 1.0f };

  // Dense list of indices of visible submeshes in the resolved parent mesh.
  std::vector<uint32_t> visible_submeshes_;

  // Internal stuff
  const scn::SceneNodeImpl* node_ { nullptr };
  RenderableFacade renderable_facade_;
  TransformFacade transform_facade_;
};

} // namespace oxygen::engine::sceneprep
