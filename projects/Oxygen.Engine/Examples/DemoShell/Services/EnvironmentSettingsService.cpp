//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/DirectionalLightResolver.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Renderer.h>

#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/SkyboxService.h"

namespace oxygen::examples {

namespace {

  constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0F;
  constexpr float kRadToDeg = 180.0F / std::numbers::pi_v<float>;
  constexpr float kMetersToKm = 0.001F;
  constexpr float kKmToMeters = 1000.0F;

  auto DirectionFromAzimuthElevation(float azimuth_deg, float elevation_deg)
    -> glm::vec3
  {
    const float az_rad = azimuth_deg * kDegToRad;
    const float el_rad = elevation_deg * kDegToRad;

    const float cos_el = std::cos(el_rad);
    return glm::vec3(
      cos_el * std::cos(az_rad), cos_el * std::sin(az_rad), std::sin(el_rad));
  }

  auto KelvinToLinearRgb(float kelvin) -> glm::vec3
  {
    kelvin = glm::clamp(kelvin, 1000.0F, 40000.0F);
    const float temp = kelvin / 100.0F;

    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;

    if (temp <= 66.0F) {
      red = 1.0F;
      green = std::clamp(
        (0.39008157877F * std::log(temp)) - 0.63184144378F, 0.0F, 1.0F);
      if (temp <= 19.0F) {
        blue = 0.0F;
      } else {
        blue = std::clamp(
          (0.54320678911F * std::log(temp - 10.0F)) - 1.19625408914F, 0.0F,
          1.0F);
      }
    } else {
      red = std::clamp(
        1.29293618606F * std::pow(temp - 60.0F, -0.1332047592F), 0.0F, 1.0F);
      green = std::clamp(
        1.1298908609F * std::pow(temp - 60.0F, -0.0755148492F), 0.0F, 1.0F);
      blue = 1.0F;
    }

    return { red, green, blue };
  }

  auto RotationFromDirection(const glm::vec3& direction_ws) -> glm::quat
  {
    if (glm::dot(direction_ws, direction_ws)
      <= math::EpsilonDirection * math::EpsilonDirection) {
      return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
    }

    constexpr glm::vec3 from_dir = space::move::Forward;
    const glm::vec3 to_dir = glm::normalize(direction_ws);

    const float cos_theta = std::clamp(glm::dot(from_dir, to_dir), -1.0F, 1.0F);
    glm::quat rotation(1.0F, 0.0F, 0.0F, 0.0F);
    if (cos_theta < 0.9999F) {
      if (cos_theta > -0.9999F) {
        const glm::vec3 axis = glm::normalize(glm::cross(from_dir, to_dir));
        const float angle = std::acos(cos_theta);
        rotation = glm::angleAxis(angle, axis);
      } else {
        constexpr auto axis = glm::vec3(0.0F, 0.0F, 1.0F);
        rotation = glm::angleAxis(glm::pi<float>(), axis);
      }
    }

    return rotation;
  }

  auto ComputeLocalRotationForWorldDirection(
    scene::SceneNode& node, const glm::vec3& direction_ws) -> glm::quat
  {
    const glm::quat desired_world_rotation
      = RotationFromDirection(direction_ws);
    const auto parent_opt = node.GetParent();
    if (!parent_opt.has_value()) {
      return desired_world_rotation;
    }

    const auto parent_world_rotation
      = parent_opt->GetTransform().GetWorldRotation();
    if (!parent_world_rotation.has_value()) {
      return desired_world_rotation;
    }

    return glm::normalize(
      glm::inverse(*parent_world_rotation) * desired_world_rotation);
  }

  auto ApplyLightDirectionWorldSpace(
    scene::SceneNode& node, const glm::vec3& direction_ws) -> void
  {
    auto transform = node.GetTransform();
    transform.SetLocalRotation(
      ComputeLocalRotationForWorldDirection(node, direction_ws));
  }

  template <typename Record>
  auto IsEnabled(const std::optional<Record>& record) -> bool
  {
    return record.has_value() && record->enabled != 0U;
  }

  auto CollectLocalFogVolumeNodes(scene::Scene& scene)
    -> std::vector<scene::SceneNode>
  {
    auto result = std::vector<scene::SceneNode> {};
    auto roots = scene.GetRootNodes();
    auto stack = std::vector<scene::SceneNode> {};
    stack.reserve(roots.size());
    for (auto& root : roots) {
      stack.push_back(root);
    }

    while (!stack.empty()) {
      auto node = stack.back();
      stack.pop_back();
      if (!node.IsAlive()) {
        continue;
      }

      if (const auto impl_opt = node.GetImpl(); impl_opt.has_value()
        && impl_opt->get().HasComponent<scene::environment::LocalFogVolume>()) {
        result.push_back(node);
      }

      auto child = node.GetFirstChild();
      while (child.has_value()) {
        stack.push_back(*child);
        child = child->GetNextSibling();
      }
    }

    std::ranges::sort(
      result, [](const scene::SceneNode& lhs, const scene::SceneNode& rhs) {
        return lhs.GetName() < rhs.GetName();
      });
    return result;
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
    const data::pak::world::SkySphereEnvironmentRecord& source) -> void
  {
    if (source.source
      == static_cast<std::uint32_t>(
        scene::environment::SkySphereSource::kSolidColor)) {
      target.SetSource(scene::environment::SkySphereSource::kSolidColor);
    } else {
      LOG_F(WARNING,
        "SkySphere cubemap source requested, but scene-authored cubemap "
        "AssetKey resolution is not implemented in this example. Keeping "
        "solid color; use the Environment panel Skybox Loader to bind a "
        "cubemap at runtime.");
      target.SetSource(scene::environment::SkySphereSource::kSolidColor);
    }

    target.SetSolidColorRgb(Vec3 { source.solid_color_rgb[0],
      source.solid_color_rgb[1], source.solid_color_rgb[2] });
    target.SetIntensity(source.intensity);
    target.SetRotationRadians(source.rotation_radians);
    target.SetTintRgb(
      Vec3 { source.tint_rgb[0], source.tint_rgb[1], source.tint_rgb[2] });
  }

  auto HydrateFog(scene::environment::Fog& target,
    const data::pak::world::FogEnvironmentRecord& source) -> void
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
    const data::pak::world::SkyLightEnvironmentRecord& source) -> void
  {
    target.SetSource(
      static_cast<scene::environment::SkyLightSource>(source.source));
    if (target.GetSource()
      == scene::environment::SkyLightSource::kSpecifiedCubemap) {
      LOG_F(1,
        "SkyLight specifies a cubemap AssetKey, but this example does not yet "
        "resolve it to a ResourceKey. Use the Environment panel Skybox Loader "
        "to bind a cubemap at runtime.");
    }
    target.SetIntensityMul(source.intensity);
    target.SetTintRgb(
      Vec3 { source.tint_rgb[0], source.tint_rgb[1], source.tint_rgb[2] });
    target.SetDiffuseIntensity(source.diffuse_intensity);
    target.SetSpecularIntensity(source.specular_intensity);
    target.SetRealTimeCaptureEnabled(source.real_time_capture_enabled != 0U);
    target.SetLowerHemisphereColor({
      source.lower_hemisphere_color[0],
      source.lower_hemisphere_color[1],
      source.lower_hemisphere_color[2],
    });
    target.SetVolumetricScatteringIntensity(
      source.volumetric_scattering_intensity);
    target.SetAffectReflections(source.affect_reflections != 0U);
    target.SetAffectGlobalIllumination(source.affect_global_illumination != 0U);
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

  auto HydratePostProcessVolume(scene::environment::PostProcessVolume& target,
    const data::pak::world::PostProcessVolumeEnvironmentRecord& source) -> void
  {
    target.SetToneMapper(source.tone_mapper);
    target.SetExposureMode(source.exposure_mode);
    target.SetExposureCompensationEv(source.exposure_compensation_ev);
    target.SetAutoExposureRangeEv(
      source.auto_exposure_min_ev, source.auto_exposure_max_ev);
    target.SetAutoExposureAdaptationSpeeds(
      source.auto_exposure_speed_up, source.auto_exposure_speed_down);
    target.SetBloomIntensity(source.bloom_intensity);
    target.SetBloomThreshold(source.bloom_threshold);
    target.SetSaturation(source.saturation);
    target.SetContrast(source.contrast);
    target.SetVignetteIntensity(source.vignette_intensity);
  }

  constexpr std::string_view kSkyAtmoEnabledKey = "env.atmo.enabled";
  constexpr std::string_view kSkyAtmoTransformModeKey
    = "env.atmo.transform_mode";
  constexpr std::string_view kPlanetRadiusKey = "env.atmo.planet_radius_km";
  constexpr std::string_view kAtmosphereHeightKey
    = "env.atmo.atmosphere_height_km";
  constexpr std::string_view kGroundAlbedoKey = "env.atmo.ground_albedo";
  constexpr std::string_view kRayleighScaleHeightKey
    = "env.atmo.rayleigh_scale_height_km";
  constexpr std::string_view kMieScaleHeightKey
    = "env.atmo.mie_scale_height_km";
  constexpr std::string_view kMieAnisotropyKey = "env.atmo.mie_anisotropy";
  constexpr std::string_view kMieAbsorptionScaleKey
    = "env.atmo.mie_absorption_scale";
  constexpr std::string_view kMultiScatteringKey = "env.atmo.multi_scattering";
  constexpr std::string_view kSkyLuminanceFactorKey
    = "env.atmo.sky_luminance_factor";
  constexpr std::string_view kSkyAndAerialLuminanceFactorKey
    = "env.atmo.sky_and_aerial_luminance_factor";
  constexpr std::string_view kSunDiskEnabledKey = "env.atmo.sun_disk_enabled";
  constexpr std::string_view kAerialPerspectiveScaleKey
    = "env.atmo.aerial_perspective_scale";
  constexpr std::string_view kAerialPerspectiveStartDepthKey
    = "env.atmo.aerial_perspective_start_depth_m";
  constexpr std::string_view kAerialScatteringStrengthKey
    = "env.atmo.aerial_scattering_strength";
  constexpr std::string_view kHeightFogContributionKey
    = "env.atmo.height_fog_contribution";
  constexpr std::string_view kTraceSampleCountScaleKey
    = "env.atmo.trace_sample_count_scale";
  constexpr std::string_view kTransmittanceMinLightElevationKey
    = "env.atmo.transmittance_min_light_elevation_deg";
  constexpr std::string_view kAtmosphereHoldoutKey = "env.atmo.holdout";
  constexpr std::string_view kAtmosphereRenderInMainPassKey
    = "env.atmo.render_in_main_pass";
  constexpr std::string_view kOzoneRgbKey = "env.atmo.ozone_rgb";

  constexpr std::string_view kOzoneProfileLayer0WidthMKey
    = "env.atmo.ozone_profile.layer0.width_m";
  constexpr std::string_view kOzoneProfileLayer0LinearTermKey
    = "env.atmo.ozone_profile.layer0.linear_term";
  constexpr std::string_view kOzoneProfileLayer0ConstantTermKey
    = "env.atmo.ozone_profile.layer0.constant_term";
  constexpr std::string_view kOzoneProfileLayer1LinearTermKey
    = "env.atmo.ozone_profile.layer1.linear_term";
  constexpr std::string_view kOzoneProfileLayer1ConstantTermKey
    = "env.atmo.ozone_profile.layer1.constant_term";
  constexpr std::string_view kSkySphereEnabledKey = "env.sky_sphere.enabled";
  constexpr std::string_view kSkySphereSourceKey = "env.sky_sphere.source";
  constexpr std::string_view kSkySphereSolidColorKey
    = "env.sky_sphere.solid_color";
  constexpr std::string_view kSkySphereIntensityKey
    = "env.sky_sphere.intensity";
  constexpr std::string_view kSkySphereRotationKey
    = "env.sky_sphere.rotation_deg";

  constexpr std::string_view kSkyboxPathKey = "env.skybox.path";
  constexpr std::string_view kSkyboxLayoutKey = "env.skybox.layout";
  constexpr std::string_view kSkyboxOutputFormatKey = "env.skybox.output";
  constexpr std::string_view kSkyboxFaceSizeKey = "env.skybox.face_size";
  constexpr std::string_view kSkyboxFlipYKey = "env.skybox.flip_y";
  constexpr std::string_view kSkyboxTonemapKey
    = "env.skybox.tonemap_hdr_to_ldr";
  constexpr std::string_view kSkyboxHdrExposureKey
    = "env.skybox.hdr_exposure_ev";

  constexpr std::string_view kSkyLightEnabledKey = "env.sky_light.enabled";
  constexpr std::string_view kSkyLightSourceKey = "env.sky_light.source";
  constexpr std::string_view kSkyLightTintKey = "env.sky_light.tint";
  constexpr std::string_view kSkyLightIntensityMulKey
    = "env.sky_light.intensity_mul";
  constexpr std::string_view kSkyLightDiffuseKey = "env.sky_light.diffuse";
  constexpr std::string_view kSkyLightSpecularKey = "env.sky_light.specular";
  constexpr std::string_view kSkyLightRealTimeCaptureKey
    = "env.sky_light.real_time_capture_enabled";
  constexpr std::string_view kSkyLightLowerHemisphereColorKey
    = "env.sky_light.lower_hemisphere_color";
  constexpr std::string_view kSkyLightVolumetricScatteringIntensityKey
    = "env.sky_light.volumetric_scattering_intensity";
  constexpr std::string_view kSkyLightAffectReflectionsKey
    = "env.sky_light.affect_reflections";
  constexpr std::string_view kSkyLightAffectGlobalIlluminationKey
    = "env.sky_light.affect_global_illumination";

  constexpr std::string_view kFogEnabledKey = "env.fog.enabled";
  constexpr std::string_view kFogModelKey = "env.fog.model";
  constexpr std::string_view kFogExtinctionSigmaTKey
    = "env.fog.extinction_sigma_t_per_m";
  constexpr std::string_view kFogHeightFalloffKey
    = "env.fog.height_falloff_per_m";
  constexpr std::string_view kFogHeightOffsetKey = "env.fog.height_offset_m";
  constexpr std::string_view kFogStartDistanceKey = "env.fog.start_distance_m";
  constexpr std::string_view kSecondFogDensityKey = "env.fog.second_density";
  constexpr std::string_view kSecondFogHeightFalloffKey
    = "env.fog.second_height_falloff";
  constexpr std::string_view kSecondFogHeightOffsetKey
    = "env.fog.second_height_offset";
  constexpr std::string_view kFogMaxOpacityKey = "env.fog.max_opacity";
  constexpr std::string_view kFogSingleScatteringAlbedoKey
    = "env.fog.single_scattering_albedo_rgb";
  constexpr std::string_view kFogInscatteringLuminanceKey
    = "env.fog.inscattering_luminance";
  constexpr std::string_view kFogSkyAtmosphereAmbientContributionKey
    = "env.fog.sky_atmo_ambient_contribution";
  constexpr std::string_view kFogInscatteringColorCubemapAngleKey
    = "env.fog.inscattering_cubemap_angle";
  constexpr std::string_view kFogInscatteringTextureTintKey
    = "env.fog.inscattering_texture_tint";
  constexpr std::string_view kFogFullyDirectionalColorDistanceKey
    = "env.fog.fully_directional_color_distance";
  constexpr std::string_view kFogNonDirectionalColorDistanceKey
    = "env.fog.non_directional_color_distance";
  constexpr std::string_view kFogDirectionalInscatteringLuminanceKey
    = "env.fog.directional_inscattering_luminance";
  constexpr std::string_view kFogDirectionalInscatteringExponentKey
    = "env.fog.directional_inscattering_exponent";
  constexpr std::string_view kFogDirectionalInscatteringStartDistanceKey
    = "env.fog.directional_inscattering_start_distance";
  constexpr std::string_view kFogEndDistanceKey = "env.fog.end_distance_m";
  constexpr std::string_view kFogCutoffDistanceKey
    = "env.fog.cutoff_distance_m";
  constexpr std::string_view kVolumetricFogScatteringDistributionKey
    = "env.fog.volumetric.scattering_distribution";
  constexpr std::string_view kVolumetricFogAlbedoKey
    = "env.fog.volumetric.albedo";
  constexpr std::string_view kVolumetricFogEmissiveKey
    = "env.fog.volumetric.emissive";
  constexpr std::string_view kVolumetricFogExtinctionScaleKey
    = "env.fog.volumetric.extinction_scale";
  constexpr std::string_view kVolumetricFogDistanceKey
    = "env.fog.volumetric.distance_m";
  constexpr std::string_view kVolumetricFogStartDistanceKey
    = "env.fog.volumetric.start_distance_m";
  constexpr std::string_view kVolumetricFogNearFadeInDistanceKey
    = "env.fog.volumetric.near_fade_in_distance_m";
  constexpr std::string_view kVolumetricFogStaticLightingScatteringKey
    = "env.fog.volumetric.static_lighting_scattering_intensity";
  constexpr std::string_view kVolumetricFogOverrideLightColorsKey
    = "env.fog.volumetric.override_light_colors";
  constexpr std::string_view kFogHoldoutKey = "env.fog.holdout";
  constexpr std::string_view kFogRenderInMainPassKey
    = "env.fog.render_in_main_pass";
  constexpr std::string_view kFogVisibleInReflectionCapturesKey
    = "env.fog.visible_in_reflection_captures";
  constexpr std::string_view kFogVisibleInRealTimeSkyCapturesKey
    = "env.fog.visible_in_real_time_sky_captures";
  constexpr std::string_view kEnvironmentPresetKey = "environment_preset_index";

  // Light Culling Key
  constexpr std::string_view kSunEnabledKey = "env.sun.enabled";
  constexpr std::string_view kLegacySunSourceKey = "env.sun.source";
  constexpr std::string_view kSunAzimuthKey = "env.sun.azimuth_deg";
  constexpr std::string_view kSunElevationKey = "env.sun.elevation_deg";
  constexpr std::string_view kSunColorKey = "env.sun.color";
  constexpr std::string_view kSunIlluminanceKey = "env.sun.illuminance_lx";
  constexpr std::string_view kSunUseTemperatureKey = "env.sun.use_temperature";
  constexpr std::string_view kSunTemperatureKey = "env.sun.temperature_kelvin";
  constexpr std::string_view kSunDiskRadiusKey = "env.sun.disk_radius_deg";
  constexpr std::string_view kSunAtmosphereLightSlotKey
    = "env.sun.atmosphere_light_slot";
  constexpr std::string_view kSunPerPixelAtmosphereTransmittanceKey
    = "env.sun.per_pixel_atmosphere_transmittance";
  constexpr std::string_view kSunAtmosphereDiskLuminanceScaleKey
    = "env.sun.atmosphere_disk_luminance_scale";
  constexpr std::string_view kSunShadowBiasKey = "env.sun.shadow.bias";
  constexpr std::string_view kSunShadowNormalBiasKey
    = "env.sun.shadow.normal_bias";
  constexpr std::string_view kSunShadowResolutionHintKey
    = "env.sun.shadow.resolution_hint";
  constexpr std::string_view kSunShadowCascadeCountKey
    = "env.sun.shadow.csm.cascade_count";
  constexpr std::string_view kLegacySunShadowCascadeCountKey
    = "env.sun.csm.cascade_count";
  constexpr std::string_view kSunShadowSplitModeKey
    = "env.sun.shadow.csm.split_mode";
  constexpr std::string_view kLegacySunShadowSplitModeKey
    = "env.sun.csm.split_mode";
  constexpr std::string_view kSunShadowMaxDistanceKey
    = "env.sun.shadow.csm.max_shadow_distance";
  constexpr std::string_view kLegacySunShadowMaxDistanceKey
    = "env.sun.csm.max_shadow_distance";
  constexpr std::string_view kSunShadowDistributionExponentKey
    = "env.sun.shadow.csm.distribution_exponent";
  constexpr std::string_view kLegacySunShadowDistributionExponentKey
    = "env.sun.csm.distribution_exponent";
  constexpr std::string_view kSunShadowTransitionFractionKey
    = "env.sun.shadow.csm.transition_fraction";
  constexpr std::string_view kLegacySunShadowTransitionFractionKey
    = "env.sun.csm.transition_fraction";
  constexpr std::string_view kSunShadowDistanceFadeoutFractionKey
    = "env.sun.shadow.csm.distance_fadeout_fraction";
  constexpr std::string_view kLegacySunShadowDistanceFadeoutFractionKey
    = "env.sun.csm.distance_fadeout_fraction";
  constexpr std::string_view kSunShadowCascadeDistancePrefixKey
    = "env.sun.shadow.csm.cascade_distance";
  constexpr std::string_view kLegacySunShadowCascadeDistancePrefixKey
    = "env.sun.csm.cascade_distance";
  constexpr std::string_view kEnvironmentSettingsSchemaVersionKey
    = "env.settings.schema_version";
  constexpr std::string_view kEnvironmentCustomStatePresentKey
    = "env.settings.custom_state_present";
  constexpr float kCurrentSettingsSchemaVersion = 5.0F;
  constexpr int kPresetUseScene = -2;
  constexpr int kPresetCustom = -1;
  // Demo policy: UI environment settings are authoritative and always
  // override scene environment data.
  constexpr bool kForceEnvironmentOverride = true;
  constexpr std::uint32_t kFogDirtyMask = (1u << 2u) | (1u << 3u);
  constexpr std::uint32_t kSunDirtyMask = (1u << 8u) | (1u << 1u);

  auto ClampVec3(const glm::vec3& value, float min_value, float max_value)
    -> glm::vec3
  {
    return glm::clamp(value, glm::vec3(min_value), glm::vec3(max_value));
  }

  auto HashCombineU64(std::uint64_t seed, std::uint64_t value) -> std::uint64_t
  {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
  }

  auto FloatBits(const float v) -> std::uint32_t
  {
    const auto bytes = reinterpret_cast<const std::uint8_t*>(&v);
    return static_cast<std::uint32_t>(bytes[0])
      | (static_cast<std::uint32_t>(bytes[1]) << 8U)
      | (static_cast<std::uint32_t>(bytes[2]) << 16U)
      | (static_cast<std::uint32_t>(bytes[3]) << 24U);
  }

  auto ApplyDirectionalSunRole(scene::SceneNode& node, const bool affects_world,
    const bool casts_shadows, const bool environment_contribution,
    const bool is_sun_light) -> bool
  {
    auto light = node.GetLightAs<scene::DirectionalLight>();
    if (!light.has_value()) {
      return false;
    }

    auto& directional = light->get();
    directional.SetIsSunLight(is_sun_light);
    directional.SetEnvironmentContribution(environment_contribution);

    auto& common = directional.Common();
    common.affects_world = affects_world;
    common.casts_shadows = casts_shadows;

    if (auto flags = node.GetFlags()) {
      flags->get().SetFlag(scene::SceneNodeFlags::kCastsShadows,
        scene::SceneFlag {}.SetEffectiveValueBit(casts_shadows));
    }

    return true;
  }

} // namespace

/*!
 Hydrates a runtime environment from a scene asset.

 @param target Mutable runtime environment to populate.
 @param source_asset Asset containing environment records.

### Performance Characteristics

- Time Complexity: $O(1)$ for fixed system set.
- Memory: $O(1)$ additional allocations.
- Optimization: Avoids system creation when records are absent.

### Usage Examples

 ```cpp
 auto env = std::make_unique<scene::SceneEnvironment>();
 EnvironmentSettingsService::HydrateEnvironment(*env, asset);
 ```

 @note SkyAtmosphere and SkySphere are treated as mutually exclusive.
*/
auto EnvironmentSettingsService::HydrateEnvironment(
  scene::SceneEnvironment& target, const data::SceneAsset& source_asset) -> void
{
  const auto sky_atmo_record = source_asset.TryGetSkyAtmosphereEnvironment();
  const auto sky_sphere_record = source_asset.TryGetSkySphereEnvironment();

  const bool sky_atmo_enabled = IsEnabled(sky_atmo_record);
  const bool sky_sphere_enabled = IsEnabled(sky_sphere_record);

  if (sky_atmo_enabled && sky_sphere_enabled) {
    LOG_F(WARNING,
      "Both SkyAtmosphere and SkySphere are enabled in the scene. They are "
      "mutually exclusive; SkyAtmosphere will be used.");
  }

  if (sky_atmo_enabled) {
    auto& atmo = target.AddSystem<scene::environment::SkyAtmosphere>();
    HydrateSkyAtmosphere(atmo, *sky_atmo_record);
    LOG_F(1, "Applied SkyAtmosphere environment");
  } else if (sky_sphere_enabled) {
    auto& sky_sphere = target.AddSystem<scene::environment::SkySphere>();
    HydrateSkySphere(sky_sphere, *sky_sphere_record);
    LOG_F(1, "Applied SkySphere environment (solid color source)");
  }

  if (const auto fog_record = source_asset.TryGetFogEnvironment();
    IsEnabled(fog_record)) {
    auto& fog = target.AddSystem<scene::environment::Fog>();
    HydrateFog(fog, *fog_record);
    LOG_F(1, "Applied Fog environment");
  }

  if (const auto sky_light_record = source_asset.TryGetSkyLightEnvironment();
    IsEnabled(sky_light_record)) {
    auto& sky_light = target.AddSystem<scene::environment::SkyLight>();
    HydrateSkyLight(sky_light, *sky_light_record);
    LOG_F(1, "Applied SkyLight environment");
  }

  if (const auto clouds_record
    = source_asset.TryGetVolumetricCloudsEnvironment();
    IsEnabled(clouds_record)) {
    auto& clouds = target.AddSystem<scene::environment::VolumetricClouds>();
    HydrateVolumetricClouds(clouds, *clouds_record);
    LOG_F(1, "Applied VolumetricClouds environment");
  }

  if (const auto pp_record = source_asset.TryGetPostProcessVolumeEnvironment();
    IsEnabled(pp_record)) {
    auto& pp = target.AddSystem<scene::environment::PostProcessVolume>();
    HydratePostProcessVolume(pp, *pp_record);
    LOG_F(1, "Applied PostProcessVolume environment");
  }
}

auto EnvironmentSettingsService::SetRuntimeConfig(
  const EnvironmentRuntimeConfig& config) -> void
{
  const bool scene_changed
    = force_scene_rebind_ || (config_.scene.get() != config.scene.get());
  config_ = config;
  force_scene_rebind_ = false;

  if (!settings_loaded_) {
    LoadSettings();
  }

  NormalizeSkySystems();

  if (scene_changed) {
    if (!config_.scene) {
      PersistSettingsIfDirty();
      pending_changes_ = false;
      dirty_domains_ = ToMask(DirtyDomain::kNone);
      batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
      needs_sync_ = true;
      return;
    }

    if (kForceEnvironmentOverride) {
      pending_changes_ = true;
      dirty_domains_ = ToMask(DirtyDomain::kAll);
      batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
      needs_sync_ = false;
      skybox_dirty_ = true;
      return;
    }

    if (preset_index_ == kPresetUseScene) {
      pending_changes_ = false;
      dirty_domains_ = ToMask(DirtyDomain::kNone);
      batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
      needs_sync_ = true;
      SyncFromSceneIfNeeded();
    } else if (preset_index_ == kPresetCustom) {
      // Custom mode applies persisted settings when available; otherwise it
      // mirrors the scene as source-of-truth until user edits.
      if (has_persisted_settings_) {
        pending_changes_ = true;
        dirty_domains_ = ToMask(DirtyDomain::kAll);
        needs_sync_ = false;
        skybox_dirty_ = true;
      } else {
        pending_changes_ = false;
        dirty_domains_ = ToMask(DirtyDomain::kNone);
        needs_sync_ = true;
        SyncFromSceneIfNeeded();
      }
    } else {
      // Built-in presets are applied by EnvironmentVm, not synced from scene.
      pending_changes_ = false;
      dirty_domains_ = ToMask(DirtyDomain::kNone);
      batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
      needs_sync_ = false;
    }
  }
}

auto EnvironmentSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto EnvironmentSettingsService::OnFrameStart(
  const engine::FrameContext& /*context*/) -> void
{
  applied_changes_this_frame_ = false;
  SyncFromSceneIfNeeded();
  ApplyPendingChanges();
  PersistSettingsIfDirty();
}

auto EnvironmentSettingsService::OnSceneActivated(scene::Scene& scene) -> void
{
  PersistSettingsIfDirty();
  config_.scene = observer_ptr { &scene };
  // Ensure the next runtime config update runs scene-transition logic even
  // though config_.scene is pre-bound here for immediate HasScene()
  // correctness.
  force_scene_rebind_ = true;
  config_.skybox_service = nullptr;
  main_view_id_.reset();
  needs_sync_ = true;
  sun_light_available_ = false;
  sun_light_node_ = {};
  pending_changes_ = false;
  dirty_domains_ = ToMask(DirtyDomain::kNone);
  batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
  epoch_++;
  EnsureSceneHasSunAtActivation();
}

auto EnvironmentSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/, const vortex::CompositionView& view)
  -> void
{
  OnRuntimeMainViewReady(view.id);
}

