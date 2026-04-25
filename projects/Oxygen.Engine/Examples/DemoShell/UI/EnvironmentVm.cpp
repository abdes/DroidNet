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

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>

#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/EnvironmentVm.h"

namespace oxygen::examples::ui {

namespace {
  constexpr int kPresetUseScene = -2;
  constexpr int kPresetCustom = -1;

  struct EnvironmentPresetData {
    std::string_view name;
    bool sun_enabled;
    float sun_azimuth_deg;
    float sun_elevation_deg;
    float sun_illuminance_lx;
    bool sun_use_temperature;
    float sun_temperature_kelvin;
    float sun_source_angle_deg;
    bool sky_atmo_enabled;
    bool sky_atmo_sun_disk_enabled;
    float planet_radius_km;
    float atmosphere_height_km;
    glm::vec3 ground_albedo;
    float rayleigh_scale_height_km;
    float mie_scale_height_km;
    float mie_anisotropy;
    float multi_scattering;
    glm::vec3 sky_and_aerial_luminance_factor;
    float aerial_perspective_scale;
    float aerial_perspective_start_depth_m;
    float aerial_scattering_strength;
    float height_fog_contribution;
    float trace_sample_count_scale;
    float transmittance_min_light_elevation_deg;
    bool atmosphere_render_in_main_pass;
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
    float sky_light_intensity_mul;
    float sky_light_diffuse;
    float sky_light_specular;
    // Fog
    bool fog_enabled;
    int fog_model;
    float fog_extinction_sigma_t_per_m;
    float fog_height_falloff_per_m;
    float fog_height_offset_m;
    float fog_start_distance_m;
    float fog_max_opacity;
    glm::vec3 fog_single_scattering_albedo_rgb;

    // Exposure (PostProcess)
    bool exposure_enabled;
    int exposure_mode; // 0=Manual, 1=Auto, 2=ManualCamera
    float manual_ev;
  };

