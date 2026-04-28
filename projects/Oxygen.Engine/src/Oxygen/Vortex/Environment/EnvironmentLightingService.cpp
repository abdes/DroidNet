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
#include <Oxygen/Vortex/Environment/Passes/VolumetricFogPass.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentAmbientBridgeBindings.h>
#include <Oxygen/Vortex/Environment/Types/EnvironmentEvaluationParameters.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>

namespace oxygen::vortex {

namespace {

  constexpr std::uint32_t kEnvironmentViewFlagAtmosphereEnabled = 1U << 0U;
  constexpr std::uint32_t kEnvironmentViewFlagReflectionCapture = 1U << 1U;
  constexpr float kPi = 3.14159265358979323846F;

  auto ResolvePlanetCenterWs(const environment::AtmosphereModel& atmosphere)
    -> glm::vec3
  {
    switch (atmosphere.transform_mode) {
    case environment::AtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin:
      return { 0.0F, 0.0F, -atmosphere.planet_radius_m };
    case environment::AtmosphereTransformMode::kPlanetTopAtComponentTransform:
      return atmosphere.planet_anchor_position_ws
        + glm::vec3 { 0.0F, 0.0F, -atmosphere.planet_radius_m };
    case environment::AtmosphereTransformMode::
      kPlanetCenterAtComponentTransform:
      return atmosphere.planet_anchor_position_ws;
    default:
      return { 0.0F, 0.0F, -atmosphere.planet_radius_m };
    }
  }

  auto SafeNormalizeOrFallback(const glm::vec3 value, const glm::vec3 fallback)
    -> glm::vec3
  {
    const auto length_sq = glm::dot(value, value);
    if (length_sq <= 1.0e-8F) {
      return fallback;
    }
    return glm::normalize(value);
  }

  auto MetersToSkyUnitVec3(const glm::vec3 meters) -> glm::vec3
  {
    return meters * engine::atmos::kMToSkyUnit;
  }

  auto CoefficientsPerMeterToPerSkyUnitVec3(const glm::vec3 coefficients_per_m)
    -> glm::vec3
  {
    return coefficients_per_m * engine::atmos::kSkyUnitToM;
  }

  auto CurrentViewWantsVolumetrics(const RenderContext& ctx) -> bool
  {
    return ctx.current_view.feature_mask.Has(
      CompositionView::ViewFeatureMask::kVolumetrics);
  }

  //! Builds the shared sky-view local basis used by both the LUT producer and
  //! the main-view sky consumer.
  /*!
   Contract:
   - rows are expressed in Oxygen world space
   - row0 = local +X = sun/physics hint projected onto the tangent plane
   - row1 = local +Y = right = cross(up, forward)
   - row2 = local +Z = up

   This basis must stay right-handed and must not silently switch to "left".
   The sky-view LUT parameterization already applies its own azimuth convention
   in shader code, so mirroring this basis would not "fix" sun position; it
   would only create a mirrored local frame shared by producer and consumer.
  */
  auto BuildSkyViewReferentialRows(const glm::vec3 up,
    const glm::vec3 forward_hint) -> std::array<glm::vec4, 3>
  {
    const auto safe_up
      = SafeNormalizeOrFallback(up, engine::atmos::kDefaultPlanetUp);
    // The sky-view referential is expressed in Oxygen world space, not view
    // space. Keep its fallback axes on the engine world basis: Z-up,
    // -Y-forward.
    auto forward = SafeNormalizeOrFallback(forward_hint, space::move::Forward);
    // +Y in the local sky-view frame is RIGHT, not LEFT. Using cross(forward,
    // up) would mirror the basis horizontally under Oxygen's right-handed Z-up
    // law.
    auto right = glm::cross(safe_up, forward);
    const auto dot_main = std::abs(glm::dot(safe_up, forward));
    if (dot_main > 0.999F || glm::dot(right, right) <= 1.0e-8F) {
      right = glm::cross(safe_up, glm::vec3(space::move::Forward));
      right = SafeNormalizeOrFallback(right, glm::vec3(space::move::Right));
      forward = SafeNormalizeOrFallback(
        glm::cross(right, safe_up), glm::vec3(space::move::Forward));
    } else {
      right = SafeNormalizeOrFallback(right, glm::vec3(space::move::Right));
      forward = SafeNormalizeOrFallback(glm::cross(right, safe_up), forward);
    }

    return {
      glm::vec4(forward, 0.0F),
      glm::vec4(right, 0.0F),
      glm::vec4(safe_up, 0.0F),
    };
  }

  auto ComputeSunDiskLuminanceRgb(
    const environment::AtmosphereLightModel& light) -> glm::vec3
  {
    const auto angular_radius_radians
      = 0.5F * std::max(0.0F, light.angular_size_radians);
    const auto solid_angle
      = 2.0F * kPi * (1.0F - std::cos(angular_radius_radians));
    const auto safe_solid_angle = std::max(solid_angle, 1.0e-6F);
    return glm::vec3(
             light.disk_luminance_scale_rgba.x * light.illuminance_rgb_lux.x,
             light.disk_luminance_scale_rgba.y * light.illuminance_rgb_lux.y,
             light.disk_luminance_scale_rgba.z * light.illuminance_rgb_lux.z)
      / safe_solid_angle;
  }

  auto ProbeBindingsHaveUsableResources(const EnvironmentProbeBindings& probes)
    -> bool
  {
    return probes.environment_map_srv.IsValid()
      && probes.diffuse_sh_srv.IsValid() && probes.probe_revision != 0U;
  }

  auto ProbeStateHasUsableResources(const EnvironmentProbeState& state) -> bool
  {
    return state.valid && ProbeBindingsHaveUsableResources(state.probes);
  }

