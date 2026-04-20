//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/EnvironmentLightingService.h>

#include <algorithm>
#include <cmath>
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

namespace {

auto ResolvePlanetCenterWs(
  const environment::AtmosphereModel& atmosphere) -> glm::vec3
{
  switch (atmosphere.transform_mode) {
  case environment::AtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin:
    return { 0.0F, 0.0F, -atmosphere.planet_radius_m };
  case environment::AtmosphereTransformMode::kPlanetTopAtComponentTransform:
    return atmosphere.planet_anchor_position_ws
      + glm::vec3 { 0.0F, 0.0F, -atmosphere.planet_radius_m };
  case environment::AtmosphereTransformMode::kPlanetCenterAtComponentTransform:
    return atmosphere.planet_anchor_position_ws;
  default:
    return { 0.0F, 0.0F, -atmosphere.planet_radius_m };
  }
}

auto SafeNormalizeOrFallback(
  const glm::vec3 value, const glm::vec3 fallback) -> glm::vec3
{
  const auto length_sq = glm::dot(value, value);
  if (length_sq <= 1.0e-8F) {
    return fallback;
  }
  return glm::normalize(value);
}

auto BuildSkyViewReferentialRows(
  const glm::vec3 up, const glm::vec3 forward_hint)
  -> std::array<glm::vec4, 3>
{
  const auto safe_up = SafeNormalizeOrFallback(up, engine::atmos::kDefaultPlanetUp);
  auto forward = SafeNormalizeOrFallback(forward_hint, space::look::Forward);
  auto left = glm::cross(forward, safe_up);
  const auto dot_main = std::abs(glm::dot(safe_up, forward));
  if (dot_main > 0.999F || glm::dot(left, left) <= 1.0e-8F) {
    const auto sign = safe_up.z >= 0.0F ? 1.0F : -1.0F;
    const auto a = -1.0F / (sign + safe_up.z);
    const auto b = safe_up.x * safe_up.y * a;
    forward = glm::vec3(
      1.0F + sign * a * safe_up.x * safe_up.x,
      sign * b,
      -sign * safe_up.x);
    left = glm::vec3(
      b,
      sign + a * safe_up.y * safe_up.y,
      -safe_up.y);
  } else {
    left = SafeNormalizeOrFallback(left, space::look::Right);
    forward = SafeNormalizeOrFallback(glm::cross(safe_up, left), forward);
  }

  return {
    glm::vec4(forward, 0.0F),
    glm::vec4(left, 0.0F),
    glm::vec4(safe_up, 0.0F),
  };
}

auto ComputeSunDiskLuminanceRgb(
  const environment::AtmosphereLightModel& light) -> glm::vec3
{
  constexpr auto kPi = 3.14159265358979323846F;
  const auto half_apex_radians
    = std::max(0.0F, 0.5F * light.angular_size_radians);
  const auto solid_angle
    = 2.0F * kPi * (1.0F - std::cos(half_apex_radians));
  const auto safe_solid_angle = std::max(solid_angle, 1.0e-6F);
  return glm::vec3(
    light.disk_luminance_scale_rgb.x * light.illuminance_rgb_lux.x,
    light.disk_luminance_scale_rgb.y * light.illuminance_rgb_lux.y,
    light.disk_luminance_scale_rgb.z * light.illuminance_rgb_lux.z)
    / safe_solid_angle;
}

} // namespace

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
    && view_products_publisher_ != nullptr && static_data_publisher_ != nullptr) {
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
    view_data_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<EnvironmentViewData>>(
      observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
      observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
      "EnvironmentViewData");
  }
  if (static_data_publisher_ == nullptr) {
    static_data_publisher_ = std::make_unique<
      internal::PerViewStructuredPublisher<EnvironmentStaticData>>(
      observer_ptr { gfx.get() }, renderer_.GetStagingProvider(),
      observer_ptr { &renderer_.GetInlineTransfersCoordinator() },
      "EnvironmentStaticData");
  }
  if (view_products_publisher_ == nullptr) {
    view_products_publisher_
      = std::make_unique<internal::PerViewStructuredPublisher<
        environment::EnvironmentViewProducts>>(observer_ptr { gfx.get() },
        renderer_.GetStagingProvider(),
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
    static_data_publisher_->OnFrameStart(sequence, slot);
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
  const environment::EnvironmentViewProducts& view_products,
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
    .transmittance_lut_srv = view_products.transmittance_lut_srv,
    .multi_scattering_lut_srv = view_products.multi_scattering_lut_srv,
    .sky_view_lut_srv = view_products.sky_view_lut_srv,
    .camera_aerial_perspective_srv = view_products.camera_aerial_perspective_srv,
    .atmosphere_light0_direction_angular_size = {
      view_products.atmosphere_lights[0].direction_to_light_ws.x,
      view_products.atmosphere_lights[0].direction_to_light_ws.y,
      view_products.atmosphere_lights[0].direction_to_light_ws.z,
      view_products.atmosphere_lights[0].angular_size_radians,
    },
    .atmosphere_light0_disk_luminance_rgb = {
      ComputeSunDiskLuminanceRgb(view_products.atmosphere_lights[0]).x,
      ComputeSunDiskLuminanceRgb(view_products.atmosphere_lights[0]).y,
      ComputeSunDiskLuminanceRgb(view_products.atmosphere_lights[0]).z,
      view_products.atmosphere_lights[0].enabled ? 1.0F : 0.0F,
    },
    .atmosphere_light1_direction_angular_size = {
      view_products.atmosphere_lights[1].direction_to_light_ws.x,
      view_products.atmosphere_lights[1].direction_to_light_ws.y,
      view_products.atmosphere_lights[1].direction_to_light_ws.z,
      view_products.atmosphere_lights[1].angular_size_radians,
    },
    .atmosphere_light1_disk_luminance_rgb = {
      ComputeSunDiskLuminanceRgb(view_products.atmosphere_lights[1]).x,
      ComputeSunDiskLuminanceRgb(view_products.atmosphere_lights[1]).y,
      ComputeSunDiskLuminanceRgb(view_products.atmosphere_lights[1]).z,
      view_products.atmosphere_lights[1].enabled ? 1.0F : 0.0F,
    },
    .probes = probe_state_.probes,
    .evaluation = EnvironmentEvaluationParameters {},
    .ambient_bridge = EnvironmentAmbientBridgeBindings {},
  };

  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    const auto disk_luminance
      = ComputeSunDiskLuminanceRgb(stable_state.view_products.atmosphere_lights[0]);
    bindings.contract_flags |= kEnvironmentContractFlagAtmosphereLight0Enabled;
    bindings.atmosphere_light0_direction_angular_size[0]
      = stable_state.view_products.atmosphere_lights[0].direction_to_light_ws.x;
    bindings.atmosphere_light0_direction_angular_size[1]
      = stable_state.view_products.atmosphere_lights[0].direction_to_light_ws.y;
    bindings.atmosphere_light0_direction_angular_size[2]
      = stable_state.view_products.atmosphere_lights[0].direction_to_light_ws.z;
    bindings.atmosphere_light0_direction_angular_size[3]
      = stable_state.view_products.atmosphere_lights[0].angular_size_radians;
    bindings.atmosphere_light0_disk_luminance_rgb[0] = disk_luminance.x;
    bindings.atmosphere_light0_disk_luminance_rgb[1] = disk_luminance.y;
    bindings.atmosphere_light0_disk_luminance_rgb[2] = disk_luminance.z;
    bindings.atmosphere_light0_disk_luminance_rgb[3] = 1.0F;
  }
  if (stable_state.view_products.atmosphere_lights[1].enabled) {
    const auto disk_luminance
      = ComputeSunDiskLuminanceRgb(stable_state.view_products.atmosphere_lights[1]);
    bindings.contract_flags |= kEnvironmentContractFlagAtmosphereLight1Enabled;
    bindings.atmosphere_light1_direction_angular_size[0]
      = stable_state.view_products.atmosphere_lights[1].direction_to_light_ws.x;
    bindings.atmosphere_light1_direction_angular_size[1]
      = stable_state.view_products.atmosphere_lights[1].direction_to_light_ws.y;
    bindings.atmosphere_light1_direction_angular_size[2]
      = stable_state.view_products.atmosphere_lights[1].direction_to_light_ws.z;
    bindings.atmosphere_light1_direction_angular_size[3]
      = stable_state.view_products.atmosphere_lights[1].angular_size_radians;
    bindings.atmosphere_light1_disk_luminance_rgb[0] = disk_luminance.x;
    bindings.atmosphere_light1_disk_luminance_rgb[1] = disk_luminance.y;
    bindings.atmosphere_light1_disk_luminance_rgb[2] = disk_luminance.z;
    bindings.atmosphere_light1_disk_luminance_rgb[3] = 1.0F;
  }
  if (stable_state.conventional_shadow_authority_slot0_only) {
    bindings.contract_flags |= kEnvironmentContractFlagShadowAuthoritySlot0Only;
  }

  if (enable_ambient_bridge) {
    bindings.evaluation.flags
      |= kEnvironmentEvaluationFlagAmbientBridgeEligible;
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

  auto camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F };
  auto view_forward_ws = glm::vec3(space::look::Forward);
  if (ctx.current_view.resolved_view != nullptr) {
    camera_position = ctx.current_view.resolved_view->CameraPosition();
    const auto inverse_view = ctx.current_view.resolved_view->InverseView();
    view_forward_ws = SafeNormalizeOrFallback(
      glm::vec3(inverse_view * glm::vec4(space::look::Forward, 0.0F)),
      glm::vec3(space::look::Forward));
  }
  const auto planet_center_ws = ResolvePlanetCenterWs(atmosphere);
  const auto planet_center_translated_ws = planet_center_ws - camera_position;
  const auto camera_to_planet_translated_ws = -planet_center_translated_ws;
  const auto distance_to_planet_center_m
    = glm::length(camera_to_planet_translated_ws);
  const auto planet_radius_offset_m = 5.0F;
  auto sky_camera_translated_world_origin = glm::vec3 { 0.0F, 0.0F, 0.0F };
  if (distance_to_planet_center_m
    < (atmosphere.planet_radius_m + planet_radius_offset_m)) {
    const auto direction = SafeNormalizeOrFallback(
      camera_to_planet_translated_ws, engine::atmos::kDefaultPlanetUp);
    sky_camera_translated_world_origin = planet_center_translated_ws
      + direction * (atmosphere.planet_radius_m + planet_radius_offset_m);
  }
  const auto sky_camera_planet_vector
    = sky_camera_translated_world_origin - planet_center_translated_ws;
  const auto planet_up_ws = SafeNormalizeOrFallback(
    sky_camera_planet_vector, engine::atmos::kDefaultPlanetUp);
  const auto view_height_m = glm::length(sky_camera_planet_vector);
  const auto camera_altitude_m = std::max(
    view_height_m - atmosphere.planet_radius_m, 0.0F);
  const auto referential_rows
    = BuildSkyViewReferentialRows(planet_up_ws, view_forward_ws);

  auto sun_direction_ws = engine::atmos::kDefaultSunDirection;
  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    const auto& slot0 = stable_state.view_products.atmosphere_lights[0];
    const auto length_sq
      = glm::dot(slot0.direction_to_light_ws, slot0.direction_to_light_ws);
    if (length_sq > 1.0e-6F) {
      sun_direction_ws = glm::normalize(slot0.direction_to_light_ws);
    }
  }

  auto data = EnvironmentViewData {};
  data.flags = atmosphere.enabled ? 1U : 0U;
  data.transform_mode = static_cast<std::uint32_t>(atmosphere.transform_mode);
  data.atmosphere_light_count
    = stable_state.view_products.atmosphere_light_count;
  data.sky_view_lut_slice = 0.0F;
  data.planet_to_sun_cos_zenith
    = glm::dot(glm::normalize(planet_up_ws), glm::normalize(sun_direction_ws));
  data.aerial_perspective_distance_scale
    = atmosphere.aerial_perspective_distance_scale;
  data.aerial_scattering_strength = atmosphere.aerial_scattering_strength;
  data.planet_center_ws_pad = glm::vec4(planet_center_ws, 0.0F);
  data.planet_up_ws_camera_altitude_m
    = glm::vec4(planet_up_ws, camera_altitude_m);
  data.sky_planet_translated_world_center_and_view_height
    = glm::vec4(planet_center_translated_ws, view_height_m);
  data.sky_camera_translated_world_origin_pad
    = glm::vec4(sky_camera_translated_world_origin, 0.0F);
  data.sky_view_lut_referential_row0 = referential_rows[0];
  data.sky_view_lut_referential_row1 = referential_rows[1];
  data.sky_view_lut_referential_row2 = referential_rows[2];
  data.sky_luminance_factor_height_fog_contribution = glm::vec4(
    atmosphere.sky_luminance_factor_rgb, atmosphere.height_fog_contribution);
  data.sky_aerial_luminance_aerial_start_depth_m
    = glm::vec4(atmosphere.sky_and_aerial_perspective_luminance_factor_rgb,
      atmosphere.aerial_perspective_start_depth_m);
  data.trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass
    = glm::vec4(atmosphere.trace_sample_count_scale,
      atmosphere.transmittance_min_light_elevation_deg,
      atmosphere.holdout ? 1.0F : 0.0F,
      atmosphere.render_in_main_pass ? 1.0F : 0.0F);
  const auto& internal_params
    = atmosphere_lut_cache_->GetState().internal_parameters;
  const auto depth_resolution = static_cast<float>(
    std::max(internal_params.camera_aerial_depth_resolution, 1U));
  const auto depth_slice_length_km
    = internal_params.camera_aerial_depth_slice_length_km;
  data.camera_aerial_volume_depth_params = glm::vec4(depth_resolution,
    1.0F / depth_resolution, depth_slice_length_km,
    depth_slice_length_km > 1.0e-6F ? 1.0F / depth_slice_length_km : 0.0F);
  return data;
}

