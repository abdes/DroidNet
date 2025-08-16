//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Extraction/RenderItemData.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

// Forward declarations for data types used by WorkItem without including heavy
// headers
namespace oxygen {
namespace data {
  class Mesh;
  class MaterialAsset;
  class GeometryAsset;
} // namespace data
} // namespace oxygen

namespace oxygen::engine::extraction {

namespace scn = oxygen::scene;
namespace scn_detail = oxygen::scene::detail;

struct ExtractorContext {
  const View& view;
  scn::Scene& scene;
  std::uint64_t frame_id = 0;
};

// Lightweight facades exposing only the extractor-used API, forwarding to
// the underlying SceneNodeImpl components.
struct RenderableFacade {
  explicit RenderableFacade(const scn_detail::RenderableComponent* c) noexcept
    : comp(c)
  {
  }

  bool UsesDistancePolicy() const noexcept
  {
    return comp->UsesDistancePolicy();
  }
  bool UsesScreenSpaceErrorPolicy() const noexcept
  {
    return comp->UsesScreenSpaceErrorPolicy();
  }

  void SelectActiveMesh(scn::NormalizedDistance d) const noexcept
  {
    comp->SelectActiveMesh(d);
  }
  void SelectActiveMesh(scn::ScreenSpaceError e) const noexcept
  {
    comp->SelectActiveMesh(e);
  }

  auto GetActiveLodIndex() const noexcept -> std::optional<std::size_t>
  {
    return comp->GetActiveLodIndex();
  }

  bool IsSubmeshVisible(std::size_t lod, std::size_t submesh) const noexcept
  {
    return comp->IsSubmeshVisible(lod, submesh);
  }

  auto ResolveSubmeshMaterial(
    std::size_t lod, std::size_t submesh) const noexcept
    -> std::shared_ptr<const oxygen::data::MaterialAsset>
  {
    return comp->ResolveSubmeshMaterial(lod, submesh);
  }

  // Geometry access (used by initial filter to populate WorkItem::proto)
  auto GetGeometry() const noexcept
    -> const std::shared_ptr<const oxygen::data::GeometryAsset>&
  {
    return comp->GetGeometry();
  }

  auto GetWorldBoundingSphere() const noexcept -> glm::vec4
  {
    return comp->GetWorldBoundingSphere();
  }

  // On-demand world-space AABB for a submesh of the current LOD.
  // Returns nullopt if unavailable (no geometry, unresolved LOD, or OOB).
  auto GetWorldSubMeshBoundingBox(std::size_t submesh_index) const noexcept
    -> std::optional<std::pair<glm::vec3, glm::vec3>>
  {
    return comp->GetWorldSubMeshBoundingBox(submesh_index);
  }

  const scn_detail::RenderableComponent* comp { nullptr };
};

struct TransformFacade {
  explicit TransformFacade(const scn_detail::TransformComponent* c) noexcept
    : comp(c)
  {
  }

  auto GetWorldMatrix() const -> const glm::mat4&
  {
    return comp->GetWorldMatrix();
  }

  const scn_detail::TransformComponent* comp { nullptr };
};

// WorkItem holds mutable per-node state as it flows through extractors.
struct WorkItem {
  // Construct with a SceneNodeImpl to cache component pointers locally.
  explicit WorkItem(const scn::SceneNodeImpl& impl) noexcept
  {
    node_ = &impl;
    if (impl.HasComponent<scn_detail::RenderableComponent>()) {
      renderable_ = &node_->GetComponent<scn_detail::RenderableComponent>();
      renderable_facade_ = RenderableFacade { renderable_ };
    }
    if (impl.HasComponent<scn_detail::TransformComponent>()) {
      transform_ = &node_->GetComponent<scn_detail::TransformComponent>();
      transform_facade_ = TransformFacade { transform_ };
    }
  }

  // Facade accessors (DCHECK on missing component in debug builds)
  auto Renderable() noexcept -> RenderableFacade&
  {
    DCHECK_F(renderable_ != nullptr, "Renderable component missing");
    return renderable_facade_;
  }
  auto Renderable() const noexcept -> const RenderableFacade&
  {
    DCHECK_F(renderable_ != nullptr, "Renderable component missing");
    return renderable_facade_;
  }
  auto Transform() noexcept -> TransformFacade&
  {
    DCHECK_F(transform_ != nullptr, "Transform component missing");
    return transform_facade_;
  }
  auto Transform() const noexcept -> const TransformFacade&
  {
    DCHECK_F(transform_ != nullptr, "Transform component missing");
    return transform_facade_;
  }

  // Presence checks
  [[nodiscard]] bool HasRenderable() const noexcept
  {
    return renderable_ != nullptr;
  }

  [[nodiscard]] bool HasTransform() const noexcept
  {
    return transform_ != nullptr;
  }

  // Access owning node (for flags and other node-level state)
  auto Node() const noexcept -> const scn::SceneNodeImpl&
  {
    DCHECK_F(node_ != nullptr, "WorkItem not associated with a SceneNodeImpl");
    return *node_;
  }

  RenderItemData proto; // collect-phase snapshot (geometry/material/transform)

  // Resolved mesh pointer (single canonical resolved LOD). Null until resolved.
  std::shared_ptr<const oxygen::data::Mesh> mesh;

  // Pending selection produced by LOD extractors; consumed by MeshResolver.
  // Explicitly ephemeral to avoid long-lived duplicated LOD state.
  std::optional<std::uint32_t> pending_lod;

  // Final single submesh selection (if we choose to emit a single submesh).
  // If unset, emitters should consult submesh_mask for per-submesh emission.
  std::optional<std::uint32_t> selected_submesh;

  // Per-submesh visibility mask. Sized to mesh->SubMeshes() after resolution.
  std::vector<char> submesh_mask; // compact boolean per submesh

  // Drop flag set by filters
  bool dropped = false;

  // diagnostics
  int debug_flags = 0;

  // Helper accessors
  std::uint32_t ResolvedMeshIndex() const noexcept
  {
    // Mesh doesn't expose a LOD index; use the pending LOD if set, otherwise
    // fall back to the collected proto value. This avoids duplicating LOD
    // state across WorkItem and Mesh.
    return pending_lod.value_or(proto.lod_index);
  }

private:
  const scn::SceneNodeImpl* node_ { nullptr };
  const scn_detail::RenderableComponent* renderable_ { nullptr };
  const scn_detail::TransformComponent* transform_ { nullptr };
  RenderableFacade renderable_facade_ { nullptr };
  TransformFacade transform_facade_ { nullptr };
};

// Collector alias
using Collector = std::vector<RenderItemData>&;

// Concepts for extractor functions
template <typename F>
concept FilterFn = requires(F f, WorkItem& w, ExtractorContext const& c) {
  { f(w, c) } -> std::convertible_to<bool>;
};

template <typename F>
concept UpdaterFn = requires(F f, WorkItem& w, ExtractorContext const& c) {
  { f(w, c) } -> std::same_as<void>;
};

template <typename F>
concept ProducerFn
  = requires(F f, WorkItem& w, ExtractorContext const& c, Collector out) {
      { f(w, c, out) } -> std::same_as<void>;
    };

// Note: LOD extractors are plain UpdaterFn that set WorkItem::pending_lod.

} // namespace oxygen::engine::extraction
