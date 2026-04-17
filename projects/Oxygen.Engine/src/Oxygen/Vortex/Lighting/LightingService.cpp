//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Lighting/LightingService.h>

#include <Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.h>
#include <Oxygen/Vortex/Lighting/Internal/ForwardLightPublisher.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/Passes/DeferredLightPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>

namespace oxygen::vortex {

LightingService::LightingService(Renderer& renderer)
  : renderer_(renderer)
  , light_grid_builder_(
      std::make_unique<lighting::internal::LightGridBuilder>(renderer))
  , publisher_(std::make_unique<lighting::internal::ForwardLightPublisher>(renderer))
  , deferred_packets_(
      std::make_unique<lighting::internal::DeferredLightPacketBuilder>())
  , deferred_pass_(std::make_unique<lighting::DeferredLightPass>(renderer))
{
}

LightingService::~LightingService() = default;

auto LightingService::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  last_grid_build_state_ = {
    .frame_sequence = sequence,
    .frame_slot = slot,
  };
  last_deferred_lighting_state_ = {};
  light_grid_builder_->OnFrameStart(sequence, slot);
  publisher_->OnFrameStart(sequence, slot);
}

auto LightingService::BuildLightGrid(const FrameLightingInputs& inputs) -> void
{
  const auto built = light_grid_builder_->Build(inputs);
  publisher_->Publish(built);

  const auto& stats = light_grid_builder_->GetLastBuildStats();
  last_grid_build_state_ = {
    .frame_sequence = stats.frame_sequence,
    .frame_slot = stats.frame_slot,
    .build_count = stats.build_count,
    .published_view_count = stats.published_view_count,
    .directional_light_count = stats.directional_light_count,
    .local_light_count = stats.local_light_count,
    .selection_epoch = stats.selection_epoch,
  };
}

auto LightingService::RenderDeferredLighting(RenderContext& ctx,
  const SceneTextures& scene_textures,
  const FrameLightSelection& frame_light_set) -> void
{
  const auto packets = deferred_packets_->Build(frame_light_set);
  const auto pass_state
    = deferred_pass_->Record(ctx, scene_textures, packets);
  last_deferred_lighting_state_ = {
    .consumed_packets = pass_state.consumed_packets,
    .accumulated_into_scene_color = pass_state.accumulated_into_scene_color,
    .used_service_owned_geometry = pass_state.used_service_owned_geometry,
    .used_outside_volume_local_lights
    = pass_state.used_outside_volume_local_lights,
    .used_camera_inside_local_lights
    = pass_state.used_camera_inside_local_lights,
    .used_non_perspective_local_lights
    = pass_state.used_non_perspective_local_lights,
    .directional_draw_count = pass_state.directional_draw_count,
    .point_light_count = pass_state.point_light_count,
    .spot_light_count = pass_state.spot_light_count,
    .local_light_count = pass_state.local_light_count,
    .outside_volume_local_light_count
    = pass_state.outside_volume_local_light_count,
    .camera_inside_local_light_count
    = pass_state.camera_inside_local_light_count,
    .local_light_draw_count = pass_state.local_light_draw_count,
    .non_perspective_local_light_count
    = pass_state.non_perspective_local_light_count,
    .selection_epoch = packets.selection_epoch,
  };
}

auto LightingService::InspectForwardLightBindings(const ViewId view_id) const
  -> const LightingFrameBindings*
{
  return publisher_->InspectBindings(view_id);
}

auto LightingService::ResolveLightingFrameSlot(const ViewId view_id) const
  -> ShaderVisibleIndex
{
  return publisher_->ResolveBindingSlot(view_id);
}

} // namespace oxygen::vortex
