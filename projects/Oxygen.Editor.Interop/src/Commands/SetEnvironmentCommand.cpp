//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <cmath>
#include <stdexcept>
#include <string>
#include <Oxygen/Scene/ExposureSettings.h>

#include <Commands/SetEnvironmentCommand.h>

#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Scene.h>

namespace {

template <typename T>
auto EnsureSystem(oxygen::scene::SceneEnvironment& environment) -> T*
{
  auto system = environment.TryGetSystem<T>();
  if (!system) {
    system = oxygen::observer_ptr { &environment.AddSystem<T>() };
  }

  return system.get();
}

auto MapExposureMode(const int value) -> oxygen::engine::ExposureMode
{
  switch (value) {
  case 0: return oxygen::engine::ExposureMode::kManual;
  case 1: return oxygen::engine::ExposureMode::kManualCamera;
  case 2: return oxygen::engine::ExposureMode::kAuto;
  default: throw std::invalid_argument("Unknown exposure mode");
  }
}

auto MapMeteringMode(const int value) -> oxygen::engine::MeteringMode
{
  switch (value) {
  case 1: return oxygen::engine::MeteringMode::kCenterWeighted;
  case 2: return oxygen::engine::MeteringMode::kSpot;
  case 0: return oxygen::engine::MeteringMode::kAverage;
  default: throw std::invalid_argument("Unknown exposure metering mode");
  }
}

auto MapToneMapper(const int value) -> oxygen::engine::ToneMapper
{
  switch (value) {
  case 0: return oxygen::engine::ToneMapper::kNone;
  case 2: return oxygen::engine::ToneMapper::kFilmic;
  case 3: return oxygen::engine::ToneMapper::kReinhard;
  case 1: return oxygen::engine::ToneMapper::kAcesFitted;
  default: throw std::invalid_argument("Unknown tone mapper");
  }
}

auto EnsureEnvironment(oxygen::scene::Scene& scene)
  -> oxygen::scene::SceneEnvironment*
{
  auto environment = scene.GetEnvironment();
  if (environment) {
    return environment.get();
  }

  auto owned = std::make_unique<oxygen::scene::SceneEnvironment>();
  scene.SetEnvironment(std::move(owned));
  return scene.GetEnvironment().get();
}

auto EnsureSkyAtmosphereWhenEnabled(
  oxygen::scene::SceneEnvironment& environment, const bool enabled)
  -> oxygen::scene::environment::SkyAtmosphere*
{
  auto atmosphere
    = environment.TryGetSystem<oxygen::scene::environment::SkyAtmosphere>();
  if (enabled && !atmosphere) {
    atmosphere = oxygen::observer_ptr {
      &environment.AddSystem<oxygen::scene::environment::SkyAtmosphere>()
    };
  }

  return atmosphere.get();
}

auto ApplySkyAtmosphere(
  oxygen::scene::environment::SkyAtmosphere& atmosphere,
  const oxygen::interop::module::SkyAtmosphereParams& params) -> void
{
  namespace atmos = oxygen::engine::atmos;
  namespace env = oxygen::scene::environment;

  atmosphere.SetEnabled(params.enabled);
  if (!params.enabled) {
    return;
  }

  atmosphere.SetTransformMode(
    env::SkyAtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin);
  atmosphere.SetRenderInMainPass(true);
  atmosphere.SetPlanetRadiusMeters(params.planet_radius_m);
  atmosphere.SetAtmosphereHeightMeters(params.atmosphere_height_m);
  atmosphere.SetGroundAlbedoRgb(params.ground_albedo_rgb);
  atmosphere.SetRayleighScatteringRgb(atmos::kDefaultRayleighScatteringRgb);
  atmosphere.SetRayleighScaleHeightMeters(params.rayleigh_scale_height_m);
  atmosphere.SetMieScatteringRgb(atmos::kDefaultMieScatteringRgb);
  atmosphere.SetMieAbsorptionRgb(atmos::kDefaultMieAbsorptionRgb);
  atmosphere.SetMieScaleHeightMeters(params.mie_scale_height_m);
  atmosphere.SetMieAnisotropy(params.mie_anisotropy);
  atmosphere.SetOzoneAbsorptionRgb(atmos::kDefaultOzoneAbsorptionRgb);
  atmosphere.SetOzoneDensityProfile(atmos::kDefaultOzoneDensityProfile);
  atmosphere.SetMultiScatteringFactor(1.0F);
  atmosphere.SetSkyLuminanceFactorRgb(params.sky_luminance_factor_rgb);
  atmosphere.SetSkyAndAerialPerspectiveLuminanceFactorRgb(
    params.sky_luminance_factor_rgb);
  atmosphere.SetSunDiskEnabled(params.sun_disk_enabled);
  atmosphere.SetAerialPerspectiveDistanceScale(
    params.aerial_perspective_distance_scale);
  atmosphere.SetAerialScatteringStrength(params.aerial_scattering_strength);
  atmosphere.SetAerialPerspectiveStartDepthMeters(
    params.aerial_perspective_start_depth_m);
  atmosphere.SetHeightFogContribution(params.height_fog_contribution);
  atmosphere.SetTraceSampleCountScale(1.0F);
  atmosphere.SetTransmittanceMinLightElevationDeg(-90.0F);
  atmosphere.SetHoldout(false);
}

auto RequireFinite(const oxygen::Vec3& value, const char* message) -> void
{
  if (!std::isfinite(value.x) || !std::isfinite(value.y)
    || !std::isfinite(value.z)) {
    throw std::invalid_argument(message);
  }
}

auto RequireNonNegative(const float value, const char* message) -> void
{
  if (!std::isfinite(value) || value < 0.0F) {
    throw std::invalid_argument(message);
  }
}

auto RequireNonNegative(const oxygen::Vec3& value, const char* message) -> void
{
  RequireFinite(value, message);
  if (value.x < 0.0F || value.y < 0.0F || value.z < 0.0F) {
    throw std::invalid_argument(message);
  }
}

auto ValidateSky(const oxygen::interop::module::SkyParams& sky) -> void
{
  const auto& sphere = sky.sky_sphere;
  if (sphere.source != 0 && sphere.source != 1) {
    throw std::invalid_argument("Unknown sky sphere source");
  }
  RequireNonNegative(sphere.solid_color_rgb,
    "Sky sphere solid color must be finite and nonnegative");
  RequireNonNegative(
    sphere.intensity, "Sky sphere intensity must be finite and nonnegative");
  if (!std::isfinite(sphere.rotation_radians)) {
    throw std::invalid_argument("Sky sphere rotation must be finite");
  }
  RequireNonNegative(
    sphere.tint_rgb, "Sky sphere tint must be finite and nonnegative");

  const auto& light = sky.sky_light;
  if (light.source != 0 && light.source != 1) {
    throw std::invalid_argument("Unknown sky light source");
  }
  for (const auto value : { light.intensity, light.diffuse_intensity,
         light.specular_intensity, light.volumetric_scattering_intensity }) {
    RequireNonNegative(
      value, "Sky light multipliers must be finite and nonnegative");
  }
  RequireNonNegative(
    light.tint_rgb, "Sky light tint must be finite and nonnegative");
  if (!std::isfinite(light.cubemap_angle_radians)) {
    throw std::invalid_argument("Sky light cubemap rotation must be finite");
  }
  RequireNonNegative(light.lower_hemisphere_color,
    "Sky light lower hemisphere color must be finite and nonnegative");
  if (!std::isfinite(light.lower_hemisphere_blend_alpha)
    || light.lower_hemisphere_blend_alpha < 0.0F
    || light.lower_hemisphere_blend_alpha > 1.0F) {
    throw std::invalid_argument(
      "Sky light lower hemisphere blend must be between 0 and 1");
  }

  const auto& color = sky.background.color_rgb;
  RequireFinite(color, "Background color must be finite.");
  if (color.x < 0.0F || color.y < 0.0F || color.z < 0.0F || color.x > 1.0F
    || color.y > 1.0F || color.z > 1.0F) {
    throw std::invalid_argument("Background color must be between 0 and 1.");
  }
}

auto ApplySky(oxygen::scene::SceneEnvironment& environment,
  const oxygen::interop::module::SkyParams& sky,
  const oxygen::content::ResourceKey sky_sphere_cubemap,
  const oxygen::content::ResourceKey sky_light_cubemap) -> void
{
  namespace env = oxygen::scene::environment;

  auto* const sphere = EnsureSystem<env::SkySphere>(environment);
  sphere->SetEnabled(sky.sky_sphere.enabled);
  sphere->SetSource(static_cast<env::SkySphereSource>(sky.sky_sphere.source));
  sphere->SetCubemapResource(sky_sphere_cubemap);
  sphere->SetSolidColorRgb(sky.sky_sphere.solid_color_rgb);
  sphere->SetIlluminanceLux(sky.sky_sphere.illuminance_lux);
  sphere->SetIntensity(sky.sky_sphere.intensity);
  sphere->SetRotationRadians(sky.sky_sphere.rotation_radians);
  sphere->SetTintRgb(sky.sky_sphere.tint_rgb);

  const auto& authored = sky.sky_light;
  auto* const light = EnsureSystem<env::SkyLight>(environment);
  light->SetEnabled(authored.enabled);
  light->SetSource(static_cast<env::SkyLightSource>(authored.source));
  light->SetCubemapResource(sky_light_cubemap);
  light->SetIlluminanceLux(authored.illuminance_lux);
  light->SetIntensityMul(authored.intensity);
  light->SetTintRgb(authored.tint_rgb);
  light->SetDiffuseIntensity(authored.diffuse_intensity);
  light->SetSpecularIntensity(authored.specular_intensity);
  light->SetSourceCubemapAngleRadians(authored.cubemap_angle_radians);
  light->SetLowerHemisphereColor(authored.lower_hemisphere_color);
  light->SetLowerHemisphereIsSolidColor(
    authored.lower_hemisphere_is_solid_color);
  light->SetLowerHemisphereBlendAlpha(authored.lower_hemisphere_blend_alpha);
  light->SetVolumetricScatteringIntensity(
    authored.volumetric_scattering_intensity);
  light->SetAffectReflections(authored.affect_reflections);

  auto* const background = EnsureSystem<env::Background>(environment);
  background->SetEnabled(sky.background.enabled);
  background->SetColorRgb(sky.background.color_rgb);
}

auto ResolvePostProcessExposure(const oxygen::interop::module::PostProcessParams& params)
  -> oxygen::scene::ExposureSettings
{
  auto exposure = oxygen::scene::ExposureSettings {};
  exposure.enabled = params.exposure_enabled;
  exposure.mode = MapExposureMode(params.exposure_mode);
  exposure.metering_mode = MapMeteringMode(params.auto_exposure_metering_mode);
  exposure.key = params.exposure_key;
  exposure.manual_ev = params.manual_exposure_ev;
  exposure.compensation_ev = params.exposure_compensation_ev;
  exposure.min_ev = params.auto_exposure_min_ev;
  exposure.max_ev = params.auto_exposure_max_ev;
  exposure.speed_up = params.auto_exposure_speed_up;
  exposure.speed_down = params.auto_exposure_speed_down;
  exposure.low_percentile = params.auto_exposure_low_percentile;
  exposure.high_percentile = params.auto_exposure_high_percentile;
  exposure.min_log_luminance = params.auto_exposure_min_log_luminance;
  exposure.log_luminance_range = params.auto_exposure_log_luminance_range;
  exposure.target_luminance = params.auto_exposure_target_luminance;
  exposure.spot_meter_radius = params.auto_exposure_spot_meter_radius;
  exposure.black_influence = params.auto_exposure_black_influence;
  exposure.transition_distance = params.auto_exposure_transition_distance_ev;
  exposure.compensation_curve = params.auto_exposure_compensation_curve;
  const auto resolved = oxygen::scene::ResolveExposureSettings(exposure);
  if (!resolved && resolved.error() != oxygen::scene::ExposureSettingsError::kMissingCameraEv) {
    throw std::invalid_argument("Exposure edit rejected: "
      + std::string(oxygen::scene::to_string(resolved.error())));
  }
  static_cast<void>(MapToneMapper(params.tone_mapper));
  for (const auto value : { params.bloom_intensity, params.bloom_threshold,
    params.saturation, params.contrast, params.vignette_intensity, params.display_gamma }) {
    if (!std::isfinite(value) || value < 0.0F) {
      throw std::invalid_argument("Post-process values must be finite and nonnegative");
    }
  }
  if (params.vignette_intensity > 1.0F || params.display_gamma < oxygen::engine::kMinDisplayGamma) {
    throw std::invalid_argument("Post-process vignette or display gamma is outside its valid range");
  }
  return exposure;
}

auto ApplyPostProcess(oxygen::scene::SceneEnvironment& environment,
  const oxygen::interop::module::PostProcessParams& params,
  const oxygen::scene::ExposureSettings& exposure) -> void
{
  namespace env = oxygen::scene::environment;

  auto* const post_process = EnsureSystem<env::PostProcessVolume>(environment);
  if (post_process == nullptr) {
    return;
  }

  post_process->SetToneMapper(MapToneMapper(params.tone_mapper));
  post_process->SetExposureSettings(exposure);
  post_process->SetBloomIntensity(params.bloom_intensity);
  post_process->SetBloomThreshold(params.bloom_threshold);
  post_process->SetSaturation(params.saturation);
  post_process->SetContrast(params.contrast);
  post_process->SetVignetteIntensity(params.vignette_intensity);
  post_process->SetDisplayGamma(params.display_gamma);
}

auto ValidateFog(const oxygen::interop::module::FogParams& params) -> void
{
  const auto finite = [](const oxygen::Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y)
      && std::isfinite(value.z);
  };
  for (const auto value : {
         params.density,
         params.height_falloff,
         params.height_offset_meters,
         params.max_opacity,
         params.second_density,
         params.second_height_falloff,
         params.second_height_offset_meters,
         params.start_distance_meters,
         params.end_distance_meters,
         params.cutoff_distance_meters,
         params.directional_inscattering_exponent,
         params.directional_inscattering_start_distance_meters,
         params.volumetric_scattering_distribution,
         params.volumetric_extinction_scale,
         params.volumetric_distance_meters,
         params.volumetric_start_distance_meters,
         params.volumetric_near_fade_in_distance_meters,
         params.volumetric_static_lighting_scattering_intensity }) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Fog values must be finite");
    }
  }
  for (const auto& value : {
         params.inscattering_luminance_rgb,
         params.sky_ambient_scale_rgb,
         params.directional_inscattering_luminance_rgb,
         params.volumetric_albedo_rgb,
         params.volumetric_emissive_rgb }) {
    if (!finite(value)) {
      throw std::invalid_argument("Fog colors must be finite");
    }
  }
}

