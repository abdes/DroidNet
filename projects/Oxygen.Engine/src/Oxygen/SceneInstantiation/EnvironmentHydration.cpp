//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/SceneInstantiation/EnvironmentHydration.h>

namespace oxygen::instantiation {

namespace {
  //! The record, when it is present and enabled.
  template <typename Record>
  auto Enabled(const std::optional<Record>& record) -> const Record*
  {
    return record.has_value() && record->enabled != 0U ? &*record : nullptr;
  }

  auto HydrateSkyAtmosphere(scene::environment::SkyAtmosphere& target,
    const data::pak::world::SkyAtmosphereEnvironmentRecord& source) -> void
  {
    target.SetTransformMode(scene::environment::SkyAtmosphereTransformMode::
        kPlanetTopAtAbsoluteWorldOrigin);
    target.SetPlanetRadiusMeters(source.planet_radius_m);
    target.SetAtmosphereHeightMeters(source.atmosphere_height_m);
    target.SetGroundAlbedoRgb(Vec3 { source.ground_albedo_rgb[0],
      source.ground_albedo_rgb[1], source.ground_albedo_rgb[2] });
    target.SetRayleighScatteringRgb(Vec3 { source.rayleigh_scattering_rgb[0],
      source.rayleigh_scattering_rgb[1], source.rayleigh_scattering_rgb[2] });
    target.SetRayleighScaleHeightMeters(source.rayleigh_scale_height_m);
    target.SetMieScatteringRgb(Vec3 { source.mie_scattering_rgb[0],
      source.mie_scattering_rgb[1], source.mie_scattering_rgb[2] });
    target.SetMieAbsorptionRgb(Vec3 { source.mie_absorption_rgb[0],
      source.mie_absorption_rgb[1], source.mie_absorption_rgb[2] });
    target.SetMieScaleHeightMeters(source.mie_scale_height_m);
    target.SetMieAnisotropy(source.mie_g);
    target.SetOzoneAbsorptionRgb(Vec3 { source.absorption_rgb[0],
      source.absorption_rgb[1], source.absorption_rgb[2] });

    // Pak format currently exposes a single absorption height parameter.
    // The physical lighting spec uses a fixed two-layer linear ozone profile,
    // so we ignore this field and apply the default Earth-like profile.
    target.SetOzoneDensityProfile(engine::atmos::kDefaultOzoneDensityProfile);
    target.SetMultiScatteringFactor(source.multi_scattering_factor);
    target.SetSkyLuminanceFactorRgb({
      source.sky_luminance_factor_rgb[0],
      source.sky_luminance_factor_rgb[1],
      source.sky_luminance_factor_rgb[2],
    });
    target.SetSkyAndAerialPerspectiveLuminanceFactorRgb({
      source.sky_and_aerial_perspective_luminance_factor_rgb[0],
      source.sky_and_aerial_perspective_luminance_factor_rgb[1],
      source.sky_and_aerial_perspective_luminance_factor_rgb[2],
    });
    target.SetSunDiskEnabled(source.sun_disk_enabled != 0U);
    target.SetAerialPerspectiveDistanceScale(
      source.aerial_perspective_distance_scale);
    target.SetAerialPerspectiveStartDepthMeters(
      source.aerial_perspective_start_depth_m);
    target.SetAerialScatteringStrength(source.aerial_scattering_strength);
    target.SetHeightFogContribution(source.height_fog_contribution);
    target.SetTraceSampleCountScale(source.trace_sample_count_scale);
    target.SetTransmittanceMinLightElevationDeg(
      source.transmittance_min_light_elevation_deg);
    target.SetHoldout(source.holdout != 0U);
    target.SetRenderInMainPass(source.render_in_main_pass != 0U);
  }

