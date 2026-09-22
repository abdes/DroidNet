//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <tuple>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridMetadata.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightCullingConfig.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>

namespace oxygen::vortex::lighting::internal {

namespace {

  auto ClampViewportDimension(const float value) -> std::uint32_t
  {
    return std::max(1U, static_cast<std::uint32_t>(value));
  }

  auto BuildGridMetadata(const PreparedViewLightingInput& view_input)
    -> LightGridMetadata
  {
    if (view_input.resolved_view == nullptr) {
      return {};
    }

    const auto viewport = view_input.resolved_view->Viewport();
    const auto dims = LightCullingConfig {}.ComputeGridDimensions({
      .width = ClampViewportDimension(viewport.width),
      .height = ClampViewportDimension(viewport.height),
    });
    const bool perspective = !view_input.resolved_view->IsOrthographic();
    const auto z_params = perspective
      ? LightCullingConfig::ComputeLightGridZParams(
          view_input.resolved_view->NearPlane(),
          view_input.resolved_view->FarPlane())
      : LightCullingConfig::ZParams {};

    return {
      .grid_size = glm::uvec3 { dims.x, dims.y, dims.z },
      .pixel_size_shift = LightCullingConfig::kLightGridPixelSizeShift,
      .content_origin_px = { viewport.top_left_x, viewport.top_left_y },
      .content_extent_px = { viewport.width, viewport.height },
      .grid_z_params = glm::vec3 { z_params.b, z_params.o, z_params.s },
      .far_depth_m = view_input.resolved_view->FarPlane(),
      .near_depth_m = view_input.resolved_view->NearPlane(),
      .projection_kind
      = perspective ? kLightGridPerspective : kLightGridOrthographic,
    };
  }

  auto BuildBindings(const LightGridMetadata& metadata,
    const FrameLightSelection& selection) -> LightingFrameBindings
  {
    auto bindings = LightingFrameBindings {};
    bindings.grid_size = glm::ivec3(metadata.grid_size);
    bindings.grid_z_params = metadata.grid_z_params;
    bindings.num_grid_cells
      = metadata.grid_size.x * metadata.grid_size.y * metadata.grid_size.z;
    bindings.max_culled_lights_per_cell
      = LightCullingConfig::kMaxCulledLightsPerCell;
    bindings.directional_light_count = selection.directional_light_count();
    bindings.local_light_count = selection.local_light_count();
    bindings.has_directional_light
      = selection.directional_light.has_value() ? 1U : 0U;
    if (selection.directional_light.has_value()) {
      bindings.directional = DirectionalLightForwardData::FromSelection(
        *selection.directional_light);
    }
    return bindings;
  }

} // namespace

LightGridBuilder::LightGridBuilder(Renderer& renderer)
  : renderer_(renderer)
{
}

auto LightGridBuilder::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  last_build_stats_.frame_sequence = sequence;
  last_build_stats_.frame_slot = slot;
  last_build_stats_.build_count = 0U;
  last_build_stats_.published_view_count = 0U;
  last_build_stats_.directional_light_count = 0U;
  last_build_stats_.local_light_count = 0U;
  last_build_stats_.selection_epoch = 0U;
}

auto LightGridBuilder::Build(const FrameLightingInputs& inputs)
  -> BuiltLightGridFrame
{
  std::ignore = renderer_;

  auto built = BuiltLightGridFrame {};
  if (inputs.frame_light_set == nullptr) {
    return built;
  }

  built.selection_epoch = inputs.frame_light_set->selection_epoch;
  built.local_light_records.reserve(
    inputs.frame_light_set->local_lights.size());
  for (const auto& light : inputs.frame_light_set->local_lights) {
    built.local_light_records.push_back(
      ForwardLocalLightRecord::FromSelection(light));
  }
  if (inputs.frame_light_set->directional_light.has_value()) {
    built.directional_light_indices.push_back(0U);
  }

  built.per_view.reserve(inputs.active_views.size());
  for (const auto& view_input : inputs.active_views) {
    const auto metadata = BuildGridMetadata(view_input);
    built.per_view.push_back(BuiltLightGridView {
      .view_id = view_input.view_id,
      .bindings = BuildBindings(metadata, *inputs.frame_light_set),
      .metadata = metadata,
    });
  }

  last_build_stats_.build_count = 1U;
  last_build_stats_.published_view_count
    = static_cast<std::uint32_t>(built.per_view.size());
  last_build_stats_.directional_light_count
    = inputs.frame_light_set->directional_light_count();
  last_build_stats_.local_light_count
    = inputs.frame_light_set->local_light_count();
  last_build_stats_.selection_epoch = built.selection_epoch;
  return built;
}

} // namespace oxygen::vortex::lighting::internal
