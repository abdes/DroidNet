//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowProjection.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowQuality.h>
#include <Oxygen/Vortex/Shadows/Internal/PointShadowSetup.h>
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

  auto BuildLocalBuckets(const PreparedViewShadowInput& view,
    const std::span<const FrameLocalLightSelection> lights, const bool cube,
    internal::ConventionalShadowTargetAllocator& allocator,
    std::unordered_map<scene::NodeHandle, std::uint32_t>& previous,
    std::vector<LightSelectionIndex>& omissions, std::span<float> strengths)
    -> std::map<std::pair<std::uint32_t, std::uint32_t>,
      std::vector<internal::ConventionalShadowTargetAllocator::LocalSelection>>
  {
    auto buckets = std::map<std::pair<std::uint32_t, std::uint32_t>,
      std::vector<
        internal::ConventionalShadowTargetAllocator::LocalSelection>> {};
    for (const auto& [index, light] : std::views::enumerate(lights)) {
      if (internal::UsesCubeLocalShadow(light) == cube
        && internal::HasLocalShadowInfluence(light, view.resolved_view.get())) {
        const auto old = previous.find(light.source_node);
        const auto quality = internal::EvaluateLocalShadowQuality(light,
          view.resolved_view.get(),
          allocator.ResolveLocalResolution(light.shadow_resolution_hint),
          old != previous.end() ? old->second : 0U);
        if (light.source_node.IsValid()) {
          previous.insert_or_assign(light.source_node, quality.resolution);
        }
        strengths[index] = quality.strength;
        if (quality.resolution == 0U) {
          omissions.emplace_back(static_cast<std::uint32_t>(index));
          continue;
        }
        const auto slot = allocator.AcquireLocalSlot(
          view.view_id, light.source_node, quality.resolution, cube);
        buckets[{ quality.resolution, slot.chunk }].push_back(
          { LightSelectionIndex { static_cast<std::uint32_t>(index) }, slot });
      }
    }
    return buckets;
  }

} // namespace

CascadeShadowPass::CascadeShadowPass(Renderer& renderer)
  : renderer_(renderer)
  , cascade_setup_(std::make_unique<internal::CascadeShadowSetup>())
  , spot_setup_(std::make_unique<internal::SpotShadowSetup>())
  , point_setup_(std::make_unique<internal::PointShadowSetup>())
  , allocator_(
      std::make_unique<internal::ConventionalShadowTargetAllocator>(renderer))
  , depth_pass_(std::make_unique<ShadowDepthPass>(renderer))
{
}

CascadeShadowPass::~CascadeShadowPass() = default;

auto CascadeShadowPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  allocator_->OnFrameStart(sequence, slot);
  depth_pass_->OnFrameStart(sequence, slot);
  std::erase_if(quality_history_, [this](const auto& entry) {
    return entry.second.last_used != current_sequence_;
  });
  current_sequence_ = sequence;
}

auto CascadeShadowPass::PrepareQualityHistory(
  const PreparedViewShadowInput& view,
  const std::span<const FrameLocalLightSelection> lights) -> ViewQuality&
{
  auto& history = quality_history_[view.view_id];
  if (history.scene_generation != view.scene_generation) {
    history = {};
    history.scene_generation = view.scene_generation;
  }
  history.last_used = current_sequence_;
  auto active = std::unordered_set<scene::NodeHandle> {};
  for (const auto& light : lights) {
    active.insert(light.source_node);
  }
  std::erase_if(history.resolutions,
    [&](const auto& entry) { return !active.contains(entry.first); });
  return history;
}

auto CascadeShadowPass::RetainLocalSources(const PreparedViewShadowInput& view,
  const std::span<const FrameLocalLightSelection> lights) -> void
{
  allocator_->RetainLocalSources(view.view_id, view.scene_generation, lights);
}

