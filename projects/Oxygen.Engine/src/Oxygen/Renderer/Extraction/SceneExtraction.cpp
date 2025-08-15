//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneExtraction.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Types/ActiveMesh.h>
#include <Oxygen/Scene/Types/Strong.h>
#include <Oxygen/Scene/Types/Traversal.h>

namespace oxygen::engine::extraction {

using oxygen::scene::NormalizedDistance;
using oxygen::scene::SceneTraversal;
using oxygen::scene::ScreenSpaceError;
using oxygen::scene::TraversalOrder;
using oxygen::scene::VisibleFilter;
using oxygen::scene::VisitResult;

namespace scn = oxygen::scene;

namespace {

  // Evaluates LOD based on the renderable's active policy. For distance policy,
  // uses normalized distance to world sphere. For SSE policy, uses a simple
  // screen-space error estimate from projected sphere size.
  void EvaluateRenderableLodForView(
    scn::SceneNode::Renderable renderable, const View& view) noexcept
  {
    const auto sphere = renderable.GetWorldBoundingSphere();
    // If no bounds (radius == 0), skip evaluation.
    if (sphere.w <= 0.0f)
      return;

    if (renderable.UsesDistancePolicy()) {
      const auto cam_pos = view.CameraPosition();
      const auto center = glm::vec3(sphere.x, sphere.y, sphere.z);
      const float dist = glm::length(cam_pos - center);
      const float radius = (std::max)(sphere.w, 1e-6f);
      const float normalized_distance = dist / radius;
      renderable.SelectActiveMesh(NormalizedDistance { normalized_distance });
    } else if (renderable.UsesScreenSpaceErrorPolicy()) {
      // Approximate SSE as the projected screen radius in pixels.
      // sse ≈ f * r / z, where f is vertical focal length in pixels.
      const auto center = glm::vec3(sphere.x, sphere.y, sphere.z);
      const auto cam_pos = view.CameraPosition();
      const float z = (std::max)(glm::length(center - cam_pos), 1e-6f);
      const float r_world = (std::max)(sphere.w, 1e-6f);
      const float f = view.FocalLengthPixels();
      if (f > 0.0f) {
        const float sse = f * r_world / z;
        renderable.SelectActiveMesh(ScreenSpaceError { sse });
      }
    } else {
      // Fixed policy: nothing to evaluate.
    }
  }

} // namespace

auto CollectRenderItems(
  scn::Scene& scene, const View& view, RenderItemsList& out) -> std::size_t
{
  // 1) Expects transforms are up to date, which should be done by the Renderer
  //    before calling CollectRenderItems

  // 2) Pre-order traversal with visibility filter
  std::size_t count = 0;
  std::size_t culled = 0;
  SceneTraversal<scn::Scene> traversal(scene.shared_from_this());
  auto visitor = [&](const auto& visited, bool /*dry_run*/) -> VisitResult {
    auto* node = visited.node_impl;
    const auto& flags = node->GetFlags();

    // Only consider nodes with meshes. We need a handle to get the mesh.
    const auto scene_sp = scene.shared_from_this();
    const auto scene_wp = std::weak_ptr<const scn::Scene>(
      std::const_pointer_cast<const scn::Scene>(scene_sp));
    scn::SceneNode node_handle(scene_wp, visited.handle);
    // Evaluate LOD for nodes with a Renderable before querying active mesh.
    auto renderable = node_handle.GetRenderable();
    EvaluateRenderableLodForView(renderable, view);
    const auto node_name = node_handle.GetName();

    if (!renderable.HasGeometry()) {
      return VisitResult::kContinue;
    }

    auto active_mesh = renderable.GetActiveMesh();
    DCHECK_F(active_mesh.has_value(), "Expected active mesh to be present");
    if (!active_mesh) {
      LOG_F(WARNING,
        "SceneExtraction: node='{}' has no active mesh despite geometry; "
        "skipping",
        node_name);
      return VisitResult::kContinue;
    }

    RenderItem item {};
    item.mesh = active_mesh->mesh;

    // World transform (cached by prior UpdateTransforms call)
    const auto transform = node_handle.GetTransform();
    if (const auto world_opt = transform.GetWorldMatrix()) {
      const auto world = *world_opt;
      item.world_transform = world;
    } else {
      // If world matrix is unavailable, skip this node to avoid incorrect
      // culling or rendering.
      LOG_F(WARNING,
        "SceneExtraction: node='{}' has no world transform; skipping",
        node_name);
      return VisitResult::kContinue;
    }

    // Snapshot flags
    item.cast_shadows
      = flags.GetEffectiveValue(scn::SceneNodeFlags::kCastsShadows);
    item.receive_shadows
      = flags.GetEffectiveValue(scn::SceneNodeFlags::kReceivesShadows);

    // Phase 1 bridge: honor per-submesh visibility and material overrides.
    // Aggregate per-submesh world AABB for tighter culling and pick material
    // from the first visible submesh; warn on mixed materials.
    std::shared_ptr<const oxygen::data::MaterialAsset> selected_material;
    bool have_material = false;
    glm::vec3 agg_min { std::numeric_limits<float>::infinity() };
    glm::vec3 agg_max { -std::numeric_limits<float>::infinity() };
    bool any_visible = false;
    bool mixed_materials = false;
    bool visibility_determined = false;
    {
      const auto lod_opt = renderable.GetActiveLodIndex();
      const auto& mesh = active_mesh->mesh;
      if (lod_opt && mesh) {
        visibility_determined = true;
        const auto lod = *lod_opt;
        const auto submeshes = mesh->SubMeshes();
        for (std::size_t i = 0; i < submeshes.size(); ++i) {
          if (!renderable.IsSubmeshVisible(lod, i)) {
            continue;
          }
          any_visible = true;
          // Material resolution
          auto mat = renderable.ResolveSubmeshMaterial(lod, i);
          if (!have_material) {
            selected_material = mat;
            have_material = true;
          } else if (mat.get() != selected_material.get()) {
            mixed_materials = true;
          }
          // Per-submesh world AABB aggregation (use submesh world AABB when
          // available)
          if (auto waabb = renderable.GetWorldSubMeshBoundingBox(i)) {
            agg_min = glm::min(agg_min, waabb->first);
            agg_max = glm::max(agg_max, waabb->second);
          } else {
            // Fallback to mesh-level AABB transformed via item helper
            // Ensure item bounds are computed so min/max exist as fallback
            if (item.bounding_box_min == glm::vec3(0.0f)
              && item.bounding_box_max == glm::vec3(0.0f)) {
              item.UpdatedTransformedProperties();
            }
            agg_min = glm::min(agg_min, item.bounding_box_min);
            agg_max = glm::max(agg_max, item.bounding_box_max);
          }
        }
      }
    }

    // If visibility was determined (LOD known) and none were visible, skip.
    if (!any_visible && visibility_determined) {
      DLOG_F(2, "SceneExtraction: node='{}' culled (all submeshes invisible)",
        node_name);
      ++culled; // treat as culled due to visibility mask
      return VisitResult::kContinue;
    }

    // If visibility wasn’t determined (no Renderable or no active LOD yet),
    // fall back to treating the mesh as visible with mesh-level material.
    if (!any_visible && !visibility_determined) {
      const auto& mesh = active_mesh->mesh;
      if (mesh) {
        const auto submeshes = mesh->SubMeshes();
        if (!submeshes.empty()) {
          selected_material = submeshes[0].Material();
          have_material = (selected_material != nullptr);
        }
      }
      item.material = have_material
        ? selected_material
        : oxygen::data::MaterialAsset::CreateDefault();
      // Compute default world-space properties (mesh-level bounds)
      item.UpdatedTransformedProperties();
    } else {
      // At least one submesh is visible: resolve material and aggregate bounds.
      // Prefer default material over debug fallback when none resolved.
      item.material = have_material
        ? selected_material
        : oxygen::data::MaterialAsset::CreateDefault();
      // First compute sphere/normal from mesh and world
      item.UpdatedTransformedProperties();
      // Then override AABB with aggregated submesh AABB for tighter culling
      item.bounding_box_min = agg_min;
      item.bounding_box_max = agg_max;
    }

    if (mixed_materials) {
      DLOG_F(2,
        "SceneExtraction: visible submeshes have mixed materials; using first "
        "visible submesh material for mesh-level item");
    }

    // CPU culling
    const auto& fr = view.GetFrustum();
    const auto visible
      = fr.IntersectsAABB(item.bounding_box_min, item.bounding_box_max);
    if (!visible) {
      ++culled;
      return VisitResult::kContinue;
    }

    // Insert into output
    out.Add(std::move(item));
    ++count;
    return VisitResult::kContinue;
  };

  auto result = traversal.Traverse(
    std::move(visitor), TraversalOrder::kPreOrder, VisibleFilter {});
  (void)result;
  DLOG_F(2, "SceneExtraction: visible={}, culled={}", count, culled);
  return count;
}

} // namespace oxygen::engine::extraction
