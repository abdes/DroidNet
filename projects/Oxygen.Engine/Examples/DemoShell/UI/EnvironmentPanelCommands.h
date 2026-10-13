//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class EnvironmentVm;

//! Names of the `demo.env.*` commands the environment panel runs.
namespace env_commands {
  inline constexpr std::string_view kAddLocalFogVolume
    = "demo.env.add_local_fog_volume";
  inline constexpr std::string_view kAerialPerspectiveEnabled
    = "demo.env.aerial_perspective_enabled";
  inline constexpr std::string_view kAerialPerspectiveScale
    = "demo.env.aerial_perspective_scale";
  inline constexpr std::string_view kAerialPerspectiveStartDepthMeters
    = "demo.env.aerial_perspective_start_depth_meters";
  inline constexpr std::string_view kAerialScatteringStrength
    = "demo.env.aerial_scattering_strength";
  inline constexpr std::string_view kAtmosphereHeightKm
    = "demo.env.atmosphere_height_km";
  inline constexpr std::string_view kAtmosphereHoldout
    = "demo.env.atmosphere_holdout";
  inline constexpr std::string_view kAtmosphereRenderInMainPass
    = "demo.env.atmosphere_render_in_main_pass";
  inline constexpr std::string_view kDirectionalInscatteringExponent
    = "demo.env.directional_inscattering_exponent";
  inline constexpr std::string_view kDirectionalInscatteringLuminance
    = "demo.env.directional_inscattering_luminance";
  inline constexpr std::string_view kDirectionalInscatteringStartDistance
    = "demo.env.directional_inscattering_start_distance";
  inline constexpr std::string_view kFogCutoffDistanceMeters
    = "demo.env.fog_cutoff_distance_meters";
  inline constexpr std::string_view kFogEnabled = "demo.env.fog_enabled";
  inline constexpr std::string_view kFogEndDistanceMeters
    = "demo.env.fog_end_distance_meters";
  inline constexpr std::string_view kFogExtinctionSigmaTPerMeter
    = "demo.env.fog_extinction_sigma_t_per_meter";
  inline constexpr std::string_view kFogHeightFalloffPerMeter
    = "demo.env.fog_height_falloff_per_meter";
  inline constexpr std::string_view kFogHeightOffsetMeters
    = "demo.env.fog_height_offset_meters";
  inline constexpr std::string_view kFogHoldout = "demo.env.fog_holdout";
  inline constexpr std::string_view kFogInscatteringLuminance
    = "demo.env.fog_inscattering_luminance";
  inline constexpr std::string_view kFogMaxOpacity = "demo.env.fog_max_opacity";
  inline constexpr std::string_view kFogModel = "demo.env.fog_model";
  inline constexpr std::string_view kFogRenderInMainPass
    = "demo.env.fog_render_in_main_pass";
  inline constexpr std::string_view kFogStartDistanceMeters
    = "demo.env.fog_start_distance_meters";
  inline constexpr std::string_view kFogVisibleInRealTimeSkyCaptures
    = "demo.env.fog_visible_in_real_time_sky_captures";
  inline constexpr std::string_view kFogVisibleInReflectionCaptures
    = "demo.env.fog_visible_in_reflection_captures";
  inline constexpr std::string_view kGroundAlbedo = "demo.env.ground_albedo";
  inline constexpr std::string_view kHeightFogContribution
    = "demo.env.height_fog_contribution";
  inline constexpr std::string_view kLoadSkybox = "demo.env.load_skybox";
  inline constexpr std::string_view kMieAbsorptionScale
    = "demo.env.mie_absorption_scale";
  inline constexpr std::string_view kMieAnisotropy = "demo.env.mie_anisotropy";
  inline constexpr std::string_view kMieScaleHeightKm
    = "demo.env.mie_scale_height_km";
  inline constexpr std::string_view kMultiScattering
    = "demo.env.multi_scattering";
  inline constexpr std::string_view kOzoneDensityProfile
    = "demo.env.ozone_density_profile";
  inline constexpr std::string_view kOzoneRgb = "demo.env.ozone_rgb";
  inline constexpr std::string_view kPlanetRadiusKm
    = "demo.env.planet_radius_km";
  inline constexpr std::string_view kPreset = "demo.env.preset";
  inline constexpr std::string_view kPreviewSunEnabled
    = "demo.env.preview_sun_enabled";
  inline constexpr std::string_view kRayleighScaleHeightKm
    = "demo.env.rayleigh_scale_height_km";
  inline constexpr std::string_view kRemoveLocalFogVolume
    = "demo.env.remove_local_fog_volume";
  inline constexpr std::string_view kSecondFogDensity
    = "demo.env.second_fog_density";
  inline constexpr std::string_view kSecondFogHeightFalloff
    = "demo.env.second_fog_height_falloff";
  inline constexpr std::string_view kSecondFogHeightOffset
    = "demo.env.second_fog_height_offset";
  inline constexpr std::string_view kSelectedLocalFogVolumeEnabled
    = "demo.env.selected_local_fog_volume_enabled";
  inline constexpr std::string_view kSelectedLocalFogVolumeFogAlbedo
    = "demo.env.selected_local_fog_volume_fog_albedo";
  inline constexpr std::string_view kSelectedLocalFogVolumeFogEmissive
    = "demo.env.selected_local_fog_volume_fog_emissive";
  inline constexpr std::string_view kSelectedLocalFogVolumeFogPhaseG
    = "demo.env.selected_local_fog_volume_fog_phase_g";
  inline constexpr std::string_view kSelectedLocalFogVolumeHeightFogExtinction
    = "demo.env.selected_local_fog_volume_height_fog_extinction";
  inline constexpr std::string_view kSelectedLocalFogVolumeHeightFogFalloff
    = "demo.env.selected_local_fog_volume_height_fog_falloff";
  inline constexpr std::string_view kSelectedLocalFogVolumeHeightFogOffset
    = "demo.env.selected_local_fog_volume_height_fog_offset";
  inline constexpr std::string_view kSelectedLocalFogVolumeIndex
    = "demo.env.selected_local_fog_volume_index";
  inline constexpr std::string_view kSelectedLocalFogVolumeRadialFogExtinction
    = "demo.env.selected_local_fog_volume_radial_fog_extinction";
  inline constexpr std::string_view kSelectedLocalFogVolumeSortPriority
    = "demo.env.selected_local_fog_volume_sort_priority";
  inline constexpr std::string_view kSkyAndAerialPerspectiveLuminanceFactor
    = "demo.env.sky_and_aerial_perspective_luminance_factor";
  inline constexpr std::string_view kSkyAtmosphereAmbientContributionColorScale
    = "demo.env.sky_atmosphere_ambient_contribution_color_scale";
  inline constexpr std::string_view kSkyAtmosphereEnabled
    = "demo.env.sky_atmosphere_enabled";
  inline constexpr std::string_view kSkyAtmosphereTransformMode
    = "demo.env.sky_atmosphere_transform_mode";
  inline constexpr std::string_view kSkyIntensity = "demo.env.sky_intensity";
  inline constexpr std::string_view kSkyLightAffectReflections
    = "demo.env.sky_light_affect_reflections";
  inline constexpr std::string_view kSkyLightDiffuse
    = "demo.env.sky_light_diffuse";
  inline constexpr std::string_view kSkyLightEnabled
    = "demo.env.sky_light_enabled";
  inline constexpr std::string_view kSkyLightIlluminanceLux
    = "demo.env.sky_light_illuminance_lux";
  inline constexpr std::string_view kSkyLightIntensityMul
    = "demo.env.sky_light_intensity_mul";
  inline constexpr std::string_view kSkyLightLowerHemisphereBlend
    = "demo.env.sky_light_lower_hemisphere_blend";
  inline constexpr std::string_view kSkyLightLowerHemisphereColor
    = "demo.env.sky_light_lower_hemisphere_color";
  inline constexpr std::string_view kSkyLightLowerHemisphereOverride
    = "demo.env.sky_light_lower_hemisphere_override";
  inline constexpr std::string_view kSkyLightSource
    = "demo.env.sky_light_source";
  inline constexpr std::string_view kSkyLightSpecular
    = "demo.env.sky_light_specular";
  inline constexpr std::string_view kSkyLightTint = "demo.env.sky_light_tint";
  inline constexpr std::string_view kSkyLightVolumetricScatteringIntensity
    = "demo.env.sky_light_volumetric_scattering_intensity";
  inline constexpr std::string_view kSkyLuminanceFactor
    = "demo.env.sky_luminance_factor";
  inline constexpr std::string_view kSkySphereEnabled
    = "demo.env.sky_sphere_enabled";
  inline constexpr std::string_view kSkySphereIlluminanceLux
    = "demo.env.sky_sphere_illuminance_lux";
  inline constexpr std::string_view kSkySphereRotationDeg
    = "demo.env.sky_sphere_rotation_deg";
  inline constexpr std::string_view kSkySphereSolidColor
    = "demo.env.sky_sphere_solid_color";
  inline constexpr std::string_view kSkySphereSource
    = "demo.env.sky_sphere_source";
  inline constexpr std::string_view kSkyboxFaceSize
    = "demo.env.skybox_face_size";
  inline constexpr std::string_view kSkyboxFlipY = "demo.env.skybox_flip_y";
  inline constexpr std::string_view kSkyboxHdrExposureEv
    = "demo.env.skybox_hdr_exposure_ev";
  inline constexpr std::string_view kSkyboxLayout = "demo.env.skybox_layout";
  inline constexpr std::string_view kSkyboxFormat = "demo.env.skybox_format";
  inline constexpr std::string_view kSkyboxPath = "demo.env.skybox_path";
  inline constexpr std::string_view kSkyboxTonemapHdrToLdr
    = "demo.env.skybox_tonemap_hdr_to_ldr";
  inline constexpr std::string_view kSunAtmosphereDiskLuminanceScale
    = "demo.env.sun_atmosphere_disk_luminance_scale";
  inline constexpr std::string_view kSunAtmosphereLightSlot
    = "demo.env.sun_atmosphere_light_slot";
  inline constexpr std::string_view kSunAzimuthDeg = "demo.env.sun_azimuth_deg";
  inline constexpr std::string_view kSunColorRgb = "demo.env.sun_color_rgb";
  inline constexpr std::string_view kSunDiskEnabled
    = "demo.env.sun_disk_enabled";
  inline constexpr std::string_view kSunElevationDeg
    = "demo.env.sun_elevation_deg";
  inline constexpr std::string_view kSunEnabled = "demo.env.sun_enabled";
  inline constexpr std::string_view kSunIlluminanceLx
    = "demo.env.sun_illuminance_lx";
  inline constexpr std::string_view kSunShadowBias = "demo.env.sun_shadow_bias";
  inline constexpr std::string_view kSunShadowCascadeCount
    = "demo.env.sun_shadow_cascade_count";
  inline constexpr std::string_view kSunShadowCascadeDistance
    = "demo.env.sun_shadow_cascade_distance";
  inline constexpr std::string_view kSunShadowDistanceFadeoutFraction
    = "demo.env.sun_shadow_distance_fadeout_fraction";
  inline constexpr std::string_view kSunShadowDistributionExponent
    = "demo.env.sun_shadow_distribution_exponent";
  inline constexpr std::string_view kSunShadowMaxDistance
    = "demo.env.sun_shadow_max_distance";
  inline constexpr std::string_view kSunShadowNormalBias
    = "demo.env.sun_shadow_normal_bias";
  inline constexpr std::string_view kSunShadowResolutionHint
    = "demo.env.sun_shadow_resolution_hint";
  inline constexpr std::string_view kSunShadowSplitMode
    = "demo.env.sun_shadow_split_mode";
  inline constexpr std::string_view kSunShadowTransitionFraction
    = "demo.env.sun_shadow_transition_fraction";
  inline constexpr std::string_view kSunSourceAngleDeg
    = "demo.env.sun_source_angle_deg";
  inline constexpr std::string_view kSunTemperatureKelvin
    = "demo.env.sun_temperature_kelvin";
  inline constexpr std::string_view kSunUsePerPixelAtmosphereTransmittance
    = "demo.env.sun_use_per_pixel_atmosphere_transmittance";
  inline constexpr std::string_view kSunUseTemperature
    = "demo.env.sun_use_temperature";
  inline constexpr std::string_view kTraceSampleCountScale
    = "demo.env.trace_sample_count_scale";
  inline constexpr std::string_view kTransmittanceMinLightElevationDeg
    = "demo.env.transmittance_min_light_elevation_deg";