  auto HydrateSkySphere(scene::environment::SkySphere& target,
    const data::pak::world::SkySphereEnvironmentRecord& source,
    const content::ResourceKey cubemap) -> void
  {
    if (source.source
      == static_cast<std::uint32_t>(
        scene::environment::SkySphereSource::kSolidColor)) {
      target.SetSource(scene::environment::SkySphereSource::kSolidColor);
    } else {
      target.SetSource(scene::environment::SkySphereSource::kCubemap);
      target.SetCubemapResource(cubemap);
      if (cubemap.get() == 0U) {
        LOG_F(WARNING, "SkySphere uses a cubemap source but binds no cubemap");
      }
    }

    target.SetSolidColorRgb(Vec3 { source.solid_color_rgb[0],
      source.solid_color_rgb[1], source.solid_color_rgb[2] });
    target.SetIlluminanceLux(source.illuminance_lux);
    target.SetIntensity(source.intensity);
    target.SetRotationRadians(source.rotation_radians);
    target.SetTintRgb(
      Vec3 { source.tint_rgb[0], source.tint_rgb[1], source.tint_rgb[2] });
  }

  auto HydrateFog(scene::environment::Fog& target,
    const data::pak::world::FogEnvironmentRecord& source,
    const content::ResourceKey cubemap) -> void
  {
    target.SetModel(static_cast<scene::environment::FogModel>(source.model));
    target.SetEnableHeightFog(source.enable_height_fog != 0U);
    target.SetEnableVolumetricFog(source.enable_volumetric_fog != 0U);
    target.SetExtinctionSigmaTPerMeter(source.extinction_sigma_t_per_m);
    target.SetHeightFalloffPerMeter(source.height_falloff_per_m);
    target.SetHeightOffsetMeters(source.height_offset_m);
    target.SetStartDistanceMeters(source.start_distance_m);
    target.SetSecondFogDensity(source.second_fog_density);
    target.SetSecondFogHeightFalloff(source.second_fog_height_falloff);
    target.SetSecondFogHeightOffset(source.second_fog_height_offset);
    target.SetMaxOpacity(source.max_opacity);
    target.SetSingleScatteringAlbedoRgb(
      Vec3 { source.single_scattering_albedo_rgb[0],
        source.single_scattering_albedo_rgb[1],
        source.single_scattering_albedo_rgb[2] });
    target.SetAnisotropy(source.anisotropy_g);
    target.SetFogInscatteringLuminance({
      source.fog_inscattering_luminance[0],
      source.fog_inscattering_luminance[1],
      source.fog_inscattering_luminance[2],
    });
    target.SetSkyAtmosphereAmbientContributionColorScale({
      source.sky_atmosphere_ambient_contribution_color_scale[0],
      source.sky_atmosphere_ambient_contribution_color_scale[1],
      source.sky_atmosphere_ambient_contribution_color_scale[2],
    });
    target.SetInscatteringColorCubemapResource(cubemap);
    target.SetInscatteringColorCubemapAngle(
      source.inscattering_color_cubemap_angle);
    target.SetInscatteringTextureTint({
      source.inscattering_texture_tint[0],
      source.inscattering_texture_tint[1],
      source.inscattering_texture_tint[2],
    });
    target.SetFullyDirectionalInscatteringColorDistance(
      source.fully_directional_inscattering_color_distance);
    target.SetNonDirectionalInscatteringColorDistance(
      source.non_directional_inscattering_color_distance);
    target.SetDirectionalInscatteringLuminance({
      source.directional_inscattering_luminance[0],
      source.directional_inscattering_luminance[1],
      source.directional_inscattering_luminance[2],
    });
    target.SetDirectionalInscatteringExponent(
      source.directional_inscattering_exponent);
    target.SetDirectionalInscatteringStartDistance(
      source.directional_inscattering_start_distance);
    target.SetEndDistanceMeters(source.end_distance_m);
    target.SetFogCutoffDistanceMeters(source.fog_cutoff_distance_m);
    target.SetVolumetricFogScatteringDistribution(
      source.volumetric_fog_scattering_distribution);
    target.SetVolumetricFogAlbedo({
      source.volumetric_fog_albedo[0],
      source.volumetric_fog_albedo[1],
      source.volumetric_fog_albedo[2],
    });
    target.SetVolumetricFogEmissive({
      source.volumetric_fog_emissive[0],
      source.volumetric_fog_emissive[1],
      source.volumetric_fog_emissive[2],
    });
    target.SetVolumetricFogExtinctionScale(
      source.volumetric_fog_extinction_scale);
    target.SetVolumetricFogDistance(source.volumetric_fog_distance);
    target.SetVolumetricFogStartDistance(source.volumetric_fog_start_distance);
    target.SetVolumetricFogNearFadeInDistance(
      source.volumetric_fog_near_fade_in_distance);
    target.SetVolumetricFogStaticLightingScatteringIntensity(
      source.volumetric_fog_static_lighting_scattering_intensity);
    target.SetOverrideLightColorsWithFogInscatteringColors(
      source.override_light_colors_with_fog_inscattering_colors != 0U);
    target.SetHoldout(source.holdout != 0U);
    target.SetRenderInMainPass(source.render_in_main_pass != 0U);
    target.SetVisibleInReflectionCaptures(
      source.visible_in_reflection_captures != 0U);
    target.SetVisibleInRealTimeSkyCaptures(
      source.visible_in_real_time_sky_captures != 0U);
  }

