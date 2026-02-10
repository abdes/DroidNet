//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>

#include <glm/vec3.hpp>

#include <Oxygen/Core/Types/Atmosphere.h>

#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/EnvironmentVm.h"
#include <Oxygen/Renderer/Passes/ToneMapPass.h>

namespace oxygen::examples::ui {

namespace {
  constexpr float kKmToMeters = 1000.0F;

  struct EnvironmentPresetData {
    std::string_view name;
    bool sun_enabled;
    int sun_source;
    float sun_azimuth_deg;
    float sun_elevation_deg;
    float sun_illuminance_lx;
    bool sun_use_temperature;
    float sun_temperature_kelvin;
    float sun_disk_radius_deg;
    bool sky_atmo_enabled;
    bool sky_atmo_sun_disk_enabled;
    float planet_radius_km;
    float atmosphere_height_km;
    glm::vec3 ground_albedo;
    float rayleigh_scale_height_km;
    float mie_scale_height_km;
    float mie_anisotropy;
    float multi_scattering;
    float aerial_perspective_scale;
    float aerial_scattering_strength;
    glm::vec3 ozone_rgb;
    float ozone_bottom_km;
    float ozone_peak_km;
    float ozone_top_km;
    bool sky_sphere_enabled;
    int sky_sphere_source;
    glm::vec3 sky_sphere_color;
    float sky_sphere_intensity;
    float sky_sphere_rotation_deg;
    bool sky_light_enabled;
    int sky_light_source;
    glm::vec3 sky_light_tint;
    float sky_light_intensity;
    float sky_light_diffuse;
    float sky_light_specular;
    // Fog
    bool fog_enabled;
    int fog_model;
    float fog_density;
    float fog_height_falloff;
    float fog_height_offset_m;
    float fog_start_distance_m;
    float fog_max_opacity;
    glm::vec3 fog_albedo;

    // Exposure (PostProcess)
    bool exposure_enabled;
    int exposure_mode; // 0=Manual, 1=Auto, 2=ManualCamera
    float manual_ev;
  };

