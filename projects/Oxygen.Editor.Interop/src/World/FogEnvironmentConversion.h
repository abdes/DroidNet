//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, on)

#include <Commands/SetEnvironmentCommand.h>
#include <World/FogEnvironmentManaged.h>

namespace Oxygen::Interop::World {

//! Copies authored fog into the native command parameters without unit changes.
inline auto ToNativeFog(FogEnvironmentManaged value)
  -> oxygen::interop::module::FogParams
{
  oxygen::interop::module::FogParams result {};
  result.enabled = value.Enabled;
  result.height_fog_enabled = value.HeightFogEnabled;
  result.density = value.Density;
  result.height_falloff = value.HeightFalloff;
  result.height_offset_meters = value.HeightOffsetMeters;
  result.max_opacity = value.MaxOpacity;
  result.inscattering_luminance_rgb = oxygen::Vec3 { value.InscatteringLuminanceRgb.X, value.InscatteringLuminanceRgb.Y, value.InscatteringLuminanceRgb.Z };
  result.sky_ambient_scale_rgb = oxygen::Vec3 { value.SkyAmbientScaleRgb.X, value.SkyAmbientScaleRgb.Y, value.SkyAmbientScaleRgb.Z };
  result.second_density = value.SecondDensity;
  result.second_height_falloff = value.SecondHeightFalloff;
  result.second_height_offset_meters = value.SecondHeightOffsetMeters;
  result.start_distance_meters = value.StartDistanceMeters;
  result.end_distance_meters = value.EndDistanceMeters;
  result.cutoff_distance_meters = value.CutoffDistanceMeters;
  result.directional_inscattering_luminance_rgb = oxygen::Vec3 { value.DirectionalInscatteringLuminanceRgb.X, value.DirectionalInscatteringLuminanceRgb.Y, value.DirectionalInscatteringLuminanceRgb.Z };
  result.directional_inscattering_exponent = value.DirectionalInscatteringExponent;
  result.directional_inscattering_start_distance_meters = value.DirectionalInscatteringStartDistanceMeters;
  result.volumetric_fog_enabled = value.VolumetricFogEnabled;
  result.volumetric_scattering_distribution = value.VolumetricScatteringDistribution;
  result.volumetric_albedo_rgb = oxygen::Vec3 { value.VolumetricAlbedoRgb.X, value.VolumetricAlbedoRgb.Y, value.VolumetricAlbedoRgb.Z };
  result.volumetric_emissive_rgb = oxygen::Vec3 { value.VolumetricEmissiveRgb.X, value.VolumetricEmissiveRgb.Y, value.VolumetricEmissiveRgb.Z };
  result.volumetric_extinction_scale = value.VolumetricExtinctionScale;
  result.volumetric_distance_meters = value.VolumetricDistanceMeters;
  result.volumetric_start_distance_meters = value.VolumetricStartDistanceMeters;
  result.volumetric_near_fade_in_distance_meters = value.VolumetricNearFadeInDistanceMeters;
  result.volumetric_static_lighting_scattering_intensity = value.VolumetricStaticLightingScatteringIntensity;
  result.override_light_colors_with_fog_inscattering = value.OverrideLightColorsWithFogInscattering;
  result.render_in_main_pass = value.RenderInMainPass;
  result.holdout = value.Holdout;
  result.visible_in_reflection_captures = value.VisibleInReflectionCaptures;
  result.visible_in_real_time_sky_captures = value.VisibleInRealTimeSkyCaptures;
  return result;
}

//! Copies observed native fog into its managed transport value.
inline auto ToManagedFog(const oxygen::interop::module::FogParams& value)
  -> FogEnvironmentManaged
{
  FogEnvironmentManaged result;
  result.Enabled = value.enabled;
  result.HeightFogEnabled = value.height_fog_enabled;
  result.Density = value.density;
  result.HeightFalloff = value.height_falloff;
  result.HeightOffsetMeters = value.height_offset_meters;
  result.MaxOpacity = value.max_opacity;
  result.InscatteringLuminanceRgb = System::Numerics::Vector3(value.inscattering_luminance_rgb.x, value.inscattering_luminance_rgb.y, value.inscattering_luminance_rgb.z);
  result.SkyAmbientScaleRgb = System::Numerics::Vector3(value.sky_ambient_scale_rgb.x, value.sky_ambient_scale_rgb.y, value.sky_ambient_scale_rgb.z);
  result.SecondDensity = value.second_density;
  result.SecondHeightFalloff = value.second_height_falloff;
  result.SecondHeightOffsetMeters = value.second_height_offset_meters;
  result.StartDistanceMeters = value.start_distance_meters;
  result.EndDistanceMeters = value.end_distance_meters;
  result.CutoffDistanceMeters = value.cutoff_distance_meters;
  result.DirectionalInscatteringLuminanceRgb = System::Numerics::Vector3(value.directional_inscattering_luminance_rgb.x, value.directional_inscattering_luminance_rgb.y, value.directional_inscattering_luminance_rgb.z);
  result.DirectionalInscatteringExponent = value.directional_inscattering_exponent;
  result.DirectionalInscatteringStartDistanceMeters = value.directional_inscattering_start_distance_meters;
  result.VolumetricFogEnabled = value.volumetric_fog_enabled;
  result.VolumetricScatteringDistribution = value.volumetric_scattering_distribution;
  result.VolumetricAlbedoRgb = System::Numerics::Vector3(value.volumetric_albedo_rgb.x, value.volumetric_albedo_rgb.y, value.volumetric_albedo_rgb.z);
  result.VolumetricEmissiveRgb = System::Numerics::Vector3(value.volumetric_emissive_rgb.x, value.volumetric_emissive_rgb.y, value.volumetric_emissive_rgb.z);
  result.VolumetricExtinctionScale = value.volumetric_extinction_scale;
  result.VolumetricDistanceMeters = value.volumetric_distance_meters;
  result.VolumetricStartDistanceMeters = value.volumetric_start_distance_meters;
  result.VolumetricNearFadeInDistanceMeters = value.volumetric_near_fade_in_distance_meters;
  result.VolumetricStaticLightingScatteringIntensity = value.volumetric_static_lighting_scattering_intensity;
  result.OverrideLightColorsWithFogInscattering = value.override_light_colors_with_fog_inscattering;
  result.RenderInMainPass = value.render_in_main_pass;
  result.Holdout = value.holdout;
  result.VisibleInReflectionCaptures = value.visible_in_reflection_captures;
  result.VisibleInRealTimeSkyCaptures = value.visible_in_real_time_sky_captures;
  return result;
}

} // namespace Oxygen::Interop::World

#pragma managed(pop)
