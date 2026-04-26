//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/ScenePrep/RenderItemData.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Translucency/TranslucencyMeshProcessor.h>
#include <Oxygen/Vortex/Types/AcceptedDrawView.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex {

namespace {

  struct SortableTranslucencyDraw {
    TranslucencyDrawCommand command {};
    float back_to_front_key { 0.0F };
  };

  auto IsPerspectiveProjection(const ResolvedView& view) -> bool
  {
    return std::abs(view.ProjectionMatrix()[3][3]) <= 1.0e-5F;
  }

  auto ComputeBackToFrontKeyFromSphere(
    const ResolvedView& resolved_view, const glm::vec4 sphere) -> float
  {
    const auto view_space_center = resolved_view.ViewMatrix()
      * glm::vec4(sphere.x, sphere.y, sphere.z, 1.0F);
    const auto forward_depth = (std::max)(-view_space_center.z, 0.0F);
    if (!std::isfinite(forward_depth)) {
      return 0.0F;
    }

    if (IsPerspectiveProjection(resolved_view)) {
      const auto radius = (std::max)(sphere.w, 0.0F);
      return forward_depth + radius;
    }

    return forward_depth;
  }

  auto FindDrawBoundingSphere(const PreparedSceneFrame& prepared_scene,
    const std::uint32_t draw_index) -> std::optional<glm::vec4>
  {
    if (draw_index < prepared_scene.draw_bounding_spheres.size()) {
      return prepared_scene.draw_bounding_spheres[draw_index];
    }

    if (draw_index < prepared_scene.render_items.size()) {
      const auto sphere
        = prepared_scene.render_items[draw_index].world_bounding_sphere;
      if (std::isfinite(sphere.x) && std::isfinite(sphere.y)
        && std::isfinite(sphere.z) && std::isfinite(sphere.w)) {
        return sphere;
      }
    }

    return std::nullopt;
  }

  auto ComputeBackToFrontKey(const PreparedSceneFrame& prepared_scene,
    const ResolvedView* resolved_view, const std::uint32_t draw_index) -> float
  {
    if (resolved_view != nullptr) {
      if (const auto sphere
        = FindDrawBoundingSphere(prepared_scene, draw_index);
        sphere.has_value()) {
        return ComputeBackToFrontKeyFromSphere(*resolved_view, *sphere);
      }
    }

    if (draw_index < prepared_scene.render_items.size()) {
      const auto sort_distance2
        = prepared_scene.render_items[draw_index].sort_distance2;
      return std::isfinite(sort_distance2)
        ? std::sqrt((std::max)(sort_distance2, 0.0F))
        : 0.0F;
    }

    return 0.0F;
  }

  auto HasValidGeometryRange(const DrawMetadata& metadata) -> bool
  {
    if (!metadata.vertex_buffer_index.IsValid()) {
      return false;
    }

    if (metadata.is_indexed != 0U) {
      return metadata.index_count > 0U && metadata.index_buffer_index.IsValid();
    }

    return metadata.vertex_count > 0U;
  }

  auto HasValidMaterial(const PreparedSceneFrame& prepared_scene,
    const DrawMetadata& metadata, const std::uint32_t draw_index) -> bool
  {
    if (draw_index < prepared_scene.render_items.size()) {
      const auto& item = prepared_scene.render_items[draw_index];
      return item.material_handle.IsValid();
    }

    return metadata.material_handle != 0U
      && metadata.material_handle != oxygen::kInvalidBindlessIndex;
  }

  auto IsDrawCommandBuildable(const PreparedSceneFrame& prepared_scene,
    const DrawMetadata& metadata, const std::uint32_t draw_index) -> bool
  {
    if (!metadata.flags.IsSet(PassMaskBit::kTransparent)) {
      DCHECK_F(false,
        "Transparent partition yielded draw {} without transparent metadata "
        "flag",
        draw_index);
      return false;
    }

    if (!metadata.flags.IsSet(PassMaskBit::kMainViewVisible)) {
      return false;
    }

    if (!HasValidMaterial(prepared_scene, metadata, draw_index)) {
      DLOG_F(
        2, "Rejecting translucent draw {} with invalid material", draw_index);
      return false;
    }

    if (!HasValidGeometryRange(metadata)) {
      DLOG_F(2, "Rejecting translucent draw {} with invalid geometry range",
        draw_index);
      return false;
    }

    return true;
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
      geometry_lod_index
        = render_item.geometry.IsValid() ? render_item.geometry.lod_index : 0U;
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

  const auto accepted_draws
    = AcceptedDrawView(prepared_scene, PassMask { PassMaskBit::kTransparent });
  if (accepted_draws.empty()) {
    return;
  }

  auto sortable_draws = std::vector<SortableTranslucencyDraw> {};

  for (const auto [metadata, draw_index] : accepted_draws) {
    if (!IsDrawCommandBuildable(prepared_scene, *metadata, draw_index)) {
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