  auto HydrateSkyLight(scene::environment::SkyLight& target,
    const data::pak::world::SkyLightEnvironmentRecord& source,
    const content::ResourceKey cubemap) -> void
  {
    target.SetSource(
      static_cast<scene::environment::SkyLightSource>(source.source));
    target.SetCubemapResource(cubemap);
    if (target.GetSource()
        == scene::environment::SkyLightSource::kSpecifiedCubemap
      && cubemap.get() == 0U) {
      LOG_F(WARNING, "SkyLight uses a specified cubemap but binds no cubemap");
    }
    target.SetIlluminanceLux(source.illuminance_lux);
    target.SetIntensityMul(source.intensity);
    target.SetTintRgb(
      Vec3 { source.tint_rgb[0], source.tint_rgb[1], source.tint_rgb[2] });
    target.SetDiffuseIntensity(source.diffuse_intensity);
    target.SetSpecularIntensity(source.specular_intensity);
    target.SetSourceCubemapAngleRadians(source.source_cubemap_angle_radians);
    target.SetLowerHemisphereColor({
      source.lower_hemisphere_color[0],
      source.lower_hemisphere_color[1],
      source.lower_hemisphere_color[2],
    });
    target.SetLowerHemisphereIsSolidColor(
      source.lower_hemisphere_is_solid_color != 0U);
    target.SetLowerHemisphereBlendAlpha(source.lower_hemisphere_blend_alpha);
    target.SetVolumetricScatteringIntensity(
      source.volumetric_scattering_intensity);
    target.SetAffectReflections(source.affect_reflections != 0U);
  }

  auto HydrateVolumetricClouds(scene::environment::VolumetricClouds& target,
    const data::pak::world::VolumetricCloudsEnvironmentRecord& source) -> void
  {
    target.SetBaseAltitudeMeters(source.base_altitude_m);
    target.SetLayerThicknessMeters(source.layer_thickness_m);
    target.SetCoverage(source.coverage);
    target.SetExtinctionSigmaTPerMeter(source.extinction_sigma_t_per_m);
    target.SetSingleScatteringAlbedoRgb(
      Vec3 { source.single_scattering_albedo_rgb[0],
        source.single_scattering_albedo_rgb[1],
        source.single_scattering_albedo_rgb[2] });
    target.SetPhaseAnisotropy(source.phase_g);
    target.SetWindDirectionWs(Vec3 {
      source.wind_dir_ws[0], source.wind_dir_ws[1], source.wind_dir_ws[2] });
    target.SetWindSpeedMps(source.wind_speed_mps);
    target.SetShadowStrength(source.shadow_strength);
  }