  constexpr std::array kEnvironmentPresets = {
    EnvironmentPresetData {
      .name = "Outdoor Sunny",
      .sun_enabled = true,
      .sun_source = 1,
      .sun_azimuth_deg = 135.0F,
      .sun_elevation_deg = 55.0F,
      .sun_illuminance_lx = 120000.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 5600.0F,
      .sun_disk_radius_deg = 0.2725F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = true,
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.2F,
      .mie_anisotropy = 0.8F,
      .multi_scattering = 1.0F,
      .aerial_perspective_scale = 1.0F,
      .aerial_scattering_strength = 1.0F,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomM * 0.001F,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakM * 0.001F,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopM * 0.001F,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 1.0F, 1.0F, 1.0F },
      .sky_light_intensity = 1.0F,
      .sky_light_diffuse = 1.0F,
      .sky_light_specular = 1.0F,
      .fog_enabled = false,
      .fog_model = 0,
      .fog_density = 0.01F,
      .fog_height_falloff = 0.2F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 1.0F,
      .fog_albedo = { 1.0F, 1.0F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 14.0F,
    },
    EnvironmentPresetData {
      .name = "Outdoor Cloudy",
      .sun_enabled = true,
      .sun_source = 1,
      .sun_azimuth_deg = 135.0F,
      .sun_elevation_deg = 30.0F,
      .sun_illuminance_lx = 15000.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 6500.0F,
      .sun_disk_radius_deg = 0.2725F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = true,
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.5F,
      .mie_anisotropy = 0.75F,
      .multi_scattering = 1.2F,
      .aerial_perspective_scale = 1.0F,
      .aerial_scattering_strength = 1.1F,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomM * 0.001F,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakM * 0.001F,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopM * 0.001F,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 1.0F, 1.0F, 1.0F },
      .sky_light_intensity = 1.2F,
      .sky_light_diffuse = 1.2F,
      .sky_light_specular = 0.7F,
      .fog_enabled = false,
      .fog_model = 0,
      .fog_density = 0.01F,
      .fog_height_falloff = 0.2F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 1.0F,
      .fog_albedo = { 1.0F, 1.0F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 12.0F,
    },
    EnvironmentPresetData {
      .name = "Foggy Daylight",
      .sun_enabled = true,
      .sun_source = 1,
      .sun_azimuth_deg = 135.0F,
      .sun_elevation_deg = 45.0F,
      .sun_illuminance_lx = 60000.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 6000.0F,
      .sun_disk_radius_deg = 0.2725F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = false, // Sun disk obscured by fog usually
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.2F,
      .mie_anisotropy = 0.8F,
      .multi_scattering = 1.0F,
      .aerial_perspective_scale = 1.0F,
      .aerial_scattering_strength = 1.0F,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomM * 0.001F,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakM * 0.001F,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopM * 0.001F,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 0.9F, 0.95F, 1.0F },
      .sky_light_intensity = 1.0F,
      .sky_light_diffuse = 1.0F,
      .sky_light_specular = 1.0F,
      .fog_enabled = true,
      .fog_model = 0, // Exponential
      .fog_density = 0.02F,
      .fog_height_falloff = 0.1F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 0.95F,
      .fog_albedo = { 0.9F, 0.95F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 13.0F,
    },
    EnvironmentPresetData {
      .name = "Outdoor Dawn",
      .sun_enabled = true,
      .sun_source = 1,
      .sun_azimuth_deg = 95.0F,
      .sun_elevation_deg = 6.0F,
      .sun_illuminance_lx = 3000.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 3500.0F,
      .sun_disk_radius_deg = 0.2725F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = true,
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.2F,
      .mie_anisotropy = 0.8F,
      .multi_scattering = 1.0F,
      .aerial_perspective_scale = 1.0F,
      .aerial_scattering_strength = 1.0F,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomM * 0.001F,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakM * 0.001F,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopM * 0.001F,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 1.0F, 0.95F, 0.9F },
      .sky_light_intensity = 0.6F,
      .sky_light_diffuse = 0.7F,
      .sky_light_specular = 0.5F,
      .fog_enabled = false,
      .fog_model = 0,
      .fog_density = 0.01F,
      .fog_height_falloff = 0.2F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 1.0F,
      .fog_albedo = { 1.0F, 1.0F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 9.0F,
    },
    EnvironmentPresetData {
      .name = "Outdoor Dusk",
      .sun_enabled = true,
      .sun_source = 1,
      .sun_azimuth_deg = 265.0F,
      .sun_elevation_deg = 4.0F,
      .sun_illuminance_lx = 1500.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 3200.0F,
      .sun_disk_radius_deg = 0.2725F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = true,
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.2F,
      .mie_anisotropy = 0.8F,
      .multi_scattering = 1.0F,
      .aerial_perspective_scale = 1.0F,
      .aerial_scattering_strength = 1.0F,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomM * 0.001F,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakM * 0.001F,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopM * 0.001F,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 0.95F, 0.92F, 0.9F },
      .sky_light_intensity = 0.6F,
      .sky_light_diffuse = 0.7F,
      .sky_light_specular = 0.5F,
      .fog_enabled = false,
      .fog_model = 0,
      .fog_density = 0.01F,
      .fog_height_falloff = 0.2F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 1.0F,
      .fog_albedo = { 1.0F, 1.0F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 8.0F,
    },
  };