auto CascadeShadowPass::RetainDirectionalSources(
  const std::span<const FrameDirectionalLightSelection> lights) -> void
{
  auto selected = std::vector<LightSelectionIndex> {};
  for (const auto& [index, light] : std::views::enumerate(lights)) {
    if (internal::HasDirectionalShadowInfluence(light)
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

  const auto render_state = allocation.surface != nullptr
    ? depth_pass_->Record(view_input, allocation.surface, state.frame_data,
        directional_light.direction)
    : ShadowDepthPass::RenderState {};
  state.shadow_caster_draw_count = render_state.shadow_caster_draw_count;
  if (allocation.surface && !render_state.recording_succeeded) {
    throw std::runtime_error("Directional shadow submission failed");
  }
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
  auto strengths = std::vector<float>(local_lights.size(), 1.0F);
  auto& history = PrepareQualityHistory(view_input, local_lights);
  for (const auto& [bucket, selected] :
    BuildLocalBuckets(view_input, local_lights, false, *allocator_,
      history.resolutions, state.quality_omissions, strengths)) {
    const auto [resolution, chunk] = bucket;
    auto required_count = 0U;
    for (const auto& selection : selected) {
      required_count = (std::max)(required_count, selection.slot.offset + 1U);
    }
    const auto allocation = allocator_->AcquireSpotSurface(
      view_input.view_id, required_count, resolution, chunk);
    auto records = spot_setup_->BuildSpotRecords(
      view_input, local_lights, selected, allocation);
    for (auto& record : records) {
      record.shadow_strength = strengths[record.selection_index.get()];
    }
    state.shadow_surfaces.push_back(allocation.surface);

    for (std::size_t light_index = 0U; light_index < records.size();
      ++light_index) {
      const auto& spot = records[light_index];
      auto depth_slices = std::array<ShadowDepthPass::DepthSlice, 1> {};
      // For the conventional perspective projection, clip W is axial distance.
      // Its XYZ coefficients give the normalized light-forward vector.
      const auto direction = glm::vec3(glm::row(spot.light_view_projection, 3));
      depth_slices[0] = ShadowDepthPass::DepthSlice {
        .light_view_projection = spot.light_view_projection,
        .shadow_bias_parameters = glm::vec4(spot.depth_bias,
          spot.depth_bias * kLocalShadowSlopeDepthBiasScale, 1.0F, 0.0F),
        .light_direction_to_source = glm::vec4(direction, 0.0F),
        .light_position_and_inv_range
        = glm::vec4(spot.shadow_origin_ws, 1.0F / spot.far_plane_m),
        .target_slice = spot.array_layer.get(),
        .light_source = local_lights[spot.selection_index.get()].source_node,
        .slot_generation = selected[light_index].slot.handle.generation.get(),
      };
      const auto render_state = allocation.surface != nullptr
        ? depth_pass_->RecordSlices(
            view_input, allocation.surface, std::span(depth_slices), true)
        : ShadowDepthPass::RenderState {};
      state.shadow_caster_draw_count = render_state.shadow_caster_draw_count;
      state.rendered_shadow_count += render_state.rendered_cascade_count;
      if (allocation.surface && !render_state.recording_succeeded) {
        throw std::runtime_error("Spot shadow submission failed");
      }
      state.rendered_draw_count += render_state.rendered_draw_count;
    }
    state.records.insert(state.records.end(), records.begin(), records.end());
  }
  return state;
}

auto CascadeShadowPass::RenderPointView(
  const PreparedViewShadowInput& view_input,
  const std::span<const FrameLocalLightSelection> local_lights)
  -> ViewPointShadowPassState
{
  auto state = ViewPointShadowPassState {};
  auto strengths = std::vector<float>(local_lights.size(), 1.0F);
  auto& history = PrepareQualityHistory(view_input, local_lights);
  for (const auto& [bucket, selected] :
    BuildLocalBuckets(view_input, local_lights, true, *allocator_,
      history.resolutions, state.quality_omissions, strengths)) {
    const auto [resolution, chunk] = bucket;
    auto required_count = 0U;
    for (const auto& selection : selected) {
      required_count = (std::max)(required_count, selection.slot.offset + 1U);
    }
    const auto allocation = allocator_->AcquirePointSurface(
      view_input.view_id, required_count, resolution, chunk);
    auto records = point_setup_->BuildPointRecords(
      view_input, local_lights, selected, allocation);
    for (auto& record : records) {
      record.shadow_strength = strengths[record.selection_index.get()];
    }
    state.shadow_surfaces.push_back(allocation.surface);

    for (std::size_t light_index = 0U; light_index < records.size();
      ++light_index) {
      const auto& point = records[light_index];
      auto depth_slices = std::array<ShadowDepthPass::DepthSlice, 6> {};
      for (std::uint32_t face_index = 0U;
        face_index < CubeLocalShadowRecord::kFaceCount; ++face_index) {
        depth_slices[face_index] = ShadowDepthPass::DepthSlice {
          .light_view_projection
          = point.face_light_view_projection.at(face_index),
          .shadow_bias_parameters = glm::vec4(point.depth_bias,
            point.depth_bias * kLocalShadowSlopeDepthBiasScale, 1.0F, 0.0F),
          .light_direction_to_source
          = glm::vec4(kPointShadowFaceDirections.at(face_index), 0.0F),
          .light_position_and_inv_range
          = glm::vec4(point.shadow_origin_ws, 1.0F / point.far_plane_m),
          .target_slice = point.first_array_layer.get() + face_index,
          .light_source = local_lights[point.selection_index.get()].source_node,
          .slot_generation = selected[light_index].slot.handle.generation.get(),
        };
      }
      const auto render_state = allocation.surface != nullptr
        ? depth_pass_->RecordSlices(
            view_input, allocation.surface, std::span(depth_slices), true)
        : ShadowDepthPass::RenderState {};
      state.shadow_caster_draw_count = render_state.shadow_caster_draw_count;
      state.rendered_shadow_count += render_state.rendered_cascade_count / 6U;
      if (allocation.surface && !render_state.recording_succeeded) {
        throw std::runtime_error("Point shadow submission failed");
      }
      state.rendered_draw_count += render_state.rendered_draw_count;
    }
    state.records.insert(state.records.end(), records.begin(), records.end());
  }
  return state;
}

} // namespace oxygen::vortex::shadows
