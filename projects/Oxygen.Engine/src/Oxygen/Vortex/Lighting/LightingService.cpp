//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <expected>
#include <memory>
#include <span>
#include <utility>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Profiling/CpuProfileScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Lighting/Internal/DeferredLightPacketBuilder.h>
#include <Oxygen/Vortex/Lighting/Internal/ForwardLightPublisher.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Lighting/Passes/DeferredLightPass.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>

namespace oxygen::vortex {

LightingService::LightingService(Renderer& renderer)
  : allocation_budget_(renderer.GetLightingAllocationBudget())
  , light_grid_builder_(
      std::make_unique<lighting::internal::LightGridBuilder>())
  , publisher_(
      std::make_unique<lighting::internal::ForwardLightPublisher>(renderer))
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
  prepared_lighting_.reset();
  light_grid_builder_->OnFrameStart(sequence, slot);
  publisher_->OnFrameStart(sequence, slot);
  deferred_pass_->OnFrameStart(sequence, slot);
}

auto LightingService::BuildLightGrid(const FrameLightingInputs& inputs)
  -> std::expected<void, LightingPreparationFailure>
{
  try {
    prepared_lighting_.reset();
    publisher_->InvalidateViews();
    last_grid_build_state_
      = { .frame_sequence = current_sequence_, .frame_slot = current_slot_ };
    auto built = light_grid_builder_->Build(inputs);
    if (!built) {
      return std::unexpected(built.error());
    }
    const auto before = allocation_budget_->Snapshot().rejected_requests;
    const auto publication = publisher_->Publish(*built);
    if (!publication) {
      const auto snapshot = allocation_budget_->Snapshot();
      if (snapshot.rejected_requests != before) {
        return std::unexpected(LightingPreparationFailure {
          .error = LightingPreparationError::kBudgetExceeded,
          .requested_bytes = snapshot.last_requested,
          .available_bytes = snapshot.last_available,
        });
      }
      return std::unexpected(publication.error());
    }
    prepared_lighting_
      = std::make_unique<lighting::internal::BuiltLightGridFrame>(
        std::move(*built));

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
    return {};
  } catch (const graphics::AllocationBudgetExceeded&) {
    const auto snapshot = allocation_budget_->Snapshot();
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kBudgetExceeded,
      .requested_bytes = snapshot.last_requested,
      .available_bytes = snapshot.last_available,
    });
  } catch (const std::exception&) {
    return std::unexpected(LightingPreparationFailure {
      .error = LightingPreparationError::kAllocationFailed,
    });
  }
}

auto LightingService::PublishShadowReferences(
  const ViewId view_id, const ShadowFrameData& shadows)
  -> std::expected<void, LightingPreparationFailure>
{
  // Cache the owning label; steady-state scope entry needs no label allocation.
  static const auto kProfile = profiling::CpuProfileScopeDesc {
    .label = "Vortex.Lighting.PublishShadowReferences",
    .category = profiling::ProfileCategory::kPass,
  };
  const auto profile = profiling::CpuProfileScope(kProfile);
  return publisher_->PublishShadowReferences(view_id, shadows);
}

auto LightingService::RenderDeferredLighting(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const SceneTextures& scene_textures,
  const FrameLightSelection& frame_light_set,
  const ShadowFrameData* shadow_data,
  std::span<const std::shared_ptr<graphics::Texture>>
    directional_shadow_surfaces,
  const graphics::Texture* spot_shadow_surface,
  const graphics::Texture* point_shadow_surface,
  const bool static_sky_light_available) -> bool
{
  if (!prepared_lighting_
    || prepared_lighting_->selection_epoch != frame_light_set.selection_epoch) {
    last_deferred_lighting_state_ = {};
    return false;
  }
  const auto packets
    = deferred_packets_->Build(frame_light_set, prepared_lighting_->evaluation);
  auto pass_state = lighting::DeferredLightPass::ExecutionState {};
  try {
    pass_state = deferred_pass_->Record(ctx, recorder, scene_textures, packets,
      shadow_data, directional_shadow_surfaces, spot_shadow_surface,
      point_shadow_surface, static_sky_light_available);
  } catch (const std::exception&) {
    last_deferred_lighting_state_ = {};
    return false;
  }
  if (!pass_state.recording_succeeded) {
    last_deferred_lighting_state_ = {};
    return false;
  }
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
    .consumed_static_sky_light_product
    = pass_state.consumed_static_sky_light_product,
    .directional_draw_count = pass_state.directional_draw_count,
    .static_sky_light_draw_count = pass_state.static_sky_light_draw_count,
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
    .consumed_directional_shadow_product
    = pass_state.consumed_directional_shadow_product,
    .directional_shadow_vsm_active = pass_state.directional_shadow_vsm_active,
    .directional_shadow_cascade_count
    = pass_state.directional_shadow_cascade_count,
    .directional_shadow_surface_srvs
    = pass_state.directional_shadow_surface_srvs,
    .consumed_spot_shadow_product = pass_state.consumed_spot_shadow_product,
    .spot_shadow_count = pass_state.spot_shadow_count,
    .spot_shadow_surface_srv = pass_state.spot_shadow_surface_srv,
    .consumed_point_shadow_product = pass_state.consumed_point_shadow_product,
    .point_shadow_count = pass_state.point_shadow_count,
    .point_shadow_surface_srv = pass_state.point_shadow_surface_srv,
    .selection_epoch = packets.selection_epoch,
  };
  return true;
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