auto EnvironmentLightingService::BuildEnvironmentStaticData(
  const environment::EnvironmentViewProducts& view_products) const
  -> EnvironmentStaticData
{
  auto data = EnvironmentStaticData {};

  data.fog.single_scattering_albedo_rgb = {
    view_products.height_fog.fog_density > 0.0F
      ? view_products.height_fog.fog_inscattering_luminance.x
      : 1.0F,
    view_products.height_fog.fog_density > 0.0F
      ? view_products.height_fog.fog_inscattering_luminance.y
      : 1.0F,
    view_products.height_fog.fog_density > 0.0F
      ? view_products.height_fog.fog_inscattering_luminance.z
      : 1.0F,
  };
  data.fog.extinction_sigma_t_per_m = view_products.height_fog.fog_density;
  data.fog.height_falloff_per_m = view_products.height_fog.fog_height_falloff;
  data.fog.height_offset_m = view_products.height_fog.fog_height_offset;
  data.fog.start_distance_m = view_products.height_fog.start_distance;
  data.fog.max_opacity = view_products.height_fog.fog_max_opacity;
  data.fog.anisotropy_g
    = view_products.height_fog.directional_inscattering_exponent;
  data.fog.model = view_products.height_fog.legacy_model;
  data.fog.enabled = view_products.height_fog.enabled ? 1U : 0U;

  const auto& atmo = view_products.atmosphere;
  data.atmosphere.planet_radius_m = atmo.planet_radius_m;
  data.atmosphere.atmosphere_height_m = atmo.atmosphere_height_m;
  data.atmosphere.multi_scattering_factor = atmo.multi_scattering_factor;
  data.atmosphere.aerial_perspective_distance_scale
    = atmo.aerial_perspective_distance_scale;
  data.atmosphere.ground_albedo_rgb = {
    atmo.ground_albedo_rgb.x, atmo.ground_albedo_rgb.y, atmo.ground_albedo_rgb.z
  };
  data.atmosphere.sun_disk_angular_radius_radians
    = atmo.sun_disk_enabled ? engine::atmos::kDefaultSunDiskAngularRadiusRad : 0.0F;
  data.atmosphere.rayleigh_scattering_rgb = {
    atmo.rayleigh_scattering_rgb.x,
    atmo.rayleigh_scattering_rgb.y,
    atmo.rayleigh_scattering_rgb.z,
  };
  data.atmosphere.rayleigh_scale_height_m = atmo.rayleigh_scale_height_m;
  data.atmosphere.mie_scattering_rgb = {
    atmo.mie_scattering_rgb.x, atmo.mie_scattering_rgb.y, atmo.mie_scattering_rgb.z
  };
  data.atmosphere.mie_scale_height_m = atmo.mie_scale_height_m;
  data.atmosphere.mie_extinction_rgb = {
    atmo.mie_scattering_rgb.x + atmo.mie_absorption_rgb.x,
    atmo.mie_scattering_rgb.y + atmo.mie_absorption_rgb.y,
    atmo.mie_scattering_rgb.z + atmo.mie_absorption_rgb.z,
  };
  data.atmosphere.mie_g = atmo.mie_anisotropy;
  data.atmosphere.absorption_rgb = {
    atmo.ozone_absorption_rgb.x,
    atmo.ozone_absorption_rgb.y,
    atmo.ozone_absorption_rgb.z,
  };
  for (std::size_t i = 0; i < data.atmosphere.absorption_density.layers.size(); ++i) {
    data.atmosphere.absorption_density.layers[i].width_m
      = atmo.ozone_density_profile.layers[i].width_m;
    data.atmosphere.absorption_density.layers[i].exp_term
      = atmo.ozone_density_profile.layers[i].exp_term;
    data.atmosphere.absorption_density.layers[i].linear_term
      = atmo.ozone_density_profile.layers[i].linear_term;
    data.atmosphere.absorption_density.layers[i].constant_term
      = atmo.ozone_density_profile.layers[i].constant_term;
  }
  data.atmosphere.sun_disk_enabled = atmo.sun_disk_enabled ? 1U : 0U;
  data.atmosphere.enabled = atmo.enabled ? 1U : 0U;
  data.atmosphere.transmittance_lut_slot = view_products.transmittance_lut_srv.get();
  data.atmosphere.sky_view_lut_slot = view_products.sky_view_lut_srv.get();
  data.atmosphere.sky_irradiance_lut_slot = kInvalidBindlessIndex;
  data.atmosphere.multi_scat_lut_slot = view_products.multi_scattering_lut_srv.get();
  data.atmosphere.camera_volume_lut_slot
    = view_products.camera_aerial_perspective_srv.get();
  data.atmosphere.blue_noise_slot = kInvalidBindlessIndex;
  const auto& internal_params = atmosphere_lut_cache_->GetState().internal_parameters;
  data.atmosphere.transmittance_lut_width
    = static_cast<float>(internal_params.transmittance_width);
  data.atmosphere.transmittance_lut_height
    = static_cast<float>(internal_params.transmittance_height);
  data.atmosphere.sky_view_lut_width
    = static_cast<float>(internal_params.sky_view_width);
  data.atmosphere.sky_view_lut_height
    = static_cast<float>(internal_params.sky_view_height);
  data.atmosphere.sky_irradiance_lut_width = 0.0F;
  data.atmosphere.sky_irradiance_lut_height = 0.0F;
  data.atmosphere.sky_view_lut_slices = 1U;
  data.atmosphere.sky_view_alt_mapping_mode = 0U;

  data.sky_light.tint_rgb = {
    view_products.sky_light.tint_rgb.x,
    view_products.sky_light.tint_rgb.y,
    view_products.sky_light.tint_rgb.z,
  };
  data.sky_light.radiance_scale = view_products.sky_light.intensity_mul;
  data.sky_light.diffuse_intensity = view_products.sky_light.diffuse_intensity;
  data.sky_light.specular_intensity = view_products.sky_light.specular_intensity;
  data.sky_light.source = view_products.sky_light.source;
  data.sky_light.enabled = view_products.sky_light.enabled ? 1U : 0U;

  data.sky_sphere.enabled = 0U;
  data.clouds.enabled = 0U;
  data.post_process.enabled = 0U;

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

  const auto transmittance_state = transmittance_lut_pass_->Record(
    ctx, stable_state, *atmosphere_lut_cache_);
  const auto multi_scattering_state = multi_scattering_lut_pass_->Record(
    ctx, stable_state, *atmosphere_lut_cache_);
  const auto distant_sky_light_state = distant_sky_light_lut_pass_->Record(
    ctx, stable_state, *atmosphere_lut_cache_);

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

  const auto sky_view_state = sky_view_lut_pass_->Record(
    ctx, view_data, stable_state, *atmosphere_lut_cache_);
  products.sky_view_lut_srv = sky_view_state.sky_view_lut_srv;

  const auto camera_aerial_state = camera_aerial_perspective_pass_->Record(
    ctx, view_data, stable_state, *atmosphere_lut_cache_);
  products.camera_aerial_perspective_srv
    = camera_aerial_state.camera_aerial_perspective_srv;

  const auto resolved_environment_static_slot = static_data_publisher_->Publish(
    ctx.current_view.view_id, BuildEnvironmentStaticData(products));

  const auto environment_view_products_slot
    = view_products_publisher_->Publish(ctx.current_view.view_id, products);
  const auto bindings
    = BuildBindings(resolved_environment_static_slot, resolved_environment_view_slot,
      environment_view_products_slot, products, enable_ambient_bridge);
  const auto slot
    = bindings_publisher_->Publish(ctx.current_view.view_id, bindings);
  published_views_.insert_or_assign(ctx.current_view.view_id,
    PublishedView { .slot = slot, .bindings = bindings, .view_data = view_data });

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
  LOG_F(INFO, "sky_view_lut_published={}",
    products.sky_view_lut_srv.IsValid() ? "true" : "false");
  LOG_F(INFO, "camera_aerial_perspective_published={}",
    products.camera_aerial_perspective_srv.IsValid() ? "true" : "false");
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
    .dual_atmosphere_lights_participating
    = cache_state.dual_light_participating,
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
    .multi_scattering_dispatch_count_x
    = multi_scattering_state.dispatch_count_x,
    .multi_scattering_dispatch_count_y
    = multi_scattering_state.dispatch_count_y,
    .multi_scattering_dispatch_count_z
    = multi_scattering_state.dispatch_count_z,
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

auto EnvironmentLightingService::InspectEnvironmentViewData(
  const ViewId view_id) const -> const EnvironmentViewData*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.view_data : nullptr;
}

auto EnvironmentLightingService::ResolveEnvironmentFrameSlot(
  const ViewId view_id) const -> ShaderVisibleIndex
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end()
    ? it->second.slot
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
