//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <vector>

#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowProjection.h>
#include <Oxygen/Vortex/Shadows/Internal/PointShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterCulling.h>
#include <Oxygen/Vortex/Shadows/Internal/SpotShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Passes/CascadeShadowPass.h>
#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>
#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex::shadows {

namespace {

  constexpr float kLocalShadowSlopeDepthBiasScale = 3.0F;

  constexpr auto kPointShadowFaceDirections = std::array {
    glm::vec3 { 1.0F, 0.0F, 0.0F },
    glm::vec3 { -1.0F, 0.0F, 0.0F },
    glm::vec3 { 0.0F, 1.0F, 0.0F },
    glm::vec3 { 0.0F, -1.0F, 0.0F },
    glm::vec3 { 0.0F, 0.0F, 1.0F },
    glm::vec3 { 0.0F, 0.0F, -1.0F },
  };

} // namespace

CascadeShadowPass::CascadeShadowPass(Renderer& renderer)
  : renderer_(renderer)
  , cascade_setup_(std::make_unique<internal::CascadeShadowSetup>())
  , spot_setup_(std::make_unique<internal::SpotShadowSetup>())
  , point_setup_(std::make_unique<internal::PointShadowSetup>())
  , allocator_(
      std::make_unique<internal::ConventionalShadowTargetAllocator>(renderer))
  , shadow_caster_culling_(std::make_unique<internal::ShadowCasterCulling>())
  , depth_pass_(std::make_unique<ShadowDepthPass>(renderer))
{
}

CascadeShadowPass::~CascadeShadowPass() = default;

auto CascadeShadowPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  allocator_->OnFrameStart(sequence);
  depth_pass_->OnFrameStart(sequence, slot);
}

auto CascadeShadowPass::RetainDirectionalSources(
  const std::span<const FrameDirectionalLightSelection> lights) -> void
{
  auto selected = std::vector<LightSelectionIndex> {};
  for (const auto& [index, light] : std::views::enumerate(lights)) {
    if ((light.shadow_flags & kDirectionalLightShadowFlagCastsShadows) != 0U
      && light.cascade_count != 0U) {
      selected.emplace_back(static_cast<std::uint32_t>(index));
    }
  }
  allocator_->RetainDirectionalSurfaces(selected);
}

auto CascadeShadowPass::RenderDirectionalView(
  const PreparedViewShadowInput& view_input,
  const FrameDirectionalLightSelection& directional_light,
  const LightSelectionIndex selection_index) -> ViewShadowPassState
{
  auto state = ViewShadowPassState {};
  const auto allocation = allocator_->AcquireDirectionalSurface(
    view_input.view_id, selection_index, directional_light.cascade_count,
    directional_light.shadow_resolution_hint);
  state.frame_data = cascade_setup_->BuildDirectionalFrameData(
    view_input, directional_light, allocation);
  state.shadow_surface = allocation.surface;

  if (view_input.prepared_scene != nullptr) {
    shadow_caster_culling_->BuildDrawCommands(*view_input.prepared_scene);
    state.shadow_caster_draw_count = static_cast<std::uint32_t>(
      shadow_caster_culling_->GetDrawCommands().size());
  }

  const auto render_state = allocation.surface != nullptr
    ? depth_pass_->Record(view_input, allocation.surface, state.frame_data,
        directional_light.direction, shadow_caster_culling_->GetDrawCommands())
    : ShadowDepthPass::RenderState {};
  state.rendered_cascade_count = render_state.rendered_cascade_count;
  state.rendered_draw_count = render_state.rendered_draw_count;
  return state;
}

