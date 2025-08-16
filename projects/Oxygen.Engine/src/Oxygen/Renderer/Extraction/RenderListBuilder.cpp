//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>

#include <glm/glm.hpp>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/RenderItemsList.h>
#include <Oxygen/Renderer/Types/View.h>
#include <Oxygen/Scene/Detail/RenderableComponent.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/SceneNodeImpl.h>
#include <Oxygen/Scene/SceneTraversal.h>
#include <Oxygen/Scene/Types/ActiveMesh.h>
#include <Oxygen/Scene/Types/Traversal.h>

#include <Oxygen/Renderer/Extraction/Extractors.h>
#include <Oxygen/Renderer/Extraction/Extractors_impl.h>
#include <Oxygen/Renderer/Extraction/Pipeline.h>
#include <Oxygen/Renderer/Extraction/RenderListBuilder.h>

using oxygen::engine::RenderContext;
using oxygen::engine::extraction::RenderListBuilder;
namespace scn = oxygen::scene;

class RenderListBuilder::Impl {
public:
  // Placeholder for caches: LOD hysteresis, residency, transform manager, etc.
};

RenderListBuilder::RenderListBuilder()
  : impl_(std::make_unique<Impl>())
{
}

RenderListBuilder::~RenderListBuilder() = default;

auto RenderListBuilder::Collect(scene::Scene& scene, const View& view,
  std::uint64_t frame_id) -> std::vector<RenderItemData>
{
  // Build a compile-time pipeline with common extractors
  using namespace oxygen::engine::extraction;
  constexpr auto pipeline = Pipeline(
    // Node must be renderable
    ShouldRenderPreFilter,
    // Extract world transform
    TransformExtractor,
    // Resolve mesh (includes LOD selection)
    MeshResolver,
    // Apply visibility filter
    VisibilityFilter,
    // Extract node flags
    NodeFlagsExtractor,
    // Resolve material later per submesh in the emitter
    MaterialResolver,
    // Emit one item per visible submesh with frustum culling
    EmitPerVisibleSubmesh);

  // The collected data for the render items list
  std::vector<RenderItemData> out;

  // Cache-friendly direct iteration over the dense NodeTable.
  const auto& node_table = scene.GetNodes();
  const auto span = node_table.Items();
  for (const auto& node_impl : span) {
    // Process this node through the extraction pipeline.
    WorkItem wi { node_impl };
    ExtractorContext ctx { view, scene, frame_id };
    pipeline(wi, ctx, out);
  }

  return out;
}

auto RenderListBuilder::Finalize(
  std::span<const RenderItemData> collected_items,
  RenderContext& render_context, RenderItemsList& output) -> void
{
  // Convert collected lightweight items into RenderItem and insert into
  // output RenderItemsList. This mirrors the behavior in SceneExtraction but
  // uses the snapshot data inside RenderItemData. For each item we:
  //  - resolve mesh pointer from geometry asset + lod (if available)
  //  - snapshot material
  //  - snapshot world transform from the SceneNode
  //  - set flags and compute transformed properties

  output.Clear();
  output.Reserve(collected_items.size());

  for (const auto& d : collected_items) {
    RenderItem it {};

    // Resolve mesh pointer from geometry asset and lod index
    if (d.geometry) {
      // GeometryAsset::MeshAt returns shared_ptr<Mesh>
      try {
        auto mesh_ptr = d.geometry->MeshAt(d.lod_index);
        it.mesh = mesh_ptr;
      } catch (...) {
        it.mesh = nullptr;
      }
    }

    // Material
    it.material = d.material;

    // Per-submesh selection
    it.submesh_index = d.submesh_index;

    // Snapshot flags
    it.cast_shadows = d.cast_shadows;
    it.receive_shadows = d.receive_shadows;
    it.render_layer = d.render_layer;

    // Use cached world transform from the collected data.
    it.world_transform = d.world_transform;

    // If geometry present, compute transformed properties conservatively
    it.UpdatedTransformedProperties();

    output.Add(std::move(it));
  }

  // Renderer may need to ensure resources for the resulting draw list.
  // Use RenderContext to allow the Renderer to wire-up resources in PreExecute.
  (void)render_context;
}

auto RenderListBuilder::EvictStaleResources(RenderContext& /*render_context*/,
  std::uint64_t /*current_frame_id*/, std::uint32_t /*keep_frame_count*/)
  -> void
{
  // No-op eviction in minimal implementation.
}
