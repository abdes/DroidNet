//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>

#include <bit>

#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::vortex::environment::internal {

namespace {

  auto HashCombineU64(std::uint64_t seed, const std::uint64_t value)
    -> std::uint64_t
  {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
  }

  auto FloatBits(const float value) -> std::uint32_t
  {
    return std::bit_cast<std::uint32_t>(value);
  }

  auto BuildAtmosphereModel(const scene::SceneEnvironment* environment_systems)
    -> environment::AtmosphereModel
  {
    auto model = environment::AtmosphereModel {};
    if (environment_systems == nullptr) {
      return model;
    }
    const auto atmosphere
      = environment_systems->TryGetSystem<scene::environment::SkyAtmosphere>();
    if (atmosphere == nullptr) {
      return model;
    }

    model.enabled = atmosphere->IsEnabled();
    model.transform_mode = static_cast<environment::AtmosphereTransformMode>(
      atmosphere->GetTransformMode());
    model.planet_radius_m = atmosphere->GetPlanetRadiusMeters();
    model.atmosphere_height_m = atmosphere->GetAtmosphereHeightMeters();
    model.ground_albedo_rgb = atmosphere->GetGroundAlbedoRgb();
    model.planet_anchor_position_ws
      = atmosphere->GetPlanetAnchorWorldPosition();
    model.rayleigh_scattering_rgb = atmosphere->GetRayleighScatteringRgb();
    model.rayleigh_scale_height_m = atmosphere->GetRayleighScaleHeightMeters();
    model.mie_scattering_rgb = atmosphere->GetMieScatteringRgb();
    model.mie_absorption_rgb = atmosphere->GetMieAbsorptionRgb();
    model.mie_scale_height_m = atmosphere->GetMieScaleHeightMeters();
    model.mie_anisotropy = atmosphere->GetMieAnisotropy();
    model.ozone_absorption_rgb = atmosphere->GetAbsorptionRgb();
    model.ozone_density_profile = atmosphere->GetOzoneDensityProfile();
    model.multi_scattering_factor = atmosphere->GetMultiScatteringFactor();
    model.sky_luminance_factor_rgb = atmosphere->GetSkyLuminanceFactorRgb();
    model.sky_and_aerial_perspective_luminance_factor_rgb
      = atmosphere->GetSkyAndAerialPerspectiveLuminanceFactorRgb();
    model.aerial_perspective_distance_scale
      = atmosphere->GetAerialPerspectiveDistanceScale();
    model.aerial_scattering_strength
      = atmosphere->GetAerialScatteringStrength();
    model.aerial_perspective_start_depth_m
      = atmosphere->GetAerialPerspectiveStartDepthMeters();
    model.height_fog_contribution = atmosphere->GetHeightFogContribution();
    model.trace_sample_count_scale = atmosphere->GetTraceSampleCountScale();
    model.transmittance_min_light_elevation_deg
      = atmosphere->GetTransmittanceMinLightElevationDeg();
    model.sun_disk_enabled = atmosphere->GetSunDiskEnabled();
    model.holdout = atmosphere->GetHoldout();
    model.render_in_main_pass = atmosphere->GetRenderInMainPass();
    return model;
  }