  // Values of the enumerated commands, in the order of the settings they
  // select; the panel posts the name at the setting's index.
  inline constexpr std::array<std::string_view, 2> kFogModels {
    "exponential-height",
    "volumetric",
  };
  inline constexpr std::array<std::string_view, 3> kAtmosphereLightSlots {
    "none",
    "primary",
    "secondary",
  };
  inline constexpr std::array<std::string_view, 4> kShadowResolutions {
    "low",
    "medium",
    "high",
    "ultra",
  };
  inline constexpr std::array<std::string_view, 2> kShadowSplitModes {
    "generated",
    "manual",
  };
  inline constexpr std::array<std::string_view, 3> kTransformModes {
    "planet-top-at-origin",
    "planet-top-at-component",
    "planet-center-at-component",
  };
  inline constexpr std::array<std::string_view, 2> kSkySphereSources {
    "cubemap",
    "solid-color",
  };
  inline constexpr std::array<std::string_view, 5> kSkyboxLayouts {
    "equirectangular",
    "horizontal-cross",
    "vertical-cross",
    "horizontal-strip",
    "vertical-strip",
  };
  inline constexpr std::array<std::string_view, 4> kSkyboxFormats {
    "rgba8",
    "rgba16f",
    "rgba32f",
    "bc7",
  };
  inline constexpr std::array<std::string_view, 2> kSkyLightSources {
    "captured-scene",
    "specified-cubemap",
  };

  //! The name at `index`, or the first one when out of range.
  template <std::size_t N>
  constexpr auto ValueAt(const std::array<std::string_view, N>& values,
    const int index) -> std::string_view
  {
    return index >= 0 && static_cast<std::size_t>(index) < N
      ? values.at(static_cast<std::size_t>(index))
      : values.front();
  }
} // namespace env_commands

//! Registers the environment panel's commands, bound to `vm`.
auto RegisterEnvironmentCommands(
  PanelCommands& commands, observer_ptr<EnvironmentVm> vm) -> void;

} // namespace oxygen::examples::ui
