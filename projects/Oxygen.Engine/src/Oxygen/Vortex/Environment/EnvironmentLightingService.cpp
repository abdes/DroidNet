//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>

#include <memory>

#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLightState.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereRenderer.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/FogRenderer.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Environment/Internal/SkyRenderer.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereCameraAerialPerspectivePass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereMultiScatteringLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/DistantSkyLightLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/LocalFogVolumeComposePass.h>
#include <Oxygen/Vortex/Environment/Passes/LocalFogVolumeTiledCullingPass.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
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
  , local_fog_state_(
      std::make_unique<environment::internal::LocalFogVolumeState>(renderer))
  , local_fog_tiled_culling_(
      std::make_unique<environment::LocalFogVolumeTiledCullingPass>(renderer))
  , local_fog_compose_(
      std::make_unique<environment::LocalFogVolumeComposePass>(renderer))
  , atmosphere_lut_cache_(
      std::make_unique<environment::internal::AtmosphereLutCache>(renderer))
  , transmittance_lut_pass_(
      std::make_unique<environment::AtmosphereTransmittanceLutPass>(renderer))
  , multi_scattering_lut_pass_(
      std::make_unique<environment::AtmosphereMultiScatteringLutPass>(renderer))
  , distant_sky_light_lut_pass_(
      std::make_unique<environment::DistantSkyLightLutPass>(renderer))
  , sky_view_lut_pass_(
      std::make_unique<environment::AtmosphereSkyViewLutPass>(renderer))
  , camera_aerial_perspective_pass_(
      std::make_unique<environment::AtmosphereCameraAerialPerspectivePass>(
        renderer))
  , ibl_(std::make_unique<environment::internal::IblProcessor>(renderer))
{
}

EnvironmentLightingService::~EnvironmentLightingService() = default;

auto EnvironmentLightingService::EnsurePublishResources() -> bool
{
  if (bindings_publisher_ != nullptr && view_data_publisher_ != nullptr
    && view_products_publisher_ != nullptr) {
    return true;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return false;
  }

  if (bindings_publisher_ == nullptr) {
    bindings_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<EnvironmentFrameBindings>>(
      observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
      observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
      "EnvironmentFrameBindings");
  }
  if (view_data_publisher_ == nullptr) {
    view_data_publisher_
      = std::make_unique<internal::PerViewStructuredPublisher<EnvironmentViewData>>(
        observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
        observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
        "EnvironmentViewData");
  }
  if (view_products_publisher_ == nullptr) {
    view_products_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<environment::EnvironmentViewProducts>>(
      observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
      observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
      "EnvironmentViewProducts");
  }
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
  last_view_product_generation_state_ = {};
  last_stage14_state_ = {};
  last_stage15_state_ = {};
  if (local_fog_state_ != nullptr) {
    local_fog_state_->OnFrameStart(sequence, slot);
  }
  if (atmosphere_lut_cache_ != nullptr) {
    atmosphere_lut_cache_->OnFrameStart(sequence, slot);
  }
  if (transmittance_lut_pass_ != nullptr) {
    transmittance_lut_pass_->OnFrameStart(sequence, slot);
  }
  if (multi_scattering_lut_pass_ != nullptr) {
    multi_scattering_lut_pass_->OnFrameStart(sequence, slot);
  }
  if (distant_sky_light_lut_pass_ != nullptr) {
    distant_sky_light_lut_pass_->OnFrameStart(sequence, slot);
  }
  if (sky_view_lut_pass_ != nullptr) {
    sky_view_lut_pass_->OnFrameStart(sequence, slot);
  }
  if (camera_aerial_perspective_pass_ != nullptr) {
    camera_aerial_perspective_pass_->OnFrameStart(sequence, slot);
  }
  if (EnsurePublishResources()) {
    bindings_publisher_->OnFrameStart(sequence, slot);
    view_data_publisher_->OnFrameStart(sequence, slot);
    view_products_publisher_->OnFrameStart(sequence, slot);
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
  const ShaderVisibleIndex environment_view_products_slot,
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
    .environment_view_products_slot = environment_view_products_slot,
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
    bindings.ambient_bridge.irradiance_map_srv
      = probe_state_.probes.irradiance_map_srv;
    bindings.ambient_bridge.ambient_intensity
      = bindings.evaluation.ambient_intensity;
    bindings.ambient_bridge.average_brightness
      = bindings.evaluation.average_brightness;
    bindings.ambient_bridge.blend_fraction = bindings.evaluation.blend_fraction;
    bindings.ambient_bridge.flags = kEnvironmentAmbientBridgeFlagEnabled;
  }

  return bindings;
}

