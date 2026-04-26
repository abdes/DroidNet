//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Translucency/TranslucencyMeshProcessor.h>
#include <Oxygen/Vortex/Types/AcceptedDrawView.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex {

namespace {

struct SortableTranslucencyDraw {
  TranslucencyDrawCommand command {};
  float back_to_front_key { 0.0F };
};

auto IsPerspectiveProjection(const ResolvedView& view) -> bool
{
  return std::abs(view.ProjectionMatrix()[2][3]) > 0.5F;
}

auto ComputeBackToFrontKey(const PreparedSceneFrame& prepared_scene,
  const ResolvedView* resolved_view, const std::uint32_t draw_index) -> float
{
  if (resolved_view != nullptr
    && draw_index < prepared_scene.draw_bounding_spheres.size()) {
    const auto sphere = prepared_scene.draw_bounding_spheres[draw_index];
    const auto view_space_center = resolved_view->ViewMatrix()
      * glm::vec4(sphere.x, sphere.y, sphere.z, 1.0F);
    const auto forward_depth = (std::max)(-view_space_center.z, 0.0F);
    if (!std::isfinite(forward_depth)) {
      return 0.0F;
    }

    if (IsPerspectiveProjection(*resolved_view)) {
      const auto radius = (std::max)(sphere.w, 0.0F);
      return forward_depth + radius;
    }

    return forward_depth;
  }

  if (draw_index < prepared_scene.render_items.size()) {
    const auto sort_distance2 = prepared_scene.render_items[draw_index]
                                  .sort_distance2;
    return std::isfinite(sort_distance2) ? sort_distance2 : 0.0F;
  }

  return 0.0F;
}

auto MakeDrawCommand(const PreparedSceneFrame& prepared_scene,
  const DrawMetadata& metadata, const std::uint32_t draw_index)
  -> TranslucencyDrawCommand
{
  auto material_handle = metadata.material_handle;
  auto geometry_lod_index = 0U;

  if (draw_index < prepared_scene.render_items.size()) {
    const auto& render_item = prepared_scene.render_items[draw_index];
    if (render_item.material_handle.IsValid()) {
      material_handle = render_item.material_handle.get();
    }
    geometry_lod_index = render_item.geometry.IsValid()
      ? render_item.geometry.lod_index
      : render_item.submesh_index;
  }

  return TranslucencyDrawCommand {
    .draw_index = draw_index,
    .material_handle = material_handle,
    .geometry_lod_index = geometry_lod_index,
    .submesh_index = metadata.submesh_index,
    .index_count = metadata.index_count,
    .vertex_count = metadata.vertex_count,
    .instance_count = (std::max)(metadata.instance_count, 1U),
    .start_index = metadata.first_index,
    .base_vertex = metadata.base_vertex,
    .start_instance = 0U,
    .is_indexed = metadata.is_indexed != 0U,
  };
}

auto StableSortBackToFront(
  std::vector<SortableTranslucencyDraw>& draw_commands) -> void
{
  std::ranges::stable_sort(draw_commands,
    [](const SortableTranslucencyDraw& lhs,
      const SortableTranslucencyDraw& rhs) -> bool {
      return lhs.back_to_front_key > rhs.back_to_front_key;
    });
}

} // namespace

TranslucencyMeshProcessor::TranslucencyMeshProcessor(Renderer& renderer)
  : renderer_(renderer)
{
}

TranslucencyMeshProcessor::~TranslucencyMeshProcessor() = default;

void TranslucencyMeshProcessor::BuildDrawCommands(
  const PreparedSceneFrame& prepared_scene, const ResolvedView* resolved_view)
{
  (void)renderer_;
  draw_commands_.clear();

  if (prepared_scene.draw_metadata_bytes.empty()) {
    return;
  }

  const auto accepted_draws = AcceptedDrawView(
    prepared_scene, PassMask { PassMaskBit::kTransparent });
  if (accepted_draws.empty()) {
    return;
  }

  auto sortable_draws = std::vector<SortableTranslucencyDraw> {};
  sortable_draws.reserve(prepared_scene.GetDrawMetadata().size());

  for (const auto [metadata, draw_index] : accepted_draws) {
    if (!metadata->flags.IsSet(PassMaskBit::kMainViewVisible)) {
      continue;
    }

    sortable_draws.push_back(SortableTranslucencyDraw {
      .command = MakeDrawCommand(prepared_scene, *metadata, draw_index),
      .back_to_front_key
      = ComputeBackToFrontKey(prepared_scene, resolved_view, draw_index),
    });
  }

  StableSortBackToFront(sortable_draws);
  draw_commands_.reserve(sortable_draws.size());
  for (const auto& sortable : sortable_draws) {
    draw_commands_.push_back(sortable.command);
  }
}

auto TranslucencyMeshProcessor::GetDrawCommands() const
  -> std::span<const TranslucencyDrawCommand>
{
  return draw_commands_;
}

} // namespace oxygen::vortex
