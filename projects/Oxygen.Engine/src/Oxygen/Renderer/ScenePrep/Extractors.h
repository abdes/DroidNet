//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// no extra STL headers required here

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/Resources/TransformUploader.h>
#include <Oxygen/Renderer/ScenePrep/Concepts.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepContext.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>

namespace oxygen::engine::sceneprep {

//! Pre-filter applied to RenderItemProto objects to identify renderable ones.
/*!
 Validates effective node visibility and seeds the prototype (`item`) with
 stable per-item data required by later extractors:
 - visibility/casting/receiving shadow flags
 - geometry reference from the node's renderable
 - world transform matrix

 If the node is not effectively visible, the item is marked dropped.

 Notes:
 - Presence of Renderable/Transform is guaranteed by `RenderItemProto` binding
   to a valid `SceneNodeImpl`.

 @param ctx Scene preparation context (unused).
 @param state Scene preparation state (unused).
 @param item Item to inspect and seed.

 ### Performance Characteristics
 - Time Complexity: O(1)
 - Memory: O(1)
*/
inline auto ExtractionPreFilter(const ScenePrepContext& /*ctx*/,
  ScenePrepState& /*state*/, RenderItemProto& item) noexcept -> void
{
  using enum scene::SceneNodeFlags;

  // Skip nodes culled by effective visibility (hierarchy-aware flag).
  if (!item.Flags().GetEffectiveValue(kVisible)) {
    item.MarkDropped();
    return;
  }

  // FIXME: currently only items with geometry pass.
  // Future Enhancement: Some items should have renderable component but not
  // geometry, and are not supported yet
  item.SetVisible();
  item.SetCastShadows(item.Flags().GetEffectiveValue(kCastsShadows));
  item.SetReceiveShadows(item.Flags().GetEffectiveValue(kReceivesShadows));
  item.SetGeometry(item.Renderable().GetGeometry());
  item.SetWorldTransform(item.Transform().GetWorldMatrix());
}
static_assert(RenderItemDataExtractor<decltype(ExtractionPreFilter)>);

//! Resolve or allocate stable transform handle (after pre-filter flags).
/*! Allocates a stable handle in TransformManager for the item's world
    transform and stores it on the prototype for downstream use. */
inline auto TransformResolveStage(const ScenePrepContext& /*ctx*/,
  const ScenePrepState& state, RenderItemProto& item) noexcept -> void
{
  if (item.IsDropped()) {
    return;
  }
  // Integrate TransformUploader: assign deduplicated handle for world
  // transform.
  auto uploader = state.GetTransformUploader();
  if (uploader) {
    const auto handle = uploader->GetOrAllocate(item.GetWorldTransform());
    item.SetTransformHandle(handle);
  } else {
    item.SetTransformHandle(TransformHandle { 0 });
  }
}
static_assert(RenderItemDataExtractor<decltype(TransformResolveStage)>);

//! Resolve active mesh LOD and mesh resource
/*!
 Delegates LOD selection to the node's `Renderable` so policy and hysteresis
 remain centralized. Supports distance-based and screen-space-error (SSE)
 policies. After selection, resolves the active mesh from `item.Geometry()` via
 `MeshAt(lod)`. On failure, marks the item as dropped.

 @param ctx Scene preparation context (provides camera position and focal
 length).
 @param state Scene preparation state (unused).
 @param item Item to update with resolved mesh and LOD index.

 ### Performance Characteristics
 - Time Complexity: O(1)
 - Memory: O(1)

 @see ExtractionPreFilter, SubMeshVisibilityFilter
*/
inline auto MeshResolver(const ScenePrepContext& ctx,
  [[maybe_unused]] ScenePrepState& state, RenderItemProto& item) noexcept
  -> void
{
  CHECK_F(!item.IsDropped());
  CHECK_NOTNULL_F(item.Geometry());

  // Perform LOD selection here to keep policy and resolution together.
  const auto sphere = item.Renderable().GetWorldBoundingSphere();
  const auto center = glm::vec3(sphere.x, sphere.y, sphere.z);
  const auto cam_pos = ctx.GetView().CameraPosition();

  if (item.Renderable().UsesDistancePolicy()) {
    const float dist = glm::length(cam_pos - center);
    const float radius = std::max(sphere.w, 1e-6f);
    const float normalized_distance = dist / radius;
    item.Renderable().SelectActiveMesh(
      scn::NormalizedDistance { normalized_distance });
  } else if (item.Renderable().UsesScreenSpaceErrorPolicy()) {
    const float z = std::max(glm::length(center - cam_pos), 1e-6f);
    const float r_world = std::max(sphere.w, 1e-6f);
    const float f = ctx.GetView().FocalLengthPixels();
    if (f > 0.0f) {
      const float sse = f * r_world / z;
      item.Renderable().SelectActiveMesh(scn::ScreenSpaceError { sse });
    }
  }
  // Use the selected LOD or fallback to the first mesh
  uint32_t lod { 0 };
  if (const auto active = item.Renderable().GetActiveLodIndex()) {
    lod = static_cast<std::uint32_t>(*active);
  }
  try {
    item.ResolveMesh(item.Geometry()->MeshAt(lod), lod);
  } catch (...) {
    item.MarkDropped();
  }
}
static_assert(RenderItemDataExtractor<decltype(MeshResolver)>);

//! Per-submesh visibility extractor with frustum culling.
/*!
 Computes the list of visible submesh indices for the resolved mesh by
 combining the node's submesh visibility state with view-frustum culling.
 When a per-submesh world AABB is available it is used for culling; otherwise
 the node's world bounding sphere is used as a fallback.

 @param ctx Scene preparation context providing the view frustum.
 @param state Scene preparation state (unused).
 @param item Item to read from and update (`VisibleSubmeshes`).

 ### Performance Characteristics
 - Time Complexity: O(S) where S is the number of submeshes in the active LOD
 - Memory: O(S) to hold the visible submesh indices

 @see MeshResolver, EmitPerVisibleSubmesh
*/
inline auto SubMeshVisibilityFilter(const ScenePrepContext& ctx,
  [[maybe_unused]] ScenePrepState& state, RenderItemProto& item) noexcept
  -> void
{
  CHECK_F(!item.IsDropped());
  CHECK_NOTNULL_F(item.Geometry());

  // Nothing to do if no mesh is resolved
  if (item.ResolvedMesh() == nullptr) {
    item.MarkDropped();
    return;
  }

  const auto lod = item.ResolvedMeshIndex();
  const auto& submeshes = item.ResolvedMesh()->SubMeshes();
  const auto& frustum = ctx.GetView().GetFrustum();
  // Fast path: single pass, cached facade, reserve upper bound, push visible
  const auto& rend = item.Renderable();
  std::vector<uint32_t> visible_submeshes;
  visible_submeshes.reserve(submeshes.size());

  // Diagnostics: allow disabling culling and/or inflating bounds slightly
  static constexpr bool kDisableSubmeshFrustumCulling = false;
  // Absolute inflation in world units (meters) and relative inflation factor
  // (percentage of AABB diagonal or sphere radius). Final inflation is
  // max(abs, rel).
  static constexpr float kBoundsInflationAbs = 0.0f;
  static constexpr float kBoundsInflationRel = 0.01f; // 1% guard band
  for (uint32_t i = 0, n = static_cast<uint32_t>(submeshes.size()); i < n;
    ++i) {
    // Visibility mask check first (cheap)
    const bool rend_vis = rend.IsSubmeshVisible(lod, i);
    if (!rend_vis) {
      continue;
    }

    // Frustum culling per submesh: prefer world AABB; fallback to node sphere
    bool in_frustum = true;
    if (!kDisableSubmeshFrustumCulling) {
      if (const auto aabb = item.Renderable().GetWorldSubMeshBoundingBox(i)) {
        // Inflate AABB slightly if requested (abs or relative)
        auto min = aabb->first;
        auto max = aabb->second;
        const float diag = glm::length(max - min);
        const float rel
          = kBoundsInflationRel > 0.0f ? (diag * kBoundsInflationRel) : 0.0f;
        const float infl = (std::max)(kBoundsInflationAbs, rel);
        if (infl > 0.0f) {
          min -= glm::vec3(infl);
          max += glm::vec3(infl);
        }
        in_frustum = frustum.IntersectsAABB(min, max);
      } else {
        const auto sphere = rend.GetWorldBoundingSphere();
        const auto c = glm::vec3(sphere);
        float r = sphere.w;
        const float rel
          = kBoundsInflationRel > 0.0f ? (r * kBoundsInflationRel) : 0.0f;
        const float infl = (std::max)(kBoundsInflationAbs, rel);
        if (infl > 0.0f) {
          r += infl;
        }
        in_frustum = frustum.IntersectsSphere(c, r);
      }
    }
    if (!in_frustum) {
      continue;
    }

    visible_submeshes.emplace_back(i);
  }
  item.SetVisibleSubmeshes(std::move(visible_submeshes));
}
static_assert(RenderItemDataExtractor<decltype(SubMeshVisibilityFilter)>);

//! Emit one render item per visible submesh
/*!
 Produces `RenderItemData` entries for all submeshes listed in
 `item.VisibleSubmeshes()`. Material is resolved per submesh by querying the
 node's renderable first, then falling back to the mesh's submesh material,
 and finally to the default material.

 Prerequisites: `ExtractionPreFilter`, `MeshResolver`, and
 `SubMeshVisibilityFilter` must have populated geometry, transform, active LOD,
 and visible submesh indices.

 @param ctx Scene preparation context (unused).
 @param state Scene preparation state that collects produced items.
 @param item Source item providing geometry, LOD, and visible submesh indices.

 ### Performance Characteristics
 - Time Complexity: O(S) material lookups per mesh LOD
 - Memory: O(1) amortized per emitted item

 @see ExtractionPreFilter, MeshResolver, SubMeshVisibilityFilter
*/
inline auto EmitPerVisibleSubmesh(const ScenePrepContext& /*ctx*/,
  ScenePrepState& state, RenderItemProto& item) noexcept -> void
{
  CHECK_F(!item.IsDropped());
  CHECK_NOTNULL_F(item.Geometry());
  CHECK_NOTNULL_F(item.ResolvedMesh());
  CHECK_NOTNULL_F(state.GetMaterialBinder());

  const auto& visible_submeshes = item.VisibleSubmeshes();
  if (visible_submeshes.empty()) {
    return; // Nothing to do if no submeshes are visible
  }

  const auto lod = item.ResolvedMeshIndex();
  const auto& submeshes = item.ResolvedMesh()->SubMeshes();
  for (auto index : visible_submeshes) {
    // Material selection chain as a local lambda
    auto resolve_material
      = [&]() -> std::shared_ptr<const data::MaterialAsset> {
      if (auto mat = item.Renderable().ResolveSubmeshMaterial(lod, index)) {
        return mat;
      }
      if (auto mesh_mat = submeshes[index].Material()) {
        return mesh_mat;
      }
      return data::MaterialAsset::CreateDefault();
    };

    auto mat_ptr = resolve_material();
    const auto mat_handle = state.GetMaterialBinder()->GetOrAllocate(mat_ptr);
    state.CollectItem(RenderItemData {
      .lod_index = lod,
      .submesh_index = index,
      .geometry = item.Geometry(),
      .material = std::move(mat_ptr),
      .material_handle = mat_handle,
      .world_bounding_sphere = item.Renderable().GetWorldBoundingSphere(),
      .transform_handle = item.GetTransformHandle(),
      .cast_shadows = item.CastsShadows(),
      .receive_shadows = item.ReceivesShadows(),
    });
  }
}
static_assert(RenderItemDataExtractor<decltype(EmitPerVisibleSubmesh)>);

} // namespace oxygen::engine::sceneprep
