//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>

#include <memory>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLightState.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereRenderer.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/FogRenderer.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Environment/Internal/SkyRenderer.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Environment/Passes/LocalFogVolumeComposePass.h>
#include <Oxygen/Vortex/Environment/Passes/LocalFogVolumeTiledCullingPass.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex {

EnvironmentLightingService::EnvironmentLightingService(Renderer& renderer)
  : renderer_(renderer)
  , sky_(std::make_unique<environment::SkyRenderer>(renderer))
  , atmosphere_(std::make_unique<environment::AtmosphereRenderer>(renderer))
  , fog_(std::make_unique<environment::FogRenderer>(renderer))
  , atmosphere_light_state_(
      std::make_unique<environment::internal::AtmosphereLightState>())
  , atmosphere_state_(
      std::make_unique<environment::internal::AtmosphereState>())
  , local_fog_state_(std::make_unique<environment::internal::LocalFogVolumeState>(renderer))
  , local_fog_tiled_culling_(
      std::make_unique<environment::LocalFogVolumeTiledCullingPass>(renderer))
  , local_fog_compose_(
      std::make_unique<environment::LocalFogVolumeComposePass>(renderer))
  , ibl_(std::make_unique<environment::internal::IblProcessor>(renderer))
{
}

EnvironmentLightingService::~EnvironmentLightingService() = default;

auto EnvironmentLightingService::EnsurePublishResources() -> bool
{
  if (bindings_publisher_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  bindings_publisher_ = std::make_unique<
    internal::PerViewStructuredPublisher<EnvironmentFrameBindings>>(
    observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
    observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
    "EnvironmentFrameBindings");
  return true;
}

auto EnvironmentLightingService::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  published_views_.clear();
  last_probe_refresh_state_ = {
    .frame_sequence = sequence,
    .frame_slot = slot,
    .valid = probe_state_.valid,
    .probe_revision = probe_state_.probes.probe_revision,
  };
  last_publication_state_ = {
    .frame_sequence = sequence,
    .frame_slot = slot,
    .probe_revision = probe_state_.probes.probe_revision,
  };
  last_stage14_state_ = {};
  last_stage15_state_ = {};
  if (local_fog_state_ != nullptr) {
    local_fog_state_->OnFrameStart(sequence, slot);
  }
  if (EnsurePublishResources()) {
    bindings_publisher_->OnFrameStart(sequence, slot);
  }
}

auto EnvironmentLightingService::RefreshPersistentProbeState(
  const bool environment_source_changed) -> void
{
  const auto refreshed
    = ibl_->RefreshPersistentProbes(probe_state_, environment_source_changed);
  probe_state_ = refreshed.probe_state;
  last_probe_refresh_state_ = {
    .frame_sequence = current_sequence_,
    .frame_slot = current_slot_,
    .requested = refreshed.requested,
    .refreshed = refreshed.refreshed,
    .valid = refreshed.probe_state.valid,
    .probe_revision = refreshed.probe_state.probes.probe_revision,
  };
  last_publication_state_.probe_revision = probe_state_.probes.probe_revision;
}

auto EnvironmentLightingService::BuildBindings(
  const ShaderVisibleIndex environment_static_slot,
  const ShaderVisibleIndex environment_view_slot,
  const bool enable_ambient_bridge) const -> EnvironmentFrameBindings
{
  const auto& stable_state = atmosphere_state_->GetState();
  auto bindings = EnvironmentFrameBindings {
    .environment_static_slot = environment_static_slot,
    .environment_view_slot = environment_view_slot,
    .atmosphere_model_slot = kInvalidShaderVisibleIndex,
    .height_fog_model_slot = kInvalidShaderVisibleIndex,
    .sky_light_model_slot = kInvalidShaderVisibleIndex,
    .volumetric_fog_model_slot = kInvalidShaderVisibleIndex,
    .environment_view_products_slot = kInvalidShaderVisibleIndex,
    .contract_flags = 0U,
    .probes = probe_state_.probes,
    .evaluation = EnvironmentEvaluationParameters {},
    .ambient_bridge = EnvironmentAmbientBridgeBindings {},
  };

  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    bindings.contract_flags |= kEnvironmentContractFlagAtmosphereLight0Enabled;
  }
  if (stable_state.view_products.atmosphere_lights[1].enabled) {
    bindings.contract_flags |= kEnvironmentContractFlagAtmosphereLight1Enabled;
  }
  if (stable_state.conventional_shadow_authority_slot0_only) {
    bindings.contract_flags |= kEnvironmentContractFlagShadowAuthoritySlot0Only;
  }

  if (enable_ambient_bridge) {
    bindings.evaluation.flags |= kEnvironmentEvaluationFlagAmbientBridgeEligible;
  }
  if (enable_ambient_bridge && probe_state_.valid) {
    bindings.ambient_bridge.irradiance_map_srv = probe_state_.probes.irradiance_map_srv;
    bindings.ambient_bridge.ambient_intensity = bindings.evaluation.ambient_intensity;
    bindings.ambient_bridge.average_brightness
      = bindings.evaluation.average_brightness;
    bindings.ambient_bridge.blend_fraction = bindings.evaluation.blend_fraction;
    bindings.ambient_bridge.flags = kEnvironmentAmbientBridgeFlagEnabled;
  }

  return bindings;
}