auto EnvironmentLightingService::BuildEnvironmentViewData(
  const RenderContext& ctx) const -> EnvironmentViewData
{
  const auto& stable_state = atmosphere_state_->GetState();
  const auto& atmosphere = stable_state.view_products.atmosphere;

  auto planet_up_ws = engine::atmos::kDefaultPlanetUp;
  auto planet_center_ws = glm::vec3 { 0.0F, 0.0F, -atmosphere.planet_radius_m };
  auto camera_altitude_m = 0.0F;
  if (ctx.current_view.resolved_view != nullptr) {
    const auto camera_position = ctx.current_view.resolved_view->CameraPosition();
    camera_altitude_m = glm::dot(camera_position - planet_center_ws, planet_up_ws)
      - atmosphere.planet_radius_m;
  }

  auto sun_direction_ws = engine::atmos::kDefaultSunDirection;
  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    const auto& slot0 = stable_state.view_products.atmosphere_lights[0];
    const auto length_sq
      = glm::dot(slot0.direction_to_light_ws, slot0.direction_to_light_ws);
    if (length_sq > 1.0e-6F) {
      sun_direction_ws
        = glm::normalize(slot0.direction_to_light_ws);
    }
  }

  auto data = EnvironmentViewData {};
  data.flags = atmosphere.enabled ? 1U : 0U;
  data.transform_mode = static_cast<std::uint32_t>(atmosphere.transform_mode);
  data.atmosphere_light_count = stable_state.view_products.atmosphere_light_count;
  data.sky_view_lut_slice = 0.0F;
  data.planet_to_sun_cos_zenith
    = glm::dot(glm::normalize(planet_up_ws), glm::normalize(sun_direction_ws));
  data.aerial_perspective_distance_scale
    = atmosphere.aerial_perspective_distance_scale;
  data.aerial_scattering_strength = atmosphere.aerial_scattering_strength;
  data.planet_center_ws_pad = glm::vec4(planet_center_ws, 0.0F);
  data.planet_up_ws_camera_altitude_m
    = glm::vec4(planet_up_ws, camera_altitude_m);
  data.sky_luminance_factor_height_fog_contribution
    = glm::vec4(atmosphere.sky_luminance_factor_rgb,
      atmosphere.height_fog_contribution);
  data.sky_aerial_luminance_aerial_start_depth_m = glm::vec4(
    atmosphere.sky_and_aerial_perspective_luminance_factor_rgb,
    atmosphere.aerial_perspective_start_depth_m);
  data.trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass
    = glm::vec4(atmosphere.trace_sample_count_scale,
      atmosphere.transmittance_min_light_elevation_deg,
      atmosphere.holdout ? 1.0F : 0.0F,
      atmosphere.render_in_main_pass ? 1.0F : 0.0F);
  return data;
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

  const auto& stable_state = atmosphere_state_->GetState();
  atmosphere_lut_cache_->RefreshForState(stable_state);
  const auto view_data = BuildEnvironmentViewData(ctx);
  const auto resolved_environment_view_slot = environment_view_slot.IsValid()
    ? environment_view_slot
    : view_data_publisher_->Publish(ctx.current_view.view_id, view_data);

  const auto transmittance_state
    = transmittance_lut_pass_->Record(ctx, stable_state, *atmosphere_lut_cache_);
  const auto multi_scattering_state
    = multi_scattering_lut_pass_->Record(ctx, stable_state, *atmosphere_lut_cache_);
  const auto distant_sky_light_state
    = distant_sky_light_lut_pass_->Record(ctx, stable_state, *atmosphere_lut_cache_);

  auto products = stable_state.view_products;
  products.transmittance_lut_srv
    = atmosphere_lut_cache_->GetState().transmittance_lut_valid
      ? atmosphere_lut_cache_->GetState().transmittance_lut_srv
      : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
  products.multi_scattering_lut_srv
    = atmosphere_lut_cache_->GetState().multi_scattering_lut_valid
      ? atmosphere_lut_cache_->GetState().multi_scattering_lut_srv
      : ShaderVisibleIndex { kInvalidShaderVisibleIndex };
  products.distant_sky_light_lut_srv
    = atmosphere_lut_cache_->GetState().distant_sky_light_lut_valid
      ? atmosphere_lut_cache_->GetState().distant_sky_light_lut_srv
      : ShaderVisibleIndex { kInvalidShaderVisibleIndex };

  const auto sky_view_state
    = sky_view_lut_pass_->Record(ctx, view_data, stable_state);
  products.sky_view_lut_srv = sky_view_state.sky_view_lut_srv;

  const auto camera_aerial_state = camera_aerial_perspective_pass_->Record(
    ctx, view_data, stable_state, products.sky_view_lut_srv);
  products.camera_aerial_perspective_srv
    = camera_aerial_state.camera_aerial_perspective_srv;

  const auto environment_view_products_slot
    = view_products_publisher_->Publish(ctx.current_view.view_id, products);
  const auto bindings = BuildBindings(environment_static_slot,
    resolved_environment_view_slot, environment_view_products_slot,
    enable_ambient_bridge);
  const auto slot
    = bindings_publisher_->Publish(ctx.current_view.view_id, bindings);
  published_views_.insert_or_assign(
    ctx.current_view.view_id, PublishedView { .slot = slot, .bindings = bindings });

  last_publication_state_.frame_sequence = current_sequence_;
  last_publication_state_.frame_slot = current_slot_;
  last_publication_state_.published_view_count += 1U;
  last_publication_state_.published_environment_view_count += 1U;
  last_publication_state_.published_environment_view_products_count += 1U;
  last_publication_state_.probe_revision = bindings.probes.probe_revision;
  if (bindings.ambient_bridge.flags != 0U) {
    last_publication_state_.ambient_bridge_view_count += 1U;
  }

  const auto& cache_state = atmosphere_lut_cache_->GetState();
  const auto atmosphere_lut_cache_valid = atmosphere_lut_cache_->IsFullyValid();
  LOG_F(INFO, "atmosphere_lut_cache_valid={}",
    atmosphere_lut_cache_valid ? "true" : "false");
  LOG_F(INFO, "transmittance_lut_published={}",
    products.transmittance_lut_srv.IsValid() ? "true" : "false");
  LOG_F(INFO, "multi_scattering_lut_published={}",
    products.multi_scattering_lut_srv.IsValid() ? "true" : "false");
  LOG_F(INFO, "distant_sky_light_lut_published={}",
    products.distant_sky_light_lut_srv.IsValid() ? "true" : "false");
  LOG_F(INFO, "dual_atmosphere_lights_participating={}",
    cache_state.dual_light_participating ? "true" : "false");

  last_view_product_generation_state_ = {
    .view_id = ctx.current_view.view_id,
    .environment_view_published = resolved_environment_view_slot.IsValid(),
    .environment_view_slot = resolved_environment_view_slot,
    .atmosphere_lut_cache_valid = atmosphere_lut_cache_valid,
    .atmosphere_lut_cache_revision
    = static_cast<std::uint32_t>(cache_state.cache_revision),
    .atmosphere_light_count = stable_state.view_products.atmosphere_light_count,
    .dual_atmosphere_lights_participating = cache_state.dual_light_participating,
    .transmittance_lut_requested = transmittance_state.requested,
    .transmittance_lut_executed = transmittance_state.executed,
    .transmittance_lut_srv = products.transmittance_lut_srv,
    .transmittance_width = transmittance_state.width,
    .transmittance_height = transmittance_state.height,
    .transmittance_dispatch_count_x = transmittance_state.dispatch_count_x,
    .transmittance_dispatch_count_y = transmittance_state.dispatch_count_y,
    .transmittance_dispatch_count_z = transmittance_state.dispatch_count_z,
    .multi_scattering_lut_requested = multi_scattering_state.requested,
    .multi_scattering_lut_executed = multi_scattering_state.executed,
    .multi_scattering_lut_srv = products.multi_scattering_lut_srv,
    .multi_scattering_width = multi_scattering_state.width,
    .multi_scattering_height = multi_scattering_state.height,
    .multi_scattering_dispatch_count_x = multi_scattering_state.dispatch_count_x,
    .multi_scattering_dispatch_count_y = multi_scattering_state.dispatch_count_y,
    .multi_scattering_dispatch_count_z = multi_scattering_state.dispatch_count_z,
    .distant_sky_light_lut_requested = distant_sky_light_state.requested,
    .distant_sky_light_lut_executed = distant_sky_light_state.executed,
    .distant_sky_light_lut_srv = products.distant_sky_light_lut_srv,
    .distant_sky_light_dispatch_count_x
    = distant_sky_light_state.dispatch_count_x,
    .distant_sky_light_dispatch_count_y
    = distant_sky_light_state.dispatch_count_y,
    .distant_sky_light_dispatch_count_z
    = distant_sky_light_state.dispatch_count_z,
    .sky_view_lut_requested = sky_view_state.requested,
    .sky_view_lut_executed = sky_view_state.executed,
    .sky_view_lut_srv = sky_view_state.sky_view_lut_srv,
    .sky_view_width = sky_view_state.width,
    .sky_view_height = sky_view_state.height,
    .sky_view_dispatch_count_x = sky_view_state.dispatch_count_x,
    .sky_view_dispatch_count_y = sky_view_state.dispatch_count_y,
    .sky_view_dispatch_count_z = sky_view_state.dispatch_count_z,
    .camera_aerial_perspective_requested = camera_aerial_state.requested,
    .camera_aerial_perspective_executed = camera_aerial_state.executed,
    .camera_aerial_perspective_srv
    = camera_aerial_state.camera_aerial_perspective_srv,
    .camera_aerial_width = camera_aerial_state.width,
    .camera_aerial_height = camera_aerial_state.height,
    .camera_aerial_depth = camera_aerial_state.depth,
    .camera_aerial_dispatch_count_x = camera_aerial_state.dispatch_count_x,
    .camera_aerial_dispatch_count_y = camera_aerial_state.dispatch_count_y,
    .camera_aerial_dispatch_count_z = camera_aerial_state.dispatch_count_z,
    .environment_view_products_published
    = environment_view_products_slot.IsValid(),
    .environment_view_products_slot = environment_view_products_slot,
  };

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

auto EnvironmentLightingService::ResolveEnvironmentFrameSlot(
  const ViewId view_id) const -> ShaderVisibleIndex
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
