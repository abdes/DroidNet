//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <optional>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>

#include "DemoShell/Services/EnvironmentHydrator.h"

namespace oxygen::examples {

namespace {

  template <typename Record>
  auto IsEnabled(const std::optional<Record>& record) -> bool
  {
    return record.has_value() && record->enabled != 0U;
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
 EnvironmentHydrator::HydrateEnvironment(*env, asset);
 ```

 @note SkyAtmosphere and SkySphere are treated as mutually exclusive.
*/
void EnvironmentHydrator::HydrateEnvironment(
  scene::SceneEnvironment& target, const data::SceneAsset& source_asset)
{
  const auto sky_atmo_record = source_asset.TryGetSkyAtmosphereEnvironment();
  const auto sky_sphere_record = source_asset.TryGetSkySphereEnvironment();

  const bool sky_atmo_enabled = IsEnabled(sky_atmo_record);
  const bool sky_sphere_enabled = IsEnabled(sky_sphere_record);

  if (sky_atmo_enabled && sky_sphere_enabled) {
    LOG_F(WARNING,
      "EnvironmentHydrator: Both SkyAtmosphere and SkySphere are enabled in "
      "the scene. They are mutually exclusive; SkyAtmosphere will be used.");
  }

  if (sky_atmo_enabled) {
    auto& atmo = target.AddSystem<scene::environment::SkyAtmosphere>();
    HydrateSystem(atmo, *sky_atmo_record);
    LOG_F(INFO, "EnvironmentHydrator: Applied SkyAtmosphere environment");
  } else if (sky_sphere_enabled) {
    auto& sky_sphere = target.AddSystem<scene::environment::SkySphere>();
    HydrateSystem(sky_sphere, *sky_sphere_record);
    LOG_F(INFO,
      "EnvironmentHydrator: Applied SkySphere environment (solid color "
      "source)");
  }

  if (const auto fog_record = source_asset.TryGetFogEnvironment();
    IsEnabled(fog_record)) {
    auto& fog = target.AddSystem<scene::environment::Fog>();
    HydrateSystem(fog, *fog_record);
    LOG_F(INFO, "EnvironmentHydrator: Applied Fog environment");
  }

  if (const auto sky_light_record = source_asset.TryGetSkyLightEnvironment();
    IsEnabled(sky_light_record)) {
    auto& sky_light = target.AddSystem<scene::environment::SkyLight>();
    HydrateSystem(sky_light, *sky_light_record);
    LOG_F(INFO, "EnvironmentHydrator: Applied SkyLight environment");
  }

  if (const auto clouds_record
    = source_asset.TryGetVolumetricCloudsEnvironment();
    IsEnabled(clouds_record)) {
    auto& clouds = target.AddSystem<scene::environment::VolumetricClouds>();
    HydrateSystem(clouds, *clouds_record);
    LOG_F(INFO, "EnvironmentHydrator: Applied VolumetricClouds environment");
  }

  if (const auto pp_record = source_asset.TryGetPostProcessVolumeEnvironment();
    IsEnabled(pp_record)) {
    auto& pp = target.AddSystem<scene::environment::PostProcessVolume>();
    HydrateSystem(pp, *pp_record);
    LOG_F(INFO, "EnvironmentHydrator: Applied PostProcessVolume environment");
  }
}

/*!
 Hydrates a SkyAtmosphere system from its asset record.

 @param target Mutable SkyAtmosphere system.
 @param source Asset record describing the system.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Direct field-to-property mapping.
*/
void EnvironmentHydrator::HydrateSystem(
  scene::environment::SkyAtmosphere& target,
  const data::pak::SkyAtmosphereEnvironmentRecord& source)
{
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
  target.SetAbsorptionRgb(Vec3 { source.absorption_rgb[0],
    source.absorption_rgb[1], source.absorption_rgb[2] });
  target.SetAbsorptionScaleHeightMeters(source.absorption_scale_height_m);
  target.SetMultiScatteringFactor(source.multi_scattering_factor);
  target.SetSunDiskEnabled(source.sun_disk_enabled != 0U);
  target.SetSunDiskAngularRadiusRadians(source.sun_disk_angular_radius_radians);
  target.SetAerialPerspectiveDistanceScale(
    source.aerial_perspective_distance_scale);
}

/*!
 Hydrates a SkySphere system from its asset record.

 @param target Mutable SkySphere system.
 @param source Asset record describing the system.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Falls back to solid color when cubemap data is unavailable.
*/
void EnvironmentHydrator::HydrateSystem(scene::environment::SkySphere& target,
  const data::pak::SkySphereEnvironmentRecord& source)
{
  if (source.source
    == static_cast<std::uint32_t>(
      scene::environment::SkySphereSource::kSolidColor)) {
    target.SetSource(scene::environment::SkySphereSource::kSolidColor);
  } else {
    LOG_F(WARNING,
      "EnvironmentHydrator: SkySphere cubemap source requested, but "
      "scene-authored cubemap AssetKey resolution is not implemented in "
      "this example. Keeping solid color; use the Environment panel Skybox "
      "Loader to bind a cubemap at runtime.");
    target.SetSource(scene::environment::SkySphereSource::kSolidColor);
  }

  target.SetSolidColorRgb(Vec3 { source.solid_color_rgb[0],
    source.solid_color_rgb[1], source.solid_color_rgb[2] });
  target.SetIntensity(source.intensity);
  target.SetRotationRadians(source.rotation_radians);
  target.SetTintRgb(
    Vec3 { source.tint_rgb[0], source.tint_rgb[1], source.tint_rgb[2] });
}

/*!
 Hydrates a Fog system from its asset record.

 @param target Mutable Fog system.
 @param source Asset record describing the system.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Direct field-to-property mapping.
*/
void EnvironmentHydrator::HydrateSystem(scene::environment::Fog& target,
  const data::pak::FogEnvironmentRecord& source)
{
  target.SetModel(static_cast<scene::environment::FogModel>(source.model));
  target.SetDensity(source.density);
  target.SetHeightFalloff(source.height_falloff);
  target.SetHeightOffsetMeters(source.height_offset_m);
  target.SetStartDistanceMeters(source.start_distance_m);
  target.SetMaxOpacity(source.max_opacity);
  target.SetAlbedoRgb(
    Vec3 { source.albedo_rgb[0], source.albedo_rgb[1], source.albedo_rgb[2] });
  target.SetAnisotropy(source.anisotropy_g);
  target.SetScatteringIntensity(source.scattering_intensity);
}

/*!
 Hydrates a SkyLight system from its asset record.

 @param target Mutable SkyLight system.
 @param source Asset record describing the system.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Defers cubemap binding to runtime UI when required.
*/
void EnvironmentHydrator::HydrateSystem(scene::environment::SkyLight& target,
  const data::pak::SkyLightEnvironmentRecord& source)
{
  target.SetSource(
    static_cast<scene::environment::SkyLightSource>(source.source));
  if (target.GetSource()
    == scene::environment::SkyLightSource::kSpecifiedCubemap) {
    LOG_F(INFO,
      "EnvironmentHydrator: SkyLight specifies a cubemap AssetKey, but this "
      "example does not yet resolve it to a ResourceKey. Use the Environment "
      "panel Skybox Loader to bind a cubemap at runtime.");
  }
  target.SetIntensity(source.intensity);
  target.SetTintRgb(
    Vec3 { source.tint_rgb[0], source.tint_rgb[1], source.tint_rgb[2] });
  target.SetDiffuseIntensity(source.diffuse_intensity);
  target.SetSpecularIntensity(source.specular_intensity);
}

/*!
 Hydrates a VolumetricClouds system from its asset record.

 @param target Mutable VolumetricClouds system.
 @param source Asset record describing the system.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Direct field-to-property mapping.
*/
void EnvironmentHydrator::HydrateSystem(
  scene::environment::VolumetricClouds& target,
  const data::pak::VolumetricCloudsEnvironmentRecord& source)
{
  target.SetBaseAltitudeMeters(source.base_altitude_m);
  target.SetLayerThicknessMeters(source.layer_thickness_m);
  target.SetCoverage(source.coverage);
  target.SetDensity(source.density);
  target.SetAlbedoRgb(
    Vec3 { source.albedo_rgb[0], source.albedo_rgb[1], source.albedo_rgb[2] });
  target.SetExtinctionScale(source.extinction_scale);
  target.SetPhaseAnisotropy(source.phase_g);
  target.SetWindDirectionWs(Vec3 {
    source.wind_dir_ws[0], source.wind_dir_ws[1], source.wind_dir_ws[2] });
  target.SetWindSpeedMps(source.wind_speed_mps);
  target.SetShadowStrength(source.shadow_strength);
}

/*!
 Hydrates a PostProcessVolume system from its asset record.

 @param target Mutable PostProcessVolume system.
 @param source Asset record describing the system.

### Performance Characteristics

- Time Complexity: $O(1)$.
- Memory: $O(1)$.
- Optimization: Direct field-to-property mapping.
*/
void EnvironmentHydrator::HydrateSystem(
  scene::environment::PostProcessVolume& target,
  const data::pak::PostProcessVolumeEnvironmentRecord& source)
{
  target.SetToneMapper(
    static_cast<scene::environment::ToneMapper>(source.tone_mapper));
  target.SetExposureMode(
    static_cast<scene::environment::ExposureMode>(source.exposure_mode));
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

} // namespace oxygen::examples