auto EnvironmentLightingService::PublishEnvironmentBindings(RenderContext& ctx,
  const ShaderVisibleIndex environment_static_slot,
  const ShaderVisibleIndex environment_view_slot,
  const bool enable_ambient_bridge) -> ShaderVisibleIndex
{
  RefreshStableAtmosphereState(ctx.GetScene().get());
  if (ctx.current_view.view_id == kInvalidViewId || !EnsurePublishResources()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto bindings = BuildBindings(
    environment_static_slot, environment_view_slot, enable_ambient_bridge);
  const auto slot
    = bindings_publisher_->Publish(ctx.current_view.view_id, bindings);
  published_views_.insert_or_assign(
    ctx.current_view.view_id, PublishedView { .slot = slot, .bindings = bindings });

  last_publication_state_.frame_sequence = current_sequence_;
  last_publication_state_.frame_slot = current_slot_;
  last_publication_state_.published_view_count += 1U;
  last_publication_state_.probe_revision = bindings.probes.probe_revision;
  if (bindings.ambient_bridge.flags != 0U) {
    last_publication_state_.ambient_bridge_view_count += 1U;
  }

  return slot;
}

auto EnvironmentLightingService::RenderSkyAndFog(
  RenderContext& ctx, const SceneTextures& scene_textures) -> void
{
  RefreshStableAtmosphereState(ctx.GetScene().get());
  auto& local_fog_products = local_fog_state_->Prepare(ctx);
  LOG_F(INFO, "local_fog_volume_instance_count={}",
    local_fog_products.instance_count);
  const auto local_fog_culling_state
    = local_fog_tiled_culling_->Record(ctx, scene_textures, local_fog_products);
  const auto sky_state = sky_->Render(ctx, scene_textures);
  const auto atmosphere_state = atmosphere_->Render(ctx, scene_textures);
  const auto fog_state = fog_->Render(ctx, scene_textures);
  const auto local_fog_compose_state
    = local_fog_compose_->Record(ctx, scene_textures, local_fog_products);
  last_stage14_state_ = {
    .view_id = ctx.current_view.view_id,
    .requested = local_fog_products.prepared,
    .local_fog_requested = local_fog_culling_state.requested,
    .local_fog_executed = local_fog_culling_state.executed,
    .local_fog_hzb_consumed
    = local_fog_culling_state.consumed_published_screen_hzb,
    .local_fog_buffer_ready
    = local_fog_products.buffer_ready && local_fog_products.tile_data_ready,
    .local_fog_instance_count = local_fog_products.instance_count,
    .local_fog_dispatch_count_x = local_fog_culling_state.dispatch_count_x,
    .local_fog_dispatch_count_y = local_fog_culling_state.dispatch_count_y,
    .local_fog_dispatch_count_z = local_fog_culling_state.dispatch_count_z,
  };
  last_stage15_state_ = {
    .view_id = ctx.current_view.view_id,
    .requested = sky_state.requested || atmosphere_state.requested
      || fog_state.requested || local_fog_compose_state.requested,
    .sky_requested = sky_state.requested,
    .sky_executed = sky_state.executed,
    .sky_draw_count = sky_state.draw_count,
    .atmosphere_requested = atmosphere_state.requested,
    .atmosphere_executed = atmosphere_state.executed,
    .atmosphere_draw_count = atmosphere_state.draw_count,
    .fog_requested = fog_state.requested,
    .fog_executed = fog_state.executed,
    .fog_draw_count = fog_state.draw_count,
    .local_fog_requested = local_fog_compose_state.requested,
    .local_fog_executed = local_fog_compose_state.executed,
    .local_fog_draw_count = local_fog_compose_state.draw_count,
    .total_draw_count = sky_state.draw_count + atmosphere_state.draw_count
      + fog_state.draw_count + local_fog_compose_state.draw_count,
  };
}

auto EnvironmentLightingService::InspectBindings(const ViewId view_id) const
  -> const EnvironmentFrameBindings*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.bindings : nullptr;
}

auto EnvironmentLightingService::ResolveEnvironmentFrameSlot(const ViewId view_id) const
  -> ShaderVisibleIndex
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? it->second.slot
                                      : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
}

auto EnvironmentLightingService::InspectAtmosphereState() const noexcept
  -> const environment::internal::StableAtmosphereState&
{
  return atmosphere_state_->GetState();
}

auto EnvironmentLightingService::InspectAtmosphereLightState() const noexcept
  -> const environment::internal::ResolvedAtmosphereLightState&
{
  return atmosphere_light_state_->GetState();
}

auto EnvironmentLightingService::RefreshStableAtmosphereState(
  const scene::Scene* scene) -> void
{
  if (scene == nullptr) {
    atmosphere_light_state_->Reset();
    atmosphere_state_->Reset();
    return;
  }

  static_cast<void>(atmosphere_light_state_->Update(*scene));
  static_cast<void>(
    atmosphere_state_->Update(*scene, atmosphere_light_state_->GetState()));
}

} // namespace oxygen::vortex
