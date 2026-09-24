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

  using internal::kLocalShadowSlopeDepthBiasScale;

  auto PointPcfSampleCount(const ShadowQualityTier tier) -> std::uint32_t
  {
    switch (tier) {
    case ShadowQualityTier::kLow:
      return 1U;
    case ShadowQualityTier::kMedium:
      return 5U;
    case ShadowQualityTier::kHigh:
    case ShadowQualityTier::kUltra:
      return 29U;
    }
    return 29U;
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

auto CascadeShadowPass::InspectLocalSharing() const -> ShadowSharingDiagnostics
{
  auto result = allocator_->InspectLocalSharing();
  for (const auto& [id, prepared] : prepared_locals_) {
    result.prepared_request_bytes
      += prepared.requests.capacity() * sizeof(internal::LocalShadowRequest);
    for (const auto& request : prepared.requests) {
      result.prepared_request_bytes += request.content.casters.capacity()
          * sizeof(std::shared_ptr<const ShadowCasterRecord>)
        + request.caster_hash_scratch.capacity() * sizeof(std::uint64_t);
    }
  }
  return result;
}

auto CascadeShadowPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  std::erase_if(prepared_locals_,
    [&](const auto& entry) { return entry.second.frame != current_sequence_; });
  allocator_->OnFrameStart(sequence, slot);
  depth_pass_->OnFrameStart(sequence, slot);
  std::erase_if(quality_history_, [this](const auto& entry) {
    return entry.second.last_used != current_sequence_;
  });
  current_sequence_ = sequence;
}

auto CascadeShadowPass::ReconcileLocalFamily(
  std::span<const PreparedViewShadowInput> views) -> void
{
  family_requests_.clear();
  for (const auto& view : views) {
    if (const auto found = prepared_locals_.find(view.view_id);
      found != prepared_locals_.end()) {
      for (const auto& request : found->second.requests) {
        family_requests_.push_back(&request);
      }
    }
  }
  allocator_->PrepareLocalFamily(family_requests_);
}

auto CascadeShadowPass::PrepareLocalRequests(
  std::span<const PreparedViewShadowInput> views,
  const FrameLightSelection* selection) -> void
{
  const auto lights = selection
    ? std::span<const FrameLocalLightSelection>(selection->local_lights)
    : std::span<const FrameLocalLightSelection> {};
  const auto epoch = selection ? selection->selection_epoch : 0;
  for (const auto& view : views) {
    auto& prepared = prepared_locals_[view.view_id];
    const auto revision
      = view.prepared_scene ? view.prepared_scene->preparation_revision : 0;
    if (revision != 0 && epoch != 0 && prepared.frame == current_sequence_
      && prepared.preparation_revision == revision
      && prepared.selection_epoch == epoch
      && prepared.scene_generation == view.scene_generation) {
      continue;
    }
    auto& history = PrepareQualityHistory(view, lights);
    size_t count = 0;
    for (size_t index = 0; index < lights.size(); ++index) {
      const auto& light = lights[index];
      if (!internal::HasLocalShadowInfluence(light, view.resolved_view.get())) {
        continue;
      }
      const auto old = history.resolutions.find(light.source_node);
      const auto quality
        = internal::EvaluateLocalShadowQuality(light, view.resolved_view.get(),
          allocator_->ResolveLocalResolution(light.shadow_resolution_hint),
          old != history.resolutions.end() ? old->second : 0U);
      if (light.source_node.IsValid()) {
        history.resolutions.insert_or_assign(
          light.source_node, quality.resolution);
      }
      if (count == prepared.requests.size()) {
        prepared.requests.emplace_back();
      }
      auto& request = prepared.requests[count++];
      if (quality.resolution != 0) {
        internal::PrepareLocalShadowRequest(view, light,
          LightSelectionIndex { static_cast<uint32_t>(index) },
          quality.resolution, quality.strength, request);
      } else {
        request.selection
          = LightSelectionIndex { static_cast<uint32_t>(index) };
        request.content.resolution = 0;
        request.content.casters.clear();
        request.content.reusable = false;
        request.content.contract = internal::UsesCubeLocalShadow(light)
          ? internal::LocalShadowDepthContract::kCubeRasterReversedV1
          : internal::LocalShadowDepthContract::kProjectedLinearReversedV1;
      }
    }
    prepared.requests.resize(count);
    prepared.frame = current_sequence_;
    prepared.preparation_revision = revision;
    prepared.selection_epoch = epoch;
    prepared.scene_generation = view.scene_generation;
  }
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
  std::span<const FrameLocalLightSelection> /*lights*/)
  -> ViewSpotShadowPassState
{
  ViewSpotShadowPassState state;
  for (const auto& request : prepared_locals_.at(view_input.view_id).requests) {
    if (request.IsCube()) {
      continue;
    }
    if (request.content.resolution == 0) {
      state.quality_omissions.push_back(request.selection);
      continue;
    }
    auto acquired = allocator_->AcquireLocalMap(view_input.view_id, request);
    const auto& slot = *acquired.owner->version->slot;
    if (!acquired.reused) {
      const auto& key = request.content;
      const std::array slices { ShadowDepthPass::DepthSlice {
        .light_view_projection = key.matrices[0],
        .shadow_bias_parameters = key.bias,
        .light_direction_to_source = key.direction,
        .light_position_and_inv_range = key.position_and_inverse_range,
        .target_slice = slot.offset } };
      const auto rendered = depth_pass_->RecordSlices(
        view_input, slot.backing->texture, slices, acquired.owner->version);
      if (!rendered.recording_succeeded) {
        throw std::runtime_error("Spot shadow submission failed");
      }
      state.rendered_shadow_count += rendered.rendered_cascade_count;
      state.rendered_draw_count += rendered.rendered_draw_count;
      state.shadow_caster_draw_count = rendered.shadow_caster_draw_count;
    }
    acquired.Commit();
    auto record = std::get<ProjectedLocalShadowRecord>(request.projection);
    record.surface_srv = slot.backing->srv;
    record.array_layer = ShadowArrayLayer { slot.offset };
    record.selection_index = request.selection;
    record.shadow_strength = request.strength;
    state.records.push_back(record);
    state.local_maps.push_back(acquired.owner);
    if (std::ranges::find(state.shadow_surfaces, slot.backing->texture)
      == state.shadow_surfaces.end()) {
      state.shadow_surfaces.push_back(slot.backing->texture);
    }
  }
  return state;
}