  auto SanitizedProbeBindings(const EnvironmentProbeState& state)
    -> EnvironmentProbeBindings
  {
    auto bindings = EnvironmentProbeBindings {};
    bindings.probe_revision = state.probes.probe_revision;
    if (ProbeStateHasUsableResources(state)) {
      bindings = state.probes;
    }
    return bindings;
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
  , volumetric_fog_pass_(
      std::make_unique<environment::VolumetricFogPass>(renderer))
  , ibl_(std::make_unique<environment::internal::IblProcessor>(renderer))
{
}

EnvironmentLightingService::~EnvironmentLightingService() = default;

auto EnvironmentLightingService::EnsurePublishResources() -> bool
{
  if (bindings_publisher_ != nullptr && view_data_publisher_ != nullptr
    && view_products_publisher_ != nullptr
    && static_data_publisher_ != nullptr) {
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
    .sky_light_ibl_valid = ProbeStateHasUsableResources(probe_state_),
    .sky_light_ibl_stale
    = (probe_state_.flags & kEnvironmentProbeStateFlagStale) != 0U,
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
  if (volumetric_fog_pass_ != nullptr) {
    volumetric_fog_pass_->OnFrameStart(sequence, slot);
  }
  pending_volumetric_fog_state_ = {};
  pending_local_fog_culling_state_ = {};
  pending_local_fog_view_id_ = kInvalidViewId;
  pending_local_fog_sequence_ = frame::SequenceNumber { 0U };
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
    .camera_aerial_perspective_srv
    = view_products.camera_aerial_perspective_srv,
    .probes = SanitizedProbeBindings(probe_state_),
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
  const auto sky_light_authored_enabled = view_products.sky_light.enabled;
  const auto sky_light_ibl_valid = ProbeStateHasUsableResources(probe_state_);
  if (sky_light_authored_enabled) {
    bindings.contract_flags |= kEnvironmentContractFlagSkyLightAuthoredEnabled;
    if (sky_light_ibl_valid) {
      bindings.contract_flags |= kEnvironmentContractFlagSkyLightIblValid;
    } else {
      bindings.contract_flags |= kEnvironmentContractFlagSkyLightIblUnavailable;
    }
  }
  if (view_products.volumetric_fog.enabled) {
    bindings.contract_flags
      |= kEnvironmentContractFlagVolumetricFogAuthoredEnabled;
    if (view_products.integrated_light_scattering_srv.IsValid()) {
      bindings.contract_flags
        |= kEnvironmentContractFlagIntegratedLightScatteringValid;
    } else {
      bindings.contract_flags
        |= kEnvironmentContractFlagIntegratedLightScatteringUnavailable;
    }
  }

  if (enable_ambient_bridge) {
    bindings.evaluation.flags
      |= kEnvironmentEvaluationFlagAmbientBridgeEligible;
  }
  if (enable_ambient_bridge && sky_light_ibl_valid) {
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

auto EnvironmentLightingService::PrepareLocalFogForStage14(
  RenderContext& ctx, const SceneTextures& scene_textures)
  -> const environment::internal::LocalFogVolumeState::ViewProducts&
{
  if (pending_local_fog_sequence_ == current_sequence_
    && pending_local_fog_view_id_ == ctx.current_view.view_id) {
    return local_fog_state_->GetCurrentProducts();
  }

  auto& local_fog_products = local_fog_state_->Prepare(ctx);
  LOG_F(INFO, "local_fog_volume_instance_count={}",
    local_fog_products.instance_count);
  pending_local_fog_culling_state_
    = local_fog_tiled_culling_->Record(ctx, scene_textures, local_fog_products);
  pending_local_fog_view_id_ = ctx.current_view.view_id;
  pending_local_fog_sequence_ = current_sequence_;
  return local_fog_products;
}

auto EnvironmentLightingService::BuildEnvironmentViewData(
  const RenderContext& ctx) const -> EnvironmentViewData
{
  const auto& stable_state = atmosphere_state_->GetState();
  const auto& atmosphere = stable_state.view_products.atmosphere;

  auto camera_position = glm::vec3 { 0.0F, 0.0F, 0.0F };
  if (ctx.current_view.resolved_view != nullptr) {
    camera_position = ctx.current_view.resolved_view->CameraPosition();
  }
  const auto planet_center_ws = ResolvePlanetCenterWs(atmosphere);
  const auto planet_center_translated_ws = planet_center_ws - camera_position;
  const auto camera_to_planet_translated_ws = -planet_center_translated_ws;
  const auto distance_to_planet_center_m
    = glm::length(camera_to_planet_translated_ws);
  const auto planet_radius_offset_m
    = engine::atmos::SkyUnitToMeters(engine::atmos::kPlanetRadiusOffsetKm);
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
  const auto camera_altitude_km = engine::atmos::MetersToSkyUnit(
    std::max(view_height_m - atmosphere.planet_radius_m, 0.0F));
  auto sun_direction_ws = engine::atmos::kDefaultSunDirection;
  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    const auto& slot0 = stable_state.view_products.atmosphere_lights[0];
    const auto length_sq
      = glm::dot(slot0.direction_to_light_ws, slot0.direction_to_light_ws);
    if (length_sq > 1.0e-6F) {
      sun_direction_ws = glm::normalize(slot0.direction_to_light_ws);
    }
  }
  const auto referential_rows
    = BuildSkyViewReferentialRows(planet_up_ws, sun_direction_ws);

  auto data = EnvironmentViewData {};
  data.flags = 0U;
  if (atmosphere.enabled) {
    data.flags |= kEnvironmentViewFlagAtmosphereEnabled;
  }
  if (ctx.current_view.is_reflection_capture) {
    data.flags |= kEnvironmentViewFlagReflectionCapture;
  }
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
  data.planet_up_ws_camera_altitude_km
    = glm::vec4(planet_up_ws, camera_altitude_km);
  data.sky_planet_translated_world_center_km_and_view_height_km
    = glm::vec4(MetersToSkyUnitVec3(planet_center_translated_ws),
      engine::atmos::MetersToSkyUnit(view_height_m));
  data.sky_camera_translated_world_origin_km_pad
    = glm::vec4(MetersToSkyUnitVec3(sky_camera_translated_world_origin), 0.0F);
  data.sky_view_lut_referential_row0 = referential_rows[0];
  data.sky_view_lut_referential_row1 = referential_rows[1];
  data.sky_view_lut_referential_row2 = referential_rows[2];
  if (stable_state.view_products.atmosphere_lights[0].enabled) {
    const auto disk_luminance = atmosphere.sun_disk_enabled
      ? ComputeSunDiskLuminanceRgb(
          stable_state.view_products.atmosphere_lights[0])
      : glm::vec3 { 0.0F, 0.0F, 0.0F };
    data.atmosphere_light0_direction_angular_size = glm::vec4(
      stable_state.view_products.atmosphere_lights[0].direction_to_light_ws,
      0.5F
        * std::max(0.0F,
          stable_state.view_products.atmosphere_lights[0]
            .angular_size_radians));
    data.atmosphere_light0_disk_luminance_rgb
      = glm::vec4(disk_luminance, atmosphere.sun_disk_enabled ? 1.0F : 0.0F);
  }
  if (stable_state.view_products.atmosphere_lights[1].enabled) {
    const auto disk_luminance = atmosphere.sun_disk_enabled
      ? ComputeSunDiskLuminanceRgb(
          stable_state.view_products.atmosphere_lights[1])
      : glm::vec3 { 0.0F, 0.0F, 0.0F };
    data.atmosphere_light1_direction_angular_size = glm::vec4(
      stable_state.view_products.atmosphere_lights[1].direction_to_light_ws,
      0.5F
        * std::max(0.0F,
          stable_state.view_products.atmosphere_lights[1]
            .angular_size_radians));
    data.atmosphere_light1_disk_luminance_rgb
      = glm::vec4(disk_luminance, atmosphere.sun_disk_enabled ? 1.0F : 0.0F);
  }
  data.sky_luminance_factor_height_fog_contribution = glm::vec4(
    atmosphere.sky_luminance_factor_rgb, atmosphere.height_fog_contribution);
  data.sky_aerial_luminance_aerial_start_depth_km
    = glm::vec4(atmosphere.sky_and_aerial_perspective_luminance_factor_rgb,
      engine::atmos::MetersToSkyUnit(
        atmosphere.aerial_perspective_start_depth_m));
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

  const auto& height_fog = view_products.height_fog;
  const auto fog_max_opacity
    = std::clamp(height_fog.fog_max_opacity, 0.0F, 1.0F);
  const auto primary_density = std::max(height_fog.fog_density, 0.0F);
  const auto secondary_density = std::max(height_fog.second_fog_density, 0.0F);
  const auto any_layer_density
    = primary_density > 0.0F || secondary_density > 0.0F;
  const auto cubemap_authored
    = height_fog.inscattering_color_cubemap_resource.get() != 0U;
  const auto cubemap_usable = false;
  const auto active_height_fog = height_fog.enabled
    && height_fog.enable_height_fog && height_fog.render_in_main_pass
    && any_layer_density && fog_max_opacity > 0.0F;
  const auto active_volumetric_fog = view_products.volumetric_fog.enabled
    && view_products.integrated_light_scattering_srv.IsValid()
    && height_fog.render_in_main_pass;
  auto fog_flags = std::uint32_t { 0U };
  if (active_height_fog || active_volumetric_fog) {
    fog_flags |= kGpuFogFlagEnabled;
  }
  if (height_fog.enable_height_fog) {
    fog_flags |= kGpuFogFlagHeightFogEnabled;
  }
  if (height_fog.enable_volumetric_fog) {
    fog_flags |= kGpuFogFlagVolumetricFogAuthored;
  }
  if (height_fog.render_in_main_pass) {
    fog_flags |= kGpuFogFlagRenderInMainPass;
  }
  if (height_fog.visible_in_reflection_captures) {
    fog_flags |= kGpuFogFlagVisibleInReflectionCaptures;
  }
  if (height_fog.visible_in_real_time_sky_captures) {
    fog_flags |= kGpuFogFlagVisibleInRealTimeSkyCaptures;
  }
  if (height_fog.holdout) {
    fog_flags |= kGpuFogFlagHoldout;
  }
  if (height_fog.directional_inscattering_exponent > 0.0F
    && !cubemap_authored) {
    fog_flags |= kGpuFogFlagDirectionalInscattering;
  }
  if (cubemap_authored) {
    fog_flags |= kGpuFogFlagCubemapAuthored;
  }
  if (cubemap_usable) {
    fog_flags |= kGpuFogFlagCubemapUsable;
  }
  const auto cubemap_fade_range
    = height_fog.fully_directional_inscattering_color_distance
    - height_fog.non_directional_inscattering_color_distance;
  const auto cubemap_fade_inv_range
    = 1.0F / std::max(cubemap_fade_range, 0.00001F);
  data.fog.fog_inscattering_luminance_rgb = {
    height_fog.fog_inscattering_luminance.x,
    height_fog.fog_inscattering_luminance.y,
    height_fog.fog_inscattering_luminance.z,
  };
  data.fog.primary_density = primary_density;
  data.fog.primary_height_falloff
    = std::max(height_fog.fog_height_falloff, 0.0F);
  data.fog.primary_height_offset_m = height_fog.fog_height_offset;
  data.fog.secondary_density = secondary_density;
  data.fog.secondary_height_falloff
    = std::max(height_fog.second_fog_height_falloff, 0.0F);
  data.fog.secondary_height_offset_m = height_fog.second_fog_height_offset;
  data.fog.start_distance_m = std::max(height_fog.start_distance, 0.0F);
  data.fog.end_distance_m = std::max(height_fog.end_distance, 0.0F);
  data.fog.cutoff_distance_m = std::max(height_fog.fog_cutoff_distance, 0.0F);
  data.fog.max_opacity = fog_max_opacity;
  data.fog.min_transmittance = 1.0F - fog_max_opacity;
  data.fog.directional_start_distance_m
    = std::max(height_fog.directional_inscattering_start_distance, 0.0F);
  data.fog.directional_exponent = std::clamp(
    height_fog.directional_inscattering_exponent, 0.000001F, 1000.0F);
  data.fog.directional_inscattering_luminance_rgb = {
    height_fog.directional_inscattering_luminance.x,
    height_fog.directional_inscattering_luminance.y,
    height_fog.directional_inscattering_luminance.z,
  };
  data.fog.cubemap_angle_radians
    = height_fog.inscattering_color_cubemap_angle * (kPi / 180.0F);
  data.fog.sky_atmosphere_ambient_contribution_color_scale_rgb = {
    height_fog.sky_atmosphere_ambient_contribution_color_scale.x,
    height_fog.sky_atmosphere_ambient_contribution_color_scale.y,
    height_fog.sky_atmosphere_ambient_contribution_color_scale.z,
  };
  data.fog.cubemap_fade_inv_range = cubemap_fade_inv_range;
  data.fog.inscattering_texture_tint_rgb = {
    height_fog.inscattering_texture_tint.x,
    height_fog.inscattering_texture_tint.y,
    height_fog.inscattering_texture_tint.z,
  };
  data.fog.cubemap_fade_bias
    = -height_fog.non_directional_inscattering_color_distance
    * cubemap_fade_inv_range;
  data.fog.cubemap_num_mips = 0.0F;
  data.fog.cubemap_srv = kInvalidBindlessIndex;
  data.fog.flags = fog_flags;
  data.fog.model = height_fog.legacy_model;

  const auto& volumetric = view_products.volumetric_fog;
  data.volumetric_fog.albedo_rgb = {
    volumetric.albedo.x,
    volumetric.albedo.y,
    volumetric.albedo.z,
  };
  data.volumetric_fog.scattering_distribution
    = std::clamp(volumetric.scattering_distribution, -0.99F, 0.99F);
  data.volumetric_fog.emissive_rgb = {
    volumetric.emissive.x,
    volumetric.emissive.y,
    volumetric.emissive.z,
  };
  data.volumetric_fog.extinction_scale
    = std::max(volumetric.extinction_scale, 0.0F);
  data.volumetric_fog.distance_m = pending_volumetric_fog_state_.executed
    ? pending_volumetric_fog_state_.end_distance_m
    : std::max(volumetric.distance, 0.0F);
  data.volumetric_fog.start_distance_m = pending_volumetric_fog_state_.executed
    ? pending_volumetric_fog_state_.start_distance_m
    : std::max(volumetric.start_distance, 0.0F);
  data.volumetric_fog.near_fade_in_distance_m
    = std::max(volumetric.near_fade_in_distance, 0.0F);
  data.volumetric_fog.static_lighting_scattering_intensity
    = std::max(volumetric.static_lighting_scattering_intensity, 0.0F);
  data.volumetric_fog.integrated_light_scattering_srv
    = view_products.integrated_light_scattering_srv.get();
  data.volumetric_fog.flags
    = volumetric.enabled ? kGpuVolumetricFogFlagEnabled : 0U;
  if (view_products.integrated_light_scattering_srv.IsValid()) {
    data.volumetric_fog.flags |= kGpuVolumetricFogFlagIntegratedScatteringValid;
  }
  data.volumetric_fog.grid_width = pending_volumetric_fog_state_.width;
  data.volumetric_fog.grid_height = pending_volumetric_fog_state_.height;
  data.volumetric_fog.grid_depth = pending_volumetric_fog_state_.depth;
  const auto volumetric_depth_span = std::max(
    data.volumetric_fog.distance_m - data.volumetric_fog.start_distance_m,
    0.0F);
  data.volumetric_fog.depth_slice_length_m
    = pending_volumetric_fog_state_.depth > 0U ? volumetric_depth_span
      / static_cast<float>(pending_volumetric_fog_state_.depth)
                                               : 0.0F;
  data.volumetric_fog.inv_depth_slice_length_m
    = data.volumetric_fog.depth_slice_length_m > 1.0e-6F
    ? 1.0F / data.volumetric_fog.depth_slice_length_m
    : 0.0F;
  data.volumetric_fog.grid_z_params = {
    pending_volumetric_fog_state_.grid_z_params[0],
    pending_volumetric_fog_state_.grid_z_params[1],
    pending_volumetric_fog_state_.grid_z_params[2],
  };

  const auto& atmo = view_products.atmosphere;
  const auto primary_sun_disk_enabled
    = atmo.sun_disk_enabled && view_products.atmosphere_lights[0].enabled;
  const auto primary_sun_disk_angular_radius_radians = primary_sun_disk_enabled
    ? 0.5F
      * std::max(0.0F, view_products.atmosphere_lights[0].angular_size_radians)
    : 0.0F;
  const auto primary_sun_disk_luminance_scale_rgb = primary_sun_disk_enabled
    ? glm::vec3 { view_products.atmosphere_lights[0].disk_luminance_scale_rgba }
    : glm::vec3 { 1.0F, 1.0F, 1.0F };
  data.atmosphere.planet_radius_km
    = engine::atmos::MetersToSkyUnit(atmo.planet_radius_m);
  data.atmosphere.atmosphere_height_km
    = engine::atmos::MetersToSkyUnit(atmo.atmosphere_height_m);
  data.atmosphere.multi_scattering_factor = atmo.multi_scattering_factor;
  data.atmosphere.aerial_perspective_distance_scale
    = atmo.aerial_perspective_distance_scale;
  data.atmosphere.ground_albedo_rgb = { atmo.ground_albedo_rgb.x,
    atmo.ground_albedo_rgb.y, atmo.ground_albedo_rgb.z };
  data.atmosphere.sun_disk_angular_radius_radians
    = primary_sun_disk_angular_radius_radians;
  data.atmosphere.sun_disk_luminance_scale_rgb = {
    primary_sun_disk_luminance_scale_rgb.x,
    primary_sun_disk_luminance_scale_rgb.y,
    primary_sun_disk_luminance_scale_rgb.z,
  };
  const auto rayleigh_scattering_per_km
    = CoefficientsPerMeterToPerSkyUnitVec3(atmo.rayleigh_scattering_rgb);
  const auto mie_scattering_per_km
    = CoefficientsPerMeterToPerSkyUnitVec3(atmo.mie_scattering_rgb);
  const auto mie_absorption_per_km
    = CoefficientsPerMeterToPerSkyUnitVec3(atmo.mie_absorption_rgb);
  const auto ozone_absorption_per_km
    = CoefficientsPerMeterToPerSkyUnitVec3(atmo.ozone_absorption_rgb);
  data.atmosphere.rayleigh_scattering_per_km_rgb = {
    rayleigh_scattering_per_km.x,
    rayleigh_scattering_per_km.y,
    rayleigh_scattering_per_km.z,
  };
  data.atmosphere.rayleigh_scale_height_km
    = engine::atmos::MetersToSkyUnit(atmo.rayleigh_scale_height_m);
  data.atmosphere.mie_scattering_per_km_rgb = { mie_scattering_per_km.x,
    mie_scattering_per_km.y, mie_scattering_per_km.z };
  data.atmosphere.mie_scale_height_km
    = engine::atmos::MetersToSkyUnit(atmo.mie_scale_height_m);
  data.atmosphere.mie_extinction_per_km_rgb = {
    mie_scattering_per_km.x + mie_absorption_per_km.x,
    mie_scattering_per_km.y + mie_absorption_per_km.y,
    mie_scattering_per_km.z + mie_absorption_per_km.z,
  };
  data.atmosphere.mie_g = atmo.mie_anisotropy;
  data.atmosphere.absorption_per_km_rgb = {
    ozone_absorption_per_km.x,
    ozone_absorption_per_km.y,
    ozone_absorption_per_km.z,
  };
  for (std::size_t i = 0; i < data.atmosphere.absorption_density.layers.size();
    ++i) {
    data.atmosphere.absorption_density.layers[i].width_km
      = engine::atmos::MetersToSkyUnit(
        atmo.ozone_density_profile.layers[i].width_m);
    data.atmosphere.absorption_density.layers[i].exp_term
      = atmo.ozone_density_profile.layers[i].exp_term;
    data.atmosphere.absorption_density.layers[i].linear_term
      = atmo.ozone_density_profile.layers[i].linear_term
      * engine::atmos::kSkyUnitToM;
    data.atmosphere.absorption_density.layers[i].constant_term
      = atmo.ozone_density_profile.layers[i].constant_term;
  }
  data.atmosphere.sun_disk_enabled = primary_sun_disk_enabled ? 1U : 0U;
  data.atmosphere.enabled = atmo.enabled ? 1U : 0U;
  data.atmosphere.transmittance_lut_slot
    = view_products.transmittance_lut_srv.get();
  data.atmosphere.sky_view_lut_slot = view_products.sky_view_lut_srv.get();
  data.atmosphere.sky_irradiance_lut_slot = kInvalidBindlessIndex;
  data.atmosphere.multi_scat_lut_slot
    = view_products.multi_scattering_lut_srv.get();
  data.atmosphere.camera_volume_lut_slot
    = view_products.camera_aerial_perspective_srv.get();
  data.atmosphere.distant_sky_light_lut_slot
    = view_products.distant_sky_light_lut_srv.get();
  const auto& internal_params
    = atmosphere_lut_cache_->GetState().internal_parameters;
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
  data.sky_light.specular_intensity
    = view_products.sky_light.specular_intensity;
  data.sky_light.source = view_products.sky_light.source;
  const auto usable_probe_bindings = SanitizedProbeBindings(probe_state_);
  const auto sky_light_ibl_valid = ProbeStateHasUsableResources(probe_state_);
  data.sky_light.enabled
    = view_products.sky_light.enabled && sky_light_ibl_valid ? 1U : 0U;
  data.sky_light.cubemap_slot = usable_probe_bindings.environment_map_srv.get();
  data.sky_light.brdf_lut_slot = usable_probe_bindings.brdf_lut_srv.get();
  data.sky_light.irradiance_map_slot
    = usable_probe_bindings.irradiance_map_srv.get();
  data.sky_light.prefilter_map_slot
    = usable_probe_bindings.prefiltered_map_srv.get();
  data.sky_light.ibl_generation
    = sky_light_ibl_valid ? usable_probe_bindings.probe_revision : 0U;

  data.sky_sphere.enabled = 0U;
  data.clouds.enabled = 0U;
  data.post_process.enabled = 0U;

  return data;
}

auto EnvironmentLightingService::PublishEnvironmentBindings(RenderContext& ctx,
  const ShaderVisibleIndex environment_static_slot,
  const ShaderVisibleIndex environment_view_slot,
  const bool enable_ambient_bridge, const SceneTextures* scene_textures)
  -> ShaderVisibleIndex
{
  RefreshStableAtmosphereState(ctx.GetScene().get());
  if (ctx.current_view.view_id == kInvalidViewId || !EnsurePublishResources()) {
    return kInvalidShaderVisibleIndex;
  }

  const auto& stable_state = atmosphere_state_->GetState();
  const auto refreshed_probe_state = ibl_->RefreshStaticSkyLightProducts(
    probe_state_, stable_state.view_products.sky_light);
  probe_state_ = refreshed_probe_state.probe_state;
  last_probe_refresh_state_ = {
    .frame_sequence = current_sequence_,
    .frame_slot = current_slot_,
    .requested = refreshed_probe_state.requested,
    .refreshed = refreshed_probe_state.refreshed,
    .valid = refreshed_probe_state.probe_state.valid,
    .probe_revision = refreshed_probe_state.probe_state.probes.probe_revision,
  };
  last_publication_state_.probe_revision = probe_state_.probes.probe_revision;
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
  const auto wants_volumetric_fog = CurrentViewWantsVolumetrics(ctx);
  if (!wants_volumetric_fog) {
    products.height_fog.enable_volumetric_fog = false;
    products.volumetric_fog.enabled = false;
    products.integrated_light_scattering_srv
      = ShaderVisibleIndex { kInvalidShaderVisibleIndex };
  }
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
  pending_volumetric_fog_state_ = {};
  if (wants_volumetric_fog) {
    const auto* local_fog_products = scene_textures != nullptr
      ? &PrepareLocalFogForStage14(ctx, *scene_textures)
      : nullptr;
    pending_volumetric_fog_state_ = volumetric_fog_pass_->Record(ctx,
      stable_state, products.distant_sky_light_lut_srv, local_fog_products);
    products.integrated_light_scattering_srv
      = pending_volumetric_fog_state_.integrated_light_scattering_srv;
  }
  const auto sky_light_authored_enabled = products.sky_light.enabled;
  const auto sky_light_ibl_valid = ProbeStateHasUsableResources(probe_state_);
  if (sky_light_authored_enabled) {
    products.flags
      |= environment::kEnvironmentViewProductFlagSkyLightAuthoredEnabled;
    if (sky_light_ibl_valid) {
      products.flags
        |= environment::kEnvironmentViewProductFlagSkyLightIblValid;
    } else {
      products.flags
        |= environment::kEnvironmentViewProductFlagSkyLightIblUnavailable;
    }
  }
  if (products.volumetric_fog.enabled) {
    products.flags
      |= environment::kEnvironmentViewProductFlagVolumetricFogAuthoredEnabled;
    if (products.integrated_light_scattering_srv.IsValid()) {
      products.flags |= environment::
        kEnvironmentViewProductFlagIntegratedLightScatteringValid;
    } else {
      products.flags |= environment::
        kEnvironmentViewProductFlagIntegratedLightScatteringUnavailable;
    }
  }

  const auto static_data = BuildEnvironmentStaticData(products);
  const auto resolved_environment_static_slot
    = environment_static_slot.IsValid()
    ? environment_static_slot
    : static_data_publisher_->Publish(ctx.current_view.view_id, static_data);

  const auto environment_view_products_slot
    = view_products_publisher_->Publish(ctx.current_view.view_id, products);
  const auto bindings = BuildBindings(resolved_environment_static_slot,
    resolved_environment_view_slot, environment_view_products_slot, products,
    enable_ambient_bridge);
  const auto slot
    = bindings_publisher_->Publish(ctx.current_view.view_id, bindings);
  published_views_.insert_or_assign(ctx.current_view.view_id,
    PublishedView {
      .slot = slot,
      .bindings = bindings,
      .static_data = static_data,
      .view_data = view_data,
      .view_products = products,
    });

  last_publication_state_.frame_sequence = current_sequence_;
  last_publication_state_.frame_slot = current_slot_;
  last_publication_state_.published_view_count += 1U;
  last_publication_state_.published_environment_view_count += 1U;
  last_publication_state_.published_environment_view_products_count += 1U;
  last_publication_state_.probe_revision = bindings.probes.probe_revision;
  if (bindings.ambient_bridge.flags != 0U) {
    last_publication_state_.ambient_bridge_view_count += 1U;
  }
  last_publication_state_.sky_light_authored_enabled
    = sky_light_authored_enabled;
  last_publication_state_.sky_light_ibl_valid = sky_light_ibl_valid;
  last_publication_state_.sky_light_ibl_unavailable
    = sky_light_authored_enabled && !sky_light_ibl_valid;
  last_publication_state_.sky_light_ibl_stale
    = (probe_state_.flags & kEnvironmentProbeStateFlagStale) != 0U;
  last_publication_state_.volumetric_fog_authored_enabled
    = products.volumetric_fog.enabled;
  last_publication_state_.integrated_light_scattering_valid
    = products.integrated_light_scattering_srv.IsValid();
  last_publication_state_.integrated_light_scattering_unavailable
    = products.volumetric_fog.enabled
    && !products.integrated_light_scattering_srv.IsValid();

  const auto& cache_state = atmosphere_lut_cache_->GetState();
  const auto atmosphere_lut_cache_valid = atmosphere_lut_cache_->IsFullyValid();

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
    .camera_aerial_sample_count_per_slice
    = cache_state.internal_parameters.camera_aerial_sample_count_per_slice,
    .camera_aerial_dispatch_count_x = camera_aerial_state.dispatch_count_x,
    .camera_aerial_dispatch_count_y = camera_aerial_state.dispatch_count_y,
    .camera_aerial_dispatch_count_z = camera_aerial_state.dispatch_count_z,
    .environment_view_products_published
    = environment_view_products_slot.IsValid(),
    .environment_view_products_slot = environment_view_products_slot,
    .sky_light_authored_enabled = sky_light_authored_enabled,
    .sky_light_ibl_valid = sky_light_ibl_valid,
    .sky_light_ibl_unavailable
    = sky_light_authored_enabled && !sky_light_ibl_valid,
    .volumetric_fog_authored_enabled = products.volumetric_fog.enabled,
    .integrated_light_scattering_valid
    = products.integrated_light_scattering_srv.IsValid(),
    .integrated_light_scattering_unavailable = products.volumetric_fog.enabled
      && !products.integrated_light_scattering_srv.IsValid(),
    .volumetric_fog_view_constants_bound
    = pending_volumetric_fog_state_.view_constants_bound,
    .volumetric_fog_ue_log_depth_distribution
    = pending_volumetric_fog_state_.ue_log_depth_distribution,
    .volumetric_fog_directional_shadowed_light_requested
    = pending_volumetric_fog_state_
      .directional_shadowed_light_injection_requested,
    .volumetric_fog_height_fog_media_requested
    = pending_volumetric_fog_state_.height_fog_media_requested,
    .volumetric_fog_height_fog_media_executed
    = pending_volumetric_fog_state_.height_fog_media_executed,
    .volumetric_fog_sky_light_injection_requested
    = pending_volumetric_fog_state_.sky_light_injection_requested,
    .volumetric_fog_sky_light_injection_executed
    = pending_volumetric_fog_state_.sky_light_injection_executed,
    .volumetric_fog_temporal_history_requested
    = pending_volumetric_fog_state_.temporal_history_requested,
    .volumetric_fog_temporal_history_reprojection_executed
    = pending_volumetric_fog_state_.temporal_history_reprojection_executed,
    .volumetric_fog_temporal_history_reset
    = pending_volumetric_fog_state_.temporal_history_reset,
    .volumetric_fog_local_fog_injection_requested
    = pending_volumetric_fog_state_.local_fog_injection_requested,
    .volumetric_fog_local_fog_injection_executed
    = pending_volumetric_fog_state_.local_fog_injection_executed,
    .volumetric_fog_local_fog_instance_count
    = pending_volumetric_fog_state_.local_fog_instance_count,
  };
  LOG_F(INFO,
    "environment_products_published={} environment_view_published={} "
    "atmosphere_lut_cache_valid={} "
    "transmittance_lut_published={} multi_scattering_lut_published={} "
    "distant_sky_light_lut_published={} sky_view_lut_published={} "
    "camera_aerial_perspective_published={} "
    "sky_light_authored_enabled={} sky_light_ibl_valid={} "
    "sky_light_ibl_unavailable={} volumetric_fog_authored_enabled={} "
    "integrated_light_scattering_valid={}",
    last_view_product_generation_state_.environment_view_products_published,
    last_view_product_generation_state_.environment_view_published,
    last_view_product_generation_state_.atmosphere_lut_cache_valid,
    last_view_product_generation_state_.transmittance_lut_srv.IsValid(),
    last_view_product_generation_state_.multi_scattering_lut_srv.IsValid(),
    last_view_product_generation_state_.distant_sky_light_lut_srv.IsValid(),
    last_view_product_generation_state_.sky_view_lut_srv.IsValid(),
    last_view_product_generation_state_.camera_aerial_perspective_srv.IsValid(),
    last_view_product_generation_state_.sky_light_authored_enabled,
    last_view_product_generation_state_.sky_light_ibl_valid,
    last_view_product_generation_state_.sky_light_ibl_unavailable,
    last_view_product_generation_state_.volumetric_fog_authored_enabled,
    last_view_product_generation_state_.integrated_light_scattering_valid);

  return slot;
}

auto EnvironmentLightingService::RenderSkyAndFog(
  RenderContext& ctx, const SceneTextures& scene_textures) -> void
{
  RefreshStableAtmosphereState(ctx.GetScene().get());
  const auto& local_fog_products
    = PrepareLocalFogForStage14(ctx, scene_textures);
  const auto local_fog_culling_state = pending_local_fog_culling_state_;
  const auto sky_state = sky_->Render(ctx, scene_textures);
  const auto atmosphere_state = atmosphere_->Render(ctx, scene_textures);
  const auto fog_state = fog_->Render(ctx, scene_textures);
  auto local_fog_compose_state
    = environment::LocalFogVolumeComposePass::RecordState {};
  if (!pending_volumetric_fog_state_.local_fog_injection_executed) {
    local_fog_compose_state
      = local_fog_compose_->Record(ctx, scene_textures, local_fog_products);
  }
  last_stage14_state_ = {
    .view_id = ctx.current_view.view_id,
    .requested
    = local_fog_products.prepared || pending_volumetric_fog_state_.requested,
    .local_fog_requested = local_fog_culling_state.requested,
    .local_fog_executed = local_fog_culling_state.executed,
    .local_fog_hzb_consumed
    = local_fog_culling_state.consumed_published_screen_hzb,
    .local_fog_hzb_unavailable = local_fog_culling_state.requested
      && !local_fog_culling_state.consumed_published_screen_hzb,
    .local_fog_buffer_ready
    = local_fog_products.buffer_ready && local_fog_products.tile_data_ready,
    .local_fog_skipped
    = local_fog_products.prepared && !local_fog_culling_state.executed,
    .local_fog_instance_count = local_fog_products.instance_count,
    .local_fog_dispatch_count_x = local_fog_culling_state.dispatch_count_x,
    .local_fog_dispatch_count_y = local_fog_culling_state.dispatch_count_y,
    .local_fog_dispatch_count_z = local_fog_culling_state.dispatch_count_z,
    .volumetric_fog_requested = pending_volumetric_fog_state_.requested,
    .volumetric_fog_executed = pending_volumetric_fog_state_.executed,
    .integrated_light_scattering_valid
    = pending_volumetric_fog_state_.integrated_light_scattering_srv.IsValid(),
    .integrated_light_scattering_srv
    = pending_volumetric_fog_state_.integrated_light_scattering_srv,
    .volumetric_fog_grid_width = pending_volumetric_fog_state_.width,
    .volumetric_fog_grid_height = pending_volumetric_fog_state_.height,
    .volumetric_fog_grid_depth = pending_volumetric_fog_state_.depth,
    .volumetric_fog_dispatch_count_x
    = pending_volumetric_fog_state_.dispatch_count_x,
    .volumetric_fog_dispatch_count_y
    = pending_volumetric_fog_state_.dispatch_count_y,
    .volumetric_fog_dispatch_count_z
    = pending_volumetric_fog_state_.dispatch_count_z,
    .volumetric_fog_view_constants_bound
    = pending_volumetric_fog_state_.view_constants_bound,
    .volumetric_fog_ue_log_depth_distribution
    = pending_volumetric_fog_state_.ue_log_depth_distribution,
    .volumetric_fog_directional_shadowed_light_requested
    = pending_volumetric_fog_state_
      .directional_shadowed_light_injection_requested,
    .volumetric_fog_height_fog_media_requested
    = pending_volumetric_fog_state_.height_fog_media_requested,
    .volumetric_fog_height_fog_media_executed
    = pending_volumetric_fog_state_.height_fog_media_executed,
    .volumetric_fog_sky_light_injection_requested
    = pending_volumetric_fog_state_.sky_light_injection_requested,
    .volumetric_fog_sky_light_injection_executed
    = pending_volumetric_fog_state_.sky_light_injection_executed,
    .volumetric_fog_temporal_history_requested
    = pending_volumetric_fog_state_.temporal_history_requested,
    .volumetric_fog_temporal_history_reprojection_executed
    = pending_volumetric_fog_state_.temporal_history_reprojection_executed,
    .volumetric_fog_temporal_history_reset
    = pending_volumetric_fog_state_.temporal_history_reset,
    .volumetric_fog_local_fog_injection_requested
    = pending_volumetric_fog_state_.local_fog_injection_requested,
    .volumetric_fog_local_fog_injection_executed
    = pending_volumetric_fog_state_.local_fog_injection_executed,
    .volumetric_fog_local_fog_instance_count
    = pending_volumetric_fog_state_.local_fog_instance_count,
  };
  LOG_F(INFO,
    "volumetric_fog_requested={} volumetric_fog_executed={} "
    "integrated_light_scattering_valid={} volumetric_fog_grid={}x{}x{} "
    "volumetric_fog_dispatch={}x{}x{} "
    "volumetric_fog_view_constants_bound={} "
    "volumetric_fog_ue_log_depth_distribution={} "
    "volumetric_fog_directional_shadowed_light_requested={} "
    "volumetric_fog_height_fog_media_requested={} "
    "volumetric_fog_height_fog_media_executed={} "
    "volumetric_fog_sky_light_injection_requested={} "
    "volumetric_fog_sky_light_injection_executed={} "
    "volumetric_fog_temporal_history_requested={} "
    "volumetric_fog_temporal_history_reprojection_executed={} "
    "volumetric_fog_temporal_history_reset={} "
    "volumetric_fog_local_fog_injection_requested={} "
    "volumetric_fog_local_fog_injection_executed={} "
    "volumetric_fog_local_fog_instance_count={}",
    last_stage14_state_.volumetric_fog_requested,
    last_stage14_state_.volumetric_fog_executed,
    last_stage14_state_.integrated_light_scattering_valid,
    last_stage14_state_.volumetric_fog_grid_width,
    last_stage14_state_.volumetric_fog_grid_height,
    last_stage14_state_.volumetric_fog_grid_depth,
    last_stage14_state_.volumetric_fog_dispatch_count_x,
    last_stage14_state_.volumetric_fog_dispatch_count_y,
    last_stage14_state_.volumetric_fog_dispatch_count_z,
    last_stage14_state_.volumetric_fog_view_constants_bound,
    last_stage14_state_.volumetric_fog_ue_log_depth_distribution,
    last_stage14_state_.volumetric_fog_directional_shadowed_light_requested,
    last_stage14_state_.volumetric_fog_height_fog_media_requested,
    last_stage14_state_.volumetric_fog_height_fog_media_executed,
    last_stage14_state_.volumetric_fog_sky_light_injection_requested,
    last_stage14_state_.volumetric_fog_sky_light_injection_executed,
    last_stage14_state_.volumetric_fog_temporal_history_requested,
    last_stage14_state_.volumetric_fog_temporal_history_reprojection_executed,
    last_stage14_state_.volumetric_fog_temporal_history_reset,
    last_stage14_state_.volumetric_fog_local_fog_injection_requested,
    last_stage14_state_.volumetric_fog_local_fog_injection_executed,
    last_stage14_state_.volumetric_fog_local_fog_instance_count);
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

auto EnvironmentLightingService::InspectEnvironmentStaticData(
  const ViewId view_id) const -> const EnvironmentStaticData*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.static_data : nullptr;
}

auto EnvironmentLightingService::InspectEnvironmentViewProducts(
  const ViewId view_id) const -> const environment::EnvironmentViewProducts*
{
  const auto it = published_views_.find(view_id);
  return it != published_views_.end() ? &it->second.view_products : nullptr;
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
