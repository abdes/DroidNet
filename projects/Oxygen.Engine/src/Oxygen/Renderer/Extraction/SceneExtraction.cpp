//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneExtraction.h"

#include <memory>
#include <utility>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Types/ActiveMesh.h>
#include <Oxygen/Scene/Types/Traversal.h>
// Evaluate LOD using Renderable component per node
#include <Oxygen/Scene/Detail/Renderable.h>

namespace oxygen::engine::extraction {

using oxygen::scene::SceneTraversal;
using oxygen::scene::TraversalOrder;
using oxygen::scene::VisibleFilter;
using oxygen::scene::VisitResult;
namespace scn = oxygen::scene;

namespace {

  // Evaluates LOD based on the renderable's active policy. For distance policy,
  // uses normalized distance to world sphere. For SSE policy, uses a simple
  // screen-space error estimate from projected sphere size.
  void EvaluateRenderableLodForView(
    scn::SceneNodeImpl* node, const View& view) noexcept
  {
    using oxygen::scene::detail::Renderable;
    if (!node->HasComponent<Renderable>())
      return;

    const auto& r = node->GetComponent<Renderable>();
    const auto sphere = r.GetWorldBoundingSphere();
    // If no bounds (radius == 0), skip evaluation.
    if (sphere.w <= 0.0f)
      return;

    if (r.UsesDistancePolicy()) {
      const auto cam_pos = view.CameraPosition();
      const auto center = glm::vec3(sphere.x, sphere.y, sphere.z);
      const float dist = glm::length(cam_pos - center);
      const float radius = (std::max)(sphere.w, 1e-6f);
      const float normalized_distance = dist / radius;
      r.SelectActiveMesh(
        Renderable::NormalizedDistance { normalized_distance });
    } else if (r.UsesScreenSpaceErrorPolicy()) {
      // Approximate SSE as the projected screen radius in pixels.
      // sse ≈ f * r / z, where f is vertical focal length in pixels.
      const auto center = glm::vec3(sphere.x, sphere.y, sphere.z);
      const auto cam_pos = view.CameraPosition();
      const float z = (std::max)(glm::length(center - cam_pos), 1e-6f);
      const float r_world = (std::max)(sphere.w, 1e-6f);
      const float f = view.FocalLengthPixels();
      if (f > 0.0f) {
        const float sse = f * r_world / z;
        r.SelectActiveMesh(Renderable::ScreenSpaceError { sse });
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
    EvaluateRenderableLodForView(node, view);

    if (!node_handle.HasGeometry()) {
      return VisitResult::kContinue;
    }

    auto active_mesh = node_handle.GetActiveMesh();
    DCHECK_F(active_mesh.has_value(), "Expected active mesh to be present");

    RenderItem item {};
    item.mesh = active_mesh->mesh;
    // Temporary: assign a default debug material until material binding flows
    // from scene/assets. This keeps examples working.
    item.material = oxygen::data::MaterialAsset::CreateDebug();

    // World transform (cached by prior UpdateTransforms call)
    const auto transform = node_handle.GetTransform();
    if (const auto world_opt = transform.GetWorldMatrix()) {
      const auto world = *world_opt;
      item.world_transform = world;
    } else {
      // If world matrix is unavailable, skip this node to avoid incorrect
      // culling or rendering.
      return VisitResult::kContinue;
    }

    // Snapshot flags
    item.cast_shadows
      = flags.GetEffectiveValue(scn::SceneNodeFlags::kCastsShadows);
    item.receive_shadows
      = flags.GetEffectiveValue(scn::SceneNodeFlags::kReceivesShadows);

    // Compute derived properties (bounds etc.)
    item.UpdateComputedProperties();

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