auto ApplyFog(oxygen::scene::SceneEnvironment& environment,
  const oxygen::interop::module::FogParams& params) -> void
{
  namespace env = oxygen::scene::environment;

  auto* const fog = EnsureSystem<env::Fog>(environment);
  if (fog == nullptr) {
    return;
  }

  fog->SetEnabled(params.enabled);
  fog->SetEnableHeightFog(params.height_fog_enabled);
  fog->SetExtinctionSigmaTPerMeter(params.density);
  fog->SetHeightFalloffPerMeter(params.height_falloff);
  fog->SetHeightOffsetMeters(params.height_offset_meters);
  fog->SetMaxOpacity(params.max_opacity);
  fog->SetFogInscatteringLuminance(params.inscattering_luminance_rgb);
  fog->SetSkyAtmosphereAmbientContributionColorScale(params.sky_ambient_scale_rgb);
  fog->SetSecondFogDensity(params.second_density);
  fog->SetSecondFogHeightFalloff(params.second_height_falloff);
  fog->SetSecondFogHeightOffset(params.second_height_offset_meters);
  fog->SetStartDistanceMeters(params.start_distance_meters);
  fog->SetEndDistanceMeters(params.end_distance_meters);
  fog->SetFogCutoffDistanceMeters(params.cutoff_distance_meters);
  fog->SetDirectionalInscatteringLuminance(params.directional_inscattering_luminance_rgb);
  fog->SetDirectionalInscatteringExponent(params.directional_inscattering_exponent);
  fog->SetDirectionalInscatteringStartDistance(params.directional_inscattering_start_distance_meters);
  fog->SetEnableVolumetricFog(params.volumetric_fog_enabled);
  fog->SetVolumetricFogScatteringDistribution(params.volumetric_scattering_distribution);
  fog->SetVolumetricFogAlbedo(params.volumetric_albedo_rgb);
  fog->SetVolumetricFogEmissive(params.volumetric_emissive_rgb);
  fog->SetVolumetricFogExtinctionScale(params.volumetric_extinction_scale);
  fog->SetVolumetricFogDistance(params.volumetric_distance_meters);
  fog->SetVolumetricFogStartDistance(params.volumetric_start_distance_meters);
  fog->SetVolumetricFogNearFadeInDistance(params.volumetric_near_fade_in_distance_meters);
  fog->SetVolumetricFogStaticLightingScatteringIntensity(params.volumetric_static_lighting_scattering_intensity);
  fog->SetOverrideLightColorsWithFogInscatteringColors(params.override_light_colors_with_fog_inscattering);
  fog->SetRenderInMainPass(params.render_in_main_pass);
  fog->SetHoldout(params.holdout);
  fog->SetVisibleInReflectionCaptures(params.visible_in_reflection_captures);
  fog->SetVisibleInRealTimeSkyCaptures(params.visible_in_real_time_sky_captures);
}

} // namespace