auto CascadeShadowPass::RenderPointView(
  const PreparedViewShadowInput& view_input,
  std::span<const FrameLocalLightSelection> /*lights*/)
  -> ViewPointShadowPassState
{
  ViewPointShadowPassState state;
  for (const auto& request : prepared_locals_.at(view_input.view_id).requests) {
    if (!request.IsCube()) {
      continue;
    }
    if (request.content.resolution == 0) {
      state.quality_omissions.push_back(request.selection);
      continue;
    }
    auto acquired = allocator_->AcquireLocalMap(view_input.view_id, request);
    const auto& slot = *acquired.owner->version->slot;
    if (!acquired.reused) {
      std::array<ShadowDepthPass::DepthSlice, 6> slices;
      for (uint32_t face = 0; face < 6; ++face) {
        slices[face]
          = { .light_view_projection = request.content.matrices[face],
              .light_position_and_inv_range
              = request.content.position_and_inverse_range,
              .target_slice = slot.offset * 6U + face };
      }
      const auto rendered = depth_pass_->RecordSlices(
        view_input, slot.backing->texture, slices, acquired.owner->version);
      if (!rendered.recording_succeeded) {
        throw std::runtime_error("Point shadow submission failed");
      }
      state.rendered_shadow_count += rendered.rendered_cascade_count / 6U;
      state.rendered_draw_count += rendered.rendered_draw_count;
      state.shadow_caster_draw_count = rendered.shadow_caster_draw_count;
    }
    acquired.Commit();
    auto record = std::get<CubeLocalShadowRecord>(request.projection);
    record.surface_srv = slot.backing->srv;
    record.first_array_layer = ShadowArrayLayer { slot.offset * 6U };
    record.selection_index = request.selection;
    record.shadow_strength = request.strength;
    record.pcf_sample_count
      = PointPcfSampleCount(renderer_.GetShadowQualityTier());
    state.records.push_back(record);
    state.local_maps.push_back(acquired.owner);
    if (std::ranges::find(state.shadow_surfaces, slot.backing->texture)
      == state.shadow_surfaces.end()) {
      state.shadow_surfaces.push_back(slot.backing->texture);
    }
  }
  return state;
}
} // namespace oxygen::vortex::shadows