  auto BuildHeightFogModel(const scene::SceneEnvironment* environment_systems)
    -> environment::HeightFogModel
  {
    auto model = environment::HeightFogModel {};
    if (environment_systems == nullptr) {
      return model;
    }
    const auto fog
      = environment_systems->TryGetSystem<scene::environment::Fog>();
    if (fog == nullptr) {
      return model;
    }

    model.enabled = fog->IsEnabled();
    model.legacy_model = static_cast<std::uint32_t>(fog->GetModel());
    model.enable_height_fog = fog->GetEnableHeightFog();
    model.enable_volumetric_fog = fog->GetEnableVolumetricFog();
    model.fog_density = fog->GetExtinctionSigmaTPerMeter();
    model.fog_height_falloff = fog->GetHeightFalloffPerMeter();
    model.fog_height_offset = fog->GetHeightOffsetMeters();
    model.second_fog_density = fog->GetSecondFogDensity();
    model.second_fog_height_falloff = fog->GetSecondFogHeightFalloff();
    model.second_fog_height_offset = fog->GetSecondFogHeightOffset();
    model.fog_inscattering_luminance = fog->GetFogInscatteringLuminance();
    model.sky_atmosphere_ambient_contribution_color_scale
      = fog->GetSkyAtmosphereAmbientContributionColorScale();
    model.inscattering_color_cubemap_resource
      = fog->GetInscatteringColorCubemapResource();
    model.inscattering_color_cubemap_angle
      = fog->GetInscatteringColorCubemapAngle();
    model.inscattering_texture_tint = fog->GetInscatteringTextureTint();
    model.fully_directional_inscattering_color_distance
      = fog->GetFullyDirectionalInscatteringColorDistance();
    model.non_directional_inscattering_color_distance
      = fog->GetNonDirectionalInscatteringColorDistance();
    model.directional_inscattering_luminance
      = fog->GetDirectionalInscatteringLuminance();
    model.directional_inscattering_exponent
      = fog->GetDirectionalInscatteringExponent();
    model.directional_inscattering_start_distance
      = fog->GetDirectionalInscatteringStartDistance();
    model.fog_max_opacity = fog->GetMaxOpacity();
    model.start_distance = fog->GetStartDistanceMeters();
    model.end_distance = fog->GetEndDistanceMeters();
    model.fog_cutoff_distance = fog->GetFogCutoffDistanceMeters();
    model.holdout = fog->GetHoldout();
    model.render_in_main_pass = fog->GetRenderInMainPass();
    model.visible_in_reflection_captures
      = fog->GetVisibleInReflectionCaptures();
    model.visible_in_real_time_sky_captures
      = fog->GetVisibleInRealTimeSkyCaptures();
    return model;
  }

  auto BuildVolumetricFogModel(
    const scene::SceneEnvironment* environment_systems)
    -> environment::VolumetricFogModel
  {
    auto model = environment::VolumetricFogModel {};
    if (environment_systems == nullptr) {
      return model;
    }
    const auto fog
      = environment_systems->TryGetSystem<scene::environment::Fog>();
    if (fog == nullptr) {
      return model;
    }

    model.enabled = fog->IsEnabled() && fog->GetEnableVolumetricFog();
    model.scattering_distribution
      = fog->GetVolumetricFogScatteringDistribution();
    model.albedo = fog->GetVolumetricFogAlbedo();
    model.emissive = fog->GetVolumetricFogEmissive();
    model.extinction_scale = fog->GetVolumetricFogExtinctionScale();
    model.distance = fog->GetVolumetricFogDistance();
    model.start_distance = fog->GetVolumetricFogStartDistance();
    model.near_fade_in_distance = fog->GetVolumetricFogNearFadeInDistance();
    model.static_lighting_scattering_intensity
      = fog->GetVolumetricFogStaticLightingScatteringIntensity();
    model.override_light_colors_with_fog_inscattering_colors
      = fog->GetOverrideLightColorsWithFogInscatteringColors();
    return model;
  }

  auto BuildSkyLightModel(const scene::SceneEnvironment* environment_systems)
    -> environment::SkyLightEnvironmentModel
  {
    auto model = environment::SkyLightEnvironmentModel {};
    if (environment_systems == nullptr) {
      return model;
    }
    const auto sky_light
      = environment_systems->TryGetSystem<scene::environment::SkyLight>();
    if (sky_light == nullptr) {
      return model;
    }

    model.enabled = sky_light->IsEnabled();
    model.source = static_cast<std::uint32_t>(sky_light->GetSource());
    model.cubemap_resource = sky_light->GetCubemapResource();
    model.intensity_mul = sky_light->GetIntensityMul();
    model.tint_rgb = sky_light->GetTintRgb();
    model.diffuse_intensity = sky_light->GetDiffuseIntensity();
    model.specular_intensity = sky_light->GetSpecularIntensity();
    model.real_time_capture_enabled = sky_light->GetRealTimeCaptureEnabled();
    model.source_cubemap_angle_radians
      = sky_light->GetSourceCubemapAngleRadians();
    model.lower_hemisphere_color = sky_light->GetLowerHemisphereColor();
    model.lower_hemisphere_is_solid_color
      = sky_light->GetLowerHemisphereIsSolidColor();
    model.lower_hemisphere_blend_alpha
      = sky_light->GetLowerHemisphereBlendAlpha();
    model.volumetric_scattering_intensity
      = sky_light->GetVolumetricScatteringIntensity();
    model.affect_reflections = sky_light->GetAffectReflections();
    model.affect_global_illumination = sky_light->GetAffectGlobalIllumination();
    return model;
  }

