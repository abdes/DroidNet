//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/SkyboxService.h"

namespace oxygen::examples {

namespace {

  constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0F;
  constexpr float kRadToDeg = 180.0F / std::numbers::pi_v<float>;
  constexpr float kMetersToKm = 0.001F;
  constexpr float kKmToMeters = 1000.0F;

  enum class AtmosphereDebugFlags : uint32_t {
    kNone = 0x0,
    kUseLut = 0x1,
    kVisualizeLut = 0x2,
    kForceAnalytic = 0x4,
  };

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
        0.39008157877F * std::log(temp) - 0.63184144378F, 0.0F, 1.0F);
      if (temp <= 19.0F) {
        blue = 0.0F;
      } else {
        blue = std::clamp(
          0.54320678911F * std::log(temp - 10.0F) - 1.19625408914F, 0.0F, 1.0F);
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
    constexpr glm::vec3 from_dir(0.0F, -1.0F, 0.0F);
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

  constexpr std::string_view kSkyAtmoEnabledKey = "env.atmo.enabled";
  constexpr std::string_view kPlanetRadiusKey = "env.atmo.planet_radius_km";
  constexpr std::string_view kAtmosphereHeightKey
    = "env.atmo.atmosphere_height_km";
  constexpr std::string_view kGroundAlbedoKey = "env.atmo.ground_albedo";
  constexpr std::string_view kRayleighScaleHeightKey
    = "env.atmo.rayleigh_scale_height_km";
  constexpr std::string_view kMieScaleHeightKey
    = "env.atmo.mie_scale_height_km";
  constexpr std::string_view kMieAnisotropyKey = "env.atmo.mie_anisotropy";
  constexpr std::string_view kMultiScatteringKey = "env.atmo.multi_scattering";
  constexpr std::string_view kSunDiskEnabledKey = "env.atmo.sun_disk_enabled";
  constexpr std::string_view kAtmoSunDiskRadiusKey
    = "env.atmo.sun_disk_radius_deg";
  constexpr std::string_view kAerialPerspectiveScaleKey
    = "env.atmo.aerial_perspective_scale";
  constexpr std::string_view kAerialScatteringStrengthKey
    = "env.atmo.aerial_scattering_strength";

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
  constexpr std::string_view kSkyLightIntensityKey = "env.sky_light.intensity";
  constexpr std::string_view kSkyLightDiffuseKey = "env.sky_light.diffuse";
  constexpr std::string_view kSkyLightSpecularKey = "env.sky_light.specular";

  constexpr std::string_view kSunEnabledKey = "env.sun.enabled";
  constexpr std::string_view kSunSourceKey = "env.sun.source";
  constexpr std::string_view kSunAzimuthKey = "env.sun.azimuth_deg";
  constexpr std::string_view kSunElevationKey = "env.sun.elevation_deg";
  constexpr std::string_view kSunColorKey = "env.sun.color";
  constexpr std::string_view kSunIntensityKey = "env.sun.intensity_lux";
  constexpr std::string_view kSunUseTemperatureKey = "env.sun.use_temperature";
  constexpr std::string_view kSunTemperatureKey = "env.sun.temperature_kelvin";
  constexpr std::string_view kSunDiskRadiusKey = "env.sun.disk_radius_deg";

  constexpr std::string_view kUseLutKey = "env.debug.use_lut";
  constexpr std::string_view kVisualizeLutKey = "env.debug.visualize_lut";
  constexpr std::string_view kForceAnalyticKey = "env.debug.force_analytic";

} // namespace

auto EnvironmentSettingsService::SetRuntimeConfig(
  const EnvironmentRuntimeConfig& config) -> void
{
  const bool scene_changed = config_.scene.get() != config.scene.get();
  config_ = config;

  if (!settings_loaded_) {
    LoadSettings();
  }

  SyncDebugFlagsFromRenderer();

  if (scene_changed) {
    needs_sync_ = true;
    apply_saved_sun_on_next_sync_ = true;
    if (config_.scene && pending_changes_) {
      ApplyPendingChanges();
    }
    SyncFromSceneIfNeeded();
  }
}

auto EnvironmentSettingsService::HasScene() const noexcept -> bool
{
  return config_.scene != nullptr;
}

auto EnvironmentSettingsService::RequestResync() -> void { needs_sync_ = true; }

auto EnvironmentSettingsService::SyncFromSceneIfNeeded() -> void
{
  if (!needs_sync_) {
    return;
  }

  SyncFromScene();
  needs_sync_ = false;
}

auto EnvironmentSettingsService::HasPendingChanges() const -> bool
{
  return pending_changes_ && !needs_sync_;
}

auto EnvironmentSettingsService::GetAtmosphereLutStatus() const
  -> std::pair<bool, bool>
{
  bool luts_valid = false;
  bool luts_dirty = true;

  if (config_.renderer) {
    if (auto lut_mgr = config_.renderer->GetSkyAtmosphereLutManager()) {
      luts_valid = lut_mgr->HasBeenGenerated();
      luts_dirty = lut_mgr->IsDirty();
    }
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
  if (enabled) {
    sky_sphere_enabled_ = false;
  }
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSunDiskEnabled() const -> bool
{
  return sun_disk_enabled_;
}

auto EnvironmentSettingsService::SetSunDiskEnabled(bool enabled) -> void
{
  if (sun_disk_enabled_ == enabled) {
    return;
  }
  sun_disk_enabled_ = enabled;
  MarkDirty();
}

auto EnvironmentSettingsService::GetAtmosphereSunDiskRadiusDeg() const -> float
{
  return sun_disk_radius_deg_;
}

auto EnvironmentSettingsService::SetAtmosphereSunDiskRadiusDeg(float value)
  -> void
{
  if (sun_disk_radius_deg_ == value) {
    return;
  }
  sun_disk_radius_deg_ = value;
  MarkDirty();
}

auto EnvironmentSettingsService::GetAerialPerspectiveScale() const -> float
{
  return aerial_perspective_scale_;
}

auto EnvironmentSettingsService::SetAerialPerspectiveScale(float value) -> void
{
  if (aerial_perspective_scale_ == value) {
    return;
  }
  aerial_perspective_scale_ = value;
  MarkDirty();
}

auto EnvironmentSettingsService::GetAerialScatteringStrength() const -> float
{
  return aerial_scattering_strength_;
}

auto EnvironmentSettingsService::SetAerialScatteringStrength(float value)
  -> void
{
  if (aerial_scattering_strength_ == value) {
    return;
  }
  aerial_scattering_strength_ = value;
  MarkDirty();
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
  if (enabled) {
    sky_atmo_enabled_ = false;
  }
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSkySphereIntensity() const -> float
{
  return sky_sphere_intensity_;
}

auto EnvironmentSettingsService::SetSkySphereIntensity(float value) -> void
{
  if (sky_sphere_intensity_ == value) {
    return;
  }
  sky_sphere_intensity_ = value;
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSkyboxHdrExposureEv() const -> float
{
  return skybox_hdr_exposure_ev_;
}

auto EnvironmentSettingsService::SetSkyboxHdrExposureEv(float value) -> void
{
  if (skybox_hdr_exposure_ev_ == value) {
    return;
  }
  skybox_hdr_exposure_ev_ = value;
  MarkDirty();
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
    { .intensity = sky_light_intensity_,
      .diffuse_intensity = sky_light_diffuse_,
      .specular_intensity = sky_light_specular_,
      .tint_rgb = sky_light_tint_ },
    [this](SkyboxService::LoadResult result) {
      skybox_status_message_ = result.status_message;
      skybox_last_face_size_ = result.face_size;
      skybox_last_resource_key_ = result.resource_key;
      if (result.success) {
        RequestResync();
      }
    });
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSkyLightIntensity() const -> float
{
  return sky_light_intensity_;
}

auto EnvironmentSettingsService::SetSkyLightIntensity(float value) -> void
{
  if (sky_light_intensity_ == value) {
    return;
  }
  sky_light_intensity_ = value;
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSunPresent() const -> bool
{
  return sun_present_;
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSunSource() const -> int
{
  return sun_source_;
}

auto EnvironmentSettingsService::SetSunSource(int source) -> void
{
  if (sun_source_ == source) {
    return;
  }

  SaveSunSettingsToProfile(sun_source_);
  sun_source_ = source;
  LoadSunSettingsFromProfile(sun_source_);
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSunIntensityLux() const -> float
{
  return sun_intensity_lux_;
}

auto EnvironmentSettingsService::SetSunIntensityLux(float value) -> void
{
  if (sun_intensity_lux_ == value) {
    return;
  }
  sun_intensity_lux_ = value;
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSunLightAvailable() const -> bool
{
  return sun_light_available_;
}

auto EnvironmentSettingsService::UpdateSunLightCandidate() -> void
{
  sun_light_available_ = false;
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

auto EnvironmentSettingsService::EnableSyntheticSun() -> void
{
  sun_present_ = true;
  SetSunSource(1);
}

auto EnvironmentSettingsService::GetUseLut() const -> bool { return use_lut_; }

auto EnvironmentSettingsService::SetUseLut(bool enabled) -> void
{
  if (use_lut_ == enabled && !force_analytic_) {
    return;
  }
  use_lut_ = enabled;
  force_analytic_ = false;
  MarkDirty();
}

auto EnvironmentSettingsService::GetVisualizeLut() const -> bool
{
  return visualize_lut_;
}

auto EnvironmentSettingsService::SetVisualizeLut(bool enabled) -> void
{
  if (visualize_lut_ == enabled) {
    return;
  }
  visualize_lut_ = enabled;
  MarkDirty();
}

auto EnvironmentSettingsService::GetForceAnalytic() const -> bool
{
  return force_analytic_;
}

auto EnvironmentSettingsService::SetForceAnalytic(bool enabled) -> void
{
  if (force_analytic_ == enabled) {
    return;
  }
  force_analytic_ = enabled;
  if (enabled) {
    use_lut_ = true;
  }
  MarkDirty();
}

auto EnvironmentSettingsService::ApplyPendingChanges() -> void
{
  if (!pending_changes_ || !config_.scene) {
    return;
  }

  auto* env = config_.scene->GetEnvironment().get();
  if (!env) {
    config_.scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    env = config_.scene->GetEnvironment().get();
  }

  auto* sun = env->TryGetSystem<scene::environment::Sun>().get();
  if (sun_present_ && !sun) {
    sun = &env->AddSystem<scene::environment::Sun>();
  }
  if (sun) {
    sun->SetEnabled(sun_enabled_);
    const auto sun_source = (sun_source_ == 0)
      ? scene::environment::SunSource::kFromScene
      : scene::environment::SunSource::kSynthetic;
    sun->SetSunSource(sun_source);
    sun->SetAzimuthElevationDegrees(sun_azimuth_deg_, sun_elevation_deg_);
    sun->SetIntensityLux(sun_intensity_lux_);
    sun->SetDiskAngularRadiusRadians(
      sun_component_disk_radius_deg_ * kDegToRad);
    if (sun_use_temperature_) {
      sun->SetLightTemperatureKelvin(sun_temperature_kelvin_);
    } else {
      sun->SetColorRgb(sun_color_rgb_);
    }

    if (sun_source == scene::environment::SunSource::kFromScene) {
      DestroySyntheticSunLight();
      UpdateSunLightCandidate();
      if (sun_light_available_) {
        if (auto light
          = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
          light->get().SetIsSunLight(true);

          auto& common = light->get().Common();
          common.affects_world = sun_enabled_;
          common.intensity = sun_intensity_lux_;
          common.color_rgb = sun_use_temperature_
            ? KelvinToLinearRgb(sun_temperature_kelvin_)
            : sun_color_rgb_;

          const auto sun_dir = DirectionFromAzimuthElevation(
            sun_azimuth_deg_, sun_elevation_deg_);
          const glm::vec3 light_dir = -sun_dir;
          auto transform = sun_light_node_.GetTransform();
          transform.SetLocalRotation(RotationFromDirection(light_dir));
        }

        sun->SetLightReference(sun_light_node_);
      } else {
        sun->ClearLightReference();
      }
    } else {
      UpdateSunLightCandidate();
      if (sun_light_available_) {
        if (auto light
          = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
          light->get().SetIsSunLight(false);
          light->get().Common().affects_world = false;
        }
      }

      EnsureSyntheticSunLight();
      if (synthetic_sun_light_node_.IsAlive()) {
        if (auto light
          = synthetic_sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
          light->get().SetIsSunLight(sun_enabled_);
          light->get().SetEnvironmentContribution(true);

          auto& common = light->get().Common();
          common.affects_world = sun_enabled_;
          common.intensity = sun_intensity_lux_;
          common.color_rgb = sun_use_temperature_
            ? KelvinToLinearRgb(sun_temperature_kelvin_)
            : sun_color_rgb_;

          const auto sun_dir = DirectionFromAzimuthElevation(
            sun_azimuth_deg_, sun_elevation_deg_);
          const glm::vec3 light_dir = -sun_dir;
          auto transform = synthetic_sun_light_node_.GetTransform();
          transform.SetLocalRotation(RotationFromDirection(light_dir));
        }

        sun->SetLightReference(synthetic_sun_light_node_);
      } else {
        sun->ClearLightReference();
      }
    }
  }

  auto* atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>().get();
  if (sky_atmo_enabled_ && !atmo) {
    atmo = &env->AddSystem<scene::environment::SkyAtmosphere>();
  }
  if (atmo) {
    atmo->SetEnabled(sky_atmo_enabled_);
    atmo->SetPlanetRadiusMeters(planet_radius_km_ * kKmToMeters);
    atmo->SetAtmosphereHeightMeters(atmosphere_height_km_ * kKmToMeters);
    atmo->SetGroundAlbedoRgb(ground_albedo_);
    atmo->SetRayleighScaleHeightMeters(rayleigh_scale_height_km_ * kKmToMeters);
    atmo->SetMieScaleHeightMeters(mie_scale_height_km_ * kKmToMeters);
    atmo->SetMieAnisotropy(mie_anisotropy_);
    atmo->SetMultiScatteringFactor(multi_scattering_);
    atmo->SetSunDiskEnabled(sun_disk_enabled_);
    atmo->SetSunDiskAngularRadiusRadians(sun_disk_radius_deg_ * kDegToRad);
    atmo->SetAerialPerspectiveDistanceScale(aerial_perspective_scale_);
    atmo->SetAerialScatteringStrength(aerial_scattering_strength_);

    if (config_.on_atmosphere_params_changed) {
      config_.on_atmosphere_params_changed();
    }
  }

  auto* sky = env->TryGetSystem<scene::environment::SkySphere>().get();
  if (sky_sphere_enabled_ && !sky) {
    sky = &env->AddSystem<scene::environment::SkySphere>();
  }
  if (sky) {
    sky->SetEnabled(sky_sphere_enabled_);
    sky->SetSource(
      static_cast<scene::environment::SkySphereSource>(sky_sphere_source_));
    sky->SetSolidColorRgb(sky_sphere_solid_color_);
    sky->SetIntensity(sky_sphere_intensity_);
    sky->SetRotationRadians(sky_sphere_rotation_deg_ * kDegToRad);
  }

  auto* light = env->TryGetSystem<scene::environment::SkyLight>().get();
  if (sky_light_enabled_ && !light) {
    light = &env->AddSystem<scene::environment::SkyLight>();
  }
  if (light) {
    light->SetEnabled(sky_light_enabled_);
    light->SetSource(
      static_cast<scene::environment::SkyLightSource>(sky_light_source_));
    light->SetTintRgb(sky_light_tint_);
    light->SetIntensity(sky_light_intensity_);
    light->SetDiffuseIntensity(sky_light_diffuse_);
    light->SetSpecularIntensity(sky_light_specular_);
  }

  if (config_.renderer) {
    const uint32_t debug_flags = GetAtmosphereFlags();
    config_.renderer->SetAtmosphereDebugFlags(debug_flags);
  }

  SaveSettings();
  pending_changes_ = false;
  saved_sun_source_ = sun_source_;
}

auto EnvironmentSettingsService::SyncFromScene() -> void
{
  if (!config_.scene) {
    return;
  }

  auto* env = config_.scene->GetEnvironment().get();
  if (!env) {
    if (apply_saved_sun_on_next_sync_) {
      ApplySavedSunSourcePreference();
      apply_saved_sun_on_next_sync_ = false;
    }
    return;
  }

  if (auto* atmo
    = env->TryGetSystem<scene::environment::SkyAtmosphere>().get()) {
    sky_atmo_enabled_ = atmo->IsEnabled();
    planet_radius_km_ = atmo->GetPlanetRadiusMeters() * kMetersToKm;
    atmosphere_height_km_ = atmo->GetAtmosphereHeightMeters() * kMetersToKm;
    ground_albedo_ = atmo->GetGroundAlbedoRgb();
    rayleigh_scale_height_km_
      = atmo->GetRayleighScaleHeightMeters() * kMetersToKm;
    mie_scale_height_km_ = atmo->GetMieScaleHeightMeters() * kMetersToKm;
    mie_anisotropy_ = atmo->GetMieAnisotropy();
    multi_scattering_ = atmo->GetMultiScatteringFactor();
    sun_disk_enabled_ = atmo->GetSunDiskEnabled();
    sun_disk_radius_deg_ = atmo->GetSunDiskAngularRadiusRadians() * kRadToDeg;
    aerial_perspective_scale_ = atmo->GetAerialPerspectiveDistanceScale();
    aerial_scattering_strength_ = atmo->GetAerialScatteringStrength();
  }

  if (auto* sky = env->TryGetSystem<scene::environment::SkySphere>().get()) {
    sky_sphere_enabled_ = sky->IsEnabled();
    sky_sphere_source_ = static_cast<int>(sky->GetSource());
    sky_sphere_solid_color_ = sky->GetSolidColorRgb();
    sky_sphere_intensity_ = sky->GetIntensity();
    sky_sphere_rotation_deg_ = sky->GetRotationRadians() * kRadToDeg;
  }

  if (auto* light = env->TryGetSystem<scene::environment::SkyLight>().get()) {
    sky_light_enabled_ = light->IsEnabled();
    sky_light_source_ = static_cast<int>(light->GetSource());
    sky_light_tint_ = light->GetTintRgb();
    sky_light_intensity_ = light->GetIntensity();
    sky_light_diffuse_ = light->GetDiffuseIntensity();
    sky_light_specular_ = light->GetSpecularIntensity();
  }

  if (auto* sun = env->TryGetSystem<scene::environment::Sun>().get()) {
    sun_present_ = true;
    sun_enabled_ = sun->IsEnabled();
    const bool from_scene
      = (sun->GetSunSource() == scene::environment::SunSource::kFromScene);
    sun_source_ = from_scene ? 0 : 1;
    sun_azimuth_deg_ = sun->GetAzimuthDegrees();
    sun_elevation_deg_ = sun->GetElevationDegrees();
    sun_color_rgb_ = sun->GetColorRgb();
    sun_intensity_lux_ = sun->GetIntensityLux();
    sun_use_temperature_ = sun->HasLightTemperature();
    if (sun_use_temperature_) {
      sun_temperature_kelvin_ = sun->GetLightTemperatureKelvin();
    }
    sun_component_disk_radius_deg_
      = sun->GetDiskAngularRadiusRadians() * kRadToDeg;

    if (from_scene) {
      UpdateSunLightCandidate();
      if (sun_light_available_) {
        if (auto light
          = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
          sun_enabled_ = light->get().Common().affects_world;
        }
      }
    }

    SaveSunSettingsToProfile(sun_source_);
  }

  if (apply_saved_sun_on_next_sync_) {
    ApplySavedSunSourcePreference();
    apply_saved_sun_on_next_sync_ = false;
  }
}

auto EnvironmentSettingsService::SyncDebugFlagsFromRenderer() -> void
{
  if (!config_.renderer) {
    return;
  }

  const uint32_t flags = config_.renderer->GetAtmosphereDebugFlags();
  force_analytic_
    = (flags & static_cast<uint32_t>(AtmosphereDebugFlags::kForceAnalytic))
    != 0;
  visualize_lut_
    = (flags & static_cast<uint32_t>(AtmosphereDebugFlags::kVisualizeLut)) != 0;
  use_lut_ = !force_analytic_;
}

auto EnvironmentSettingsService::LoadSettings() -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

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
      out = static_cast<int>(value);
      return true;
    }
    return false;
  };

  bool any_loaded = false;
  any_loaded |= load_bool(kSkyAtmoEnabledKey, sky_atmo_enabled_);
  any_loaded |= load_float(kPlanetRadiusKey, planet_radius_km_);
  any_loaded |= load_float(kAtmosphereHeightKey, atmosphere_height_km_);
  any_loaded |= load_vec3(kGroundAlbedoKey, ground_albedo_);
  any_loaded |= load_float(kRayleighScaleHeightKey, rayleigh_scale_height_km_);
  any_loaded |= load_float(kMieScaleHeightKey, mie_scale_height_km_);
  any_loaded |= load_float(kMieAnisotropyKey, mie_anisotropy_);
  any_loaded |= load_float(kMultiScatteringKey, multi_scattering_);
  any_loaded |= load_bool(kSunDiskEnabledKey, sun_disk_enabled_);
  any_loaded |= load_float(kAtmoSunDiskRadiusKey, sun_disk_radius_deg_);
  any_loaded
    |= load_float(kAerialPerspectiveScaleKey, aerial_perspective_scale_);
  any_loaded
    |= load_float(kAerialScatteringStrengthKey, aerial_scattering_strength_);

  any_loaded |= load_bool(kSkySphereEnabledKey, sky_sphere_enabled_);
  any_loaded |= load_int(kSkySphereSourceKey, sky_sphere_source_);
  any_loaded |= load_vec3(kSkySphereSolidColorKey, sky_sphere_solid_color_);
  any_loaded |= load_float(kSkySphereIntensityKey, sky_sphere_intensity_);
  any_loaded |= load_float(kSkySphereRotationKey, sky_sphere_rotation_deg_);

  any_loaded |= load_int(kSkyboxLayoutKey, skybox_layout_idx_);
  any_loaded |= load_int(kSkyboxOutputFormatKey, skybox_output_format_idx_);
  any_loaded |= load_int(kSkyboxFaceSizeKey, skybox_face_size_);
  any_loaded |= load_bool(kSkyboxFlipYKey, skybox_flip_y_);
  any_loaded |= load_bool(kSkyboxTonemapKey, skybox_tonemap_hdr_to_ldr_);
  any_loaded |= load_float(kSkyboxHdrExposureKey, skybox_hdr_exposure_ev_);
  if (const auto path = settings->GetString(kSkyboxPathKey)) {
    skybox_path_ = *path;
    any_loaded = true;
  }

  any_loaded |= load_bool(kSkyLightEnabledKey, sky_light_enabled_);
  any_loaded |= load_int(kSkyLightSourceKey, sky_light_source_);
  any_loaded |= load_vec3(kSkyLightTintKey, sky_light_tint_);
  any_loaded |= load_float(kSkyLightIntensityKey, sky_light_intensity_);
  any_loaded |= load_float(kSkyLightDiffuseKey, sky_light_diffuse_);
  any_loaded |= load_float(kSkyLightSpecularKey, sky_light_specular_);

  any_loaded |= load_bool(kSunEnabledKey, sun_enabled_);
  const bool sun_source_loaded = load_int(kSunSourceKey, sun_source_);
  any_loaded |= sun_source_loaded;
  any_loaded |= load_float(kSunAzimuthKey, sun_azimuth_deg_);
  any_loaded |= load_float(kSunElevationKey, sun_elevation_deg_);
  any_loaded |= load_vec3(kSunColorKey, sun_color_rgb_);
  any_loaded |= load_float(kSunIntensityKey, sun_intensity_lux_);
  any_loaded |= load_bool(kSunUseTemperatureKey, sun_use_temperature_);
  any_loaded |= load_float(kSunTemperatureKey, sun_temperature_kelvin_);
  any_loaded |= load_float(kSunDiskRadiusKey, sun_component_disk_radius_deg_);

  if (sun_source_loaded) {
    saved_sun_source_ = sun_source_;
    apply_saved_sun_on_next_sync_ = true;
    SaveSunSettingsToProfile(sun_source_);
    if (sun_source_ == 1) {
      sun_present_ = true;
    }
  }

  any_loaded |= load_bool(kUseLutKey, use_lut_);
  any_loaded |= load_bool(kVisualizeLutKey, visualize_lut_);
  any_loaded |= load_bool(kForceAnalyticKey, force_analytic_);

  if (any_loaded) {
    settings_loaded_ = true;
    needs_sync_ = false;
    pending_changes_ = true;
  }
}

auto EnvironmentSettingsService::SaveSettings() const -> void
{
  const auto settings = ResolveSettings();
  if (!settings) {
    return;
  }

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

  save_bool(kSkyAtmoEnabledKey, sky_atmo_enabled_);
  save_float(kPlanetRadiusKey, planet_radius_km_);
  save_float(kAtmosphereHeightKey, atmosphere_height_km_);
  save_vec3(kGroundAlbedoKey, ground_albedo_);
  save_float(kRayleighScaleHeightKey, rayleigh_scale_height_km_);
  save_float(kMieScaleHeightKey, mie_scale_height_km_);
  save_float(kMieAnisotropyKey, mie_anisotropy_);
  save_float(kMultiScatteringKey, multi_scattering_);
  save_bool(kSunDiskEnabledKey, sun_disk_enabled_);
  save_float(kAtmoSunDiskRadiusKey, sun_disk_radius_deg_);
  save_float(kAerialPerspectiveScaleKey, aerial_perspective_scale_);
  save_float(kAerialScatteringStrengthKey, aerial_scattering_strength_);

  save_bool(kSkySphereEnabledKey, sky_sphere_enabled_);
  save_int(kSkySphereSourceKey, sky_sphere_source_);
  save_vec3(kSkySphereSolidColorKey, sky_sphere_solid_color_);
  save_float(kSkySphereIntensityKey, sky_sphere_intensity_);
  save_float(kSkySphereRotationKey, sky_sphere_rotation_deg_);

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
  save_float(kSkyLightIntensityKey, sky_light_intensity_);
  save_float(kSkyLightDiffuseKey, sky_light_diffuse_);
  save_float(kSkyLightSpecularKey, sky_light_specular_);

  save_bool(kSunEnabledKey, sun_enabled_);
  save_int(kSunSourceKey, sun_source_);
  save_float(kSunAzimuthKey, sun_azimuth_deg_);
  save_float(kSunElevationKey, sun_elevation_deg_);
  save_vec3(kSunColorKey, sun_color_rgb_);
  save_float(kSunIntensityKey, sun_intensity_lux_);
  save_bool(kSunUseTemperatureKey, sun_use_temperature_);
  save_float(kSunTemperatureKey, sun_temperature_kelvin_);
  save_float(kSunDiskRadiusKey, sun_component_disk_radius_deg_);

  save_bool(kUseLutKey, use_lut_);
  save_bool(kVisualizeLutKey, visualize_lut_);
  save_bool(kForceAnalyticKey, force_analytic_);
}

auto EnvironmentSettingsService::MarkDirty() -> void
{
  pending_changes_ = true;
  SaveSettings();
}

auto EnvironmentSettingsService::ApplySavedSunSourcePreference() -> void
{
  if (!saved_sun_source_) {
    return;
  }

  const int desired_source = *saved_sun_source_;
  if (desired_source == 1) {
    sun_source_ = 1;
    sun_present_ = true;
    LoadSunSettingsFromProfile(sun_source_);
    pending_changes_ = true;
    return;
  }

  if (sun_source_ != desired_source) {
    sun_source_ = desired_source;
    LoadSunSettingsFromProfile(sun_source_);
    pending_changes_ = true;
  }
}

auto EnvironmentSettingsService::ResetSunUiToDefaults() -> void
{
  const scene::environment::Sun defaults;

  sun_present_ = true;
  sun_enabled_ = defaults.IsEnabled();
  sun_source_
    = (defaults.GetSunSource() == scene::environment::SunSource::kFromScene)
    ? 0
    : 1;
  sun_azimuth_deg_ = defaults.GetAzimuthDegrees();
  sun_elevation_deg_ = defaults.GetElevationDegrees();
  sun_color_rgb_ = defaults.GetColorRgb();
  sun_intensity_lux_ = defaults.GetIntensityLux();
  sun_use_temperature_ = defaults.HasLightTemperature();
  sun_temperature_kelvin_
    = sun_use_temperature_ ? defaults.GetLightTemperatureKelvin() : 6500.0F;
  sun_component_disk_radius_deg_
    = defaults.GetDiskAngularRadiusRadians() * kRadToDeg;

  SaveSunSettingsToProfile(0);
  SaveSunSettingsToProfile(1);
}

auto EnvironmentSettingsService::FindSunLightCandidate() const
  -> std::optional<scene::SceneNode>
{
  if (!config_.scene) {
    return std::nullopt;
  }

  auto roots = config_.scene->GetRootNodes();
  std::vector<scene::SceneNode> stack;
  stack.reserve(roots.size());
  for (auto& root : roots) {
    stack.push_back(root);
  }

  std::optional<scene::SceneNode> first_directional;
  while (!stack.empty()) {
    auto node = stack.back();
    stack.pop_back();

    if (!node.IsAlive()) {
      continue;
    }

    if (auto light = node.GetLightAs<scene::DirectionalLight>()) {
      if (light->get().IsSunLight()) {
        return node;
      }
      if (!first_directional.has_value()) {
        first_directional = node;
      }
    }

    auto child_opt = node.GetFirstChild();
    while (child_opt.has_value()) {
      stack.push_back(*child_opt);
      child_opt = child_opt->GetNextSibling();
    }
  }

  return first_directional;
}

auto EnvironmentSettingsService::EnsureSyntheticSunLight() -> void
{
  if (!config_.scene) {
    return;
  }
  if (synthetic_sun_light_created_ && synthetic_sun_light_node_.IsAlive()) {
    return;
  }

  auto node = config_.scene->CreateNode("Synthetic Sun");
  if (!node.IsAlive()) {
    return;
  }
  if (!node.HasLight()) {
    auto light = std::make_unique<scene::DirectionalLight>();
    (void)node.AttachLight(std::move(light));
  }
  synthetic_sun_light_node_ = node;
  synthetic_sun_light_created_ = true;
}

auto EnvironmentSettingsService::DestroySyntheticSunLight() -> void
{
  if (!synthetic_sun_light_created_) {
    return;
  }

  if (synthetic_sun_light_node_.IsAlive()) {
    (void)config_.scene->DestroyNode(synthetic_sun_light_node_);
  }
  synthetic_sun_light_node_ = {};
  synthetic_sun_light_created_ = false;
}

auto EnvironmentSettingsService::GetSunSettingsForSource(const int source)
  -> SunUiSettings&
{
  return (source == 0) ? sun_scene_settings_ : sun_synthetic_settings_;
}

auto EnvironmentSettingsService::LoadSunSettingsFromProfile(const int source)
  -> void
{
  const auto& settings = GetSunSettingsForSource(source);
  sun_enabled_ = settings.enabled;
  sun_azimuth_deg_ = settings.azimuth_deg;
  sun_elevation_deg_ = settings.elevation_deg;
  sun_color_rgb_ = settings.color_rgb;
  sun_intensity_lux_ = settings.intensity_lux;
  sun_use_temperature_ = settings.use_temperature;
  sun_temperature_kelvin_ = settings.temperature_kelvin;
  sun_component_disk_radius_deg_ = settings.disk_radius_deg;
}

auto EnvironmentSettingsService::SaveSunSettingsToProfile(const int source)
  -> void
{
  auto& settings = GetSunSettingsForSource(source);
  settings.enabled = sun_enabled_;
  settings.azimuth_deg = sun_azimuth_deg_;
  settings.elevation_deg = sun_elevation_deg_;
  settings.color_rgb = sun_color_rgb_;
  settings.intensity_lux = sun_intensity_lux_;
  settings.use_temperature = sun_use_temperature_;
  settings.temperature_kelvin = sun_temperature_kelvin_;
  settings.disk_radius_deg = sun_component_disk_radius_deg_;
}

auto EnvironmentSettingsService::GetAtmosphereFlags() const -> uint32_t
{
  uint32_t flags = 0;
  if (use_lut_) {
    flags |= static_cast<uint32_t>(AtmosphereDebugFlags::kUseLut);
  }
  if (visualize_lut_) {
    flags |= static_cast<uint32_t>(AtmosphereDebugFlags::kVisualizeLut);
  }
  if (force_analytic_) {
    flags |= static_cast<uint32_t>(AtmosphereDebugFlags::kForceAnalytic);
  }
  return flags;
}

auto EnvironmentSettingsService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::Default();
}

} // namespace oxygen::examples