  void HydratePostProcessVolume(scene::environment::PostProcessVolume& target,
    const data::pak::world::PostProcessVolumeEnvironmentRecord& source,
    const std::span<const data::pak::world::ExposureCompensationKeyRecord>
      curve,
    const content::ResourceKey metering_mask)
  {
    auto exposure = scene::ExposureSettings {
      .enabled = source.exposure_enabled != 0U,
      .mode = source.exposure_mode,
      .manual_ev = source.manual_exposure_ev,
      .compensation_ev = source.exposure_compensation_ev,
      .key = source.exposure_key,
      .min_ev = source.auto_exposure_min_ev,
      .max_ev = source.auto_exposure_max_ev,
      .speed_up = source.auto_exposure_speed_up,
      .speed_down = source.auto_exposure_speed_down,
      .metering_mode = source.auto_exposure_metering_mode,
      .low_percentile = source.auto_exposure_low_percentile,
      .high_percentile = source.auto_exposure_high_percentile,
      .min_log_luminance = source.auto_exposure_min_log_luminance,
      .log_luminance_range = source.auto_exposure_log_luminance_range,
      .target_luminance = source.auto_exposure_target_luminance,
      .spot_meter_radius = source.auto_exposure_spot_meter_radius,
      .black_influence = source.auto_exposure_black_influence,
      .transition_distance = source.auto_exposure_transition_distance_ev,
      .metering_mask = metering_mask,
    };
    exposure.compensation_curve.reserve(curve.size());
    for (const auto& key : curve) {
      exposure.compensation_curve.push_back({
        .metered_ev = key.metered_ev,
        .compensation_ev = key.compensation_ev,
      });
    }
    target.SetExposureSettings(exposure);
    target.SetToneMapper(source.tone_mapper);
    target.SetDisplayGamma(source.display_gamma);
    target.SetBloomIntensity(source.bloom_intensity);
    target.SetBloomThreshold(source.bloom_threshold);
    target.SetSaturation(source.saturation);
    target.SetContrast(source.contrast);
    target.SetVignetteIntensity(source.vignette_intensity);
  }

} // namespace

auto HydrateEnvironment(scene::SceneEnvironment& target,
  const data::SceneAsset& source_asset, const EnvironmentTextureKeys& textures)
  -> void
{
  const auto require_binding
    = [](const bool authored, const content::ResourceKey key,
        const char* message) -> void {
    if (authored != (key.get() != 0U)) {
      throw std::invalid_argument(message);
    }
  };
  const auto post_record = source_asset.TryGetPostProcessVolumeEnvironment();
  require_binding(post_record
      && post_record->auto_exposure_metering_mask != data::kNoResourceReference,
    textures.metering_mask,
    "Scene exposure mask requires a resolved source resource key");
  const auto fog_binding = source_asset.TryGetFogEnvironment();
  require_binding(fog_binding
      && fog_binding->inscattering_color_cubemap != data::kNoResourceReference,
    textures.fog_cubemap,
    "Scene fog cubemap requires a resolved source resource key");
  const auto sky_light_binding = source_asset.TryGetSkyLightEnvironment();
  require_binding(sky_light_binding
      && sky_light_binding->cubemap != data::kNoResourceReference,
    textures.sky_light_cubemap,
    "Scene sky light cubemap requires a resolved source resource key");
  const auto sky_sphere_binding = source_asset.TryGetSkySphereEnvironment();
  require_binding(sky_sphere_binding
      && sky_sphere_binding->cubemap != data::kNoResourceReference,
    textures.sky_sphere_cubemap,
    "Scene sky sphere cubemap requires a resolved source resource key");
  const auto background_record = source_asset.TryGetBackgroundEnvironment();
  if (const auto* record = Enabled(background_record); record != nullptr) {
    auto& background = target.AddSystem<scene::environment::Background>();
    background.SetColorRgb(
      { record->color_rgb[0], record->color_rgb[1], record->color_rgb[2] });
  }

  const auto sky_atmo_optional = source_asset.TryGetSkyAtmosphereEnvironment();
  const auto* sky_atmo_record = Enabled(sky_atmo_optional);
  const auto* sky_sphere_record = Enabled(sky_sphere_binding);

  if (sky_atmo_record != nullptr && sky_sphere_record != nullptr) {
    LOG_F(WARNING,
      "Both SkyAtmosphere and SkySphere are enabled in the scene. They are "
      "mutually exclusive; SkyAtmosphere will be used.");
  }

  if (sky_atmo_record != nullptr) {
    auto& atmo = target.AddSystem<scene::environment::SkyAtmosphere>();
    HydrateSkyAtmosphere(atmo, *sky_atmo_record);
    LOG_F(1, "Applied SkyAtmosphere environment");
  } else if (sky_sphere_record != nullptr) {
    auto& sky_sphere = target.AddSystem<scene::environment::SkySphere>();
    HydrateSkySphere(
      sky_sphere, *sky_sphere_record, textures.sky_sphere_cubemap);
    LOG_F(1, "Applied SkySphere environment");
  }

  if (const auto* fog_record = Enabled(fog_binding); fog_record != nullptr) {
    auto& fog = target.AddSystem<scene::environment::Fog>();
    HydrateFog(fog, *fog_record, textures.fog_cubemap);
    LOG_F(1, "Applied Fog environment");
  }

  if (const auto* sky_light_record = Enabled(sky_light_binding);
    sky_light_record != nullptr) {
    auto& sky_light = target.AddSystem<scene::environment::SkyLight>();
    HydrateSkyLight(sky_light, *sky_light_record, textures.sky_light_cubemap);
    LOG_F(1, "Applied SkyLight environment");
  }

  const auto clouds_optional = source_asset.TryGetVolumetricCloudsEnvironment();
  if (const auto* clouds_record = Enabled(clouds_optional);
    clouds_record != nullptr) {
    auto& clouds = target.AddSystem<scene::environment::VolumetricClouds>();
    HydrateVolumetricClouds(clouds, *clouds_record);
    LOG_F(1, "Applied VolumetricClouds environment");
  }

  if (const auto* pp_record = Enabled(post_record); pp_record != nullptr) {
    auto& pp = target.AddSystem<scene::environment::PostProcessVolume>();
    HydratePostProcessVolume(pp, *pp_record,
      source_asset.GetPostProcessCompensationCurve(), textures.metering_mask);
    LOG_F(INFO,
      "Scene exposure applied: asset={} mode={} mask={} curve_keys={}",
      data::to_string(source_asset.GetAssetKey()),
      engine::to_string(pp_record->exposure_mode),
      pp_record->auto_exposure_metering_mask.get(), pp_record->curve_key_count);
  }
}

auto BuildEnvironment(const data::SceneAsset& asset,
  content::IAssetLoader& loader) -> std::unique_ptr<scene::SceneEnvironment>
{
  const auto resolve
    = [&loader, &asset](const data::ResourceReferenceIndex reference,
        const char* what) -> content::ResourceKey {
    if (reference == data::kNoResourceReference) {
      return {};
    }
    const auto key = loader.MakeTextureResourceKeyForAsset(asset, reference);
    if (!key) {
      throw std::runtime_error(std::string("Scene ") + what
        + " binding failed: scene=" + data::to_string(asset.GetAssetKey())
        + " texture_reference=" + std::to_string(reference.get()));
    }
    return *key;
  };
  auto textures = EnvironmentTextureKeys {};
  if (const auto post = asset.TryGetPostProcessVolumeEnvironment()) {
    textures.metering_mask
      = resolve(post->auto_exposure_metering_mask, "exposure mask");
  }
  if (const auto fog = asset.TryGetFogEnvironment()) {
    textures.fog_cubemap
      = resolve(fog->inscattering_color_cubemap, "fog cubemap");
  }
  if (const auto sky_light = asset.TryGetSkyLightEnvironment()) {
    textures.sky_light_cubemap
      = resolve(sky_light->cubemap, "sky light cubemap");
  }
  if (const auto sky_sphere = asset.TryGetSkySphereEnvironment()) {
    textures.sky_sphere_cubemap
      = resolve(sky_sphere->cubemap, "sky sphere cubemap");
  }
  auto environment = std::make_unique<scene::SceneEnvironment>();
  HydrateEnvironment(*environment, asset, textures);
  return environment;
}

} // namespace oxygen::instantiation