  constexpr std::array kEnvironmentPresets = {
    EnvironmentPresetData {
      .name = "Outdoor Sunny",
      .sun_enabled = true,
      .sun_azimuth_deg = 135.0F,
      .sun_elevation_deg = 55.0F,
      .sun_illuminance_lx = 120000.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 5600.0F,
      .sun_source_angle_deg = 0.545F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = true,
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.2F,
      .mie_anisotropy = 0.8F,
      .multi_scattering = 1.0F,
      .sky_and_aerial_luminance_factor = { 1.0F, 1.0F, 1.0F },
      .aerial_perspective_scale = 1.0F,
      .aerial_perspective_start_depth_m = 100.0F,
      .aerial_scattering_strength = 1.0F,
      .height_fog_contribution = 1.0F,
      .trace_sample_count_scale = 1.0F,
      .transmittance_min_light_elevation_deg = -90.0F,
      .atmosphere_render_in_main_pass = true,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomKm,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakKm,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopKm,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 1.0F, 1.0F, 1.0F },
      .sky_light_intensity_mul = 1.0F,
      .sky_light_diffuse = 1.0F,
      .sky_light_specular = 1.0F,
      .fog_enabled = false,
      .fog_model = 0,
      .fog_extinction_sigma_t_per_m = 0.002F,
      .fog_height_falloff_per_m = 0.02F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 1.0F,
      .fog_single_scattering_albedo_rgb = { 1.0F, 1.0F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 14.0F,
    },
    EnvironmentPresetData {
      .name = "Outdoor Cloudy",
      .sun_enabled = true,
      .sun_azimuth_deg = 135.0F,
      .sun_elevation_deg = 30.0F,
      .sun_illuminance_lx = 15000.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 6500.0F,
      .sun_source_angle_deg = 0.545F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = true,
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.5F,
      .mie_anisotropy = 0.75F,
      .multi_scattering = 1.2F,
      .sky_and_aerial_luminance_factor = { 1.0F, 1.0F, 1.0F },
      .aerial_perspective_scale = 1.0F,
      .aerial_perspective_start_depth_m = 100.0F,
      .aerial_scattering_strength = 1.1F,
      .height_fog_contribution = 1.0F,
      .trace_sample_count_scale = 1.0F,
      .transmittance_min_light_elevation_deg = -90.0F,
      .atmosphere_render_in_main_pass = true,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomKm,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakKm,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopKm,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 1.0F, 1.0F, 1.0F },
      .sky_light_intensity_mul = 1.2F,
      .sky_light_diffuse = 1.2F,
      .sky_light_specular = 0.7F,
      .fog_enabled = false,
      .fog_model = 0,
      .fog_extinction_sigma_t_per_m = 0.002F,
      .fog_height_falloff_per_m = 0.02F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 1.0F,
      .fog_single_scattering_albedo_rgb = { 1.0F, 1.0F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 12.0F,
    },
    EnvironmentPresetData {
      .name = "Foggy Daylight",
      .sun_enabled = true,
      .sun_azimuth_deg = 135.0F,
      .sun_elevation_deg = 45.0F,
      .sun_illuminance_lx = 60000.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 6000.0F,
      .sun_source_angle_deg = 0.545F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = false, // Sun disk obscured by fog usually
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.2F,
      .mie_anisotropy = 0.8F,
      .multi_scattering = 1.0F,
      .sky_and_aerial_luminance_factor = { 1.0F, 1.0F, 1.0F },
      .aerial_perspective_scale = 4.0F,
      .aerial_perspective_start_depth_m = 0.0F,
      .aerial_scattering_strength = 2.0F,
      .height_fog_contribution = 1.0F,
      .trace_sample_count_scale = 1.0F,
      .transmittance_min_light_elevation_deg = -90.0F,
      .atmosphere_render_in_main_pass = true,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomKm,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakKm,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopKm,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 0.9F, 0.95F, 1.0F },
      .sky_light_intensity_mul = 1.0F,
      .sky_light_diffuse = 1.0F,
      .sky_light_specular = 1.0F,
      .fog_enabled = true,
      .fog_model = 0, // Exponential
      .fog_extinction_sigma_t_per_m = 0.02F,
      .fog_height_falloff_per_m = 0.02F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 0.95F,
      .fog_single_scattering_albedo_rgb = { 0.9F, 0.95F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 13.0F,
    },
    EnvironmentPresetData {
      .name = "Outdoor Dawn",
      .sun_enabled = true,
      .sun_azimuth_deg = 95.0F,
      .sun_elevation_deg = 6.0F,
      .sun_illuminance_lx = 3000.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 3500.0F,
      .sun_source_angle_deg = 0.545F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = true,
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.2F,
      .mie_anisotropy = 0.8F,
      .multi_scattering = 1.0F,
      .sky_and_aerial_luminance_factor = { 1.0F, 1.0F, 1.0F },
      .aerial_perspective_scale = 1.0F,
      .aerial_perspective_start_depth_m = 100.0F,
      .aerial_scattering_strength = 1.0F,
      .height_fog_contribution = 1.0F,
      .trace_sample_count_scale = 1.0F,
      .transmittance_min_light_elevation_deg = -90.0F,
      .atmosphere_render_in_main_pass = true,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomKm,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakKm,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopKm,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 1.0F, 0.95F, 0.9F },
      .sky_light_intensity_mul = 0.6F,
      .sky_light_diffuse = 0.7F,
      .sky_light_specular = 0.5F,
      .fog_enabled = false,
      .fog_model = 0,
      .fog_extinction_sigma_t_per_m = 0.002F,
      .fog_height_falloff_per_m = 0.02F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 1.0F,
      .fog_single_scattering_albedo_rgb = { 1.0F, 1.0F, 1.0F },
      .exposure_enabled = true,
      .exposure_mode = 1, // Auto
      .manual_ev = 9.0F,
    },
    EnvironmentPresetData {
      .name = "Outdoor Dusk",
      .sun_enabled = true,
      .sun_azimuth_deg = 265.0F,
      .sun_elevation_deg = 4.0F,
      .sun_illuminance_lx = 1500.0F,
      .sun_use_temperature = true,
      .sun_temperature_kelvin = 3200.0F,
      .sun_source_angle_deg = 0.545F,
      .sky_atmo_enabled = true,
      .sky_atmo_sun_disk_enabled = true,
      .planet_radius_km = 6360.0F,
      .atmosphere_height_km = 100.0F,
      .ground_albedo = { 0.06F, 0.05F, 0.04F },
      .rayleigh_scale_height_km = 8.0F,
      .mie_scale_height_km = 1.2F,
      .mie_anisotropy = 0.8F,
      .multi_scattering = 1.0F,
      .sky_and_aerial_luminance_factor = { 1.0F, 1.0F, 1.0F },
      .aerial_perspective_scale = 1.0F,
      .aerial_perspective_start_depth_m = 100.0F,
      .aerial_scattering_strength = 1.0F,
      .height_fog_contribution = 1.0F,
      .trace_sample_count_scale = 1.0F,
      .transmittance_min_light_elevation_deg = -90.0F,
      .atmosphere_render_in_main_pass = true,
      .ozone_rgb = engine::atmos::kDefaultOzoneAbsorptionRgb,
      .ozone_bottom_km = engine::atmos::kDefaultOzoneBottomKm,
      .ozone_peak_km = engine::atmos::kDefaultOzonePeakKm,
      .ozone_top_km = engine::atmos::kDefaultOzoneTopKm,
      .sky_sphere_enabled = false,
      .sky_sphere_source = 0,
      .sky_sphere_color = { 0.0F, 0.0F, 0.0F },
      .sky_sphere_intensity = 1.0F,
      .sky_sphere_rotation_deg = 0.0F,
      .sky_light_enabled = true,
      .sky_light_source = 0,
      .sky_light_tint = { 0.95F, 0.92F, 0.9F },
      .sky_light_intensity_mul = 0.6F,
      .sky_light_diffuse = 0.7F,
      .sky_light_specular = 0.5F,
      .fog_enabled = false,
      .fog_model = 0,
      .fog_extinction_sigma_t_per_m = 0.002F,
      .fog_height_falloff_per_m = 0.02F,
      .fog_height_offset_m = 0.0F,
      .fog_start_distance_m = 0.0F,
      .fog_max_opacity = 1.0F,
      .fog_single_scattering_albedo_rgb = { 1.0F, 1.0F, 1.0F },
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
  const auto* scene_ptr = config.scene.get();
  const auto* skybox_service_ptr = config.skybox_service.get();
  const auto* renderer_ptr = config.renderer.get();
  const bool runtime_changed = !runtime_config_initialized_
    || last_runtime_config_scene_ != scene_ptr
    || last_runtime_config_skybox_service_ != skybox_service_ptr
    || last_runtime_config_renderer_ != renderer_ptr;

  // Always forward runtime config so the service can consume scene-activation
  // transitions even when pointer values are reused.
  service_->SetRuntimeConfig(config);

  if (runtime_changed) {
    last_runtime_config_scene_ = const_cast<scene::Scene*>(scene_ptr);
    last_runtime_config_skybox_service_
      = const_cast<SkyboxService*>(skybox_service_ptr);
    last_runtime_config_renderer_ = const_cast<vortex::Renderer*>(renderer_ptr);
    runtime_config_initialized_ = true;
  }
  MaybeApplyStartupPreset(config);
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
  return static_cast<int>(kEnvironmentPresets.size()) + 2;
}

auto EnvironmentVm::GetPresetName(int index) const -> std::string_view
{
  if (index == 0) {
    return "Use Scene";
  }
  if (index == 1) {
    return "Custom";
  }
  return GetPreset(index - 2).name;
}

auto EnvironmentVm::GetPresetLabel() const -> std::string_view
{
  return GetPresetName(GetPresetIndex());
}

auto EnvironmentVm::GetPresetIndex() const -> int
{
  const int stored = service_->GetPresetIndex();
  if (stored == kPresetUseScene) {
    return 0;
  }
  if (stored == kPresetCustom) {
    return 1;
  }
  return stored + 2;
}

auto EnvironmentVm::ApplyPreset(int index) -> void
{
  LOG_F(1, "ApplyPreset(ui_index={})", index);
  if (index == 0) {
    LOG_F(1, "preset mode -> Use Scene");
    service_->SetPresetIndex(kPresetUseScene);
    service_->ActivateUseSceneMode();
    startup_preset_applied_ = true;
    return;
  }

  const int preset_index = index - 2;
  const auto& preset = GetPreset(preset_index);
  LOG_F(1, "applying built-in preset (index={}, name='{}')", preset_index,
    preset.name.data());
  service_->BeginUpdate();
  applying_preset_ = true;
  service_->SetPresetIndex(preset_index);

  // 1. Disable all systems to prevent intermediate state updates
  SetSunEnabled(false);
  SetSkyAtmosphereEnabled(false);
  SetSkySphereEnabled(false);
  SetSkyLightEnabled(false);

  // 2. Configure systems
  // Sun
  SetSunAzimuthDeg(preset.sun_azimuth_deg);
  SetSunElevationDeg(preset.sun_elevation_deg);
  SetSunIlluminanceLx(preset.sun_illuminance_lx);
  SetSunUseTemperature(preset.sun_use_temperature);
  SetSunTemperatureKelvin(preset.sun_temperature_kelvin);
  SetSunSourceAngleDeg(preset.sun_source_angle_deg);
  SetSunAtmosphereLightSlot(
    static_cast<int>(scene::AtmosphereLightSlot::kPrimary));
  SetSunUsePerPixelAtmosphereTransmittance(false);
  SetSunAtmosphereDiskLuminanceScale({ 1.0F, 1.0F, 1.0F, 1.0F });

  // Sky Atmosphere
  SetSunDiskEnabled(preset.sky_atmo_sun_disk_enabled);
  SetPlanetRadiusKm(preset.planet_radius_km);
  SetAtmosphereHeightKm(preset.atmosphere_height_km);
  SetGroundAlbedo(preset.ground_albedo);
  SetRayleighScaleHeightKm(preset.rayleigh_scale_height_km);
  SetMieScaleHeightKm(preset.mie_scale_height_km);
  SetMieAnisotropy(preset.mie_anisotropy);
  SetMultiScattering(preset.multi_scattering);
  SetSkyAndAerialPerspectiveLuminanceFactor(
    preset.sky_and_aerial_luminance_factor);
  SetAerialPerspectiveScale(preset.aerial_perspective_scale);
  SetAerialPerspectiveStartDepthMeters(preset.aerial_perspective_start_depth_m);
  SetAerialScatteringStrength(preset.aerial_scattering_strength);
  SetHeightFogContribution(preset.height_fog_contribution);
  SetTraceSampleCountScale(preset.trace_sample_count_scale);
  SetTransmittanceMinLightElevationDeg(
    preset.transmittance_min_light_elevation_deg);
  SetAtmosphereRenderInMainPass(preset.atmosphere_render_in_main_pass);
  SetOzoneRgb(preset.ozone_rgb);
  // Presets use the canonical Earth-like ozone profile to stay physically
  // consistent with renderer assumptions and service validation bounds.
  SetOzoneDensityProfile(engine::atmos::kDefaultOzoneDensityProfile);

  // Sky Sphere
  SetSkySphereSource(preset.sky_sphere_source);
  SetSkySphereSolidColor(preset.sky_sphere_color);
  SetSkyIntensity(preset.sky_sphere_intensity);
  SetSkySphereRotationDeg(preset.sky_sphere_rotation_deg);

  // Sky Light
  SetSkyLightSource(preset.sky_light_source);
  SetSkyLightTint(preset.sky_light_tint);
  SetSkyLightIntensityMul(preset.sky_light_intensity_mul);
  SetSkyLightDiffuse(preset.sky_light_diffuse);
  SetSkyLightSpecular(preset.sky_light_specular);

  // Fog
  SetFogEnabled(preset.fog_enabled);
  SetFogModel(preset.fog_model);
  SetFogExtinctionSigmaTPerMeter(preset.fog_extinction_sigma_t_per_m);
  SetFogHeightFalloffPerMeter(preset.fog_height_falloff_per_m);
  SetFogHeightOffsetMeters(preset.fog_height_offset_m);
  SetFogStartDistanceMeters(preset.fog_start_distance_m);
  SetFogMaxOpacity(preset.fog_max_opacity);
  SetFogSingleScatteringAlbedoRgb(preset.fog_single_scattering_albedo_rgb);

  // 3. Re-enable systems in dependency order
  // Background
  SetSkyAtmosphereEnabled(preset.sky_atmo_enabled);
  SetSkySphereEnabled(preset.sky_sphere_enabled);

  // Direct Light
  SetSunEnabled(preset.sun_enabled);

  // Global Illumination (captures background + direct)
  SetSkyLightEnabled(preset.sky_light_enabled);

  service_->EndUpdate();
  applying_preset_ = false;

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

auto EnvironmentVm::MaybeApplyStartupPreset(
  const EnvironmentRuntimeConfig& config) -> void
{
  auto* scene_ptr = config.scene.get();
  if (scene_ptr != runtime_scene_) {
    runtime_scene_ = scene_ptr;
    startup_preset_applied_ = false;
    LOG_F(1, "runtime scene changed; reset startup preset latch");
  }

  if (runtime_scene_ == nullptr || startup_preset_applied_) {
    return;
  }

  const int preset_index = service_->GetPresetIndex();
  LOG_F(1, "startup preset evaluation (stored_index={})", preset_index);
  if (preset_index == kPresetUseScene) {
    LOG_F(1, "startup action -> Use Scene");
    service_->ActivateUseSceneMode();
    startup_preset_applied_ = true;
    return;
  }
  if (preset_index == kPresetCustom) {
    // Custom startup application is handled by EnvironmentSettingsService
    // (persisted cache if present, otherwise scene sync).
    LOG_F(1, "startup action -> Custom (service-managed)");
    startup_preset_applied_ = true;
    return;
  }

  const int preset_count = static_cast<int>(kEnvironmentPresets.size());
  if (preset_index >= 0 && preset_index < preset_count) {
    LOG_F(1, "startup action -> built-in preset (index={})", preset_index);
    ApplyPreset(preset_index + 2);
  } else {
    LOG_F(ERROR,
      "invalid persisted preset (index={}, valid_range=[0, {}]); keeping "
      "current scene state",
      preset_index, preset_count - 1);
  }
  startup_preset_applied_ = true;
}

auto EnvironmentVm::PrepareForManualOverride() -> void
{
  if (service_ == nullptr) {
    return;
  }
  if (applying_preset_) {
    return;
  }
  if (service_->GetPresetIndex() != kPresetCustom) {
    service_->ActivateCustomMode();
  }
}

auto EnvironmentVm::GetSkyAtmosphereEnabled() const -> bool
{
  return service_->GetSkyAtmosphereEnabled();
}

auto EnvironmentVm::SetSkyAtmosphereEnabled(bool enabled) -> void
{
  PrepareForManualOverride();
  service_->SetSkyAtmosphereEnabled(enabled);
}

auto EnvironmentVm::GetSkyAtmosphereTransformMode() const -> int
{
  return service_->GetSkyAtmosphereTransformMode();
}

auto EnvironmentVm::SetSkyAtmosphereTransformMode(int value) -> void
{
  service_->SetSkyAtmosphereTransformMode(value);
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

auto EnvironmentVm::GetSkyLuminanceFactor() const -> glm::vec3
{
  return service_->GetSkyLuminanceFactor();
}

auto EnvironmentVm::SetSkyLuminanceFactor(const glm::vec3& value) -> void
{
  service_->SetSkyLuminanceFactor(value);
}

auto EnvironmentVm::GetSkyAndAerialPerspectiveLuminanceFactor() const
  -> glm::vec3
{
  return service_->GetSkyAndAerialPerspectiveLuminanceFactor();
}

auto EnvironmentVm::SetSkyAndAerialPerspectiveLuminanceFactor(
  const glm::vec3& value) -> void
{
  PrepareForManualOverride();
  service_->SetSkyAndAerialPerspectiveLuminanceFactor(value);
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
  PrepareForManualOverride();
  service_->SetAerialPerspectiveScale(value);
}

auto EnvironmentVm::GetAerialPerspectiveEnabled() const -> bool
{
  return service_->GetAerialPerspectiveEnabled();
}

auto EnvironmentVm::SetAerialPerspectiveEnabled(bool enabled) -> void
{
  PrepareForManualOverride();
  service_->SetAerialPerspectiveEnabled(enabled);
}

auto EnvironmentVm::GetAerialPerspectiveStartDepthMeters() const -> float
{
  return service_->GetAerialPerspectiveStartDepthMeters();
}

auto EnvironmentVm::SetAerialPerspectiveStartDepthMeters(float value) -> void
{
  PrepareForManualOverride();
  service_->SetAerialPerspectiveStartDepthMeters(value);
}

auto EnvironmentVm::GetAerialScatteringStrength() const -> float
{
  return service_->GetAerialScatteringStrength();
}

auto EnvironmentVm::SetAerialScatteringStrength(float value) -> void
{
  PrepareForManualOverride();
  service_->SetAerialScatteringStrength(value);
}

auto EnvironmentVm::GetHeightFogContribution() const -> float
{
  return service_->GetHeightFogContribution();
}

auto EnvironmentVm::SetHeightFogContribution(float value) -> void
{
  PrepareForManualOverride();
  service_->SetHeightFogContribution(value);
}

auto EnvironmentVm::GetTraceSampleCountScale() const -> float
{
  return service_->GetTraceSampleCountScale();
}

auto EnvironmentVm::SetTraceSampleCountScale(float value) -> void
{
  PrepareForManualOverride();
  service_->SetTraceSampleCountScale(value);
}

auto EnvironmentVm::GetTransmittanceMinLightElevationDeg() const -> float
{
  return service_->GetTransmittanceMinLightElevationDeg();
}

auto EnvironmentVm::SetTransmittanceMinLightElevationDeg(float value) -> void
{
  PrepareForManualOverride();
  service_->SetTransmittanceMinLightElevationDeg(value);
}

auto EnvironmentVm::GetAtmosphereHoldout() const -> bool
{
  return service_->GetAtmosphereHoldout();
}

auto EnvironmentVm::SetAtmosphereHoldout(bool enabled) -> void
{
  service_->SetAtmosphereHoldout(enabled);
}

auto EnvironmentVm::GetAtmosphereRenderInMainPass() const -> bool
{
  return service_->GetAtmosphereRenderInMainPass();
}

auto EnvironmentVm::SetAtmosphereRenderInMainPass(bool enabled) -> void
{
  PrepareForManualOverride();
  service_->SetAtmosphereRenderInMainPass(enabled);
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

auto EnvironmentVm::GetAerialPerspectiveLutWidth() const -> int
{
  return service_->GetAerialPerspectiveLutWidth();
}

auto EnvironmentVm::GetAerialPerspectiveLutDepthResolution() const -> int
{
  return service_->GetAerialPerspectiveLutDepthResolution();
}

auto EnvironmentVm::GetAerialPerspectiveLutDepthKm() const -> float
{
  return service_->GetAerialPerspectiveLutDepthKm();
}

auto EnvironmentVm::GetAerialPerspectiveLutSampleCountMaxPerSlice() const
  -> float
{
  return service_->GetAerialPerspectiveLutSampleCountMaxPerSlice();
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
  PrepareForManualOverride();
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

auto EnvironmentVm::GetSkyLightIntensityMul() const -> float
{
  return service_->GetSkyLightIntensityMul();
}

auto EnvironmentVm::SetSkyLightIntensityMul(const float value) -> void
{
  service_->SetSkyLightIntensityMul(value);
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

auto EnvironmentVm::GetSkyLightRealTimeCaptureEnabled() const -> bool
{
  return service_->GetSkyLightRealTimeCaptureEnabled();
}

auto EnvironmentVm::SetSkyLightRealTimeCaptureEnabled(bool enabled) -> void
{
  service_->SetSkyLightRealTimeCaptureEnabled(enabled);
}

auto EnvironmentVm::GetSkyLightLowerHemisphereColor() const -> glm::vec3
{
  return service_->GetSkyLightLowerHemisphereColor();
}

auto EnvironmentVm::SetSkyLightLowerHemisphereColor(const glm::vec3& value)
  -> void
{
  service_->SetSkyLightLowerHemisphereColor(value);
}

auto EnvironmentVm::GetSkyLightVolumetricScatteringIntensity() const -> float
{
  return service_->GetSkyLightVolumetricScatteringIntensity();
}

auto EnvironmentVm::SetSkyLightVolumetricScatteringIntensity(float value)
  -> void
{
  service_->SetSkyLightVolumetricScatteringIntensity(value);
}

auto EnvironmentVm::GetSkyLightAffectReflections() const -> bool
{
  return service_->GetSkyLightAffectReflections();
}

auto EnvironmentVm::SetSkyLightAffectReflections(bool enabled) -> void
{
  service_->SetSkyLightAffectReflections(enabled);
}

auto EnvironmentVm::GetSkyLightAffectGlobalIllumination() const -> bool
{
  return service_->GetSkyLightAffectGlobalIllumination();
}

auto EnvironmentVm::SetSkyLightAffectGlobalIllumination(bool enabled) -> void
{
  service_->SetSkyLightAffectGlobalIllumination(enabled);
}

auto EnvironmentVm::GetFogEnabled() const -> bool
{
  return service_->GetFogEnabled();
}

auto EnvironmentVm::SetFogEnabled(const bool enabled) -> void
{
  PrepareForManualOverride();
  service_->SetFogEnabled(enabled);
}

auto EnvironmentVm::GetHeightFogPassRequested() const -> bool
{
  return service_->GetHeightFogPassRequested();
}

auto EnvironmentVm::GetFogModel() const -> int
{
  return service_->GetFogModel();
}

auto EnvironmentVm::SetFogModel(const int model) -> void
{
  service_->SetFogModel(model);
}

auto EnvironmentVm::GetFogExtinctionSigmaTPerMeter() const -> float
{
  return service_->GetFogExtinctionSigmaTPerMeter();
}

auto EnvironmentVm::SetFogExtinctionSigmaTPerMeter(const float value) -> void
{
  service_->SetFogExtinctionSigmaTPerMeter(value);
}

auto EnvironmentVm::GetFogHeightFalloffPerMeter() const -> float
{
  return service_->GetFogHeightFalloffPerMeter();
}

auto EnvironmentVm::SetFogHeightFalloffPerMeter(const float value) -> void
{
  service_->SetFogHeightFalloffPerMeter(value);
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

auto EnvironmentVm::GetSecondFogDensity() const -> float
{
  return service_->GetSecondFogDensity();
}

auto EnvironmentVm::SetSecondFogDensity(const float value) -> void
{
  service_->SetSecondFogDensity(value);
}

auto EnvironmentVm::GetSecondFogHeightFalloff() const -> float
{
  return service_->GetSecondFogHeightFalloff();
}

auto EnvironmentVm::SetSecondFogHeightFalloff(const float value) -> void
{
  service_->SetSecondFogHeightFalloff(value);
}

auto EnvironmentVm::GetSecondFogHeightOffset() const -> float
{
  return service_->GetSecondFogHeightOffset();
}

auto EnvironmentVm::SetSecondFogHeightOffset(const float value) -> void
{
  service_->SetSecondFogHeightOffset(value);
}

auto EnvironmentVm::GetFogMaxOpacity() const -> float
{
  return service_->GetFogMaxOpacity();
}

auto EnvironmentVm::SetFogMaxOpacity(const float value) -> void
{
  service_->SetFogMaxOpacity(value);
}

auto EnvironmentVm::GetFogSingleScatteringAlbedoRgb() const -> glm::vec3
{
  return service_->GetFogSingleScatteringAlbedoRgb();
}

auto EnvironmentVm::SetFogSingleScatteringAlbedoRgb(const glm::vec3& value)
  -> void
{
  service_->SetFogSingleScatteringAlbedoRgb(value);
}

auto EnvironmentVm::GetFogInscatteringLuminance() const -> glm::vec3
{
  return service_->GetFogInscatteringLuminance();
}

auto EnvironmentVm::SetFogInscatteringLuminance(const glm::vec3& value) -> void
{
  service_->SetFogInscatteringLuminance(value);
}

auto EnvironmentVm::GetSkyAtmosphereAmbientContributionColorScale() const
  -> glm::vec3
{
  return service_->GetSkyAtmosphereAmbientContributionColorScale();
}

auto EnvironmentVm::SetSkyAtmosphereAmbientContributionColorScale(
  const glm::vec3& value) -> void
{
  service_->SetSkyAtmosphereAmbientContributionColorScale(value);
}

auto EnvironmentVm::GetInscatteringColorCubemapAngle() const -> float
{
  return service_->GetInscatteringColorCubemapAngle();
}

auto EnvironmentVm::SetInscatteringColorCubemapAngle(const float value) -> void
{
  service_->SetInscatteringColorCubemapAngle(value);
}

auto EnvironmentVm::GetInscatteringTextureTint() const -> glm::vec3
{
  return service_->GetInscatteringTextureTint();
}

auto EnvironmentVm::SetInscatteringTextureTint(const glm::vec3& value) -> void
{
  service_->SetInscatteringTextureTint(value);
}

auto EnvironmentVm::GetFullyDirectionalInscatteringColorDistance() const
  -> float
{
  return service_->GetFullyDirectionalInscatteringColorDistance();
}

auto EnvironmentVm::SetFullyDirectionalInscatteringColorDistance(
  const float value) -> void
{
  service_->SetFullyDirectionalInscatteringColorDistance(value);
}

auto EnvironmentVm::GetNonDirectionalInscatteringColorDistance() const -> float
{
  return service_->GetNonDirectionalInscatteringColorDistance();
}

auto EnvironmentVm::SetNonDirectionalInscatteringColorDistance(
  const float value) -> void
{
  service_->SetNonDirectionalInscatteringColorDistance(value);
}

auto EnvironmentVm::GetDirectionalInscatteringLuminance() const -> glm::vec3
{
  return service_->GetDirectionalInscatteringLuminance();
}

auto EnvironmentVm::SetDirectionalInscatteringLuminance(const glm::vec3& value)
  -> void
{
  service_->SetDirectionalInscatteringLuminance(value);
}

auto EnvironmentVm::GetDirectionalInscatteringExponent() const -> float
{
  return service_->GetDirectionalInscatteringExponent();
}

auto EnvironmentVm::SetDirectionalInscatteringExponent(const float value)
  -> void
{
  service_->SetDirectionalInscatteringExponent(value);
}

auto EnvironmentVm::GetDirectionalInscatteringStartDistance() const -> float
{
  return service_->GetDirectionalInscatteringStartDistance();
}

auto EnvironmentVm::SetDirectionalInscatteringStartDistance(const float value)
  -> void
{
  service_->SetDirectionalInscatteringStartDistance(value);
}

auto EnvironmentVm::GetFogEndDistanceMeters() const -> float
{
  return service_->GetFogEndDistanceMeters();
}

auto EnvironmentVm::SetFogEndDistanceMeters(const float value) -> void
{
  service_->SetFogEndDistanceMeters(value);
}

auto EnvironmentVm::GetFogCutoffDistanceMeters() const -> float
{
  return service_->GetFogCutoffDistanceMeters();
}

auto EnvironmentVm::SetFogCutoffDistanceMeters(const float value) -> void
{
  service_->SetFogCutoffDistanceMeters(value);
}

auto EnvironmentVm::GetVolumetricFogScatteringDistribution() const -> float
{
  return service_->GetVolumetricFogScatteringDistribution();
}

auto EnvironmentVm::SetVolumetricFogScatteringDistribution(const float value)
  -> void
{
  service_->SetVolumetricFogScatteringDistribution(value);
}

auto EnvironmentVm::GetVolumetricFogAlbedo() const -> glm::vec3
{
  return service_->GetVolumetricFogAlbedo();
}

auto EnvironmentVm::SetVolumetricFogAlbedo(const glm::vec3& value) -> void
{
  service_->SetVolumetricFogAlbedo(value);
}

auto EnvironmentVm::GetVolumetricFogEmissive() const -> glm::vec3
{
  return service_->GetVolumetricFogEmissive();
}

auto EnvironmentVm::SetVolumetricFogEmissive(const glm::vec3& value) -> void
{
  service_->SetVolumetricFogEmissive(value);
}

auto EnvironmentVm::GetVolumetricFogExtinctionScale() const -> float
{
  return service_->GetVolumetricFogExtinctionScale();
}

auto EnvironmentVm::SetVolumetricFogExtinctionScale(const float value) -> void
{
  service_->SetVolumetricFogExtinctionScale(value);
}

auto EnvironmentVm::GetVolumetricFogDistanceMeters() const -> float
{
  return service_->GetVolumetricFogDistanceMeters();
}

auto EnvironmentVm::SetVolumetricFogDistanceMeters(const float value) -> void
{
  service_->SetVolumetricFogDistanceMeters(value);
}

auto EnvironmentVm::GetVolumetricFogStartDistanceMeters() const -> float
{
  return service_->GetVolumetricFogStartDistanceMeters();
}

auto EnvironmentVm::SetVolumetricFogStartDistanceMeters(const float value)
  -> void
{
  service_->SetVolumetricFogStartDistanceMeters(value);
}

auto EnvironmentVm::GetVolumetricFogNearFadeInDistanceMeters() const -> float
{
  return service_->GetVolumetricFogNearFadeInDistanceMeters();
}

auto EnvironmentVm::SetVolumetricFogNearFadeInDistanceMeters(const float value)
  -> void
{
  service_->SetVolumetricFogNearFadeInDistanceMeters(value);
}

auto EnvironmentVm::GetVolumetricFogStaticLightingScatteringIntensity() const
  -> float
{
  return service_->GetVolumetricFogStaticLightingScatteringIntensity();
}

auto EnvironmentVm::SetVolumetricFogStaticLightingScatteringIntensity(
  const float value) -> void
{
  service_->SetVolumetricFogStaticLightingScatteringIntensity(value);
}

auto EnvironmentVm::GetOverrideLightColorsWithFogInscatteringColors() const
  -> bool
{
  return service_->GetOverrideLightColorsWithFogInscatteringColors();
}

auto EnvironmentVm::SetOverrideLightColorsWithFogInscatteringColors(
  const bool enabled) -> void
{
  service_->SetOverrideLightColorsWithFogInscatteringColors(enabled);
}

auto EnvironmentVm::GetFogHoldout() const -> bool
{
  return service_->GetFogHoldout();
}

auto EnvironmentVm::SetFogHoldout(const bool enabled) -> void
{
  service_->SetFogHoldout(enabled);
}

auto EnvironmentVm::GetFogRenderInMainPass() const -> bool
{
  return service_->GetFogRenderInMainPass();
}

auto EnvironmentVm::SetFogRenderInMainPass(const bool enabled) -> void
{
  service_->SetFogRenderInMainPass(enabled);
}

auto EnvironmentVm::GetFogVisibleInReflectionCaptures() const -> bool
{
  return service_->GetFogVisibleInReflectionCaptures();
}

auto EnvironmentVm::SetFogVisibleInReflectionCaptures(const bool enabled)
  -> void
{
  service_->SetFogVisibleInReflectionCaptures(enabled);
}

auto EnvironmentVm::GetFogVisibleInRealTimeSkyCaptures() const -> bool
{
  return service_->GetFogVisibleInRealTimeSkyCaptures();
}

auto EnvironmentVm::SetFogVisibleInRealTimeSkyCaptures(const bool enabled)
  -> void
{
  service_->SetFogVisibleInRealTimeSkyCaptures(enabled);
}

auto EnvironmentVm::GetLocalFogVolumeCount() const -> int
{
  return service_->GetLocalFogVolumeCount();
}

auto EnvironmentVm::GetSelectedLocalFogVolumeIndex() const -> int
{
  return service_->GetSelectedLocalFogVolumeIndex();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeIndex(const int index) -> void
{
  service_->SetSelectedLocalFogVolumeIndex(index);
}

auto EnvironmentVm::AddLocalFogVolume() -> void
{
  service_->AddLocalFogVolume();
}

auto EnvironmentVm::RemoveSelectedLocalFogVolume() -> void
{
  service_->RemoveSelectedLocalFogVolume();
}

auto EnvironmentVm::GetSelectedLocalFogVolumeEnabled() const -> bool
{
  return service_->GetSelectedLocalFogVolumeEnabled();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeEnabled(const bool enabled) -> void
{
  service_->SetSelectedLocalFogVolumeEnabled(enabled);
}

auto EnvironmentVm::GetSelectedLocalFogVolumeRadialFogExtinction() const
  -> float
{
  return service_->GetSelectedLocalFogVolumeRadialFogExtinction();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeRadialFogExtinction(
  const float value) -> void
{
  service_->SetSelectedLocalFogVolumeRadialFogExtinction(value);
}

auto EnvironmentVm::GetSelectedLocalFogVolumeHeightFogExtinction() const
  -> float
{
  return service_->GetSelectedLocalFogVolumeHeightFogExtinction();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeHeightFogExtinction(
  const float value) -> void
{
  service_->SetSelectedLocalFogVolumeHeightFogExtinction(value);
}

auto EnvironmentVm::GetSelectedLocalFogVolumeHeightFogFalloff() const -> float
{
  return service_->GetSelectedLocalFogVolumeHeightFogFalloff();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeHeightFogFalloff(const float value)
  -> void
{
  service_->SetSelectedLocalFogVolumeHeightFogFalloff(value);
}

auto EnvironmentVm::GetSelectedLocalFogVolumeHeightFogOffset() const -> float
{
  return service_->GetSelectedLocalFogVolumeHeightFogOffset();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeHeightFogOffset(const float value)
  -> void
{
  service_->SetSelectedLocalFogVolumeHeightFogOffset(value);
}

auto EnvironmentVm::GetSelectedLocalFogVolumeFogPhaseG() const -> float
{
  return service_->GetSelectedLocalFogVolumeFogPhaseG();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeFogPhaseG(const float value)
  -> void
{
  service_->SetSelectedLocalFogVolumeFogPhaseG(value);
}

auto EnvironmentVm::GetSelectedLocalFogVolumeFogAlbedo() const -> glm::vec3
{
  return service_->GetSelectedLocalFogVolumeFogAlbedo();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeFogAlbedo(const glm::vec3& value)
  -> void
{
  service_->SetSelectedLocalFogVolumeFogAlbedo(value);
}

auto EnvironmentVm::GetSelectedLocalFogVolumeFogEmissive() const -> glm::vec3
{
  return service_->GetSelectedLocalFogVolumeFogEmissive();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeFogEmissive(const glm::vec3& value)
  -> void
{
  service_->SetSelectedLocalFogVolumeFogEmissive(value);
}

auto EnvironmentVm::GetSelectedLocalFogVolumeSortPriority() const -> int
{
  return service_->GetSelectedLocalFogVolumeSortPriority();
}

auto EnvironmentVm::SetSelectedLocalFogVolumeSortPriority(const int value)
  -> void
{
  service_->SetSelectedLocalFogVolumeSortPriority(value);
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
  PrepareForManualOverride();
  service_->SetSunEnabled(enabled);
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

auto EnvironmentVm::GetSunSourceAngleDeg() const -> float
{
  return service_->GetSunSourceAngleDeg();
}

auto EnvironmentVm::SetSunSourceAngleDeg(float value) -> void
{
  service_->SetSunSourceAngleDeg(value);
}

auto EnvironmentVm::GetSunAtmosphereLightSlot() const -> int
{
  return service_->GetSunAtmosphereLightSlot();
}

auto EnvironmentVm::SetSunAtmosphereLightSlot(int value) -> void
{
  service_->SetSunAtmosphereLightSlot(value);
}

auto EnvironmentVm::GetSunUsePerPixelAtmosphereTransmittance() const -> bool
{
  return service_->GetSunUsePerPixelAtmosphereTransmittance();
}

auto EnvironmentVm::SetSunUsePerPixelAtmosphereTransmittance(bool enabled)
  -> void
{
  service_->SetSunUsePerPixelAtmosphereTransmittance(enabled);
}

auto EnvironmentVm::GetSunAtmosphereDiskLuminanceScale() const -> glm::vec4
{
  return service_->GetSunAtmosphereDiskLuminanceScale();
}

auto EnvironmentVm::SetSunAtmosphereDiskLuminanceScale(const glm::vec4& value)
  -> void
{
  service_->SetSunAtmosphereDiskLuminanceScale(value);
}

auto EnvironmentVm::GetSunShadowBias() const -> float
{
  return service_->GetSunShadowBias();
}

auto EnvironmentVm::SetSunShadowBias(float value) -> void
{
  service_->SetSunShadowBias(value);
}

auto EnvironmentVm::GetSunShadowNormalBias() const -> float
{
  return service_->GetSunShadowNormalBias();
}

auto EnvironmentVm::SetSunShadowNormalBias(float value) -> void
{
  service_->SetSunShadowNormalBias(value);
}

auto EnvironmentVm::GetSunShadowResolutionHint() const -> int
{
  return service_->GetSunShadowResolutionHint();
}

auto EnvironmentVm::SetSunShadowResolutionHint(int value) -> void
{
  service_->SetSunShadowResolutionHint(value);
}

auto EnvironmentVm::GetSunShadowCascadeCount() const -> int
{
  return service_->GetSunShadowCascadeCount();
}

auto EnvironmentVm::SetSunShadowCascadeCount(int value) -> void
{
  service_->SetSunShadowCascadeCount(value);
}

auto EnvironmentVm::GetSunShadowSplitMode() const -> int
{
  return service_->GetSunShadowSplitMode();
}

auto EnvironmentVm::SetSunShadowSplitMode(int value) -> void
{
  service_->SetSunShadowSplitMode(value);
}

auto EnvironmentVm::GetSunShadowMaxDistance() const -> float
{
  return service_->GetSunShadowMaxDistance();
}

auto EnvironmentVm::SetSunShadowMaxDistance(float value) -> void
{
  service_->SetSunShadowMaxDistance(value);
}

auto EnvironmentVm::GetSunShadowDistributionExponent() const -> float
{
  return service_->GetSunShadowDistributionExponent();
}

auto EnvironmentVm::SetSunShadowDistributionExponent(float value) -> void
{
  service_->SetSunShadowDistributionExponent(value);
}

auto EnvironmentVm::GetSunShadowTransitionFraction() const -> float
{
  return service_->GetSunShadowTransitionFraction();
}

auto EnvironmentVm::SetSunShadowTransitionFraction(float value) -> void
{
  service_->SetSunShadowTransitionFraction(value);
}

auto EnvironmentVm::GetSunShadowDistanceFadeoutFraction() const -> float
{
  return service_->GetSunShadowDistanceFadeoutFraction();
}

auto EnvironmentVm::SetSunShadowDistanceFadeoutFraction(float value) -> void
{
  service_->SetSunShadowDistanceFadeoutFraction(value);
}

auto EnvironmentVm::GetSunShadowCascadeDistance(const int index) const -> float
{
  return service_->GetSunShadowCascadeDistance(index);
}

auto EnvironmentVm::SetSunShadowCascadeDistance(int index, float value) -> void
{
  service_->SetSunShadowCascadeDistance(index, value);
}

auto EnvironmentVm::GetSunLightAvailable() const -> bool
{
  return service_->GetSunLightAvailable();
}

auto EnvironmentVm::UpdateSunLightCandidate() -> void
{
  service_->UpdateSunLightCandidate();
}

auto EnvironmentVm::GetUseLut() const -> bool { return service_->GetUseLut(); }

auto EnvironmentVm::SetUseLut(bool enabled) -> void
{
  service_->SetUseLut(enabled);
}

} // namespace oxygen::examples::ui