auto CascadeShadowPass::RenderSpotView(
  const PreparedViewShadowInput& view_input,
  const std::span<const FrameLocalLightSelection> local_lights)
  -> ViewSpotShadowPassState
{
  auto state = ViewSpotShadowPassState {};
  auto shadowed_spot_count = 0U;
  auto resolution_hint = scene::ShadowResolutionHint::kLow;
  for (const auto& light : local_lights) {
    if (!internal::UsesCubeLocalShadow(light)
      && internal::HasLocalShadowInfluence(light)) {
      ++shadowed_spot_count;
      resolution_hint
        = (std::max)(resolution_hint, light.shadow_resolution_hint);
    }
  }
  if (shadowed_spot_count == 0U) {
    return state;
  }

  const auto allocation = allocator_->AcquireSpotSurface(
    view_input.view_id, shadowed_spot_count, resolution_hint);
  state.records
    = spot_setup_->BuildSpotRecords(view_input, local_lights, allocation);
  state.shadow_surface = allocation.surface;

  if (view_input.prepared_scene != nullptr) {
    shadow_caster_culling_->BuildDrawCommands(*view_input.prepared_scene);
    state.shadow_caster_draw_count = static_cast<std::uint32_t>(
      shadow_caster_culling_->GetDrawCommands().size());
  }

  auto depth_slices = std::vector<ShadowDepthPass::DepthSlice> {};
  depth_slices.reserve(state.records.size());
  for (const auto& spot : state.records) {
    // For the conventional perspective projection, clip W is axial distance.
    // Its XYZ coefficients give the normalized light-forward vector.
    const auto direction = glm::vec3(glm::row(spot.light_view_projection, 3));
    depth_slices.push_back(ShadowDepthPass::DepthSlice {
      .light_view_projection = spot.light_view_projection,
      .shadow_bias_parameters = glm::vec4(spot.depth_bias,
        spot.depth_bias * kLocalShadowSlopeDepthBiasScale, 1.0F, 0.0F),
      .light_direction_to_source = glm::vec4(direction, 0.0F),
      .light_position_and_inv_range
      = glm::vec4(spot.shadow_origin_ws, 1.0F / spot.far_plane_m),
      .target_slice = spot.array_layer.get(),
    });
  }

  const auto render_state = allocation.surface != nullptr
    ? depth_pass_->RecordSlices(view_input, allocation.surface,
        std::span(depth_slices), shadow_caster_culling_->GetDrawCommands())
    : ShadowDepthPass::RenderState {};
  state.rendered_shadow_count = render_state.rendered_cascade_count;
  state.rendered_draw_count = render_state.rendered_draw_count;
  return state;
}

auto CascadeShadowPass::RenderPointView(
  const PreparedViewShadowInput& view_input,
  const std::span<const FrameLocalLightSelection> local_lights)
  -> ViewPointShadowPassState
{
  auto state = ViewPointShadowPassState {};
  auto shadowed_point_count = 0U;
  auto resolution_hint = scene::ShadowResolutionHint::kLow;
  for (const auto& light : local_lights) {
    if (internal::UsesCubeLocalShadow(light)
      && internal::HasLocalShadowInfluence(light)) {
      ++shadowed_point_count;
      resolution_hint
        = (std::max)(resolution_hint, light.shadow_resolution_hint);
    }
  }
  if (shadowed_point_count == 0U) {
    return state;
  }

  const auto allocation = allocator_->AcquirePointSurface(
    view_input.view_id, shadowed_point_count, resolution_hint);
  state.records
    = point_setup_->BuildPointRecords(view_input, local_lights, allocation);
  state.shadow_surface = allocation.surface;

  if (view_input.prepared_scene != nullptr) {
    shadow_caster_culling_->BuildDrawCommands(*view_input.prepared_scene);
    state.shadow_caster_draw_count = static_cast<std::uint32_t>(
      shadow_caster_culling_->GetDrawCommands().size());
  }

  auto depth_slices = std::vector<ShadowDepthPass::DepthSlice> {};
  depth_slices.reserve(
    state.records.size() * CubeLocalShadowRecord::kFaceCount);
  for (const auto& point : state.records) {
    for (std::uint32_t face_index = 0U;
      face_index < CubeLocalShadowRecord::kFaceCount; ++face_index) {
      depth_slices.push_back(ShadowDepthPass::DepthSlice {
        .light_view_projection
        = point.face_light_view_projection.at(face_index),
        .shadow_bias_parameters = glm::vec4(point.depth_bias,
          point.depth_bias * kLocalShadowSlopeDepthBiasScale, 1.0F, 0.0F),
        .light_direction_to_source
        = glm::vec4(kPointShadowFaceDirections.at(face_index), 0.0F),
        .light_position_and_inv_range
        = glm::vec4(point.shadow_origin_ws, 1.0F / point.far_plane_m),
        .target_slice = point.first_array_layer.get() + face_index,
      });
    }
  }

  const auto render_state = allocation.surface != nullptr
    ? depth_pass_->RecordSlices(view_input, allocation.surface,
        std::span(depth_slices), shadow_caster_culling_->GetDrawCommands())
    : ShadowDepthPass::RenderState {};
  state.rendered_shadow_count = render_state.rendered_cascade_count / 6U;
  state.rendered_draw_count = render_state.rendered_draw_count;
  return state;
}

} // namespace oxygen::vortex::shadows