  auto GetPreset(int index) -> const EnvironmentPresetData&
  {
    const int clamped
      = std::clamp(index, 0, static_cast<int>(kEnvironmentPresets.size()) - 1);
    return kEnvironmentPresets[static_cast<std::size_t>(clamped)];
  }
} // namespace

EnvironmentVm::EnvironmentVm(observer_ptr<EnvironmentSettingsService> service,
  observer_ptr<PostProcessSettingsService> post_process_service,
  observer_ptr<FileBrowserService> file_browser_service)
  : service_(service)
  , post_process_service_(post_process_service)
  , file_browser_(file_browser_service)
{
}

auto EnvironmentVm::SetRuntimeConfig(const EnvironmentRuntimeConfig& config)
  -> void
{
  service_->SetRuntimeConfig(config);
}

auto EnvironmentVm::HasScene() const -> bool { return service_->HasScene(); }

auto EnvironmentVm::RequestResync() -> void { service_->RequestResync(); }

auto EnvironmentVm::SyncFromSceneIfNeeded() -> void
{
  service_->SyncFromSceneIfNeeded();
}

auto EnvironmentVm::HasPendingChanges() const -> bool
{
  return service_->HasPendingChanges();
}

auto EnvironmentVm::ApplyPendingChanges() -> void
{
  service_->ApplyPendingChanges();
}

auto EnvironmentVm::GetAtmosphereLutStatus() const -> std::pair<bool, bool>
{
  return service_->GetAtmosphereLutStatus();
}

auto EnvironmentVm::GetPresetCount() const -> int
{
  return static_cast<int>(kEnvironmentPresets.size());
}

auto EnvironmentVm::GetPresetName(int index) const -> std::string_view
{
  return GetPreset(index).name;
}

auto EnvironmentVm::GetPresetLabel() const -> std::string_view
{
  const int index = service_->GetPresetIndex();
  if (index < 0) {
    return "Custom";
  }
  return GetPreset(index).name;
}

auto EnvironmentVm::GetPresetIndex() const -> int
{
  return service_->GetPresetIndex();
}

auto EnvironmentVm::ApplyPreset(int index) -> void
{
  const auto& preset = GetPreset(index);
  service_->SetPresetIndex(index);

  service_->BeginUpdate();

  // 1. Disable all systems to prevent intermediate state updates
  SetSunEnabled(false);
  SetSkyAtmosphereEnabled(false);
  SetSkySphereEnabled(false);
  SetSkyLightEnabled(false);

  // 2. Configure systems
  // Sun
  if (preset.sun_enabled && preset.sun_source == 1) {
    EnableSyntheticSun();
  }
  // Set source first to load profiles if needed
  SetSunSource(preset.sun_source);
  SetSunAzimuthDeg(preset.sun_azimuth_deg);
  SetSunElevationDeg(preset.sun_elevation_deg);
  SetSunIlluminanceLx(preset.sun_illuminance_lx);
  SetSunUseTemperature(preset.sun_use_temperature);
  SetSunTemperatureKelvin(preset.sun_temperature_kelvin);
  SetSunDiskRadiusDeg(preset.sun_disk_radius_deg);

  // Sky Atmosphere
  SetSunDiskEnabled(preset.sky_atmo_sun_disk_enabled);
  SetPlanetRadiusKm(preset.planet_radius_km);
  SetAtmosphereHeightKm(preset.atmosphere_height_km);
  SetGroundAlbedo(preset.ground_albedo);
  SetRayleighScaleHeightKm(preset.rayleigh_scale_height_km);
  SetMieScaleHeightKm(preset.mie_scale_height_km);
  SetMieAnisotropy(preset.mie_anisotropy);
  SetMultiScattering(preset.multi_scattering);
  SetAerialPerspectiveScale(preset.aerial_perspective_scale);
  SetAerialScatteringStrength(preset.aerial_scattering_strength);
  SetOzoneRgb(preset.ozone_rgb);
  SetOzoneDensityProfile(engine::atmos::MakeOzoneTwoLayerLinearDensityProfile(
    preset.ozone_bottom_km * kKmToMeters, preset.ozone_peak_km * kKmToMeters,
    preset.ozone_top_km * kKmToMeters));

  // Sky Sphere
  SetSkySphereSource(preset.sky_sphere_source);
  SetSkySphereSolidColor(preset.sky_sphere_color);
  SetSkyIntensity(preset.sky_sphere_intensity);
  SetSkySphereRotationDeg(preset.sky_sphere_rotation_deg);

  // Sky Light
  SetSkyLightSource(preset.sky_light_source);
  SetSkyLightTint(preset.sky_light_tint);
  SetSkyLightIntensity(preset.sky_light_intensity);
  SetSkyLightDiffuse(preset.sky_light_diffuse);
  SetSkyLightSpecular(preset.sky_light_specular);

  // Fog
  SetFogEnabled(preset.fog_enabled);
  SetFogModel(preset.fog_model);
  SetFogDensity(preset.fog_density);
  SetFogHeightFalloff(preset.fog_height_falloff);
  SetFogHeightOffsetMeters(preset.fog_height_offset_m);
  SetFogStartDistanceMeters(preset.fog_start_distance_m);
  SetFogMaxOpacity(preset.fog_max_opacity);
  SetFogAlbedo(preset.fog_albedo);

  // 3. Re-enable systems in dependency order
  // Background
  SetSkyAtmosphereEnabled(preset.sky_atmo_enabled);
  SetSkySphereEnabled(preset.sky_sphere_enabled);

  // Direct Light
  SetSunEnabled(preset.sun_enabled);

  // Global Illumination (captures background + direct)
  SetSkyLightEnabled(preset.sky_light_enabled);

  service_->EndUpdate();

  // 4. Apply PostProcess settings (outside of EnvironmentSettingsService
  // batch)
  if (post_process_service_) {
    // Set manual EV first to establish a baseline
    post_process_service_->SetManualExposureEv(preset.manual_ev);

    // Reset auto-exposure history to the manual EV as a starting point.
    // This prevents the camera from adapting from a dark/default state when
    // switching to a bright scene, causing a flash.
    post_process_service_->ResetAutoExposure(preset.manual_ev);

    // Apply mode
    if (preset.exposure_mode == 0) {
      post_process_service_->SetExposureMode(engine::ExposureMode::kManual);
    } else if (preset.exposure_mode == 1) {
      post_process_service_->SetExposureMode(engine::ExposureMode::kAuto);
    } else if (preset.exposure_mode == 2) {
      post_process_service_->SetExposureMode(
        engine::ExposureMode::kManualCamera);
    }
    post_process_service_->SetExposureEnabled(preset.exposure_enabled);
  }
}

auto EnvironmentVm::GetSkyAtmosphereEnabled() const -> bool
{
  return service_->GetSkyAtmosphereEnabled();
}

auto EnvironmentVm::SetSkyAtmosphereEnabled(bool enabled) -> void
{
  service_->SetSkyAtmosphereEnabled(enabled);
}

auto EnvironmentVm::GetPlanetRadiusKm() const -> float
{
  return service_->GetPlanetRadiusKm();
}

auto EnvironmentVm::SetPlanetRadiusKm(float value) -> void
{
  service_->SetPlanetRadiusKm(value);
}

auto EnvironmentVm::GetAtmosphereHeightKm() const -> float
{
  return service_->GetAtmosphereHeightKm();
}

auto EnvironmentVm::SetAtmosphereHeightKm(float value) -> void
{
  service_->SetAtmosphereHeightKm(value);
}

auto EnvironmentVm::GetGroundAlbedo() const -> glm::vec3
{
  return service_->GetGroundAlbedo();
}

auto EnvironmentVm::SetGroundAlbedo(const glm::vec3& value) -> void
{
  service_->SetGroundAlbedo(value);
}

auto EnvironmentVm::GetRayleighScaleHeightKm() const -> float
{
  return service_->GetRayleighScaleHeightKm();
}

auto EnvironmentVm::SetRayleighScaleHeightKm(float value) -> void
{
  service_->SetRayleighScaleHeightKm(value);
}

auto EnvironmentVm::GetMieScaleHeightKm() const -> float
{
  return service_->GetMieScaleHeightKm();
}

auto EnvironmentVm::SetMieScaleHeightKm(float value) -> void
{
  service_->SetMieScaleHeightKm(value);
}

auto EnvironmentVm::GetMieAnisotropy() const -> float
{
  return service_->GetMieAnisotropy();
}

auto EnvironmentVm::SetMieAnisotropy(float value) -> void
{
  service_->SetMieAnisotropy(value);
}

auto EnvironmentVm::GetMieAbsorptionScale() const -> float
{
  return service_->GetMieAbsorptionScale();
}

auto EnvironmentVm::SetMieAbsorptionScale(float value) -> void
{
  service_->SetMieAbsorptionScale(value);
}

auto EnvironmentVm::GetOzoneRgb() const -> glm::vec3
{
  return service_->GetOzoneRgb();
}

auto EnvironmentVm::SetOzoneRgb(const glm::vec3& value) -> void
{
  service_->SetOzoneRgb(value);
}

auto EnvironmentVm::GetMultiScattering() const -> float
{
  return service_->GetMultiScattering();
}

auto EnvironmentVm::SetMultiScattering(float value) -> void
{
  service_->SetMultiScattering(value);
}

auto EnvironmentVm::GetSunDiskEnabled() const -> bool
{
  return service_->GetSunDiskEnabled();
}

auto EnvironmentVm::SetSunDiskEnabled(bool enabled) -> void
{
  service_->SetSunDiskEnabled(enabled);
}

auto EnvironmentVm::GetAerialPerspectiveScale() const -> float
{
  return service_->GetAerialPerspectiveScale();
}

auto EnvironmentVm::SetAerialPerspectiveScale(float value) -> void
{
  service_->SetAerialPerspectiveScale(value);
}

auto EnvironmentVm::GetAerialScatteringStrength() const -> float
{
  return service_->GetAerialScatteringStrength();
}

auto EnvironmentVm::SetAerialScatteringStrength(float value) -> void
{
  service_->SetAerialScatteringStrength(value);
}

auto EnvironmentVm::GetOzoneDensityProfile() const
  -> engine::atmos::DensityProfile
{
  return service_->GetOzoneDensityProfile();
}

auto EnvironmentVm::SetOzoneDensityProfile(
  const engine::atmos::DensityProfile& profile) -> void
{
  service_->SetOzoneDensityProfile(profile);
}

auto EnvironmentVm::GetSkyViewLutSlices() const -> int
{
  return service_->GetSkyViewLutSlices();
}

auto EnvironmentVm::SetSkyViewLutSlices(int value) -> void
{
  service_->SetSkyViewLutSlices(value);
}

auto EnvironmentVm::GetSkyViewAltMappingMode() const -> int
{
  return service_->GetSkyViewAltMappingMode();
}

auto EnvironmentVm::SetSkyViewAltMappingMode(int value) -> void
{
  service_->SetSkyViewAltMappingMode(value);
}

auto EnvironmentVm::RequestRegenerateLut() -> void
{
  service_->RequestRegenerateLut();
}

auto EnvironmentVm::GetSkySphereEnabled() const -> bool
{
  return service_->GetSkySphereEnabled();
}

auto EnvironmentVm::SetSkySphereEnabled(bool enabled) -> void
{
  service_->SetSkySphereEnabled(enabled);
}

auto EnvironmentVm::GetSkySphereSource() const -> int
{
  return service_->GetSkySphereSource();
}

auto EnvironmentVm::SetSkySphereSource(int source) -> void
{
  service_->SetSkySphereSource(source);
}

auto EnvironmentVm::GetSkySphereSolidColor() const -> glm::vec3
{
  return service_->GetSkySphereSolidColor();
}

auto EnvironmentVm::SetSkySphereSolidColor(const glm::vec3& value) -> void
{
  service_->SetSkySphereSolidColor(value);
}

auto EnvironmentVm::GetSkyIntensity() const -> float
{
  return service_->GetSkyIntensity();
}

auto EnvironmentVm::SetSkyIntensity(const float value) -> void
{
  service_->SetSkyIntensity(value);
}

auto EnvironmentVm::GetSkySphereRotationDeg() const -> float
{
  return service_->GetSkySphereRotationDeg();
}

auto EnvironmentVm::SetSkySphereRotationDeg(float value) -> void
{
  service_->SetSkySphereRotationDeg(value);
}

auto EnvironmentVm::GetSkyboxPath() const -> std::string
{
  return service_->GetSkyboxPath();
}

auto EnvironmentVm::SetSkyboxPath(std::string_view path) -> void
{
  service_->SetSkyboxPath(path);
}

auto EnvironmentVm::GetSkyboxLayoutIndex() const -> int
{
  return service_->GetSkyboxLayoutIndex();
}

auto EnvironmentVm::SetSkyboxLayoutIndex(int index) -> void
{
  service_->SetSkyboxLayoutIndex(index);
}

auto EnvironmentVm::GetSkyboxOutputFormatIndex() const -> int
{
  return service_->GetSkyboxOutputFormatIndex();
}

auto EnvironmentVm::SetSkyboxOutputFormatIndex(int index) -> void
{
  service_->SetSkyboxOutputFormatIndex(index);
}

auto EnvironmentVm::GetSkyboxFaceSize() const -> int
{
  return service_->GetSkyboxFaceSize();
}

auto EnvironmentVm::SetSkyboxFaceSize(int size) -> void
{
  service_->SetSkyboxFaceSize(size);
}

auto EnvironmentVm::GetSkyboxFlipY() const -> bool
{
  return service_->GetSkyboxFlipY();
}

auto EnvironmentVm::SetSkyboxFlipY(bool flip) -> void
{
  service_->SetSkyboxFlipY(flip);
}

auto EnvironmentVm::GetSkyboxTonemapHdrToLdr() const -> bool
{
  return service_->GetSkyboxTonemapHdrToLdr();
}

auto EnvironmentVm::SetSkyboxTonemapHdrToLdr(bool enabled) -> void
{
  service_->SetSkyboxTonemapHdrToLdr(enabled);
}

auto EnvironmentVm::GetSkyboxHdrExposureEv() const -> float
{
  return service_->GetSkyboxHdrExposureEv();
}

auto EnvironmentVm::SetSkyboxHdrExposureEv(float value) -> void
{
  service_->SetSkyboxHdrExposureEv(std::max(value, 0.0F));
}

auto EnvironmentVm::GetSkyboxStatusMessage() const -> std::string_view
{
  return service_->GetSkyboxStatusMessage();
}

auto EnvironmentVm::GetSkyboxLastFaceSize() const -> int
{
  return service_->GetSkyboxLastFaceSize();
}

auto EnvironmentVm::GetSkyboxLastResourceKey() const -> content::ResourceKey
{
  return service_->GetSkyboxLastResourceKey();
}

auto EnvironmentVm::LoadSkybox(std::string_view path, int layout_index,
  int output_format_index, int face_size, bool flip_y, bool tonemap_hdr_to_ldr,
  float hdr_exposure_ev) -> void
{
  service_->LoadSkybox(path, layout_index, output_format_index, face_size,
    flip_y, tonemap_hdr_to_ldr, hdr_exposure_ev);
}

auto EnvironmentVm::BeginSkyboxBrowse(std::string_view current_path) -> void
{
  if (!file_browser_) {
    return;
  }

  const auto roots = file_browser_->GetContentRoots();
  auto picker_config = MakeSkyboxFileBrowserConfig(roots);

  if (current_path.size() != 0U) {
    const std::filesystem::path current_path_fs { std::string(current_path) };
    if (current_path_fs.has_parent_path()) {
      picker_config.initial_directory = current_path_fs.parent_path();
    }
  }

  skybox_browse_request_id_ = file_browser_->Open(picker_config);
}

auto EnvironmentVm::ConsumeSkyboxBrowseResult()
  -> std::optional<std::filesystem::path>
{
  if (!file_browser_ || skybox_browse_request_id_ == 0) {
    return std::nullopt;
  }

  const auto result = file_browser_->ConsumeResult(skybox_browse_request_id_);
  if (!result) {
    return std::nullopt;
  }

  skybox_browse_request_id_ = 0;
  if (result->kind != FileBrowserService::ResultKind::kSelected) {
    return std::nullopt;
  }

  auto path = result->path;
  const auto path_string = path.string();
  service_->SetSkyboxPath(path_string);
  return path;
}

auto EnvironmentVm::GetSkyLightEnabled() const -> bool
{
  return service_->GetSkyLightEnabled();
}

auto EnvironmentVm::SetSkyLightEnabled(bool enabled) -> void
{
  service_->SetSkyLightEnabled(enabled);
}

auto EnvironmentVm::GetSkyLightSource() const -> int
{
  return service_->GetSkyLightSource();
}

auto EnvironmentVm::SetSkyLightSource(int source) -> void
{
  service_->SetSkyLightSource(source);
}

auto EnvironmentVm::GetSkyLightTint() const -> glm::vec3
{
  return service_->GetSkyLightTint();
}

auto EnvironmentVm::SetSkyLightTint(const glm::vec3& value) -> void
{
  service_->SetSkyLightTint(value);
}

auto EnvironmentVm::GetSkyLightIntensity() const -> float
{
  return service_->GetSkyLightIntensity();
}

auto EnvironmentVm::SetSkyLightIntensity(const float value) -> void
{
  service_->SetSkyLightIntensity(value);
}

auto EnvironmentVm::GetSkyLightDiffuse() const -> float
{
  return service_->GetSkyLightDiffuse();
}

auto EnvironmentVm::SetSkyLightDiffuse(float value) -> void
{
  service_->SetSkyLightDiffuse(value);
}

auto EnvironmentVm::GetSkyLightSpecular() const -> float
{
  return service_->GetSkyLightSpecular();
}

auto EnvironmentVm::SetSkyLightSpecular(float value) -> void
{
  service_->SetSkyLightSpecular(value);
}

auto EnvironmentVm::GetFogEnabled() const -> bool
{
  return service_->GetFogEnabled();
}

auto EnvironmentVm::SetFogEnabled(const bool enabled) -> void
{
  service_->SetFogEnabled(enabled);
}

auto EnvironmentVm::GetFogModel() const -> int
{
  return service_->GetFogModel();
}

auto EnvironmentVm::SetFogModel(const int model) -> void
{
  service_->SetFogModel(model);
}

auto EnvironmentVm::GetFogDensity() const -> float
{
  return service_->GetFogDensity();
}

auto EnvironmentVm::SetFogDensity(const float value) -> void
{
  service_->SetFogDensity(value);
}

auto EnvironmentVm::GetFogHeightFalloff() const -> float
{
  return service_->GetFogHeightFalloff();
}

auto EnvironmentVm::SetFogHeightFalloff(const float value) -> void
{
  service_->SetFogHeightFalloff(value);
}

auto EnvironmentVm::GetFogHeightOffsetMeters() const -> float
{
  return service_->GetFogHeightOffsetMeters();
}

auto EnvironmentVm::SetFogHeightOffsetMeters(const float value) -> void
{
  service_->SetFogHeightOffsetMeters(value);
}

auto EnvironmentVm::GetFogStartDistanceMeters() const -> float
{
  return service_->GetFogStartDistanceMeters();
}

auto EnvironmentVm::SetFogStartDistanceMeters(const float value) -> void
{
  service_->SetFogStartDistanceMeters(value);
}

auto EnvironmentVm::GetFogMaxOpacity() const -> float
{
  return service_->GetFogMaxOpacity();
}

auto EnvironmentVm::SetFogMaxOpacity(const float value) -> void
{
  service_->SetFogMaxOpacity(value);
}

auto EnvironmentVm::GetFogAlbedo() const -> glm::vec3
{
  return service_->GetFogAlbedo();
}

auto EnvironmentVm::SetFogAlbedo(const glm::vec3& value) -> void
{
  service_->SetFogAlbedo(value);
}

auto EnvironmentVm::GetSunPresent() const -> bool
{
  return service_->GetSunPresent();
}

auto EnvironmentVm::GetSunEnabled() const -> bool
{
  return service_->GetSunEnabled();
}

auto EnvironmentVm::SetSunEnabled(bool enabled) -> void
{
  service_->SetSunEnabled(enabled);
}

auto EnvironmentVm::GetSunSource() const -> int
{
  return service_->GetSunSource();
}

auto EnvironmentVm::SetSunSource(int source) -> void
{
  service_->SetSunSource(source);
}

auto EnvironmentVm::GetSunAzimuthDeg() const -> float
{
  return service_->GetSunAzimuthDeg();
}

auto EnvironmentVm::SetSunAzimuthDeg(float value) -> void
{
  service_->SetSunAzimuthDeg(value);
}

auto EnvironmentVm::GetSunElevationDeg() const -> float
{
  return service_->GetSunElevationDeg();
}

auto EnvironmentVm::SetSunElevationDeg(float value) -> void
{
  service_->SetSunElevationDeg(value);
}

auto EnvironmentVm::GetSunColorRgb() const -> glm::vec3
{
  return service_->GetSunColorRgb();
}

auto EnvironmentVm::SetSunColorRgb(const glm::vec3& value) -> void
{
  service_->SetSunColorRgb(value);
}

auto EnvironmentVm::GetSunIlluminanceLx() const -> float
{
  return service_->GetSunIlluminanceLx();
}

auto EnvironmentVm::SetSunIlluminanceLx(float value) -> void
{
  service_->SetSunIlluminanceLx(value);
}

auto EnvironmentVm::GetSunUseTemperature() const -> bool
{
  return service_->GetSunUseTemperature();
}

auto EnvironmentVm::SetSunUseTemperature(bool enabled) -> void
{
  service_->SetSunUseTemperature(enabled);
}

auto EnvironmentVm::GetSunTemperatureKelvin() const -> float
{
  return service_->GetSunTemperatureKelvin();
}

auto EnvironmentVm::SetSunTemperatureKelvin(float value) -> void
{
  service_->SetSunTemperatureKelvin(value);
}

auto EnvironmentVm::GetSunDiskRadiusDeg() const -> float
{
  return service_->GetSunDiskRadiusDeg();
}

auto EnvironmentVm::SetSunDiskRadiusDeg(float value) -> void
{
  service_->SetSunDiskRadiusDeg(value);
}

auto EnvironmentVm::GetSunLightAvailable() const -> bool
{
  return service_->GetSunLightAvailable();
}

auto EnvironmentVm::UpdateSunLightCandidate() -> void
{
  service_->UpdateSunLightCandidate();
}

auto EnvironmentVm::EnableSyntheticSun() -> void
{
  service_->EnableSyntheticSun();
}

auto EnvironmentVm::GetUseLut() const -> bool { return service_->GetUseLut(); }

auto EnvironmentVm::SetUseLut(bool enabled) -> void
{
  service_->SetUseLut(enabled);
}

auto EnvironmentVm::GetVisualizeLut() const -> bool
{
  return service_->GetVisualizeLut();
}

auto EnvironmentVm::SetVisualizeLut(bool enabled) -> void
{
  service_->SetVisualizeLut(enabled);
}

auto EnvironmentVm::GetForceAnalytic() const -> bool
{
  return service_->GetForceAnalytic();
}

auto EnvironmentVm::SetForceAnalytic(bool enabled) -> void
{
  service_->SetForceAnalytic(enabled);
}

} // namespace oxygen::examples::ui