  auto HashAtmosphereModel(const environment::AtmosphereModel& model)
    -> std::uint64_t
  {
    auto seed = std::uint64_t { 0U };
    seed = HashCombineU64(seed, static_cast<std::uint64_t>(model.enabled));
    seed
      = HashCombineU64(seed, static_cast<std::uint64_t>(model.transform_mode));
    seed = HashCombineU64(seed, FloatBits(model.planet_radius_m));
    seed = HashCombineU64(seed, FloatBits(model.atmosphere_height_m));
    seed = HashCombineU64(seed, FloatBits(model.ground_albedo_rgb.x));
    seed = HashCombineU64(seed, FloatBits(model.ground_albedo_rgb.y));
    seed = HashCombineU64(seed, FloatBits(model.ground_albedo_rgb.z));
    seed = HashCombineU64(seed, FloatBits(model.planet_anchor_position_ws.x));
    seed = HashCombineU64(seed, FloatBits(model.planet_anchor_position_ws.y));
    seed = HashCombineU64(seed, FloatBits(model.planet_anchor_position_ws.z));
    seed = HashCombineU64(seed, FloatBits(model.rayleigh_scattering_rgb.x));
    seed = HashCombineU64(seed, FloatBits(model.rayleigh_scattering_rgb.y));
    seed = HashCombineU64(seed, FloatBits(model.rayleigh_scattering_rgb.z));
    seed = HashCombineU64(seed, FloatBits(model.rayleigh_scale_height_m));
    seed = HashCombineU64(seed, FloatBits(model.mie_scattering_rgb.x));
    seed = HashCombineU64(seed, FloatBits(model.mie_scattering_rgb.y));
    seed = HashCombineU64(seed, FloatBits(model.mie_scattering_rgb.z));
    seed = HashCombineU64(seed, FloatBits(model.mie_absorption_rgb.x));
    seed = HashCombineU64(seed, FloatBits(model.mie_absorption_rgb.y));
    seed = HashCombineU64(seed, FloatBits(model.mie_absorption_rgb.z));
    seed = HashCombineU64(seed, FloatBits(model.mie_scale_height_m));
    seed = HashCombineU64(seed, FloatBits(model.mie_anisotropy));
    seed = HashCombineU64(seed, FloatBits(model.ozone_absorption_rgb.x));
    seed = HashCombineU64(seed, FloatBits(model.ozone_absorption_rgb.y));
    seed = HashCombineU64(seed, FloatBits(model.ozone_absorption_rgb.z));
    for (const auto& layer : model.ozone_density_profile.layers) {
      seed = HashCombineU64(seed, FloatBits(layer.width_m));
      seed = HashCombineU64(seed, FloatBits(layer.exp_term));
      seed = HashCombineU64(seed, FloatBits(layer.linear_term));
      seed = HashCombineU64(seed, FloatBits(layer.constant_term));
    }
    seed = HashCombineU64(seed, FloatBits(model.multi_scattering_factor));
    seed = HashCombineU64(seed, FloatBits(model.sky_luminance_factor_rgb.x));
    seed = HashCombineU64(seed, FloatBits(model.sky_luminance_factor_rgb.y));
    seed = HashCombineU64(seed, FloatBits(model.sky_luminance_factor_rgb.z));
    seed = HashCombineU64(
      seed, FloatBits(model.sky_and_aerial_perspective_luminance_factor_rgb.x));
    seed = HashCombineU64(
      seed, FloatBits(model.sky_and_aerial_perspective_luminance_factor_rgb.y));
    seed = HashCombineU64(
      seed, FloatBits(model.sky_and_aerial_perspective_luminance_factor_rgb.z));
    seed = HashCombineU64(
      seed, FloatBits(model.aerial_perspective_distance_scale));
    seed = HashCombineU64(seed, FloatBits(model.aerial_scattering_strength));
    seed
      = HashCombineU64(seed, FloatBits(model.aerial_perspective_start_depth_m));
    seed = HashCombineU64(seed, FloatBits(model.height_fog_contribution));
    seed = HashCombineU64(seed, FloatBits(model.trace_sample_count_scale));
    seed = HashCombineU64(
      seed, FloatBits(model.transmittance_min_light_elevation_deg));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.sun_disk_enabled));
    seed = HashCombineU64(seed, static_cast<std::uint64_t>(model.holdout));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.render_in_main_pass));
    return seed;
  }

  auto HashHeightFogModel(const environment::HeightFogModel& model)
    -> std::uint64_t
  {
    auto seed = std::uint64_t { 0U };
    seed = HashCombineU64(seed, static_cast<std::uint64_t>(model.enabled));
    seed = HashCombineU64(seed, model.legacy_model);
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.enable_height_fog));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.enable_volumetric_fog));
    seed = HashCombineU64(seed, FloatBits(model.fog_density));
    seed = HashCombineU64(seed, FloatBits(model.fog_height_falloff));
    seed = HashCombineU64(seed, FloatBits(model.fog_height_offset));
    seed = HashCombineU64(seed, FloatBits(model.second_fog_density));
    seed = HashCombineU64(seed, FloatBits(model.second_fog_height_falloff));
    seed = HashCombineU64(seed, FloatBits(model.second_fog_height_offset));
    seed = HashCombineU64(seed, FloatBits(model.fog_inscattering_luminance.x));
    seed = HashCombineU64(seed, FloatBits(model.fog_inscattering_luminance.y));
    seed = HashCombineU64(seed, FloatBits(model.fog_inscattering_luminance.z));
    seed = HashCombineU64(
      seed, FloatBits(model.sky_atmosphere_ambient_contribution_color_scale.x));
    seed = HashCombineU64(
      seed, FloatBits(model.sky_atmosphere_ambient_contribution_color_scale.y));
    seed = HashCombineU64(
      seed, FloatBits(model.sky_atmosphere_ambient_contribution_color_scale.z));
    seed
      = HashCombineU64(seed, model.inscattering_color_cubemap_resource.get());
    seed
      = HashCombineU64(seed, FloatBits(model.inscattering_color_cubemap_angle));
    seed = HashCombineU64(seed, FloatBits(model.inscattering_texture_tint.x));
    seed = HashCombineU64(seed, FloatBits(model.inscattering_texture_tint.y));
    seed = HashCombineU64(seed, FloatBits(model.inscattering_texture_tint.z));
    seed = HashCombineU64(
      seed, FloatBits(model.fully_directional_inscattering_color_distance));
    seed = HashCombineU64(
      seed, FloatBits(model.non_directional_inscattering_color_distance));
    seed = HashCombineU64(
      seed, FloatBits(model.directional_inscattering_luminance.x));
    seed = HashCombineU64(
      seed, FloatBits(model.directional_inscattering_luminance.y));
    seed = HashCombineU64(
      seed, FloatBits(model.directional_inscattering_luminance.z));
    seed = HashCombineU64(
      seed, FloatBits(model.directional_inscattering_exponent));
    seed = HashCombineU64(
      seed, FloatBits(model.directional_inscattering_start_distance));
    seed = HashCombineU64(seed, FloatBits(model.fog_max_opacity));
    seed = HashCombineU64(seed, FloatBits(model.start_distance));
    seed = HashCombineU64(seed, FloatBits(model.end_distance));
    seed = HashCombineU64(seed, FloatBits(model.fog_cutoff_distance));
    seed = HashCombineU64(seed, static_cast<std::uint64_t>(model.holdout));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.render_in_main_pass));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.visible_in_reflection_captures));
    seed = HashCombineU64(seed,
      static_cast<std::uint64_t>(model.visible_in_real_time_sky_captures));
    return seed;
  }

  auto HashSkyLightModel(const environment::SkyLightEnvironmentModel& model)
    -> std::uint64_t
  {
    auto seed = std::uint64_t { 0U };
    seed = HashCombineU64(seed, static_cast<std::uint64_t>(model.enabled));
    seed = HashCombineU64(seed, model.source);
    seed = HashCombineU64(seed, model.cubemap_resource.get());
    seed = HashCombineU64(seed, FloatBits(model.intensity_mul));
    seed = HashCombineU64(seed, FloatBits(model.tint_rgb.x));
    seed = HashCombineU64(seed, FloatBits(model.tint_rgb.y));
    seed = HashCombineU64(seed, FloatBits(model.tint_rgb.z));
    seed = HashCombineU64(seed, FloatBits(model.diffuse_intensity));
    seed = HashCombineU64(seed, FloatBits(model.specular_intensity));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.real_time_capture_enabled));
    seed = HashCombineU64(seed, FloatBits(model.source_cubemap_angle_radians));
    seed = HashCombineU64(seed, FloatBits(model.lower_hemisphere_color.x));
    seed = HashCombineU64(seed, FloatBits(model.lower_hemisphere_color.y));
    seed = HashCombineU64(seed, FloatBits(model.lower_hemisphere_color.z));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.lower_hemisphere_is_solid_color));
    seed = HashCombineU64(seed, FloatBits(model.lower_hemisphere_blend_alpha));
    seed
      = HashCombineU64(seed, FloatBits(model.volumetric_scattering_intensity));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.affect_reflections));
    seed = HashCombineU64(
      seed, static_cast<std::uint64_t>(model.affect_global_illumination));
    return seed;
  }

  auto HashVolumetricFogModel(const environment::VolumetricFogModel& model)
    -> std::uint64_t
  {
    auto seed = std::uint64_t { 0U };
    seed = HashCombineU64(seed, static_cast<std::uint64_t>(model.enabled));
    seed = HashCombineU64(seed, FloatBits(model.scattering_distribution));
    seed = HashCombineU64(seed, FloatBits(model.albedo.x));
    seed = HashCombineU64(seed, FloatBits(model.albedo.y));
    seed = HashCombineU64(seed, FloatBits(model.albedo.z));
    seed = HashCombineU64(seed, FloatBits(model.emissive.x));
    seed = HashCombineU64(seed, FloatBits(model.emissive.y));
    seed = HashCombineU64(seed, FloatBits(model.emissive.z));
    seed = HashCombineU64(seed, FloatBits(model.extinction_scale));
    seed = HashCombineU64(seed, FloatBits(model.distance));
    seed = HashCombineU64(seed, FloatBits(model.start_distance));
    seed = HashCombineU64(seed, FloatBits(model.near_fade_in_distance));
    seed = HashCombineU64(
      seed, FloatBits(model.static_lighting_scattering_intensity));
    seed = HashCombineU64(seed,
      static_cast<std::uint64_t>(
        model.override_light_colors_with_fog_inscattering_colors));
    return seed;
  }

} // namespace