auto EnvironmentSettingsService::OnRuntimeMainViewReady(const ViewId view_id)
  -> void
{
  main_view_id_ = view_id;
}

auto EnvironmentSettingsService::HasScene() const noexcept -> bool
{
  return config_.scene != nullptr;
}

auto EnvironmentSettingsService::RequestResync() -> void { needs_sync_ = true; }

auto EnvironmentSettingsService::BeginUpdate() -> void { update_depth_++; }

auto EnvironmentSettingsService::EndUpdate() -> void
{
  if (update_depth_ > 0) {
    update_depth_--;
    if (update_depth_ == 0) {
      if (batched_dirty_domains_ != ToMask(DirtyDomain::kNone)) {
        const auto merged_domains = batched_dirty_domains_;
        batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
        MarkDirty(merged_domains);
      }
    }
  }
}

auto EnvironmentSettingsService::GetPresetIndex() const -> int
{
  return preset_index_;
}

auto EnvironmentSettingsService::SetPresetIndex(int index) -> void
{
  if (preset_index_ == index) {
    return;
  }
  preset_index_ = index;
  settings_persist_dirty_ = true;
  settings_revision_++;
  DLOG_F(1, "preset index changed to {} (revision={})", preset_index_,
    settings_revision_);
}

auto EnvironmentSettingsService::ActivateUseSceneMode() -> void
{
  if (kForceEnvironmentOverride) {
    pending_changes_ = true;
    dirty_domains_ = ToMask(DirtyDomain::kAll);
    batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
    needs_sync_ = false;
    skybox_dirty_ = true;
    return;
  }

  pending_changes_ = false;
  dirty_domains_ = ToMask(DirtyDomain::kNone);
  batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
  needs_sync_ = true;
  SyncFromSceneIfNeeded();
}

auto EnvironmentSettingsService::ActivateCustomMode() -> void
{
  preset_index_ = kPresetCustom;
  settings_persist_dirty_ = true;
  needs_sync_ = false;
}

auto EnvironmentSettingsService::SyncFromSceneIfNeeded() -> void
{
  if (kForceEnvironmentOverride) {
    needs_sync_ = false;
    return;
  }

  if (!needs_sync_) {
    return;
  }
  if (pending_changes_) {
    DLOG_F(WARNING,
      "deferring scene sync while pending UI changes exist (mask=0x{:X}, "
      "revision={})",
      dirty_domains_, settings_revision_);
    return;
  }
  if (update_depth_ > 0) {
    DLOG_F(WARNING,
      "deferring scene sync while update transaction is active (depth={})",
      update_depth_);
    return;
  }

  SyncFromScene();
  needs_sync_ = false;
}

auto EnvironmentSettingsService::HasPendingChanges() const -> bool
{
  return pending_changes_;
}

auto EnvironmentSettingsService::GetAtmosphereLutStatus() const
  -> std::pair<bool, bool>
{
  bool luts_valid = false;
  bool luts_dirty = true;

  if (config_.renderer && main_view_id_.has_value()) {
    const auto state = config_.renderer->GetLastEnvironmentLightingState();
    luts_valid = state.published_bindings && state.sky_executed
      && state.atmosphere_executed;
    luts_dirty = !luts_valid;
  }

  return { luts_valid, luts_dirty };
}

auto EnvironmentSettingsService::GetSkyAtmosphereEnabled() const -> bool
{
  return sky_atmo_enabled_;
}