namespace oxygen::interop::module {

  void SetEnvironmentCommand::Execute(CommandContext& context)
  {
    if (!context.Scene) {
      return;
    }

    ValidateFog(fog_);
    ValidateSky(sky_);
    auto apply = [atmosphere_params = atmosphere_, post_process = post_process_,
                   fog_params = fog_, sky = sky_,
                   exposure = ResolvePostProcessExposure(post_process_)](
                   scene::Scene& scene,
                   const EnvironmentTextureKeys& textures) mutable {
      auto* const environment = EnsureEnvironment(scene);
      auto* const atmosphere
        = EnsureSkyAtmosphereWhenEnabled(*environment, atmosphere_params.enabled);
      if (atmosphere != nullptr) {
        ApplySkyAtmosphere(*atmosphere, atmosphere_params);
      }
      exposure.metering_mask = textures.at(
        static_cast<std::size_t>(EnvironmentTextureSlot::kMeteringMask));
      ApplySky(*environment, sky,
        textures.at(
          static_cast<std::size_t>(EnvironmentTextureSlot::kSkySphereCubemap)),
        textures.at(
          static_cast<std::size_t>(EnvironmentTextureSlot::kSkyLightCubemap)));
      ApplyPostProcess(*environment, post_process, exposure);
      ApplyFog(*environment, fog_params);
      scene.Update(false);
      scene.NotifyEnvironmentAuthoringChange();
    };
    auto sources = EnvironmentTextureSources {};
    sources.at(static_cast<std::size_t>(EnvironmentTextureSlot::kMeteringMask))
      = { .locator = post_process_.auto_exposure_metering_mask,
          .project_mount = post_process_.auto_exposure_metering_mask_mount };
    sources.at(
      static_cast<std::size_t>(EnvironmentTextureSlot::kSkySphereCubemap))
      = { .locator = sky_.sky_sphere.cubemap.locator,
          .project_mount = sky_.sky_sphere.cubemap.project_mount };
    sources.at(static_cast<std::size_t>(EnvironmentTextureSlot::kSkyLightCubemap))
      = { .locator = sky_.sky_light.cubemap.locator,
          .project_mount = sky_.sky_light.cubemap.project_mount };
    if (context.AssetRequests) {
      context.AssetRequests->SetEnvironmentTextures(*context.Scene,
        std::move(sources), std::move(apply), std::move(failure_callback_),
        std::move(success_callback_));
    } else if (std::ranges::any_of(sources,
                 [](const auto& source) { return source.locator.has_value(); })) {
      throw std::logic_error(
        "Environment textures require scene asset request state");
    } else {
      apply(*context.Scene, {});
    }
  }

} // namespace oxygen::interop::module