auto AtmosphereState::Update(const scene::Scene& scene_ref,
  const ResolvedAtmosphereLightState& light_state) -> bool
{
  const auto environment_systems = scene_ref.GetEnvironment().get();

  auto next = StableAtmosphereState {};
  next.view_products.atmosphere = BuildAtmosphereModel(environment_systems);
  next.view_products.height_fog = BuildHeightFogModel(environment_systems);
  next.view_products.sky_light = BuildSkyLightModel(environment_systems);
  next.view_products.volumetric_fog
    = BuildVolumetricFogModel(environment_systems);
  next.view_products.atmosphere_lights = light_state.atmosphere_lights;
  next.view_products.atmosphere_light_count = light_state.active_light_count;
  next.view_products.conventional_shadow_authority_slot
    = light_state.shadow_authority_slot;
  next.conventional_shadow_authority_slot = light_state.shadow_authority_slot;
  next.conventional_shadow_cascade_count
    = light_state.shadow_authority_slot == 0U
    ? light_state.source_cascade_counts[0]
    : 0U;
  next.conventional_shadow_authority_slot0_only
    = light_state.shadow_authority_slot0_only;
  next.light_revision = light_state.revision;

  next.authored_hash = HashAtmosphereModel(next.view_products.atmosphere);
  next.authored_hash = HashCombineU64(
    next.authored_hash, HashHeightFogModel(next.view_products.height_fog));
  next.authored_hash = HashCombineU64(
    next.authored_hash, HashSkyLightModel(next.view_products.sky_light));
  next.authored_hash = HashCombineU64(next.authored_hash,
    HashVolumetricFogModel(next.view_products.volumetric_fog));
  next.atmosphere_revision = next.authored_hash == state_.authored_hash
    ? state_.atmosphere_revision
    : state_.atmosphere_revision + 1U;

  const auto stable_hash
    = HashCombineU64(next.authored_hash, light_state.authored_hash);
  next.stable_revision = stable_hash == stable_hash_
    ? state_.stable_revision
    : state_.stable_revision + 1U;
  if (stable_hash == stable_hash_) {
    return false;
  }

  stable_hash_ = stable_hash;
  state_ = next;
  return true;
}

auto AtmosphereState::Reset() -> void
{
  stable_hash_ = 0U;
  state_ = StableAtmosphereState {};
}

} // namespace oxygen::vortex::environment::internal