auto EnvironmentSettingsService::SetSkyAtmosphereEnabled(bool enabled) -> void
{
  if (sky_atmo_enabled_ == enabled) {
    return;
  }
  sky_atmo_enabled_ = enabled;
  NormalizeSkySystems();
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetSkyAtmosphereTransformMode() const -> int
{
  return sky_atmo_transform_mode_;
}

auto EnvironmentSettingsService::SetSkyAtmosphereTransformMode(int value)
  -> void
{
  if (sky_atmo_transform_mode_ == value) {
    return;
  }
  sky_atmo_transform_mode_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetPlanetRadiusKm() const -> float
{
  return planet_radius_km_;
}

auto EnvironmentSettingsService::SetPlanetRadiusKm(float value) -> void
{
  if (planet_radius_km_ == value) {
    return;
  }
  planet_radius_km_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetAtmosphereHeightKm() const -> float
{
  return atmosphere_height_km_;
}

auto EnvironmentSettingsService::SetAtmosphereHeightKm(float value) -> void
{
  if (atmosphere_height_km_ == value) {
    return;
  }
  atmosphere_height_km_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetGroundAlbedo() const -> glm::vec3
{
  return ground_albedo_;
}

auto EnvironmentSettingsService::SetGroundAlbedo(const glm::vec3& value) -> void
{
  if (ground_albedo_ == value) {
    return;
  }
  ground_albedo_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetRayleighScaleHeightKm() const -> float
{
  return rayleigh_scale_height_km_;
}

auto EnvironmentSettingsService::SetRayleighScaleHeightKm(float value) -> void
{
  if (rayleigh_scale_height_km_ == value) {
    return;
  }
  rayleigh_scale_height_km_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetMieScaleHeightKm() const -> float
{
  return mie_scale_height_km_;
}

auto EnvironmentSettingsService::SetMieScaleHeightKm(float value) -> void
{
  if (mie_scale_height_km_ == value) {
    return;
  }
  mie_scale_height_km_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetMieAnisotropy() const -> float
{
  return mie_anisotropy_;
}

auto EnvironmentSettingsService::SetMieAnisotropy(float value) -> void
{
  if (mie_anisotropy_ == value) {
    return;
  }
  mie_anisotropy_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetMieAbsorptionScale() const -> float
{
  return mie_absorption_scale_;
}

auto EnvironmentSettingsService::SetMieAbsorptionScale(float value) -> void
{
  value = std::clamp(value, 0.0F, 5.0F);
  if (mie_absorption_scale_ == value) {
    return;
  }
  mie_absorption_scale_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetMultiScattering() const -> float
{
  return multi_scattering_;
}

auto EnvironmentSettingsService::SetMultiScattering(float value) -> void
{
  if (multi_scattering_ == value) {
    return;
  }
  multi_scattering_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetSkyLuminanceFactor() const -> glm::vec3
{
  return sky_luminance_factor_;
}

auto EnvironmentSettingsService::SetSkyLuminanceFactor(const glm::vec3& value)
  -> void
{
  if (sky_luminance_factor_ == value) {
    return;
  }
  sky_luminance_factor_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetSkyAndAerialPerspectiveLuminanceFactor()
  const -> glm::vec3
{
  return sky_and_aerial_perspective_luminance_factor_;
}

auto EnvironmentSettingsService::SetSkyAndAerialPerspectiveLuminanceFactor(
  const glm::vec3& value) -> void
{
  if (sky_and_aerial_perspective_luminance_factor_ == value) {
    return;
  }
  sky_and_aerial_perspective_luminance_factor_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetOzoneRgb() const -> glm::vec3
{
  return ozone_rgb_;
}

auto EnvironmentSettingsService::SetOzoneRgb(const glm::vec3& value) -> void
{
  if (ozone_rgb_ == value) {
    return;
  }
  ozone_rgb_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetOzoneDensityProfile() const
  -> engine::atmos::DensityProfile
{
  return ozone_profile_;
}

auto EnvironmentSettingsService::SetOzoneDensityProfile(
  const engine::atmos::DensityProfile& profile) -> void
{
  ozone_profile_ = profile;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetSunDiskEnabled() const -> bool
{
  return sun_disk_enabled_;
}

auto EnvironmentSettingsService::SetSunDiskEnabled(const bool enabled) -> void
{
  if (sun_disk_enabled_ == enabled) {
    return;
  }
  sun_disk_enabled_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetAerialPerspectiveScale() const -> float
{
  return aerial_perspective_scale_;
}

auto EnvironmentSettingsService::SetAerialPerspectiveScale(const float value)
  -> void
{
  if (aerial_perspective_scale_ == value) {
    return;
  }
  aerial_perspective_scale_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetAerialPerspectiveStartDepthMeters() const
  -> float
{
  return aerial_perspective_start_depth_m_;
}

auto EnvironmentSettingsService::SetAerialPerspectiveStartDepthMeters(
  const float value) -> void
{
  if (aerial_perspective_start_depth_m_ == value) {
    return;
  }
  aerial_perspective_start_depth_m_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetAerialScatteringStrength() const -> float
{
  return aerial_scattering_strength_;
}

auto EnvironmentSettingsService::SetAerialScatteringStrength(const float value)
  -> void
{
  if (aerial_scattering_strength_ == value) {
    return;
  }
  aerial_scattering_strength_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetHeightFogContribution() const -> float
{
  return height_fog_contribution_;
}

auto EnvironmentSettingsService::SetHeightFogContribution(const float value)
  -> void
{
  if (height_fog_contribution_ == value) {
    return;
  }
  height_fog_contribution_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetTraceSampleCountScale() const -> float
{
  return trace_sample_count_scale_;
}

auto EnvironmentSettingsService::SetTraceSampleCountScale(const float value)
  -> void
{
  if (trace_sample_count_scale_ == value) {
    return;
  }
  trace_sample_count_scale_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetTransmittanceMinLightElevationDeg() const
  -> float
{
  return transmittance_min_light_elevation_deg_;
}

auto EnvironmentSettingsService::SetTransmittanceMinLightElevationDeg(
  const float value) -> void
{
  if (transmittance_min_light_elevation_deg_ == value) {
    return;
  }
  transmittance_min_light_elevation_deg_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetAtmosphereHoldout() const -> bool
{
  return atmosphere_holdout_;
}

auto EnvironmentSettingsService::SetAtmosphereHoldout(const bool enabled)
  -> void
{
  if (atmosphere_holdout_ == enabled) {
    return;
  }
  atmosphere_holdout_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetAtmosphereRenderInMainPass() const -> bool
{
  return atmosphere_render_in_main_pass_;
}

auto EnvironmentSettingsService::SetAtmosphereRenderInMainPass(
  const bool enabled) -> void
{
  if (atmosphere_render_in_main_pass_ == enabled) {
    return;
  }
  atmosphere_render_in_main_pass_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereModel));
}

auto EnvironmentSettingsService::GetSkyViewLutSlices() const -> int
{
  return sky_view_lut_slices_;
}

auto EnvironmentSettingsService::SetSkyViewLutSlices(int value) -> void
{
  value = std::clamp(value, 1, 128);
  if (sky_view_lut_slices_ == value) {
    return;
  }
  DLOG_F(1,
    "SkyView LUT slices are renderer-owned; ignoring UI write {} "
    "(current={})",
    value, sky_view_lut_slices_);
}

auto EnvironmentSettingsService::GetSkyViewAltMappingMode() const -> int
{
  return sky_view_alt_mapping_mode_;
}

auto EnvironmentSettingsService::SetSkyViewAltMappingMode(int value) -> void
{
  value = std::clamp(value, 0, 1);
  if (sky_view_alt_mapping_mode_ == value) {
    return;
  }
  DLOG_F(1,
    "SkyView mapping mode is renderer-owned; ignoring UI write {} "
    "(current={})",
    value, sky_view_alt_mapping_mode_);
}

auto EnvironmentSettingsService::RequestRegenerateLut() -> void
{
  DLOG_F(1, "RequestRegenerateLut ignored; renderer owns LUT regeneration");
}

auto EnvironmentSettingsService::GetSkySphereEnabled() const -> bool
{
  return sky_sphere_enabled_;
}

auto EnvironmentSettingsService::SetSkySphereEnabled(bool enabled) -> void
{
  if (sky_sphere_enabled_ == enabled) {
    return;
  }
  sky_sphere_enabled_ = enabled;
  NormalizeSkySystems();
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkySphere) | ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkySphereSource() const -> int
{
  return sky_sphere_source_;
}

auto EnvironmentSettingsService::SetSkySphereSource(int source) -> void
{
  if (sky_sphere_source_ == source) {
    return;
  }
  sky_sphere_source_ = source;
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkySphere) | ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkySphereSolidColor() const -> glm::vec3
{
  return sky_sphere_solid_color_;
}

auto EnvironmentSettingsService::SetSkySphereSolidColor(const glm::vec3& value)
  -> void
{
  if (sky_sphere_solid_color_ == value) {
    return;
  }
  sky_sphere_solid_color_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkySphere));
}

auto EnvironmentSettingsService::GetSkyIntensity() const -> float
{
  return sky_intensity_;
}

auto EnvironmentSettingsService::SetSkyIntensity(const float value) -> void
{
  if (sky_intensity_ == value) {
    return;
  }
  sky_intensity_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkySphere));
}

auto EnvironmentSettingsService::GetSkySphereRotationDeg() const -> float
{
  return sky_sphere_rotation_deg_;
}

auto EnvironmentSettingsService::SetSkySphereRotationDeg(float value) -> void
{
  if (sky_sphere_rotation_deg_ == value) {
    return;
  }
  sky_sphere_rotation_deg_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkySphere));
}

auto EnvironmentSettingsService::GetSkyboxPath() const -> std::string
{
  return skybox_path_;
}

auto EnvironmentSettingsService::SetSkyboxPath(std::string_view path) -> void
{
  if (skybox_path_ == path) {
    return;
  }
  skybox_path_ = std::string(path);
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkyboxLayoutIndex() const -> int
{
  return skybox_layout_idx_;
}

auto EnvironmentSettingsService::SetSkyboxLayoutIndex(int index) -> void
{
  if (skybox_layout_idx_ == index) {
    return;
  }
  skybox_layout_idx_ = index;
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkyboxOutputFormatIndex() const -> int
{
  return skybox_output_format_idx_;
}

auto EnvironmentSettingsService::SetSkyboxOutputFormatIndex(int index) -> void
{
  if (skybox_output_format_idx_ == index) {
    return;
  }
  skybox_output_format_idx_ = index;
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkyboxFaceSize() const -> int
{
  return skybox_face_size_;
}

auto EnvironmentSettingsService::SetSkyboxFaceSize(int size) -> void
{
  if (skybox_face_size_ == size) {
    return;
  }
  skybox_face_size_ = size;
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkyboxFlipY() const -> bool
{
  return skybox_flip_y_;
}

auto EnvironmentSettingsService::SetSkyboxFlipY(bool flip) -> void
{
  if (skybox_flip_y_ == flip) {
    return;
  }
  skybox_flip_y_ = flip;
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkyboxTonemapHdrToLdr() const -> bool
{
  return skybox_tonemap_hdr_to_ldr_;
}

auto EnvironmentSettingsService::SetSkyboxTonemapHdrToLdr(bool enabled) -> void
{
  if (skybox_tonemap_hdr_to_ldr_ == enabled) {
    return;
  }
  skybox_tonemap_hdr_to_ldr_ = enabled;
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkyboxHdrExposureEv() const -> float
{
  return skybox_hdr_exposure_ev_;
}

auto EnvironmentSettingsService::SetSkyboxHdrExposureEv(float value) -> void
{
  value = std::max(value, 0.0F);
  if (skybox_hdr_exposure_ev_ == value) {
    return;
  }
  skybox_hdr_exposure_ev_ = value;
  skybox_dirty_ = true;
  MarkDirty(ToMask(DirtyDomain::kSkybox));
}

auto EnvironmentSettingsService::GetSkyboxStatusMessage() const
  -> std::string_view
{
  return skybox_status_message_;
}

auto EnvironmentSettingsService::GetSkyboxLastFaceSize() const -> int
{
  return skybox_last_face_size_;
}

auto EnvironmentSettingsService::GetSkyboxLastResourceKey() const
  -> content::ResourceKey
{
  return skybox_last_resource_key_;
}

auto EnvironmentSettingsService::LoadSkybox(std::string_view path,
  int layout_index, int output_format_index, int face_size, bool flip_y,
  bool tonemap_hdr_to_ldr, float hdr_exposure_ev) -> void
{
  skybox_status_message_ = "Loading skybox...";
  skybox_last_face_size_ = 0;
  skybox_last_resource_key_ = content::ResourceKey { 0U };

  if (!config_.skybox_service) {
    skybox_status_message_ = "Skybox service unavailable";
    return;
  }

  SkyboxService::LoadOptions options;
  options.layout
    = static_cast<SkyboxService::Layout>(std::clamp(layout_index, 0, 4));
  options.output_format = static_cast<SkyboxService::OutputFormat>(
    std::clamp(output_format_index, 0, 3));
  options.cube_face_size = std::clamp(face_size, 16, 4096);
  options.flip_y = flip_y;
  options.tonemap_hdr_to_ldr = tonemap_hdr_to_ldr;
  options.hdr_exposure_ev = hdr_exposure_ev;

  config_.skybox_service->LoadAndEquip(std::string(path), options,
    { .sky_sphere_intensity = sky_intensity_,
      .intensity_mul = sky_light_intensity_mul_,
      .diffuse_intensity = sky_light_diffuse_,
      .specular_intensity = sky_light_specular_,
      .tint_rgb = sky_light_tint_ },
    [this](const SkyboxService::LoadResult& result) {
      skybox_status_message_ = result.status_message;
      skybox_last_face_size_ = result.face_size;
      skybox_last_resource_key_ = result.resource_key;
      if (result.success) {
        RequestResync();
      }
    });

  skybox_dirty_ = false;
  last_loaded_skybox_path_ = std::string(path);
  last_loaded_skybox_layout_idx_ = layout_index;
  last_loaded_skybox_output_format_idx_ = output_format_index;
  last_loaded_skybox_face_size_ = face_size;
  last_loaded_skybox_flip_y_ = flip_y;
  last_loaded_skybox_tonemap_hdr_to_ldr_ = tonemap_hdr_to_ldr;
  last_loaded_skybox_hdr_exposure_ev_ = hdr_exposure_ev;
}

auto EnvironmentSettingsService::GetSkyLightEnabled() const -> bool
{
  return sky_light_enabled_;
}

auto EnvironmentSettingsService::SetSkyLightEnabled(bool enabled) -> void
{
  if (sky_light_enabled_ == enabled) {
    return;
  }
  sky_light_enabled_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightSource() const -> int
{
  return sky_light_source_;
}

auto EnvironmentSettingsService::SetSkyLightSource(int source) -> void
{
  if (sky_light_source_ == source) {
    return;
  }
  sky_light_source_ = source;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightTint() const -> glm::vec3
{
  return sky_light_tint_;
}

auto EnvironmentSettingsService::SetSkyLightTint(const glm::vec3& value) -> void
{
  if (sky_light_tint_ == value) {
    return;
  }
  sky_light_tint_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightIntensityMul() const -> float
{
  return sky_light_intensity_mul_;
}

auto EnvironmentSettingsService::SetSkyLightIntensityMul(const float value)
  -> void
{
  if (sky_light_intensity_mul_ == value) {
    return;
  }
  sky_light_intensity_mul_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightDiffuse() const -> float
{
  return sky_light_diffuse_;
}

auto EnvironmentSettingsService::SetSkyLightDiffuse(float value) -> void
{
  if (sky_light_diffuse_ == value) {
    return;
  }
  sky_light_diffuse_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightSpecular() const -> float
{
  return sky_light_specular_;
}

auto EnvironmentSettingsService::SetSkyLightSpecular(float value) -> void
{
  if (sky_light_specular_ == value) {
    return;
  }
  sky_light_specular_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightRealTimeCaptureEnabled() const
  -> bool
{
  return sky_light_real_time_capture_enabled_;
}

auto EnvironmentSettingsService::SetSkyLightRealTimeCaptureEnabled(
  const bool enabled) -> void
{
  if (sky_light_real_time_capture_enabled_ == enabled) {
    return;
  }
  sky_light_real_time_capture_enabled_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightLowerHemisphereColor() const
  -> glm::vec3
{
  return sky_light_lower_hemisphere_color_;
}

auto EnvironmentSettingsService::SetSkyLightLowerHemisphereColor(
  const glm::vec3& value) -> void
{
  if (sky_light_lower_hemisphere_color_ == value) {
    return;
  }
  sky_light_lower_hemisphere_color_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightVolumetricScatteringIntensity()
  const -> float
{
  return sky_light_volumetric_scattering_intensity_;
}

auto EnvironmentSettingsService::SetSkyLightVolumetricScatteringIntensity(
  const float value) -> void
{
  if (sky_light_volumetric_scattering_intensity_ == value) {
    return;
  }
  sky_light_volumetric_scattering_intensity_ = value;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightAffectReflections() const -> bool
{
  return sky_light_affect_reflections_;
}

auto EnvironmentSettingsService::SetSkyLightAffectReflections(
  const bool enabled) -> void
{
  if (sky_light_affect_reflections_ == enabled) {
    return;
  }
  sky_light_affect_reflections_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetSkyLightAffectGlobalIllumination() const
  -> bool
{
  return sky_light_affect_global_illumination_;
}

auto EnvironmentSettingsService::SetSkyLightAffectGlobalIllumination(
  const bool enabled) -> void
{
  if (sky_light_affect_global_illumination_ == enabled) {
    return;
  }
  sky_light_affect_global_illumination_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kSkyLight));
}

auto EnvironmentSettingsService::GetFogEnabled() const -> bool
{
  return fog_enabled_;
}

auto EnvironmentSettingsService::SetFogEnabled(const bool enabled) -> void
{
  if (fog_enabled_ == enabled) {
    return;
  }
  fog_enabled_ = enabled;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogModel() const -> int
{
  return fog_model_;
}

auto EnvironmentSettingsService::SetFogModel(const int model) -> void
{
  if (fog_model_ == model) {
    return;
  }
  fog_model_ = model;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogExtinctionSigmaTPerMeter() const -> float
{
  return fog_extinction_sigma_t_per_m_;
}

auto EnvironmentSettingsService::SetFogExtinctionSigmaTPerMeter(
  const float value) -> void
{
  if (fog_extinction_sigma_t_per_m_ == value) {
    return;
  }
  fog_extinction_sigma_t_per_m_ = value;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogHeightFalloffPerMeter() const -> float
{
  return fog_height_falloff_per_m_;
}

auto EnvironmentSettingsService::SetFogHeightFalloffPerMeter(const float value)
  -> void
{
  if (fog_height_falloff_per_m_ == value) {
    return;
  }
  fog_height_falloff_per_m_ = value;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogHeightOffsetMeters() const -> float
{
  return fog_height_offset_m_;
}

auto EnvironmentSettingsService::SetFogHeightOffsetMeters(const float value)
  -> void
{
  if (fog_height_offset_m_ == value) {
    return;
  }
  fog_height_offset_m_ = value;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogStartDistanceMeters() const -> float
{
  return fog_start_distance_m_;
}

auto EnvironmentSettingsService::SetFogStartDistanceMeters(const float value)
  -> void
{
  if (fog_start_distance_m_ == value) {
    return;
  }
  fog_start_distance_m_ = value;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetSecondFogDensity() const -> float
{
  return second_fog_density_;
}

auto EnvironmentSettingsService::SetSecondFogDensity(const float value) -> void
{
  if (second_fog_density_ == value) {
    return;
  }
  second_fog_density_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetSecondFogHeightFalloff() const -> float
{
  return second_fog_height_falloff_;
}

auto EnvironmentSettingsService::SetSecondFogHeightFalloff(const float value)
  -> void
{
  if (second_fog_height_falloff_ == value) {
    return;
  }
  second_fog_height_falloff_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetSecondFogHeightOffset() const -> float
{
  return second_fog_height_offset_;
}

auto EnvironmentSettingsService::SetSecondFogHeightOffset(const float value)
  -> void
{
  if (second_fog_height_offset_ == value) {
    return;
  }
  second_fog_height_offset_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetFogMaxOpacity() const -> float
{
  return fog_max_opacity_;
}

auto EnvironmentSettingsService::SetFogMaxOpacity(const float value) -> void
{
  if (fog_max_opacity_ == value) {
    return;
  }
  fog_max_opacity_ = value;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogSingleScatteringAlbedoRgb() const
  -> glm::vec3
{
  return fog_single_scattering_albedo_rgb_;
}

auto EnvironmentSettingsService::SetFogSingleScatteringAlbedoRgb(
  const glm::vec3& value) -> void
{
  if (fog_single_scattering_albedo_rgb_ == value) {
    return;
  }
  fog_single_scattering_albedo_rgb_ = value;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogInscatteringLuminance() const
  -> glm::vec3
{
  return fog_inscattering_luminance_;
}

auto EnvironmentSettingsService::SetFogInscatteringLuminance(
  const glm::vec3& value) -> void
{
  if (fog_inscattering_luminance_ == value) {
    return;
  }
  fog_inscattering_luminance_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetSkyAtmosphereAmbientContributionColorScale()
  const -> glm::vec3
{
  return sky_atmosphere_ambient_contribution_color_scale_;
}

auto EnvironmentSettingsService::SetSkyAtmosphereAmbientContributionColorScale(
  const glm::vec3& value) -> void
{
  if (sky_atmosphere_ambient_contribution_color_scale_ == value) {
    return;
  }
  sky_atmosphere_ambient_contribution_color_scale_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetInscatteringColorCubemapAngle() const
  -> float
{
  return inscattering_color_cubemap_angle_;
}

auto EnvironmentSettingsService::SetInscatteringColorCubemapAngle(
  const float value) -> void
{
  if (inscattering_color_cubemap_angle_ == value) {
    return;
  }
  inscattering_color_cubemap_angle_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetInscatteringTextureTint() const -> glm::vec3
{
  return inscattering_texture_tint_;
}

auto EnvironmentSettingsService::SetInscatteringTextureTint(
  const glm::vec3& value) -> void
{
  if (inscattering_texture_tint_ == value) {
    return;
  }
  inscattering_texture_tint_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetFullyDirectionalInscatteringColorDistance()
  const -> float
{
  return fully_directional_inscattering_color_distance_;
}

auto EnvironmentSettingsService::SetFullyDirectionalInscatteringColorDistance(
  const float value) -> void
{
  if (fully_directional_inscattering_color_distance_ == value) {
    return;
  }
  fully_directional_inscattering_color_distance_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetNonDirectionalInscatteringColorDistance()
  const -> float
{
  return non_directional_inscattering_color_distance_;
}

auto EnvironmentSettingsService::SetNonDirectionalInscatteringColorDistance(
  const float value) -> void
{
  if (non_directional_inscattering_color_distance_ == value) {
    return;
  }
  non_directional_inscattering_color_distance_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetDirectionalInscatteringLuminance() const
  -> glm::vec3
{
  return directional_inscattering_luminance_;
}

auto EnvironmentSettingsService::SetDirectionalInscatteringLuminance(
  const glm::vec3& value) -> void
{
  if (directional_inscattering_luminance_ == value) {
    return;
  }
  directional_inscattering_luminance_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetDirectionalInscatteringExponent() const
  -> float
{
  return directional_inscattering_exponent_;
}

auto EnvironmentSettingsService::SetDirectionalInscatteringExponent(
  const float value) -> void
{
  if (directional_inscattering_exponent_ == value) {
    return;
  }
  directional_inscattering_exponent_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetDirectionalInscatteringStartDistance() const
  -> float
{
  return directional_inscattering_start_distance_;
}

auto EnvironmentSettingsService::SetDirectionalInscatteringStartDistance(
  const float value) -> void
{
  if (directional_inscattering_start_distance_ == value) {
    return;
  }
  directional_inscattering_start_distance_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetFogEndDistanceMeters() const -> float
{
  return fog_end_distance_m_;
}

auto EnvironmentSettingsService::SetFogEndDistanceMeters(const float value)
  -> void
{
  if (fog_end_distance_m_ == value) {
    return;
  }
  fog_end_distance_m_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetFogCutoffDistanceMeters() const -> float
{
  return fog_cutoff_distance_m_;
}

auto EnvironmentSettingsService::SetFogCutoffDistanceMeters(const float value)
  -> void
{
  if (fog_cutoff_distance_m_ == value) {
    return;
  }
  fog_cutoff_distance_m_ = value;
  MarkDirty(ToMask(DirtyDomain::kHeightFog));
}

auto EnvironmentSettingsService::GetVolumetricFogScatteringDistribution() const
  -> float
{
  return volumetric_fog_scattering_distribution_;
}

auto EnvironmentSettingsService::SetVolumetricFogScatteringDistribution(
  const float value) -> void
{
  if (volumetric_fog_scattering_distribution_ == value) {
    return;
  }
  volumetric_fog_scattering_distribution_ = value;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::GetVolumetricFogAlbedo() const -> glm::vec3
{
  return volumetric_fog_albedo_;
}

auto EnvironmentSettingsService::SetVolumetricFogAlbedo(const glm::vec3& value)
  -> void
{
  if (volumetric_fog_albedo_ == value) {
    return;
  }
  volumetric_fog_albedo_ = value;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::GetVolumetricFogEmissive() const -> glm::vec3
{
  return volumetric_fog_emissive_;
}

auto EnvironmentSettingsService::SetVolumetricFogEmissive(
  const glm::vec3& value) -> void
{
  if (volumetric_fog_emissive_ == value) {
    return;
  }
  volumetric_fog_emissive_ = value;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::GetVolumetricFogExtinctionScale() const
  -> float
{
  return volumetric_fog_extinction_scale_;
}

auto EnvironmentSettingsService::SetVolumetricFogExtinctionScale(
  const float value) -> void
{
  if (volumetric_fog_extinction_scale_ == value) {
    return;
  }
  volumetric_fog_extinction_scale_ = value;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::GetVolumetricFogDistanceMeters() const -> float
{
  return volumetric_fog_distance_m_;
}

auto EnvironmentSettingsService::SetVolumetricFogDistanceMeters(
  const float value) -> void
{
  if (volumetric_fog_distance_m_ == value) {
    return;
  }
  volumetric_fog_distance_m_ = value;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::GetVolumetricFogStartDistanceMeters() const
  -> float
{
  return volumetric_fog_start_distance_m_;
}

auto EnvironmentSettingsService::SetVolumetricFogStartDistanceMeters(
  const float value) -> void
{
  if (volumetric_fog_start_distance_m_ == value) {
    return;
  }
  volumetric_fog_start_distance_m_ = value;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::GetVolumetricFogNearFadeInDistanceMeters()
  const -> float
{
  return volumetric_fog_near_fade_in_distance_m_;
}

auto EnvironmentSettingsService::SetVolumetricFogNearFadeInDistanceMeters(
  const float value) -> void
{
  if (volumetric_fog_near_fade_in_distance_m_ == value) {
    return;
  }
  volumetric_fog_near_fade_in_distance_m_ = value;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::
  GetVolumetricFogStaticLightingScatteringIntensity() const -> float
{
  return volumetric_fog_static_lighting_scattering_intensity_;
}

auto EnvironmentSettingsService::
  SetVolumetricFogStaticLightingScatteringIntensity(const float value) -> void
{
  if (volumetric_fog_static_lighting_scattering_intensity_ == value) {
    return;
  }
  volumetric_fog_static_lighting_scattering_intensity_ = value;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::
  GetOverrideLightColorsWithFogInscatteringColors() const -> bool
{
  return override_light_colors_with_fog_inscattering_colors_;
}

auto EnvironmentSettingsService::
  SetOverrideLightColorsWithFogInscatteringColors(const bool enabled) -> void
{
  if (override_light_colors_with_fog_inscattering_colors_ == enabled) {
    return;
  }
  override_light_colors_with_fog_inscattering_colors_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kVolumetricFog));
}

auto EnvironmentSettingsService::GetFogHoldout() const -> bool
{
  return fog_holdout_;
}

auto EnvironmentSettingsService::SetFogHoldout(const bool enabled) -> void
{
  if (fog_holdout_ == enabled) {
    return;
  }
  fog_holdout_ = enabled;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogRenderInMainPass() const -> bool
{
  return fog_render_in_main_pass_;
}

auto EnvironmentSettingsService::SetFogRenderInMainPass(const bool enabled)
  -> void
{
  if (fog_render_in_main_pass_ == enabled) {
    return;
  }
  fog_render_in_main_pass_ = enabled;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogVisibleInReflectionCaptures() const
  -> bool
{
  return fog_visible_in_reflection_captures_;
}

auto EnvironmentSettingsService::SetFogVisibleInReflectionCaptures(
  const bool enabled) -> void
{
  if (fog_visible_in_reflection_captures_ == enabled) {
    return;
  }
  fog_visible_in_reflection_captures_ = enabled;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetFogVisibleInRealTimeSkyCaptures() const
  -> bool
{
  return fog_visible_in_real_time_sky_captures_;
}

auto EnvironmentSettingsService::SetFogVisibleInRealTimeSkyCaptures(
  const bool enabled) -> void
{
  if (fog_visible_in_real_time_sky_captures_ == enabled) {
    return;
  }
  fog_visible_in_real_time_sky_captures_ = enabled;
  MarkDirty(kFogDirtyMask);
}

auto EnvironmentSettingsService::GetLocalFogVolumeCount() const -> int
{
  return static_cast<int>(local_fog_volumes_.size());
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeIndex() const -> int
{
  return selected_local_fog_volume_index_;
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeIndex(const int index)
  -> void
{
  if (local_fog_volumes_.empty()) {
    selected_local_fog_volume_index_ = -1;
    return;
  }
  selected_local_fog_volume_index_
    = std::clamp(index, 0, static_cast<int>(local_fog_volumes_.size()) - 1);
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeMutable()
  -> LocalFogVolumeUiState*
{
  if (selected_local_fog_volume_index_ < 0
    || selected_local_fog_volume_index_
      >= static_cast<int>(local_fog_volumes_.size())) {
    return nullptr;
  }
  return &local_fog_volumes_[static_cast<size_t>(
    selected_local_fog_volume_index_)];
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolume() const
  -> const LocalFogVolumeUiState*
{
  if (selected_local_fog_volume_index_ < 0
    || selected_local_fog_volume_index_
      >= static_cast<int>(local_fog_volumes_.size())) {
    return nullptr;
  }
  return &local_fog_volumes_[static_cast<size_t>(
    selected_local_fog_volume_index_)];
}

auto EnvironmentSettingsService::AddLocalFogVolume() -> void
{
  if (!config_.scene) {
    return;
  }

  auto node = config_.scene->CreateNode(
    "Local Fog Volume " + std::to_string(local_fog_volumes_.size() + 1));
  const auto impl_opt = node.GetImpl();
  if (!impl_opt.has_value()) {
    return;
  }

  impl_opt->get().AddComponent<scene::environment::LocalFogVolume>();
  const auto& local_fog
    = impl_opt->get().GetComponent<scene::environment::LocalFogVolume>();
  local_fog_volumes_.push_back(LocalFogVolumeUiState {
    .node = node,
    .enabled = local_fog.IsEnabled(),
    .radial_fog_extinction = local_fog.GetRadialFogExtinction(),
    .height_fog_extinction = local_fog.GetHeightFogExtinction(),
    .height_fog_falloff = local_fog.GetHeightFogFalloff(),
    .height_fog_offset = local_fog.GetHeightFogOffset(),
    .fog_phase_g = local_fog.GetFogPhaseG(),
    .fog_albedo = local_fog.GetFogAlbedo(),
    .fog_emissive = local_fog.GetFogEmissive(),
    .sort_priority = local_fog.GetSortPriority(),
  });
  selected_local_fog_volume_index_
    = static_cast<int>(local_fog_volumes_.size()) - 1;
  MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
}

auto EnvironmentSettingsService::RemoveSelectedLocalFogVolume() -> void
{
  auto* selected = GetSelectedLocalFogVolumeMutable();
  if (selected == nullptr) {
    return;
  }
  if (selected->node.IsAlive()) {
    removed_local_fog_nodes_.push_back(selected->node.GetHandle());
  }

  local_fog_volumes_.erase(local_fog_volumes_.begin()
    + static_cast<std::ptrdiff_t>(selected_local_fog_volume_index_));
  if (local_fog_volumes_.empty()) {
    selected_local_fog_volume_index_ = -1;
  } else {
    selected_local_fog_volume_index_
      = std::clamp(selected_local_fog_volume_index_, 0,
        static_cast<int>(local_fog_volumes_.size()) - 1);
  }
  MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeEnabled() const
  -> bool
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->enabled : false;
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeEnabled(
  const bool enabled) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->enabled = enabled;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeRadialFogExtinction()
  const -> float
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->radial_fog_extinction : 0.0F;
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeRadialFogExtinction(
  const float value) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->radial_fog_extinction = value;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeHeightFogExtinction()
  const -> float
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->height_fog_extinction : 0.0F;
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeHeightFogExtinction(
  const float value) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->height_fog_extinction = value;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeHeightFogFalloff()
  const -> float
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->height_fog_falloff : 0.0F;
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeHeightFogFalloff(
  const float value) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->height_fog_falloff = value;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeHeightFogOffset()
  const -> float
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->height_fog_offset : 0.0F;
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeHeightFogOffset(
  const float value) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->height_fog_offset = value;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeFogPhaseG() const
  -> float
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->fog_phase_g : 0.0F;
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeFogPhaseG(
  const float value) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->fog_phase_g = value;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeFogAlbedo() const
  -> glm::vec3
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->fog_albedo : glm::vec3 { 1.0F };
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeFogAlbedo(
  const glm::vec3& value) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->fog_albedo = value;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeFogEmissive() const
  -> glm::vec3
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->fog_emissive : glm::vec3 { 0.0F };
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeFogEmissive(
  const glm::vec3& value) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->fog_emissive = value;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSelectedLocalFogVolumeSortPriority() const
  -> int
{
  const auto* selected = GetSelectedLocalFogVolume();
  return selected != nullptr ? selected->sort_priority : 0;
}

auto EnvironmentSettingsService::SetSelectedLocalFogVolumeSortPriority(
  const int value) -> void
{
  if (auto* selected = GetSelectedLocalFogVolumeMutable()) {
    selected->sort_priority = value;
    MarkDirty(ToMask(DirtyDomain::kLocalFogVolumes));
  }
}

auto EnvironmentSettingsService::GetSunPresent() const -> bool
{
  return config_.scene != nullptr;
}

auto EnvironmentSettingsService::GetSunEnabled() const -> bool
{
  return sun_enabled_;
}

auto EnvironmentSettingsService::SetSunEnabled(bool enabled) -> void
{
  if (sun_enabled_ == enabled) {
    return;
  }
  sun_enabled_ = enabled;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunAzimuthDeg() const -> float
{
  return sun_azimuth_deg_;
}

auto EnvironmentSettingsService::SetSunAzimuthDeg(float value) -> void
{
  if (sun_azimuth_deg_ == value) {
    return;
  }
  sun_azimuth_deg_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunElevationDeg() const -> float
{
  return sun_elevation_deg_;
}

auto EnvironmentSettingsService::SetSunElevationDeg(float value) -> void
{
  if (sun_elevation_deg_ == value) {
    return;
  }
  sun_elevation_deg_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunColorRgb() const -> glm::vec3
{
  return sun_color_rgb_;
}

auto EnvironmentSettingsService::SetSunColorRgb(const glm::vec3& value) -> void
{
  if (sun_color_rgb_ == value) {
    return;
  }
  sun_color_rgb_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunIlluminanceLx() const -> float
{
  return sun_illuminance_lx_;
}

auto EnvironmentSettingsService::SetSunIlluminanceLx(float value) -> void
{
  if (sun_illuminance_lx_ == value) {
    return;
  }
  sun_illuminance_lx_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunUseTemperature() const -> bool
{
  return sun_use_temperature_;
}

auto EnvironmentSettingsService::SetSunUseTemperature(bool enabled) -> void
{
  if (sun_use_temperature_ == enabled) {
    return;
  }
  sun_use_temperature_ = enabled;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunTemperatureKelvin() const -> float
{
  return sun_temperature_kelvin_;
}

auto EnvironmentSettingsService::SetSunTemperatureKelvin(float value) -> void
{
  if (sun_temperature_kelvin_ == value) {
    return;
  }
  sun_temperature_kelvin_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunDiskRadiusDeg() const -> float
{
  return sun_component_disk_radius_deg_;
}

auto EnvironmentSettingsService::SetSunDiskRadiusDeg(float value) -> void
{
  if (sun_component_disk_radius_deg_ == value) {
    return;
  }
  sun_component_disk_radius_deg_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunAtmosphereLightSlot() const -> int
{
  return sun_atmosphere_light_slot_;
}

auto EnvironmentSettingsService::SetSunAtmosphereLightSlot(int value) -> void
{
  if (sun_atmosphere_light_slot_ == value) {
    return;
  }
  sun_atmosphere_light_slot_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereLights));
}

auto EnvironmentSettingsService::GetSunUsePerPixelAtmosphereTransmittance()
  const -> bool
{
  return sun_use_per_pixel_atmosphere_transmittance_;
}

auto EnvironmentSettingsService::SetSunUsePerPixelAtmosphereTransmittance(
  bool enabled) -> void
{
  if (sun_use_per_pixel_atmosphere_transmittance_ == enabled) {
    return;
  }
  sun_use_per_pixel_atmosphere_transmittance_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereLights));
}

auto EnvironmentSettingsService::GetSunAtmosphereDiskLuminanceScale() const
  -> glm::vec3
{
  return sun_atmosphere_disk_luminance_scale_;
}

auto EnvironmentSettingsService::SetSunAtmosphereDiskLuminanceScale(
  const glm::vec3& value) -> void
{
  if (sun_atmosphere_disk_luminance_scale_ == value) {
    return;
  }
  sun_atmosphere_disk_luminance_scale_ = value;
  MarkDirty(ToMask(DirtyDomain::kAtmosphereLights));
}

auto EnvironmentSettingsService::GetSunShadowBias() const -> float
{
  return sun_shadow_bias_;
}

auto EnvironmentSettingsService::SetSunShadowBias(float value) -> void
{
  if (sun_shadow_bias_ == value) {
    return;
  }
  sun_shadow_bias_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowNormalBias() const -> float
{
  return sun_shadow_normal_bias_;
}

auto EnvironmentSettingsService::SetSunShadowNormalBias(float value) -> void
{
  if (sun_shadow_normal_bias_ == value) {
    return;
  }
  sun_shadow_normal_bias_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowResolutionHint() const -> int
{
  return sun_shadow_resolution_hint_;
}

auto EnvironmentSettingsService::SetSunShadowResolutionHint(int value) -> void
{
  if (sun_shadow_resolution_hint_ == value) {
    return;
  }
  sun_shadow_resolution_hint_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowCascadeCount() const -> int
{
  return sun_shadow_cascade_count_;
}

auto EnvironmentSettingsService::SetSunShadowCascadeCount(int value) -> void
{
  if (sun_shadow_cascade_count_ == value) {
    return;
  }
  sun_shadow_cascade_count_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowSplitMode() const -> int
{
  return sun_shadow_split_mode_;
}

auto EnvironmentSettingsService::SetSunShadowSplitMode(int value) -> void
{
  if (sun_shadow_split_mode_ == value) {
    return;
  }
  sun_shadow_split_mode_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowMaxDistance() const -> float
{
  return sun_shadow_max_distance_;
}

auto EnvironmentSettingsService::SetSunShadowMaxDistance(float value) -> void
{
  if (sun_shadow_max_distance_ == value) {
    return;
  }
  sun_shadow_max_distance_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowDistributionExponent() const
  -> float
{
  return sun_shadow_distribution_exponent_;
}

auto EnvironmentSettingsService::SetSunShadowDistributionExponent(float value)
  -> void
{
  if (sun_shadow_distribution_exponent_ == value) {
    return;
  }
  sun_shadow_distribution_exponent_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowTransitionFraction() const -> float
{
  return sun_shadow_transition_fraction_;
}

auto EnvironmentSettingsService::SetSunShadowTransitionFraction(float value)
  -> void
{
  if (sun_shadow_transition_fraction_ == value) {
    return;
  }
  sun_shadow_transition_fraction_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowDistanceFadeoutFraction() const
  -> float
{
  return sun_shadow_distance_fadeout_fraction_;
}

auto EnvironmentSettingsService::SetSunShadowDistanceFadeoutFraction(
  float value) -> void
{
  if (sun_shadow_distance_fadeout_fraction_ == value) {
    return;
  }
  sun_shadow_distance_fadeout_fraction_ = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunShadowCascadeDistance(
  const int index) const -> float
{
  if (index < 0
    || index >= static_cast<int>(sun_shadow_cascade_distances_.size())) {
    return 0.0F;
  }
  return sun_shadow_cascade_distances_[static_cast<std::size_t>(index)];
}

auto EnvironmentSettingsService::SetSunShadowCascadeDistance(
  const int index, float value) -> void
{
  if (index < 0
    || index >= static_cast<int>(sun_shadow_cascade_distances_.size())) {
    return;
  }
  const auto idx = static_cast<std::size_t>(index);
  if (sun_shadow_cascade_distances_[idx] == value) {
    return;
  }
  sun_shadow_cascade_distances_[idx] = value;
  MarkDirty(kSunDirtyMask);
}

auto EnvironmentSettingsService::GetSunLightAvailable() const -> bool
{
  return sun_light_available_;
}

auto EnvironmentSettingsService::UpdateSunLightCandidate() -> void
{
  sun_light_available_ = false;
  sun_light_node_ = {};
  if (!config_.scene) {
    return;
  }

  const auto candidate = FindSunLightCandidate();
  if (!candidate.has_value()) {
    return;
  }

  sun_light_node_ = *candidate;
  sun_light_available_ = sun_light_node_.IsAlive();
}

auto EnvironmentSettingsService::CaptureSunShadowSettingsFromLight(
  const scene::DirectionalLight& light) -> void
{
  sun_atmosphere_light_slot_ = static_cast<int>(light.GetAtmosphereLightSlot());
  sun_use_per_pixel_atmosphere_transmittance_
    = light.GetUsePerPixelAtmosphereTransmittance();
  sun_atmosphere_disk_luminance_scale_
    = light.GetAtmosphereDiskLuminanceScale();

  const auto& shadow = light.Common().shadow;
  sun_shadow_bias_ = shadow.bias;
  sun_shadow_normal_bias_ = shadow.normal_bias;
  sun_shadow_resolution_hint_ = static_cast<int>(shadow.resolution_hint);

  const auto csm
    = scene::CanonicalizeCascadedShadowSettings(light.CascadedShadows());
  sun_shadow_cascade_count_ = static_cast<int>(csm.cascade_count);
  sun_shadow_split_mode_ = static_cast<int>(csm.split_mode);
  sun_shadow_max_distance_ = csm.max_shadow_distance;
  sun_shadow_cascade_distances_ = csm.cascade_distances;
  sun_shadow_distribution_exponent_ = csm.distribution_exponent;
  sun_shadow_transition_fraction_ = csm.transition_fraction;
  sun_shadow_distance_fadeout_fraction_ = csm.distance_fadeout_fraction;
}

auto EnvironmentSettingsService::ApplySunShadowSettingsToLight(
  scene::DirectionalLight& light) const -> void
{
  light.SetAtmosphereLightSlot(static_cast<scene::AtmosphereLightSlot>(
    std::clamp(sun_atmosphere_light_slot_,
      static_cast<int>(scene::AtmosphereLightSlot::kNone),
      static_cast<int>(scene::AtmosphereLightSlot::kSecondary))));
  light.SetUsePerPixelAtmosphereTransmittance(
    sun_use_per_pixel_atmosphere_transmittance_);
  light.SetAtmosphereDiskLuminanceScale(sun_atmosphere_disk_luminance_scale_);

  auto& shadow = light.Common().shadow;
  shadow.bias = sun_shadow_bias_;
  shadow.normal_bias = sun_shadow_normal_bias_;
  shadow.resolution_hint = static_cast<scene::ShadowResolutionHint>(
    std::clamp(sun_shadow_resolution_hint_,
      static_cast<int>(scene::ShadowResolutionHint::kLow),
      static_cast<int>(scene::ShadowResolutionHint::kUltra)));

  scene::CascadedShadowSettings csm {};
  csm.cascade_count
    = static_cast<std::uint32_t>(std::max(sun_shadow_cascade_count_, 1));
  csm.split_mode
    = static_cast<scene::DirectionalCsmSplitMode>(sun_shadow_split_mode_);
  csm.max_shadow_distance = sun_shadow_max_distance_;
  csm.cascade_distances = sun_shadow_cascade_distances_;
  csm.distribution_exponent = sun_shadow_distribution_exponent_;
  csm.transition_fraction = sun_shadow_transition_fraction_;
  csm.distance_fadeout_fraction = sun_shadow_distance_fadeout_fraction_;
  light.CascadedShadows() = scene::CanonicalizeCascadedShadowSettings(csm);
}

auto EnvironmentSettingsService::GetUseLut() const -> bool { return use_lut_; }

auto EnvironmentSettingsService::SetUseLut(bool enabled) -> void
{
  if (use_lut_ == enabled) {
    return;
  }
  use_lut_ = enabled;
  MarkDirty(ToMask(DirtyDomain::kRendererFlags));
}

auto EnvironmentSettingsService::ApplyPendingChanges() -> void
{
  if (!pending_changes_ || !config_.scene) {
    return;
  }
  if (dirty_domains_ == ToMask(DirtyDomain::kNone)) {
    pending_changes_ = false;
    return;
  }

  DLOG_F(1, "applying pending changes (mask=0x{:X}, revision={})",
    dirty_domains_, settings_revision_);
  ValidateAndClampState();
  NormalizeSkySystems();
  const bool apply_atmosphere
    = HasDirty(dirty_domains_, DirtyDomain::kAtmosphereModel);
  const bool apply_sun = HasDirty(dirty_domains_, DirtyDomain::kSun)
    || HasDirty(dirty_domains_, DirtyDomain::kAtmosphereLights);
  const bool apply_fog = HasDirty(dirty_domains_, DirtyDomain::kHeightFog)
    || HasDirty(dirty_domains_, DirtyDomain::kVolumetricFog);
  const bool apply_local_fog
    = HasDirty(dirty_domains_, DirtyDomain::kLocalFogVolumes);
  const bool apply_sky_sphere
    = HasDirty(dirty_domains_, DirtyDomain::kSkySphere);
  const bool apply_sky_light = HasDirty(dirty_domains_, DirtyDomain::kSkyLight);
  const bool apply_skybox = HasDirty(dirty_domains_, DirtyDomain::kSkybox);

  const auto cache_atmo_before = CaptureAtmosphereCanonicalState();
  const auto scene_atmo_before = CaptureSceneAtmosphereCanonicalState();
  if (apply_atmosphere) {
    DLOG_F(2, "atmosphere hash before apply: cache=0x{:X}, scene=0x{:X}",
      HashAtmosphereState(cache_atmo_before),
      scene_atmo_before.has_value() ? HashAtmosphereState(*scene_atmo_before)
                                    : 0ULL);
  }

  auto env = config_.scene->GetEnvironment();
  if (!env) {
    config_.scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    env = config_.scene->GetEnvironment();
  }

  if (apply_sun && !sun_enabled_) {
    UpdateSunLightCandidate();
    if (sun_light_available_) {
      if (auto light = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
        ApplySunShadowSettingsToLight(light->get());
      }
      if (ApplyDirectionalSunRole(sun_light_node_, false, false, true, true)) {
        LOG_F(INFO,
          "disabled scene sun candidate '{}' while sun system is disabled",
          sun_light_node_.GetName());
      }
    }

  }

  if (apply_sun && sun_enabled_) {
    const auto scene_name
      = config_.scene ? config_.scene->GetName() : std::string_view {};
    UpdateSunLightCandidate();
    if (sun_light_available_) {
      if (auto light
        = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
        CHECK_F(ApplyDirectionalSunRole(
                  sun_light_node_, sun_enabled_, true, true, true),
          "failed to apply scene sun role to directional node '{}'",
          sun_light_node_.GetName());

        ApplySunShadowSettingsToLight(light->get());
        light->get().SetIntensityLux(sun_illuminance_lx_);
        auto& common = light->get().Common();
        common.color_rgb = sun_use_temperature_
          ? KelvinToLinearRgb(sun_temperature_kelvin_)
          : sun_color_rgb_;

        const auto sun_dir = DirectionFromAzimuthElevation(
          sun_azimuth_deg_, sun_elevation_deg_);
        const glm::vec3 light_dir = -sun_dir;
        ApplyLightDirectionWorldSpace(sun_light_node_, light_dir);
      }

      LOG_F(INFO,
        "using scene directional '{}' as sun "
        "(source=scene, casts_shadows=true, environment_contribution=true)",
        sun_light_node_.GetName());
    } else {
      LOG_F(WARNING,
        "no resolved scene sun is currently available in scene '{}'",
        scene_name);
    }
  }

  auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>();
  if (apply_atmosphere && sky_atmo_enabled_ && !atmo) {
    atmo
      = observer_ptr { &env->AddSystem<scene::environment::SkyAtmosphere>() };
  }
  if (apply_atmosphere && atmo) {
    atmo->SetEnabled(sky_atmo_enabled_);
  }
  if (apply_atmosphere && sky_atmo_enabled_ && atmo) {
    const auto atmosphere_state = CaptureAtmosphereCanonicalState();
    atmo->SetTransformMode(
      static_cast<scene::environment::SkyAtmosphereTransformMode>(
        atmosphere_state.transform_mode));
    atmo->SetPlanetRadiusMeters(
      atmosphere_state.planet_radius_km * kKmToMeters);
    atmo->SetAtmosphereHeightMeters(
      atmosphere_state.atmosphere_height_km * kKmToMeters);
    atmo->SetGroundAlbedoRgb(atmosphere_state.ground_albedo);
    atmo->SetRayleighScaleHeightMeters(
      atmosphere_state.rayleigh_scale_height_km * kKmToMeters);
    atmo->SetMieScaleHeightMeters(
      atmosphere_state.mie_scale_height_km * kKmToMeters);
    atmo->SetMieAnisotropy(atmosphere_state.mie_anisotropy);
    atmo->SetMieAbsorptionRgb(atmosphere_state.mie_absorption_scale
      * engine::atmos::kDefaultMieAbsorptionRgb);
    // We now control absorption explicitly via the new parameters.
    atmo->SetOzoneAbsorptionRgb(atmosphere_state.ozone_rgb);

    // Apply the 2-layer ozone density profile as-authored (UI/settings).
    atmo->SetOzoneDensityProfile(atmosphere_state.ozone_profile);

    atmo->SetMultiScatteringFactor(atmosphere_state.multi_scattering);
    atmo->SetSkyLuminanceFactorRgb(atmosphere_state.sky_luminance_factor);
    atmo->SetSkyAndAerialPerspectiveLuminanceFactorRgb(
      atmosphere_state.sky_and_aerial_luminance_factor);
    atmo->SetSunDiskEnabled(atmosphere_state.sun_disk_enabled);
    atmo->SetAerialPerspectiveDistanceScale(
      atmosphere_state.aerial_perspective_scale);
    atmo->SetAerialPerspectiveStartDepthMeters(
      atmosphere_state.aerial_perspective_start_depth_m);
    atmo->SetAerialScatteringStrength(
      atmosphere_state.aerial_scattering_strength);
    atmo->SetHeightFogContribution(atmosphere_state.height_fog_contribution);
    atmo->SetTraceSampleCountScale(atmosphere_state.trace_sample_count_scale);
    atmo->SetTransmittanceMinLightElevationDeg(
      atmosphere_state.transmittance_min_light_elevation_deg);
    atmo->SetHoldout(atmosphere_state.holdout);
    atmo->SetRenderInMainPass(atmosphere_state.render_in_main_pass);

    if (config_.on_atmosphere_params_changed) {
      config_.on_atmosphere_params_changed();
      LOG_F(1, "atmosphere parameters changed (SunDiskEnabled={})",
        sun_disk_enabled_);
    }
  }

  auto fog = env->TryGetSystem<scene::environment::Fog>();
  const bool volumetric_enabled
    = fog_model_ == static_cast<int>(scene::environment::FogModel::kVolumetric);
  const bool any_fog_enabled = fog_enabled_ || volumetric_enabled;
  if (apply_fog && any_fog_enabled && !fog) {
    fog = observer_ptr { &env->AddSystem<scene::environment::Fog>() };
  }
  if (apply_fog && fog) {
    fog->SetEnabled(any_fog_enabled);
  }
  if (apply_fog && any_fog_enabled && fog) {
    fog->SetModel(static_cast<scene::environment::FogModel>(fog_model_));
    fog->SetEnableHeightFog(fog_enabled_);
    fog->SetExtinctionSigmaTPerMeter(fog_extinction_sigma_t_per_m_);
    fog->SetHeightFalloffPerMeter(fog_height_falloff_per_m_);
    fog->SetHeightOffsetMeters(fog_height_offset_m_);
    fog->SetStartDistanceMeters(fog_start_distance_m_);
    fog->SetSecondFogDensity(second_fog_density_);
    fog->SetSecondFogHeightFalloff(second_fog_height_falloff_);
    fog->SetSecondFogHeightOffset(second_fog_height_offset_);
    fog->SetMaxOpacity(fog_max_opacity_);
    fog->SetSingleScatteringAlbedoRgb(fog_single_scattering_albedo_rgb_);
    fog->SetFogInscatteringLuminance(fog_inscattering_luminance_);
    fog->SetSkyAtmosphereAmbientContributionColorScale(
      sky_atmosphere_ambient_contribution_color_scale_);
    fog->SetInscatteringColorCubemapAngle(inscattering_color_cubemap_angle_);
    fog->SetInscatteringTextureTint(inscattering_texture_tint_);
    fog->SetFullyDirectionalInscatteringColorDistance(
      fully_directional_inscattering_color_distance_);
    fog->SetNonDirectionalInscatteringColorDistance(
      non_directional_inscattering_color_distance_);
    fog->SetDirectionalInscatteringLuminance(
      directional_inscattering_luminance_);
    fog->SetDirectionalInscatteringExponent(directional_inscattering_exponent_);
    fog->SetDirectionalInscatteringStartDistance(
      directional_inscattering_start_distance_);
    fog->SetEndDistanceMeters(fog_end_distance_m_);
    fog->SetFogCutoffDistanceMeters(fog_cutoff_distance_m_);
    fog->SetVolumetricFogScatteringDistribution(
      volumetric_fog_scattering_distribution_);
    fog->SetVolumetricFogAlbedo(volumetric_fog_albedo_);
    fog->SetVolumetricFogEmissive(volumetric_fog_emissive_);
    fog->SetVolumetricFogExtinctionScale(volumetric_fog_extinction_scale_);
    fog->SetVolumetricFogDistance(volumetric_fog_distance_m_);
    fog->SetVolumetricFogStartDistance(volumetric_fog_start_distance_m_);
    fog->SetVolumetricFogNearFadeInDistance(
      volumetric_fog_near_fade_in_distance_m_);
    fog->SetVolumetricFogStaticLightingScatteringIntensity(
      volumetric_fog_static_lighting_scattering_intensity_);
    fog->SetOverrideLightColorsWithFogInscatteringColors(
      override_light_colors_with_fog_inscattering_colors_);
    fog->SetHoldout(fog_holdout_);
    fog->SetRenderInMainPass(fog_render_in_main_pass_);
    fog->SetVisibleInReflectionCaptures(fog_visible_in_reflection_captures_);
    fog->SetVisibleInRealTimeSkyCaptures(
      fog_visible_in_real_time_sky_captures_);
  }

  if (apply_local_fog && config_.scene) {
    for (const auto& removed_handle : removed_local_fog_nodes_) {
      if (auto* node_impl = config_.scene->TryGetNodeImpl(removed_handle)) {
        if (node_impl->HasComponent<scene::environment::LocalFogVolume>()) {
          node_impl->RemoveComponent<scene::environment::LocalFogVolume>();
        }
      }
    }
    removed_local_fog_nodes_.clear();

    for (auto& entry : local_fog_volumes_) {
      if (!entry.node.IsAlive()) {
        continue;
      }
      const auto impl_opt = entry.node.GetImpl();
      if (!impl_opt.has_value()) {
        continue;
      }

      auto& node_impl = impl_opt->get();
      if (!node_impl.HasComponent<scene::environment::LocalFogVolume>()) {
        node_impl.AddComponent<scene::environment::LocalFogVolume>();
      }

      auto& local_fog
        = node_impl.GetComponent<scene::environment::LocalFogVolume>();
      local_fog.SetEnabled(entry.enabled);
      local_fog.SetRadialFogExtinction(entry.radial_fog_extinction);
      local_fog.SetHeightFogExtinction(entry.height_fog_extinction);
      local_fog.SetHeightFogFalloff(entry.height_fog_falloff);
      local_fog.SetHeightFogOffset(entry.height_fog_offset);
      local_fog.SetFogPhaseG(entry.fog_phase_g);
      local_fog.SetFogAlbedo(entry.fog_albedo);
      local_fog.SetFogEmissive(entry.fog_emissive);
      local_fog.SetSortPriority(entry.sort_priority);
    }
  }

  auto sky = env->TryGetSystem<scene::environment::SkySphere>();
  if (apply_sky_sphere && sky_sphere_enabled_ && !sky) {
    sky = observer_ptr { &env->AddSystem<scene::environment::SkySphere>() };
  }
  if (apply_sky_sphere && sky) {
    sky->SetEnabled(sky_sphere_enabled_);
  }
  if (apply_sky_sphere && sky_sphere_enabled_ && sky) {
    sky->SetSource(
      static_cast<scene::environment::SkySphereSource>(sky_sphere_source_));
    sky->SetSolidColorRgb(sky_sphere_solid_color_);
    sky->SetIntensity(sky_intensity_);
    sky->SetRotationRadians(sky_sphere_rotation_deg_ * kDegToRad);
  }

  auto light = env->TryGetSystem<scene::environment::SkyLight>();
  if (apply_sky_light && sky_light_enabled_ && !light) {
    light = observer_ptr { &env->AddSystem<scene::environment::SkyLight>() };
  }
  if (apply_sky_light && light) {
    light->SetEnabled(sky_light_enabled_);
  }
  if (apply_sky_light && sky_light_enabled_ && light) {
    light->SetSource(
      static_cast<scene::environment::SkyLightSource>(sky_light_source_));
    light->SetTintRgb(sky_light_tint_);
    light->SetIntensityMul(sky_light_intensity_mul_);
    light->SetDiffuseIntensity(sky_light_diffuse_);
    light->SetSpecularIntensity(sky_light_specular_);
    light->SetRealTimeCaptureEnabled(sky_light_real_time_capture_enabled_);
    light->SetLowerHemisphereColor(sky_light_lower_hemisphere_color_);
    light->SetVolumetricScatteringIntensity(
      sky_light_volumetric_scattering_intensity_);
    light->SetAffectReflections(sky_light_affect_reflections_);
    light->SetAffectGlobalIllumination(sky_light_affect_global_illumination_);
  }

  if (apply_skybox || apply_sky_sphere) {
    MaybeAutoLoadSkybox();
  }

  config_.scene->Update(false);

  settings_persist_dirty_ = true;
  applied_changes_this_frame_ = true;
  pending_changes_ = false;
  dirty_domains_ = ToMask(DirtyDomain::kNone);
  epoch_++;
}

auto EnvironmentSettingsService::SyncFromScene() -> void
{
  if (!config_.scene) {
    return;
  }
  const auto cache_atmo_before = CaptureAtmosphereCanonicalState();

  auto env = config_.scene->GetEnvironment();
  if (!env) {
    pending_changes_ = false;
    dirty_domains_ = ToMask(DirtyDomain::kNone);
    return;
  }

  if (const auto atmosphere_from_scene = CaptureSceneAtmosphereCanonicalState();
    atmosphere_from_scene.has_value()) {
    const auto& atmo_state = *atmosphere_from_scene;
    sky_atmo_enabled_ = atmo_state.enabled;
    sky_atmo_transform_mode_ = atmo_state.transform_mode;
    planet_radius_km_ = atmo_state.planet_radius_km;
    atmosphere_height_km_ = atmo_state.atmosphere_height_km;
    ground_albedo_ = atmo_state.ground_albedo;
    rayleigh_scale_height_km_ = atmo_state.rayleigh_scale_height_km;
    mie_scale_height_km_ = atmo_state.mie_scale_height_km;
    mie_anisotropy_ = atmo_state.mie_anisotropy;
    mie_absorption_scale_
      = std::clamp(atmo_state.mie_absorption_scale, 0.0F, 5.0F);
    multi_scattering_ = atmo_state.multi_scattering;
    sky_luminance_factor_ = atmo_state.sky_luminance_factor;
    sky_and_aerial_perspective_luminance_factor_
      = atmo_state.sky_and_aerial_luminance_factor;
    sun_disk_enabled_ = atmo_state.sun_disk_enabled;
    aerial_perspective_scale_ = atmo_state.aerial_perspective_scale;
    aerial_perspective_start_depth_m_
      = atmo_state.aerial_perspective_start_depth_m;
    aerial_scattering_strength_ = atmo_state.aerial_scattering_strength;
    height_fog_contribution_ = atmo_state.height_fog_contribution;
    trace_sample_count_scale_ = atmo_state.trace_sample_count_scale;
    transmittance_min_light_elevation_deg_
      = atmo_state.transmittance_min_light_elevation_deg;
    atmosphere_holdout_ = atmo_state.holdout;
    atmosphere_render_in_main_pass_ = atmo_state.render_in_main_pass;
    ozone_rgb_ = atmo_state.ozone_rgb;
    ozone_profile_ = atmo_state.ozone_profile;
  } else {
    sky_atmo_enabled_ = false;
  }

  if (auto fog = env->TryGetSystem<scene::environment::Fog>()) {
    fog_enabled_ = fog->GetEnableHeightFog();
    fog_model_ = static_cast<int>(fog->GetModel());
    fog_extinction_sigma_t_per_m_ = fog->GetExtinctionSigmaTPerMeter();
    fog_height_falloff_per_m_ = fog->GetHeightFalloffPerMeter();
    fog_height_offset_m_ = fog->GetHeightOffsetMeters();
    fog_start_distance_m_ = fog->GetStartDistanceMeters();
    second_fog_density_ = fog->GetSecondFogDensity();
    second_fog_height_falloff_ = fog->GetSecondFogHeightFalloff();
    second_fog_height_offset_ = fog->GetSecondFogHeightOffset();
    fog_max_opacity_ = fog->GetMaxOpacity();
    fog_single_scattering_albedo_rgb_ = fog->GetSingleScatteringAlbedoRgb();
    fog_inscattering_luminance_ = fog->GetFogInscatteringLuminance();
    sky_atmosphere_ambient_contribution_color_scale_
      = fog->GetSkyAtmosphereAmbientContributionColorScale();
    inscattering_color_cubemap_angle_ = fog->GetInscatteringColorCubemapAngle();
    inscattering_texture_tint_ = fog->GetInscatteringTextureTint();
    fully_directional_inscattering_color_distance_
      = fog->GetFullyDirectionalInscatteringColorDistance();
    non_directional_inscattering_color_distance_
      = fog->GetNonDirectionalInscatteringColorDistance();
    directional_inscattering_luminance_
      = fog->GetDirectionalInscatteringLuminance();
    directional_inscattering_exponent_
      = fog->GetDirectionalInscatteringExponent();
    directional_inscattering_start_distance_
      = fog->GetDirectionalInscatteringStartDistance();
    fog_end_distance_m_ = fog->GetEndDistanceMeters();
    fog_cutoff_distance_m_ = fog->GetFogCutoffDistanceMeters();
    volumetric_fog_scattering_distribution_
      = fog->GetVolumetricFogScatteringDistribution();
    volumetric_fog_albedo_ = fog->GetVolumetricFogAlbedo();
    volumetric_fog_emissive_ = fog->GetVolumetricFogEmissive();
    volumetric_fog_extinction_scale_ = fog->GetVolumetricFogExtinctionScale();
    volumetric_fog_distance_m_ = fog->GetVolumetricFogDistance();
    volumetric_fog_start_distance_m_ = fog->GetVolumetricFogStartDistance();
    volumetric_fog_near_fade_in_distance_m_
      = fog->GetVolumetricFogNearFadeInDistance();
    volumetric_fog_static_lighting_scattering_intensity_
      = fog->GetVolumetricFogStaticLightingScatteringIntensity();
    override_light_colors_with_fog_inscattering_colors_
      = fog->GetOverrideLightColorsWithFogInscatteringColors();
    fog_holdout_ = fog->GetHoldout();
    fog_render_in_main_pass_ = fog->GetRenderInMainPass();
    fog_visible_in_reflection_captures_ = fog->GetVisibleInReflectionCaptures();
    fog_visible_in_real_time_sky_captures_
      = fog->GetVisibleInRealTimeSkyCaptures();
  } else {
    fog_enabled_ = false;
  }

  SyncLocalFogVolumesFromScene();

  // DemoShell currently owns the user-facing slice/mapping controls; keep the
  // persisted values as the source of truth until the Vortex runtime surfaces
  // per-view LUT parameter introspection directly.

  if (auto sky = env->TryGetSystem<scene::environment::SkySphere>()) {
    sky_sphere_enabled_ = sky->IsEnabled();
    sky_sphere_source_ = static_cast<int>(sky->GetSource());
    sky_sphere_solid_color_ = sky->GetSolidColorRgb();
    sky_intensity_ = sky->GetIntensity();
    sky_sphere_rotation_deg_ = sky->GetRotationRadians() * kRadToDeg;
  } else {
    sky_sphere_enabled_ = false;
  }

  if (auto light = env->TryGetSystem<scene::environment::SkyLight>()) {
    sky_light_enabled_ = light->IsEnabled();
    sky_light_source_ = static_cast<int>(light->GetSource());
    sky_light_tint_ = light->GetTintRgb();
    sky_light_intensity_mul_ = light->GetIntensityMul();
    sky_light_diffuse_ = light->GetDiffuseIntensity();
    sky_light_specular_ = light->GetSpecularIntensity();
    sky_light_real_time_capture_enabled_ = light->GetRealTimeCaptureEnabled();
    sky_light_lower_hemisphere_color_ = light->GetLowerHemisphereColor();
    sky_light_volumetric_scattering_intensity_
      = light->GetVolumetricScatteringIntensity();
    sky_light_affect_reflections_ = light->GetAffectReflections();
    sky_light_affect_global_illumination_
      = light->GetAffectGlobalIllumination();
  } else {
    sky_light_enabled_ = false;
  }

  UpdateSunLightCandidate();
  if (sun_light_available_) {
    if (auto light = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
      sun_enabled_ = light->get().Common().affects_world;
      sun_color_rgb_ = light->get().Common().color_rgb;
      sun_illuminance_lx_ = light->get().GetIntensityLux();
      sun_component_disk_radius_deg_
        = light->get().GetAngularSizeRadians() * kRadToDeg;
      sun_use_temperature_ = false;
      const auto& resolver = config_.scene->GetDirectionalLightResolver();
      if (const auto primary = resolver.ResolvePrimarySun();
        primary.has_value()
        && primary->NodeHandle() == sun_light_node_.GetHandle()) {
        const auto direction_to_light_ws = primary->DirectionToLightWs();
        sun_azimuth_deg_
          = std::atan2(direction_to_light_ws.y, direction_to_light_ws.x)
          * kRadToDeg;
        if (sun_azimuth_deg_ < 0.0F) {
          sun_azimuth_deg_ += 360.0F;
        }
        sun_elevation_deg_
          = std::asin(std::clamp(direction_to_light_ws.z, -1.0F, 1.0F))
          * kRadToDeg;
      }
      CaptureSunShadowSettingsFromLight(light->get());
    }
  } else {
    sun_light_available_ = false;
  }

  ValidateAndClampState();
  NormalizeSkySystems();
  pending_changes_ = false;
  dirty_domains_ = ToMask(DirtyDomain::kNone);
  epoch_++;
}

auto EnvironmentSettingsService::SyncLocalFogVolumesFromScene() -> void
{
  local_fog_volumes_.clear();
  removed_local_fog_nodes_.clear();
  if (!config_.scene) {
    selected_local_fog_volume_index_ = -1;
    return;
  }

  for (auto& node : CollectLocalFogVolumeNodes(*config_.scene)) {
    const auto impl_opt = node.GetImpl();
    if (!impl_opt.has_value()) {
      continue;
    }
    const auto& local_fog
      = impl_opt->get().GetComponent<scene::environment::LocalFogVolume>();
    local_fog_volumes_.push_back(LocalFogVolumeUiState {
      .node = node,
      .enabled = local_fog.IsEnabled(),
      .radial_fog_extinction = local_fog.GetRadialFogExtinction(),
      .height_fog_extinction = local_fog.GetHeightFogExtinction(),
      .height_fog_falloff = local_fog.GetHeightFogFalloff(),
      .height_fog_offset = local_fog.GetHeightFogOffset(),
      .fog_phase_g = local_fog.GetFogPhaseG(),
      .fog_albedo = local_fog.GetFogAlbedo(),
      .fog_emissive = local_fog.GetFogEmissive(),
      .sort_priority = local_fog.GetSortPriority(),
    });
  }

  if (local_fog_volumes_.empty()) {
    selected_local_fog_volume_index_ = -1;
  } else {
    selected_local_fog_volume_index_
      = std::clamp(selected_local_fog_volume_index_, 0,
        static_cast<int>(local_fog_volumes_.size()) - 1);
  }
}

auto EnvironmentSettingsService::NormalizeSkySystems() -> void
{
  if (sky_atmo_enabled_ && sky_sphere_enabled_) {
    sky_sphere_enabled_ = false;
  }
}

auto EnvironmentSettingsService::MaybeAutoLoadSkybox() -> void
{
  if (!config_.skybox_service) {
    return;
  }
  if (!sky_sphere_enabled_ || sky_sphere_source_ != 0) {
    return;
  }
  if (skybox_path_.empty()) {
    return;
  }

  const bool settings_changed = last_loaded_skybox_path_ != skybox_path_
    || last_loaded_skybox_layout_idx_ != skybox_layout_idx_
    || last_loaded_skybox_output_format_idx_ != skybox_output_format_idx_
    || last_loaded_skybox_face_size_ != skybox_face_size_
    || last_loaded_skybox_flip_y_ != skybox_flip_y_
    || last_loaded_skybox_tonemap_hdr_to_ldr_ != skybox_tonemap_hdr_to_ldr_
    || last_loaded_skybox_hdr_exposure_ev_ != skybox_hdr_exposure_ev_;
  const bool needs_load = skybox_dirty_ || settings_changed
    || skybox_last_resource_key_.IsPlaceholder();
  if (!needs_load) {
    return;
  }

  LoadSkybox(skybox_path_, skybox_layout_idx_, skybox_output_format_idx_,
    skybox_face_size_, skybox_flip_y_, skybox_tonemap_hdr_to_ldr_,
    skybox_hdr_exposure_ev_);
}

auto EnvironmentSettingsService::CaptureAtmosphereCanonicalState() const
  -> AtmosphereCanonicalState
{
  AtmosphereCanonicalState state {};
  state.enabled = sky_atmo_enabled_;
  state.transform_mode = sky_atmo_transform_mode_;
  state.planet_radius_km = planet_radius_km_;
  state.atmosphere_height_km = atmosphere_height_km_;
  state.ground_albedo = ground_albedo_;
  state.rayleigh_scale_height_km = rayleigh_scale_height_km_;
  state.mie_scale_height_km = mie_scale_height_km_;
  state.mie_anisotropy = mie_anisotropy_;
  state.mie_absorption_scale = mie_absorption_scale_;
  state.multi_scattering = multi_scattering_;
  state.sky_luminance_factor = sky_luminance_factor_;
  state.sky_and_aerial_luminance_factor
    = sky_and_aerial_perspective_luminance_factor_;
  state.ozone_rgb = ozone_rgb_;
  state.ozone_profile = ozone_profile_;
  state.sun_disk_enabled = sun_disk_enabled_;
  state.aerial_perspective_scale = aerial_perspective_scale_;
  state.aerial_perspective_start_depth_m = aerial_perspective_start_depth_m_;
  state.aerial_scattering_strength = aerial_scattering_strength_;
  state.height_fog_contribution = height_fog_contribution_;
  state.trace_sample_count_scale = trace_sample_count_scale_;
  state.transmittance_min_light_elevation_deg
    = transmittance_min_light_elevation_deg_;
  state.holdout = atmosphere_holdout_;
  state.render_in_main_pass = atmosphere_render_in_main_pass_;
  return state;
}

auto EnvironmentSettingsService::CaptureSceneAtmosphereCanonicalState() const
  -> std::optional<AtmosphereCanonicalState>
{
  if (!config_.scene) {
    return std::nullopt;
  }
  auto env = config_.scene->GetEnvironment();
  if (!env) {
    return std::nullopt;
  }
  auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>();
  if (!atmo) {
    return std::nullopt;
  }

  AtmosphereCanonicalState state {};
  state.enabled = atmo->IsEnabled();
  state.transform_mode = static_cast<int>(atmo->GetTransformMode());
  state.planet_radius_km = atmo->GetPlanetRadiusMeters() * kMetersToKm;
  state.atmosphere_height_km = atmo->GetAtmosphereHeightMeters() * kMetersToKm;
  state.ground_albedo = atmo->GetGroundAlbedoRgb();
  state.rayleigh_scale_height_km
    = atmo->GetRayleighScaleHeightMeters() * kMetersToKm;
  state.mie_scale_height_km = atmo->GetMieScaleHeightMeters() * kMetersToKm;
  state.mie_anisotropy = atmo->GetMieAnisotropy();
  const auto absorption = atmo->GetMieAbsorptionRgb();
  const auto base_absorption = engine::atmos::kDefaultMieAbsorptionRgb;
  const float base_avg
    = (base_absorption.x + base_absorption.y + base_absorption.z) / 3.0F;
  state.mie_absorption_scale = (base_avg > 0.0F)
    ? (absorption.x + absorption.y + absorption.z) / (3.0F * base_avg)
    : 0.0F;
  state.multi_scattering = atmo->GetMultiScatteringFactor();
  state.sky_luminance_factor = atmo->GetSkyLuminanceFactorRgb();
  state.sky_and_aerial_luminance_factor
    = atmo->GetSkyAndAerialPerspectiveLuminanceFactorRgb();
  state.ozone_rgb = atmo->GetAbsorptionRgb();
  state.ozone_profile = atmo->GetOzoneDensityProfile();
  state.sun_disk_enabled = atmo->GetSunDiskEnabled();
  state.aerial_perspective_scale = atmo->GetAerialPerspectiveDistanceScale();
  state.aerial_perspective_start_depth_m
    = atmo->GetAerialPerspectiveStartDepthMeters();
  state.aerial_scattering_strength = atmo->GetAerialScatteringStrength();
  state.height_fog_contribution = atmo->GetHeightFogContribution();
  state.trace_sample_count_scale = atmo->GetTraceSampleCountScale();
  state.transmittance_min_light_elevation_deg
    = atmo->GetTransmittanceMinLightElevationDeg();
  state.holdout = atmo->GetHoldout();
  state.render_in_main_pass = atmo->GetRenderInMainPass();
  return state;
}

auto EnvironmentSettingsService::HashAtmosphereState(
  const AtmosphereCanonicalState& state) -> std::uint64_t
{
  std::uint64_t seed = 1469598103934665603ULL;
  seed = HashCombineU64(seed, static_cast<std::uint64_t>(state.enabled));
  seed = HashCombineU64(seed, FloatBits(state.planet_radius_km));
  seed = HashCombineU64(seed, FloatBits(state.atmosphere_height_km));
  seed = HashCombineU64(seed, FloatBits(state.ground_albedo.x));
  seed = HashCombineU64(seed, FloatBits(state.ground_albedo.y));
  seed = HashCombineU64(seed, FloatBits(state.ground_albedo.z));
  seed = HashCombineU64(seed, FloatBits(state.rayleigh_scale_height_km));
  seed = HashCombineU64(seed, FloatBits(state.mie_scale_height_km));
  seed = HashCombineU64(seed, FloatBits(state.mie_anisotropy));
  seed = HashCombineU64(seed, FloatBits(state.mie_absorption_scale));
  seed = HashCombineU64(seed, FloatBits(state.multi_scattering));
  seed = HashCombineU64(seed, FloatBits(state.sky_luminance_factor.x));
  seed = HashCombineU64(seed, FloatBits(state.sky_luminance_factor.y));
  seed = HashCombineU64(seed, FloatBits(state.sky_luminance_factor.z));
  seed
    = HashCombineU64(seed, FloatBits(state.sky_and_aerial_luminance_factor.x));
  seed
    = HashCombineU64(seed, FloatBits(state.sky_and_aerial_luminance_factor.y));
  seed
    = HashCombineU64(seed, FloatBits(state.sky_and_aerial_luminance_factor.z));
  seed = HashCombineU64(seed, FloatBits(state.ozone_rgb.x));
  seed = HashCombineU64(seed, FloatBits(state.ozone_rgb.y));
  seed = HashCombineU64(seed, FloatBits(state.ozone_rgb.z));
  seed = HashCombineU64(seed, FloatBits(state.ozone_profile.layers[0].width_m));
  seed
    = HashCombineU64(seed, FloatBits(state.ozone_profile.layers[0].exp_term));
  seed = HashCombineU64(
    seed, FloatBits(state.ozone_profile.layers[0].linear_term));
  seed = HashCombineU64(
    seed, FloatBits(state.ozone_profile.layers[0].constant_term));
  seed = HashCombineU64(seed, FloatBits(state.ozone_profile.layers[1].width_m));
  seed
    = HashCombineU64(seed, FloatBits(state.ozone_profile.layers[1].exp_term));
  seed = HashCombineU64(
    seed, FloatBits(state.ozone_profile.layers[1].linear_term));
  seed = HashCombineU64(
    seed, FloatBits(state.ozone_profile.layers[1].constant_term));
  seed
    = HashCombineU64(seed, static_cast<std::uint64_t>(state.sun_disk_enabled));
  seed = HashCombineU64(seed, FloatBits(state.aerial_perspective_scale));
  seed
    = HashCombineU64(seed, FloatBits(state.aerial_perspective_start_depth_m));
  seed = HashCombineU64(seed, FloatBits(state.aerial_scattering_strength));
  seed = HashCombineU64(seed, FloatBits(state.height_fog_contribution));
  seed = HashCombineU64(seed, FloatBits(state.trace_sample_count_scale));
  seed = HashCombineU64(
    seed, FloatBits(state.transmittance_min_light_elevation_deg));
  seed = HashCombineU64(seed, static_cast<std::uint64_t>(state.holdout));
  seed = HashCombineU64(
    seed, static_cast<std::uint64_t>(state.render_in_main_pass));
  return seed;
}

auto EnvironmentSettingsService::ValidateAndClampState() -> void
{
  auto clamp_float = [](float& value, float min_v, float max_v) {
    value = std::clamp(value, min_v, max_v);
  };
  auto clamp_int = [](int& value, int min_v, int max_v) {
    value = std::clamp(value, min_v, max_v);
  };
  auto clamp_vec3 = [](glm::vec3& value, float min_v, float max_v) {
    value = ClampVec3(value, min_v, max_v);
  };
  auto clamp_vec3_min = [](glm::vec3& value, float min_v) {
    value = glm::max(value, glm::vec3(min_v));
  };

  clamp_float(planet_radius_km_, 1.0F, 100000.0F);
  clamp_float(atmosphere_height_km_, 0.1F, 1000.0F);
  clamp_int(sky_atmo_transform_mode_, 0, 2);
  clamp_vec3(ground_albedo_, 0.0F, 1.0F);
  clamp_float(rayleigh_scale_height_km_, 0.01F, 100.0F);
  clamp_float(mie_scale_height_km_, 0.01F, 50.0F);
  clamp_float(mie_anisotropy_, 0.0F, 0.999F);
  clamp_float(mie_absorption_scale_, 0.0F, 5.0F);
  clamp_float(multi_scattering_, 0.0F, 5.0F);
  clamp_vec3_min(sky_luminance_factor_, 0.0F);
  clamp_vec3_min(sky_and_aerial_perspective_luminance_factor_, 0.0F);
  clamp_vec3_min(ozone_rgb_, 0.0F);
  clamp_float(aerial_perspective_scale_, 0.0F, 16.0F);
  clamp_float(aerial_perspective_start_depth_m_, 1.0F, 1000000.0F);
  clamp_float(aerial_scattering_strength_, 0.0F, 16.0F);
  clamp_float(height_fog_contribution_, 0.0F, 16.0F);
  clamp_float(trace_sample_count_scale_, 0.25F, 8.0F);
  clamp_float(transmittance_min_light_elevation_deg_, -90.0F, 90.0F);

  clamp_float(ozone_profile_.layers[0].width_m, 0.0F, 200000.0F);
  clamp_float(ozone_profile_.layers[0].linear_term, -1.0F, 1.0F);
  clamp_float(ozone_profile_.layers[0].constant_term, -1.0F, 1.0F);
  ozone_profile_.layers[0].exp_term = 0.0F;
  ozone_profile_.layers[1].width_m = 0.0F;
  ozone_profile_.layers[1].exp_term = 0.0F;
  clamp_float(ozone_profile_.layers[1].linear_term, -1.0F, 1.0F);
  // For the canonical two-layer ozone profile, this term is commonly > 1
  // (Earth defaults to ~2.6667), so [-1, 1] causes false clamping.
  clamp_float(ozone_profile_.layers[1].constant_term, -1.0F, 8.0F);

  clamp_int(sky_view_lut_slices_, 1, 128);
  clamp_int(sky_view_alt_mapping_mode_, 0, 1);

  clamp_int(sky_sphere_source_, 0, 1);
  clamp_vec3_min(sky_sphere_solid_color_, 0.0F);
  clamp_float(sky_intensity_, 0.0F, 1000.0F);
  clamp_float(sky_sphere_rotation_deg_, -3600.0F, 3600.0F);

  clamp_int(skybox_layout_idx_, 0, 4);
  clamp_int(skybox_output_format_idx_, 0, 3);
  clamp_int(skybox_face_size_, 16, 4096);
  clamp_float(skybox_hdr_exposure_ev_, 0.0F, 24.0F);

  clamp_int(sky_light_source_, 0, 1);
  clamp_vec3_min(sky_light_tint_, 0.0F);
  clamp_float(sky_light_intensity_mul_, 0.0F, 100.0F);
  clamp_float(sky_light_diffuse_, 0.0F, 100.0F);
  clamp_float(sky_light_specular_, 0.0F, 100.0F);
  clamp_vec3_min(sky_light_lower_hemisphere_color_, 0.0F);
  clamp_float(sky_light_volumetric_scattering_intensity_, 0.0F, 100.0F);

  clamp_int(fog_model_, 0, 1);
  clamp_float(fog_extinction_sigma_t_per_m_, 0.0F, 10.0F);
  clamp_float(fog_height_falloff_per_m_, 0.0F, 10.0F);
  clamp_float(fog_height_offset_m_, -100000.0F, 100000.0F);
  clamp_float(fog_start_distance_m_, 0.0F, 1000000.0F);
  clamp_float(second_fog_density_, 0.0F, 10.0F);
  clamp_float(second_fog_height_falloff_, 0.0F, 10.0F);
  clamp_float(second_fog_height_offset_, -100000.0F, 100000.0F);
  clamp_float(fog_max_opacity_, 0.0F, 1.0F);
  clamp_vec3(fog_single_scattering_albedo_rgb_, 0.0F, 1.0F);
  clamp_vec3_min(fog_inscattering_luminance_, 0.0F);
  clamp_vec3_min(sky_atmosphere_ambient_contribution_color_scale_, 0.0F);
  clamp_float(inscattering_color_cubemap_angle_, -3600.0F, 3600.0F);
  clamp_vec3_min(inscattering_texture_tint_, 0.0F);
  clamp_float(fully_directional_inscattering_color_distance_, 0.0F, 1000000.0F);
  clamp_float(non_directional_inscattering_color_distance_, 0.0F, 1000000.0F);
  clamp_vec3_min(directional_inscattering_luminance_, 0.0F);
  clamp_float(directional_inscattering_exponent_, 0.0F, 128.0F);
  clamp_float(directional_inscattering_start_distance_, 0.0F, 1000000.0F);
  clamp_float(fog_end_distance_m_, 0.0F, 1000000.0F);
  clamp_float(fog_cutoff_distance_m_, 0.0F, 1000000.0F);
  clamp_float(volumetric_fog_scattering_distribution_, -0.9F, 0.9F);
  clamp_vec3(volumetric_fog_albedo_, 0.0F, 1.0F);
  clamp_vec3_min(volumetric_fog_emissive_, 0.0F);
  clamp_float(volumetric_fog_extinction_scale_, 0.0F, 100.0F);
  clamp_float(volumetric_fog_distance_m_, 0.0F, 1000000.0F);
  clamp_float(volumetric_fog_start_distance_m_, 0.0F, 1000000.0F);
  clamp_float(volumetric_fog_near_fade_in_distance_m_, 0.0F, 1000000.0F);
  clamp_float(
    volumetric_fog_static_lighting_scattering_intensity_, 0.0F, 100.0F);

  clamp_float(sun_azimuth_deg_, -720.0F, 720.0F);
  clamp_float(sun_elevation_deg_, -90.0F, 90.0F);
  clamp_vec3_min(sun_color_rgb_, 0.0F);
  clamp_float(sun_illuminance_lx_, 0.0F, 250000.0F);
  clamp_float(sun_temperature_kelvin_, 1000.0F, 40000.0F);
  clamp_float(sun_component_disk_radius_deg_, 0.01F, 2.0F);
  clamp_int(sun_atmosphere_light_slot_,
    static_cast<int>(scene::AtmosphereLightSlot::kNone),
    static_cast<int>(scene::AtmosphereLightSlot::kSecondary));
  clamp_vec3_min(sun_atmosphere_disk_luminance_scale_, 0.0F);
  clamp_float(sun_shadow_bias_, 0.0F, 10.0F);
  clamp_float(sun_shadow_normal_bias_, 0.0F, 10.0F);
  clamp_int(sun_shadow_resolution_hint_,
    static_cast<int>(scene::ShadowResolutionHint::kLow),
    static_cast<int>(scene::ShadowResolutionHint::kUltra));

  scene::CascadedShadowSettings csm {};
  csm.cascade_count
    = static_cast<std::uint32_t>(std::max(sun_shadow_cascade_count_, 1));
  csm.split_mode
    = static_cast<scene::DirectionalCsmSplitMode>(sun_shadow_split_mode_);
  csm.max_shadow_distance = sun_shadow_max_distance_;
  csm.cascade_distances = sun_shadow_cascade_distances_;
  csm.distribution_exponent = sun_shadow_distribution_exponent_;
  csm.transition_fraction = sun_shadow_transition_fraction_;
  csm.distance_fadeout_fraction = sun_shadow_distance_fadeout_fraction_;
  csm = scene::CanonicalizeCascadedShadowSettings(csm);
  sun_shadow_cascade_count_ = static_cast<int>(csm.cascade_count);
  sun_shadow_split_mode_ = static_cast<int>(csm.split_mode);
  sun_shadow_max_distance_ = csm.max_shadow_distance;
  sun_shadow_cascade_distances_ = csm.cascade_distances;
  sun_shadow_distribution_exponent_ = csm.distribution_exponent;
  sun_shadow_transition_fraction_ = csm.transition_fraction;
  sun_shadow_distance_fadeout_fraction_ = csm.distance_fadeout_fraction;

  clamp_int(preset_index_, -2, 64);
}

auto EnvironmentSettingsService::PersistSettingsIfDirty() -> void
{
  if (!settings_persist_dirty_) {
    return;
  }
  if (settings_revision_ == last_persisted_settings_revision_) {
    settings_persist_dirty_ = false;
    return;
  }

  SaveSettings();
  last_persisted_settings_revision_ = settings_revision_;
  settings_persist_dirty_ = false;
}

auto EnvironmentSettingsService::LoadSettings() -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);
  const float loaded_schema_version
    = settings->GetFloat(kEnvironmentSettingsSchemaVersionKey).value_or(1.0F);

  auto load_bool = [&](std::string_view key, bool& out) -> bool {
    if (const auto value = settings->GetBool(key)) {
      out = *value;
      return true;
    }
    return false;
  };
  auto load_float = [&](std::string_view key, float& out) -> bool {
    if (const auto value = settings->GetFloat(key)) {
      out = *value;
      return true;
    }
    return false;
  };
  auto load_vec3 = [&](std::string_view prefix, glm::vec3& out) -> bool {
    bool loaded = false;
    std::string key(prefix);
    key += ".x";
    loaded |= load_float(key, out.x);
    key.resize(prefix.size());
    key += ".y";
    loaded |= load_float(key, out.y);
    key.resize(prefix.size());
    key += ".z";
    loaded |= load_float(key, out.z);
    return loaded;
  };
  auto load_int = [&](std::string_view key, int& out) -> bool {
    float value = 0.0F;
    if (load_float(key, value)) {
      if (!std::isfinite(value)) {
        return false;
      }
      value = std::round(value);
      value
        = std::clamp(value, static_cast<float>(std::numeric_limits<int>::min()),
          static_cast<float>(std::numeric_limits<int>::max()));
      out = static_cast<int>(value);
      return true;
    }
    return false;
  };
  auto load_int_with_legacy
    = [&](std::string_view primary_key, std::string_view legacy_key, int& out) {
        return load_int(primary_key, out) || load_int(legacy_key, out);
      };
  auto load_float_with_legacy = [&](std::string_view primary_key,
                                  std::string_view legacy_key, float& out) {
    return load_float(primary_key, out) || load_float(legacy_key, out);
  };

  bool any_loaded = false;
  any_loaded |= load_int(kEnvironmentPresetKey, preset_index_);
  const bool load_custom_state = preset_index_ == kPresetCustom;
  bool custom_state_loaded = false;
  if (load_custom_state) {
    custom_state_loaded
      = settings->GetBool(kEnvironmentCustomStatePresentKey).value_or(false)
      || settings->GetBool(kSkyAtmoEnabledKey).has_value();
  }

  bool skybox_settings_loaded = false;
  if (load_custom_state) {
    any_loaded |= load_bool(kSkyAtmoEnabledKey, sky_atmo_enabled_);
    any_loaded |= load_int(kSkyAtmoTransformModeKey, sky_atmo_transform_mode_);
    any_loaded |= load_float(kPlanetRadiusKey, planet_radius_km_);
    any_loaded |= load_float(kAtmosphereHeightKey, atmosphere_height_km_);
    any_loaded |= load_vec3(kGroundAlbedoKey, ground_albedo_);
    any_loaded
      |= load_float(kRayleighScaleHeightKey, rayleigh_scale_height_km_);
    any_loaded |= load_float(kMieScaleHeightKey, mie_scale_height_km_);
    any_loaded |= load_float(kMieAnisotropyKey, mie_anisotropy_);
    any_loaded |= load_float(kMieAbsorptionScaleKey, mie_absorption_scale_);
    mie_absorption_scale_ = std::clamp(mie_absorption_scale_, 0.0F, 5.0F);
    any_loaded |= load_float(kMultiScatteringKey, multi_scattering_);
    any_loaded |= load_vec3(kSkyLuminanceFactorKey, sky_luminance_factor_);
    any_loaded |= load_vec3(kSkyAndAerialLuminanceFactorKey,
      sky_and_aerial_perspective_luminance_factor_);
    any_loaded |= load_bool(kSunDiskEnabledKey, sun_disk_enabled_);
    any_loaded
      |= load_float(kAerialPerspectiveScaleKey, aerial_perspective_scale_);
    any_loaded |= load_float(
      kAerialPerspectiveStartDepthKey, aerial_perspective_start_depth_m_);
    any_loaded
      |= load_float(kAerialScatteringStrengthKey, aerial_scattering_strength_);
    any_loaded
      |= load_float(kHeightFogContributionKey, height_fog_contribution_);
    any_loaded
      |= load_float(kTraceSampleCountScaleKey, trace_sample_count_scale_);
    any_loaded |= load_float(kTransmittanceMinLightElevationKey,
      transmittance_min_light_elevation_deg_);
    any_loaded |= load_bool(kAtmosphereHoldoutKey, atmosphere_holdout_);
    any_loaded |= load_bool(
      kAtmosphereRenderInMainPassKey, atmosphere_render_in_main_pass_);
    any_loaded |= load_vec3(kOzoneRgbKey, ozone_rgb_);

    engine::atmos::DensityProfile loaded_profile = ozone_profile_;
    bool ozone_profile_loaded = false;
    ozone_profile_loaded |= load_float(
      kOzoneProfileLayer0WidthMKey, loaded_profile.layers[0].width_m);
    ozone_profile_loaded |= load_float(
      kOzoneProfileLayer0LinearTermKey, loaded_profile.layers[0].linear_term);
    ozone_profile_loaded |= load_float(kOzoneProfileLayer0ConstantTermKey,
      loaded_profile.layers[0].constant_term);
    ozone_profile_loaded |= load_float(
      kOzoneProfileLayer1LinearTermKey, loaded_profile.layers[1].linear_term);
    ozone_profile_loaded |= load_float(kOzoneProfileLayer1ConstantTermKey,
      loaded_profile.layers[1].constant_term);

    if (ozone_profile_loaded) {
      loaded_profile.layers[0].exp_term = 0.0F;
      loaded_profile.layers[1].width_m = 0.0F;
      loaded_profile.layers[1].exp_term = 0.0F;
      ozone_profile_ = loaded_profile;
    }

    any_loaded |= load_bool(kSkySphereEnabledKey, sky_sphere_enabled_);
    any_loaded |= load_int(kSkySphereSourceKey, sky_sphere_source_);
    any_loaded |= load_vec3(kSkySphereSolidColorKey, sky_sphere_solid_color_);
    any_loaded |= load_float(kSkySphereRotationKey, sky_sphere_rotation_deg_);

    const bool sky_intensity_loaded
      = load_float(kSkySphereIntensityKey, sky_intensity_);
    const bool sky_light_intensity_mul_loaded
      = load_float(kSkyLightIntensityMulKey, sky_light_intensity_mul_);

    any_loaded |= sky_intensity_loaded || sky_light_intensity_mul_loaded;

    skybox_settings_loaded |= load_int(kSkyboxLayoutKey, skybox_layout_idx_);
    skybox_settings_loaded
      |= load_int(kSkyboxOutputFormatKey, skybox_output_format_idx_);
    skybox_settings_loaded |= load_int(kSkyboxFaceSizeKey, skybox_face_size_);
    skybox_settings_loaded |= load_bool(kSkyboxFlipYKey, skybox_flip_y_);
    skybox_settings_loaded
      |= load_bool(kSkyboxTonemapKey, skybox_tonemap_hdr_to_ldr_);
    skybox_settings_loaded
      |= load_float(kSkyboxHdrExposureKey, skybox_hdr_exposure_ev_);
    if (const auto path = settings->GetString(kSkyboxPathKey)) {
      skybox_path_ = *path;
      skybox_settings_loaded = true;
    }
    any_loaded |= skybox_settings_loaded;

    any_loaded |= load_bool(kSkyLightEnabledKey, sky_light_enabled_);
    any_loaded |= load_int(kSkyLightSourceKey, sky_light_source_);
    any_loaded |= load_vec3(kSkyLightTintKey, sky_light_tint_);
    any_loaded
      |= load_float(kSkyLightIntensityMulKey, sky_light_intensity_mul_);
    any_loaded |= load_float(kSkyLightDiffuseKey, sky_light_diffuse_);
    any_loaded |= load_float(kSkyLightSpecularKey, sky_light_specular_);
    any_loaded |= load_bool(
      kSkyLightRealTimeCaptureKey, sky_light_real_time_capture_enabled_);
    any_loaded |= load_vec3(
      kSkyLightLowerHemisphereColorKey, sky_light_lower_hemisphere_color_);
    any_loaded |= load_float(kSkyLightVolumetricScatteringIntensityKey,
      sky_light_volumetric_scattering_intensity_);
    any_loaded |= load_bool(
      kSkyLightAffectReflectionsKey, sky_light_affect_reflections_);
    any_loaded |= load_bool(kSkyLightAffectGlobalIlluminationKey,
      sky_light_affect_global_illumination_);

    any_loaded |= load_bool(kFogEnabledKey, fog_enabled_);
    any_loaded |= load_int(kFogModelKey, fog_model_);
    any_loaded
      |= load_float(kFogExtinctionSigmaTKey, fog_extinction_sigma_t_per_m_);
    any_loaded |= load_float(kFogHeightFalloffKey, fog_height_falloff_per_m_);
    any_loaded |= load_float(kFogHeightOffsetKey, fog_height_offset_m_);
    any_loaded |= load_float(kFogStartDistanceKey, fog_start_distance_m_);
    any_loaded |= load_float(kFogMaxOpacityKey, fog_max_opacity_);
    any_loaded |= load_vec3(
      kFogSingleScatteringAlbedoKey, fog_single_scattering_albedo_rgb_);

    any_loaded |= load_bool(kSunEnabledKey, sun_enabled_);
    any_loaded |= load_float(kSunAzimuthKey, sun_azimuth_deg_);
    any_loaded |= load_float(kSunElevationKey, sun_elevation_deg_);
    any_loaded |= load_vec3(kSunColorKey, sun_color_rgb_);
    any_loaded |= load_float(kSunIlluminanceKey, sun_illuminance_lx_);
    any_loaded |= load_bool(kSunUseTemperatureKey, sun_use_temperature_);
    any_loaded |= load_float(kSunTemperatureKey, sun_temperature_kelvin_);
    any_loaded |= load_float(kSunDiskRadiusKey, sun_component_disk_radius_deg_);
    any_loaded
      |= load_int(kSunAtmosphereLightSlotKey, sun_atmosphere_light_slot_);
    any_loaded |= load_bool(kSunPerPixelAtmosphereTransmittanceKey,
      sun_use_per_pixel_atmosphere_transmittance_);
    any_loaded |= load_vec3(kSunAtmosphereDiskLuminanceScaleKey,
      sun_atmosphere_disk_luminance_scale_);
    any_loaded |= load_float(kSunShadowBiasKey, sun_shadow_bias_);
    any_loaded |= load_float(kSunShadowNormalBiasKey, sun_shadow_normal_bias_);
    any_loaded
      |= load_int(kSunShadowResolutionHintKey, sun_shadow_resolution_hint_);
    any_loaded |= load_int_with_legacy(kSunShadowCascadeCountKey,
      kLegacySunShadowCascadeCountKey, sun_shadow_cascade_count_);
    any_loaded |= load_int_with_legacy(kSunShadowSplitModeKey,
      kLegacySunShadowSplitModeKey, sun_shadow_split_mode_);
    any_loaded |= load_float_with_legacy(kSunShadowMaxDistanceKey,
      kLegacySunShadowMaxDistanceKey, sun_shadow_max_distance_);
    any_loaded |= load_float_with_legacy(kSunShadowDistributionExponentKey,
      kLegacySunShadowDistributionExponentKey,
      sun_shadow_distribution_exponent_);
    any_loaded |= load_float_with_legacy(kSunShadowTransitionFractionKey,
      kLegacySunShadowTransitionFractionKey, sun_shadow_transition_fraction_);
    any_loaded |= load_float_with_legacy(kSunShadowDistanceFadeoutFractionKey,
      kLegacySunShadowDistanceFadeoutFractionKey,
      sun_shadow_distance_fadeout_fraction_);
    for (std::size_t i = 0; i < sun_shadow_cascade_distances_.size(); ++i) {
      std::string key(kSunShadowCascadeDistancePrefixKey);
      key += '.';
      key += std::to_string(i);
      std::string legacy_key(kLegacySunShadowCascadeDistancePrefixKey);
      legacy_key += '.';
      legacy_key += std::to_string(i);
      any_loaded |= load_float(key, sun_shadow_cascade_distances_[i])
        || load_float(legacy_key, sun_shadow_cascade_distances_[i]);
    }
  }

  if (settings->Remove(kLegacySunSourceKey)) {
    settings_persist_dirty_ = true;
  }

  if (load_custom_state && loaded_schema_version < 2.0F) {
    // v1 stored invalid coupled intensity defaults; force safe independent
    // values on migration.
    sky_intensity_ = std::clamp(sky_intensity_, 0.0F, 1000.0F);
    sky_light_intensity_mul_
      = std::clamp(sky_light_intensity_mul_, 0.0F, 100.0F);
    any_loaded = true;
    settings_persist_dirty_ = true;
  }

  ValidateAndClampState();
  settings_loaded_ = true;
  has_persisted_settings_ = custom_state_loaded;
  if (any_loaded) {
    if (kForceEnvironmentOverride) {
      needs_sync_ = false;
      pending_changes_ = true;
      dirty_domains_ = ToMask(DirtyDomain::kAll);
      skybox_dirty_ = skybox_settings_loaded;
      settings_revision_++;
      return;
    }

    if (preset_index_ == kPresetUseScene) {
      needs_sync_ = true;
      pending_changes_ = false;
      dirty_domains_ = ToMask(DirtyDomain::kNone);
    } else if (preset_index_ == kPresetCustom) {
      if (custom_state_loaded) {
        needs_sync_ = false;
        pending_changes_ = true;
        dirty_domains_ = ToMask(DirtyDomain::kAll);
        skybox_dirty_ = skybox_settings_loaded;
        settings_revision_++;
      } else {
        needs_sync_ = true;
        pending_changes_ = false;
        dirty_domains_ = ToMask(DirtyDomain::kNone);
      }
    } else {
      // Built-in preset selection is persisted, but environment field values
      // are applied by EnvironmentVm and not loaded from disk.
      needs_sync_ = false;
      pending_changes_ = false;
      dirty_domains_ = ToMask(DirtyDomain::kNone);
    }
  }
}

auto EnvironmentSettingsService::SaveSettings() const -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  auto save_bool
    = [&](std::string_view key, bool value) { settings->SetBool(key, value); };
  auto save_float = [&](std::string_view key, float value) {
    settings->SetFloat(key, value);
  };
  auto save_vec3 = [&](std::string_view prefix, const glm::vec3& value) {
    std::string key(prefix);
    key += ".x";
    save_float(key, value.x);
    key.resize(prefix.size());
    key += ".y";
    save_float(key, value.y);
    key.resize(prefix.size());
    key += ".z";
    save_float(key, value.z);
  };
  auto save_int = [&](std::string_view key, int value) {
    save_float(key, static_cast<float>(value));
  };

  save_float(
    kEnvironmentSettingsSchemaVersionKey, kCurrentSettingsSchemaVersion);
  save_bool(kEnvironmentCustomStatePresentKey, preset_index_ == kPresetCustom);
  save_int(kEnvironmentPresetKey, preset_index_);
  (void)settings->Remove(kLegacySunSourceKey);

  if (preset_index_ != kPresetCustom) {
    return;
  }

  save_bool(kSkyAtmoEnabledKey, sky_atmo_enabled_);
  save_int(kSkyAtmoTransformModeKey, sky_atmo_transform_mode_);
  save_float(kPlanetRadiusKey, planet_radius_km_);
  save_float(kAtmosphereHeightKey, atmosphere_height_km_);
  save_vec3(kGroundAlbedoKey, ground_albedo_);
  save_float(kRayleighScaleHeightKey, rayleigh_scale_height_km_);
  save_float(kMieScaleHeightKey, mie_scale_height_km_);
  save_float(kMieAnisotropyKey, mie_anisotropy_);
  save_float(kMieAbsorptionScaleKey, mie_absorption_scale_);
  save_float(kMultiScatteringKey, multi_scattering_);
  save_vec3(kSkyLuminanceFactorKey, sky_luminance_factor_);
  save_vec3(kSkyAndAerialLuminanceFactorKey,
    sky_and_aerial_perspective_luminance_factor_);
  save_bool(kSunDiskEnabledKey, sun_disk_enabled_);
  save_float(kAerialPerspectiveScaleKey, aerial_perspective_scale_);
  save_float(
    kAerialPerspectiveStartDepthKey, aerial_perspective_start_depth_m_);
  save_float(kAerialScatteringStrengthKey, aerial_scattering_strength_);
  save_float(kHeightFogContributionKey, height_fog_contribution_);
  save_float(kTraceSampleCountScaleKey, trace_sample_count_scale_);
  save_float(
    kTransmittanceMinLightElevationKey, transmittance_min_light_elevation_deg_);
  save_bool(kAtmosphereHoldoutKey, atmosphere_holdout_);
  save_bool(kAtmosphereRenderInMainPassKey, atmosphere_render_in_main_pass_);

  save_vec3(kOzoneRgbKey, ozone_rgb_);

  save_float(kOzoneProfileLayer0WidthMKey, ozone_profile_.layers[0].width_m);
  save_float(
    kOzoneProfileLayer0LinearTermKey, ozone_profile_.layers[0].linear_term);
  save_float(
    kOzoneProfileLayer0ConstantTermKey, ozone_profile_.layers[0].constant_term);
  save_float(
    kOzoneProfileLayer1LinearTermKey, ozone_profile_.layers[1].linear_term);
  save_float(
    kOzoneProfileLayer1ConstantTermKey, ozone_profile_.layers[1].constant_term);

  save_bool(kSkySphereEnabledKey, sky_sphere_enabled_);
  save_int(kSkySphereSourceKey, sky_sphere_source_);
  save_vec3(kSkySphereSolidColorKey, sky_sphere_solid_color_);
  save_float(kSkySphereRotationKey, sky_sphere_rotation_deg_);
  save_float(kSkySphereIntensityKey, sky_intensity_);

  save_int(kSkyboxLayoutKey, skybox_layout_idx_);
  save_int(kSkyboxOutputFormatKey, skybox_output_format_idx_);
  save_int(kSkyboxFaceSizeKey, skybox_face_size_);
  save_bool(kSkyboxFlipYKey, skybox_flip_y_);
  save_bool(kSkyboxTonemapKey, skybox_tonemap_hdr_to_ldr_);
  save_float(kSkyboxHdrExposureKey, skybox_hdr_exposure_ev_);
  if (!skybox_path_.empty()) {
    settings->SetString(kSkyboxPathKey, skybox_path_);
  }

  save_bool(kSkyLightEnabledKey, sky_light_enabled_);
  save_int(kSkyLightSourceKey, sky_light_source_);
  save_vec3(kSkyLightTintKey, sky_light_tint_);
  save_float(kSkyLightIntensityMulKey, sky_light_intensity_mul_);
  save_float(kSkyLightDiffuseKey, sky_light_diffuse_);
  save_float(kSkyLightSpecularKey, sky_light_specular_);
  save_bool(kSkyLightRealTimeCaptureKey, sky_light_real_time_capture_enabled_);
  save_vec3(
    kSkyLightLowerHemisphereColorKey, sky_light_lower_hemisphere_color_);
  save_float(kSkyLightVolumetricScatteringIntensityKey,
    sky_light_volumetric_scattering_intensity_);
  save_bool(kSkyLightAffectReflectionsKey, sky_light_affect_reflections_);
  save_bool(kSkyLightAffectGlobalIlluminationKey,
    sky_light_affect_global_illumination_);

  save_bool(kFogEnabledKey, fog_enabled_);
  save_int(kFogModelKey, fog_model_);
  save_float(kFogExtinctionSigmaTKey, fog_extinction_sigma_t_per_m_);
  save_float(kFogHeightFalloffKey, fog_height_falloff_per_m_);
  save_float(kFogHeightOffsetKey, fog_height_offset_m_);
  save_float(kFogStartDistanceKey, fog_start_distance_m_);
  save_float(kFogMaxOpacityKey, fog_max_opacity_);
  save_vec3(kFogSingleScatteringAlbedoKey, fog_single_scattering_albedo_rgb_);

  save_bool(kSunEnabledKey, sun_enabled_);
  save_float(kSunAzimuthKey, sun_azimuth_deg_);
  save_float(kSunElevationKey, sun_elevation_deg_);
  save_vec3(kSunColorKey, sun_color_rgb_);
  save_float(kSunIlluminanceKey, sun_illuminance_lx_);
  save_bool(kSunUseTemperatureKey, sun_use_temperature_);
  save_float(kSunTemperatureKey, sun_temperature_kelvin_);
  save_float(kSunDiskRadiusKey, sun_component_disk_radius_deg_);
  save_int(kSunAtmosphereLightSlotKey, sun_atmosphere_light_slot_);
  save_bool(kSunPerPixelAtmosphereTransmittanceKey,
    sun_use_per_pixel_atmosphere_transmittance_);
  save_vec3(
    kSunAtmosphereDiskLuminanceScaleKey, sun_atmosphere_disk_luminance_scale_);
  save_float(kSunShadowBiasKey, sun_shadow_bias_);
  save_float(kSunShadowNormalBiasKey, sun_shadow_normal_bias_);
  save_int(kSunShadowResolutionHintKey, sun_shadow_resolution_hint_);
  save_int(kSunShadowCascadeCountKey, sun_shadow_cascade_count_);
  save_int(kSunShadowSplitModeKey, sun_shadow_split_mode_);
  save_float(kSunShadowMaxDistanceKey, sun_shadow_max_distance_);
  save_float(
    kSunShadowDistributionExponentKey, sun_shadow_distribution_exponent_);
  save_float(kSunShadowTransitionFractionKey, sun_shadow_transition_fraction_);
  save_float(kSunShadowDistanceFadeoutFractionKey,
    sun_shadow_distance_fadeout_fraction_);
  for (std::size_t i = 0; i < sun_shadow_cascade_distances_.size(); ++i) {
    std::string key(kSunShadowCascadeDistancePrefixKey);
    key += '.';
    key += std::to_string(i);
    save_float(key, sun_shadow_cascade_distances_[i]);
  }
}

auto EnvironmentSettingsService::MarkDirty(uint32_t dirty_domains) -> void
{
  ValidateAndClampState();
  uint32_t effective_domains = dirty_domains;
  if ((dirty_domains & ToMask(DirtyDomain::kSun)) != 0U && sky_atmo_enabled_) {
    effective_domains |= ToMask(DirtyDomain::kAtmosphereLights);
  }
  if (update_depth_ > 0) {
    batched_dirty_domains_ |= effective_domains;
    settings_persist_dirty_ = true;
    DLOG_F(2,
      "batched dirty domains (domains=0x{:X}, pending=0x{:X}, depth={})",
      dirty_domains, batched_dirty_domains_, update_depth_);
    return;
  }
  pending_changes_ = true;
  dirty_domains_ |= effective_domains;
  settings_persist_dirty_ = true;
  settings_revision_++;
  DLOG_F(1,
    "marked dirty (domains=0x{:X}, effective=0x{:X}, current_mask=0x{:X}, "
    "revision={})",
    dirty_domains, effective_domains, dirty_domains_, settings_revision_);
  if (update_depth_ == 0) {
    epoch_++;
  }
}

auto EnvironmentSettingsService::ResetSunUiToDefaults() -> void
{
  const scene::CascadedShadowSettings default_csm;

  sun_enabled_ = true;
  sun_azimuth_deg_ = kDefaultSunAzimuthDeg;
  sun_elevation_deg_ = kDefaultSunElevationDeg;
  sun_color_rgb_ = { 1.0F, 1.0F, 1.0F };
  sun_illuminance_lx_ = kDefaultSunIlluminanceLx;
  sun_use_temperature_ = false;
  sun_temperature_kelvin_ = 6500.0F;
  sun_component_disk_radius_deg_ = kDefaultSunDiskAngularRadiusDeg;
  sun_shadow_bias_ = scene::kDefaultShadowBias;
  sun_shadow_normal_bias_ = scene::kDefaultShadowNormalBias;
  sun_shadow_resolution_hint_
    = static_cast<int>(scene::ShadowResolutionHint::kMedium);
  sun_shadow_cascade_count_ = static_cast<int>(default_csm.cascade_count);
  sun_shadow_split_mode_ = static_cast<int>(default_csm.split_mode);
  sun_shadow_max_distance_ = default_csm.max_shadow_distance;
  sun_shadow_cascade_distances_ = default_csm.cascade_distances;
  sun_shadow_distribution_exponent_ = default_csm.distribution_exponent;
  sun_shadow_transition_fraction_ = default_csm.transition_fraction;
  sun_shadow_distance_fadeout_fraction_ = default_csm.distance_fadeout_fraction;
}

auto EnvironmentSettingsService::EnsureSceneHasSunAtActivation() -> void
{
  if (!config_.scene) {
    return;
  }

  const auto& resolver = config_.scene->GetDirectionalLightResolver();
  resolver.Validate();
  if (const auto primary = resolver.ResolvePrimarySun();
    primary.has_value()) {
    auto node = config_.scene->GetNode(primary->NodeHandle());
    CHECK_F(node.has_value(),
      "failed to resolve scene node for primary sun handle");
    sun_light_node_ = *node;
    sun_light_available_ = sun_light_node_.IsAlive();
    CHECK_F(ApplyDirectionalSunRole(sun_light_node_, true, true, true, true),
      "failed to promote scene directional '{}' to active sun during activation",
      sun_light_node_.GetName());
    sun_enabled_ = true;
    if (auto light = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
      CaptureSunShadowSettingsFromLight(light->get());
    }
    LOG_F(INFO,
      "activated scene '{}' selected scene directional '{}' as resolved primary sun",
      config_.scene->GetName(), sun_light_node_.GetName());
    return;
  }

  sun_light_available_ = false;
  sun_light_node_ = {};
  sun_enabled_ = false;
  LOG_F(WARNING,
    "scene '{}' has no resolved sun directional light",
    config_.scene->GetName());
}

auto EnvironmentSettingsService::FindSunLightCandidate() const
  -> std::optional<scene::SceneNode>
{
  if (!config_.scene) {
    return std::nullopt;
  }

  const auto& resolver = config_.scene->GetDirectionalLightResolver();
  resolver.Validate();
  if (const auto primary = resolver.ResolvePrimarySun();
    primary.has_value()) {
    DLOG_F(1,
      "resolved scene sun candidate '{}' in scene '{}'",
      primary->Node().GetName(),
      config_.scene->GetName());
    return config_.scene->GetNode(primary->NodeHandle());
  }

  DLOG_F(1,
    "resolved no scene sun candidate in scene '{}'",
    config_.scene->GetName());
  return std::nullopt;
}

} // namespace oxygen::examples
