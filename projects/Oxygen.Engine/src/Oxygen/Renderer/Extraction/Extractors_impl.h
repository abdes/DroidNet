//===----------------------------------------------------------------------===//
// Header-only example extractors: Transform, MeshResolver (with LOD select),
// Visibility, MaterialResolver, EmitMeshLevel
//===----------------------------------------------------------------------===//
#pragma once

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/SceneFlags.h>

#include <Oxygen/Renderer/Extraction/Extractors.h>

namespace oxygen::engine::extraction {

//! Pre-filter for renderable work items
/*!
 Ensures node visibility, presence of required components (Renderable and
 Transform), and a valid geometry asset. Seeds `w.proto.geometry` for downstream
 stages.

 @param w Work item to inspect and seed with geometry.
 @param ctx Extractor context (unused).
 @return `true` if the item should continue through the pipeline; `false` if it
   should be filtered out.

 ### Performance Characteristics
 - Time Complexity: O(1)
 - Memory: O(1)

 @see TransformExtractor, MeshResolver, VisibilityFilter, EmitPerVisibleSubmesh
*/
inline bool ShouldRenderPreFilter(WorkItem& w, ExtractorContext const&) noexcept
{
  // Skip nodes culled by effective visibility (hierarchy-aware flag).
  const auto& flags = w.Node().GetFlags();
  if (!flags.GetEffectiveValue(scn::SceneNodeFlags::kVisible)) {
    return false;
  }

  if (!w.HasRenderable() || !w.HasTransform()) {
    return false;
  }

  const auto& geom = w.Renderable().GetGeometry();
  if (!geom) {
    return false;
  }
  w.proto.geometry = geom;
  return true;
}
static_assert(
  oxygen::engine::extraction::FilterFn<decltype(ShouldRenderPreFilter)>);

//! Populate world transform and bounds
/*!
 Copies the node's world transform and world-space bounding sphere into the work
 item's prototype data. Requires `ShouldRenderPreFilter` to have validated
 component presence.

 @param w Work item whose prototype will be updated.
 @param ctx Extractor context (unused).

 ### Performance Characteristics
 - Time Complexity: O(1)
 - Memory: O(1)

 @see ShouldRenderPreFilter
*/
inline void TransformExtractor(WorkItem& w, ExtractorContext const&) noexcept
{
  // Transform comes from the TransformComponent; bounds come from the
  // RenderableComponent. ShouldRenderPreFilter guarantees both exist.
  DCHECK_F(w.HasTransform());
  DCHECK_F(w.HasRenderable());
  // Copy world matrix
  w.proto.world_transform = w.Transform().GetWorldMatrix();
  // Copy world-space bounding sphere
  w.proto.world_bounding_sphere = w.Renderable().GetWorldBoundingSphere();
}
static_assert(
  oxygen::engine::extraction::UpdaterFn<decltype(TransformExtractor)>);

//! Copy effective node flags into prototype
/*!
 Reads effective node flags (hierarchy-aware) and stores relevant values in
 `w.proto` such as `cast_shadows` and `receive_shadows`.

 @param w Work item to update.
 @param ctx Extractor context (unused).

 ### Performance Characteristics
 - Time Complexity: O(1)
 - Memory: O(1)
*/
inline void NodeFlagsExtractor(WorkItem& w, ExtractorContext const&) noexcept
{
  auto& node = w.Node();
  w.proto.cast_shadows
    = node.GetFlags().GetEffectiveValue(scn::SceneNodeFlags::kCastsShadows);
  w.proto.receive_shadows
    = node.GetFlags().GetEffectiveValue(scn::SceneNodeFlags::kReceivesShadows);
}
static_assert(
  oxygen::engine::extraction::UpdaterFn<decltype(NodeFlagsExtractor)>);

//! Resolve active mesh LOD and mesh resource
/*!
 Delegates LOD policy and selection to the node's `Renderable` so that LOD state
 (including hysteresis) remains centralized. Supports distance-based and
 screen-space-error (SSE) policies. After selection, resolves the active mesh
 from `w.proto.geometry` via `MeshAt(lod)`. On failure, marks the work item as
 dropped.

 Requires `ShouldRenderPreFilter` to have validated presence of a renderable,
 transform, and geometry.

 @param w Work item to update with `pending_lod` and `mesh`.
 @param ctx Extractor context providing view parameters (camera position, focal
            length, frustum).

 ### Performance Characteristics
 - Time Complexity: O(1)
 - Memory: O(1)

 @see ShouldRenderPreFilter, VisibilityFilter
*/
inline void MeshResolver(WorkItem& w, ExtractorContext const& ctx) noexcept
{
  DCHECK_F(static_cast<bool>(w.proto.geometry));
  DCHECK_F(w.HasRenderable());
  // Perform LOD selection here to keep policy and resolution together.
  const auto sphere = w.proto.world_bounding_sphere;
  const auto center = glm::vec3(sphere.x, sphere.y, sphere.z);
  const auto cam_pos = ctx.view.CameraPosition();

  if (w.Renderable().UsesDistancePolicy()) {
    const float dist = glm::length(cam_pos - center);
    const float radius = std::max(sphere.w, 1e-6f);
    const float normalized_distance = dist / radius;
    w.Renderable().SelectActiveMesh(
      scn::NormalizedDistance { normalized_distance });
  } else if (w.Renderable().UsesScreenSpaceErrorPolicy()) {
    const float z = std::max(glm::length(center - cam_pos), 1e-6f);
    const float r_world = std::max(sphere.w, 1e-6f);
    const float f = ctx.view.FocalLengthPixels();
    if (f > 0.0f) {
      const float sse = f * r_world / z;
      w.Renderable().SelectActiveMesh(scn::ScreenSpaceError { sse });
    }
  }
  if (auto active = w.Renderable().GetActiveLodIndex()) {
    w.pending_lod = static_cast<std::uint32_t>(*active);
  } else {
    w.pending_lod.reset();
  }
  std::uint32_t lod = w.pending_lod.value_or(w.proto.lod_index);
  try {
    w.mesh = w.proto.geometry->MeshAt(lod);
    // Ensure submesh_mask matches resolved mesh size
    w.submesh_mask.assign(w.mesh->SubMeshes().size(), 0);
  } catch (...) {
    w.mesh = nullptr;
    w.dropped = true;
  }
}
static_assert(oxygen::engine::extraction::UpdaterFn<decltype(MeshResolver)>);

//! Per-submesh visibility mask and overall visibility
/*!
 Computes the per-submesh visibility mask for the resolved mesh and returns
 whether any submesh is visible according to the `Renderable`'s submesh
 visibility state. Guards against null meshes.

 @param w Work item to read from and update (`submesh_mask`).
 @param ctx Extractor context (unused).
 @return `true` if at least one submesh is visible; otherwise `false`.

 ### Performance Characteristics
 - Time Complexity: O(S) where S is the number of submeshes in the active LOD
 - Memory: O(S) for the visibility mask

 @see MeshResolver
*/
inline bool VisibilityFilter(WorkItem& w, ExtractorContext const&) noexcept
{
  if (!w.mesh) {
    return false;
  }
  const auto lod = w.ResolvedMeshIndex();
  const auto& submeshes = w.mesh->SubMeshes();
  w.submesh_mask.assign(submeshes.size(), 0);
  bool any = false;
  for (std::size_t i = 0; i < submeshes.size(); ++i) {
    const bool visible = w.Renderable().IsSubmeshVisible(lod, i);
    w.submesh_mask[i] = visible ? 1 : 0;
    any = any || visible;
  }
  return any;
}
static_assert(oxygen::engine::extraction::FilterFn<decltype(VisibilityFilter)>);

//! Material resolver placeholder
/*!
 No-op: material resolution is performed per-submesh in the emitter to allow
 per-submesh materials. Kept as a hook for future material preprocessing.

 @param w Work item (unused).
 @param ctx Extractor context (unused).

 @see EmitPerVisibleSubmesh
*/
inline void MaterialResolver(WorkItem& w, ExtractorContext const&) noexcept
{
  (void)w;
}
static_assert(
  oxygen::engine::extraction::UpdaterFn<decltype(MaterialResolver)>);

//! Emit one render item per visible and frustum-visible submesh
/*!
 Produces `RenderItemData` entries for all submeshes that are both marked
 visible (by mask) and inside the view frustum. Uses per-submesh world AABBs
 when available; falls back to the world bounding sphere. Resolves the material
 per submesh, falling back to the mesh submesh's material, and then to the
 default material.

 Requires `ShouldRenderPreFilter` and `MeshResolver` to have prepared geometry,
 transforms, bounds, and the active mesh.

 @param w Work item providing mesh, bounds, and prototype data.
 @param ctx Extractor context providing view frustum.
 @param out Collector to which produced `RenderItemData` are appended.

 ### Performance Characteristics
 - Time Complexity: O(S) frustum tests and material lookups per mesh LOD
 - Memory: O(1) amortized per emitted item

 @see ShouldRenderPreFilter, MeshResolver, VisibilityFilter
*/
inline void EmitPerVisibleSubmesh(
  WorkItem& w, ExtractorContext const& ctx, Collector out) noexcept
{
  if (w.dropped || !w.mesh) {
    return;
  }

  const auto lod = w.ResolvedMeshIndex();
  const auto& submeshes = w.mesh->SubMeshes();
  const auto& frustum = ctx.view.GetFrustum();

  for (std::size_t i = 0; i < submeshes.size(); ++i) {
    if (!w.submesh_mask.empty() && !w.submesh_mask[i]) {
      continue; // invisible by mask
    }

    // Per-submesh frustum culling: prefer world AABB; fallback to sphere
    bool visible = true;
    if (auto aabb = w.Renderable().GetWorldSubMeshBoundingBox(i)) {
      visible = frustum.IntersectsAABB(aabb->first, aabb->second);
    } else {
      const auto c = glm::vec3(w.proto.world_bounding_sphere);
      const float r = w.proto.world_bounding_sphere.w;
      visible = frustum.IntersectsSphere(c, r);
    }
    if (!visible) {
      continue;
    }

    RenderItemData r = w.proto;
    r.lod_index = lod;
    r.submesh_index = static_cast<std::uint32_t>(i);

    // Resolve material per submesh now
    auto mat = w.Renderable().ResolveSubmeshMaterial(lod, i);
    if (mat) {
      r.material = std::move(mat);
    } else {
      r.material = submeshes[i].Material();
      if (!r.material) {
        r.material = oxygen::data::MaterialAsset::CreateDefault();
      }
    }
    if (r.material) {
      r.domain = r.material->GetMaterialDomain();
    }

    out.push_back(std::move(r));
  }
}
static_assert(
  oxygen::engine::extraction::ProducerFn<decltype(EmitPerVisibleSubmesh)>);

} // namespace oxygen::engine::extraction
