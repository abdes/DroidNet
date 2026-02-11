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

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
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

  template <typename Record>
  auto IsEnabled(const std::optional<Record>& record) -> bool
  {
    return record.has_value() && record->enabled != 0U;
  }

  auto HydrateSkyAtmosphere(scene::environment::SkyAtmosphere& target,
    const data::pak::SkyAtmosphereEnvironmentRecord& source) -> void
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
    target.SetOzoneAbsorptionRgb(Vec3 { source.absorption_rgb[0],
      source.absorption_rgb[1], source.absorption_rgb[2] });

    // Pak format currently exposes a single absorption height parameter.
    // The physical lighting spec uses a fixed two-layer linear ozone profile,
    // so we ignore this field and apply the default Earth-like profile.
    target.SetOzoneDensityProfile(engine::atmos::kDefaultOzoneDensityProfile);
    // New parameters not yet in PakFormat, use defaults or derived values if
    // needed. For now, we leave them as component defaults.
    target.SetMultiScatteringFactor(source.multi_scattering_factor);
    target.SetSunDiskEnabled(source.sun_disk_enabled != 0U);
    target.SetAerialPerspectiveDistanceScale(
      source.aerial_perspective_distance_scale);
  }

  auto HydrateSkySphere(scene::environment::SkySphere& target,
    const data::pak::SkySphereEnvironmentRecord& source) -> void
  {
    if (source.source
      == static_cast<std::uint32_t>(
        scene::environment::SkySphereSource::kSolidColor)) {
      target.SetSource(scene::environment::SkySphereSource::kSolidColor);
    } else {
      LOG_F(WARNING,
        "EnvironmentSettingsService: SkySphere cubemap source requested, "
        "but scene-authored cubemap AssetKey resolution is not implemented "
        "in this example. Keeping solid color; use the Environment panel "
        "Skybox Loader to bind a cubemap at runtime.");
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
    const data::pak::FogEnvironmentRecord& source) -> void
  {
    target.SetModel(static_cast<scene::environment::FogModel>(source.model));
    target.SetExtinctionSigmaTPerMeter(source.extinction_sigma_t_per_m);
    target.SetHeightFalloffPerMeter(source.height_falloff_per_m);
    target.SetHeightOffsetMeters(source.height_offset_m);
    target.SetStartDistanceMeters(source.start_distance_m);
    target.SetMaxOpacity(source.max_opacity);
    target.SetSingleScatteringAlbedoRgb(
      Vec3 { source.single_scattering_albedo_rgb[0],
        source.single_scattering_albedo_rgb[1],
        source.single_scattering_albedo_rgb[2] });
    target.SetAnisotropy(source.anisotropy_g);
  }

  auto HydrateSkyLight(scene::environment::SkyLight& target,
    const data::pak::SkyLightEnvironmentRecord& source) -> void
  {
    target.SetSource(
      static_cast<scene::environment::SkyLightSource>(source.source));
    if (target.GetSource()
      == scene::environment::SkyLightSource::kSpecifiedCubemap) {
      LOG_F(INFO,
        "EnvironmentSettingsService: SkyLight specifies a cubemap AssetKey, "
        "but this example does not yet resolve it to a ResourceKey. Use "
        "the Environment panel Skybox Loader to bind a cubemap at runtime.");
    }
    target.SetIntensityMul(source.intensity);
    target.SetTintRgb(
      Vec3 { source.tint_rgb[0], source.tint_rgb[1], source.tint_rgb[2] });
    target.SetDiffuseIntensity(source.diffuse_intensity);
    target.SetSpecularIntensity(source.specular_intensity);
  }

  auto HydrateVolumetricClouds(scene::environment::VolumetricClouds& target,
    const data::pak::VolumetricCloudsEnvironmentRecord& source) -> void
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
    const data::pak::PostProcessVolumeEnvironmentRecord& source) -> void
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
  constexpr std::string_view kAerialPerspectiveScaleKey
    = "env.atmo.aerial_perspective_scale";
  constexpr std::string_view kAerialScatteringStrengthKey
    = "env.atmo.aerial_scattering_strength";
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
  constexpr std::string_view kSkyViewLutSlicesKey
    = "env.atmo.sky_view_lut_slices";
  constexpr std::string_view kSkyViewAltMappingModeKey
    = "env.atmo.sky_view_alt_mapping_mode";

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

  constexpr std::string_view kFogEnabledKey = "env.fog.enabled";
  constexpr std::string_view kFogModelKey = "env.fog.model";
  constexpr std::string_view kFogExtinctionSigmaTKey
    = "env.fog.extinction_sigma_t_per_m";
  constexpr std::string_view kFogHeightFalloffKey
    = "env.fog.height_falloff_per_m";
  constexpr std::string_view kFogHeightOffsetKey = "env.fog.height_offset_m";
  constexpr std::string_view kFogStartDistanceKey = "env.fog.start_distance_m";
  constexpr std::string_view kFogMaxOpacityKey = "env.fog.max_opacity";
  constexpr std::string_view kFogSingleScatteringAlbedoKey
    = "env.fog.single_scattering_albedo_rgb";
  constexpr std::string_view kEnvironmentPresetKey = "environment_preset_index";

  // Light Culling Key
  constexpr std::string_view kSunEnabledKey = "env.sun.enabled";
  constexpr std::string_view kSunSourceKey = "env.sun.source";
  constexpr std::string_view kSunAzimuthKey = "env.sun.azimuth_deg";
  constexpr std::string_view kSunElevationKey = "env.sun.elevation_deg";
  constexpr std::string_view kSunColorKey = "env.sun.color";
  constexpr std::string_view kSunIlluminanceKey = "env.sun.illuminance_lx";
  constexpr std::string_view kSunUseTemperatureKey = "env.sun.use_temperature";
  constexpr std::string_view kSunTemperatureKey = "env.sun.temperature_kelvin";
  constexpr std::string_view kSunDiskRadiusKey = "env.sun.disk_radius_deg";

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
      "EnvironmentSettingsService: Both SkyAtmosphere and SkySphere are "
      "enabled in the scene. They are mutually exclusive; SkyAtmosphere "
      "will be used.");
  }

  if (sky_atmo_enabled) {
    auto& atmo = target.AddSystem<scene::environment::SkyAtmosphere>();
    HydrateSkyAtmosphere(atmo, *sky_atmo_record);
    LOG_F(
      INFO, "EnvironmentSettingsService: Applied SkyAtmosphere environment");
  } else if (sky_sphere_enabled) {
    auto& sky_sphere = target.AddSystem<scene::environment::SkySphere>();
    HydrateSkySphere(sky_sphere, *sky_sphere_record);
    LOG_F(INFO,
      "EnvironmentSettingsService: Applied SkySphere environment (solid "
      "color source)");
  }

  if (const auto fog_record = source_asset.TryGetFogEnvironment();
    IsEnabled(fog_record)) {
    auto& fog = target.AddSystem<scene::environment::Fog>();
    HydrateFog(fog, *fog_record);
    LOG_F(INFO, "EnvironmentSettingsService: Applied Fog environment");
  }

  if (const auto sky_light_record = source_asset.TryGetSkyLightEnvironment();
    IsEnabled(sky_light_record)) {
    auto& sky_light = target.AddSystem<scene::environment::SkyLight>();
    HydrateSkyLight(sky_light, *sky_light_record);
    LOG_F(INFO, "EnvironmentSettingsService: Applied SkyLight environment");
  }

  if (const auto clouds_record
    = source_asset.TryGetVolumetricCloudsEnvironment();
    IsEnabled(clouds_record)) {
    auto& clouds = target.AddSystem<scene::environment::VolumetricClouds>();
    HydrateVolumetricClouds(clouds, *clouds_record);
    LOG_F(
      INFO, "EnvironmentSettingsService: Applied VolumetricClouds environment");
  }

  if (const auto pp_record = source_asset.TryGetPostProcessVolumeEnvironment();
    IsEnabled(pp_record)) {
    auto& pp = target.AddSystem<scene::environment::PostProcessVolume>();
    HydratePostProcessVolume(pp, *pp_record);
    LOG_F(INFO,
      "EnvironmentSettingsService: Applied PostProcessVolume environment");
  }
}

auto EnvironmentSettingsService::SetRuntimeConfig(
  const EnvironmentRuntimeConfig& config) -> void
{
  const bool scene_changed = config_.scene.get() != config.scene.get();
  config_ = config;

  if (!settings_loaded_) {
    LoadSettings();
  }

  NormalizeSkySystems();

  if (scene_changed) {
    needs_sync_ = true;
    apply_saved_sun_on_next_sync_ = true;
    if (!pending_changes_) {
      SyncFromSceneIfNeeded();
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
  MaybeRequestSkyCapture();
}

auto EnvironmentSettingsService::OnSceneActivated(scene::Scene& /*scene*/)
  -> void
{
  config_.scene = nullptr;
  config_.skybox_service = nullptr;
  needs_sync_ = true;
  apply_saved_sun_on_next_sync_ = true;
  sun_light_available_ = false;
  sun_light_node_ = {};
  synthetic_sun_light_node_ = {};
  synthetic_sun_light_created_ = false;
  epoch_++;
}

auto EnvironmentSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/, const CompositionView& /*view*/)
  -> void
{
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
      MarkDirty();
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
  MarkDirty();
}

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
  return pending_changes_;
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
  NormalizeSkySystems();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
}

auto EnvironmentSettingsService::GetSkyViewLutSlices() const -> int
{
  return sky_view_lut_slices_;
}

auto EnvironmentSettingsService::SetSkyViewLutSlices(int value) -> void
{
  if (sky_view_lut_slices_ == value) {
    return;
  }
  sky_view_lut_slices_ = value;
  MarkDirty();
}

auto EnvironmentSettingsService::GetSkyViewAltMappingMode() const -> int
{
  return sky_view_alt_mapping_mode_;
}

auto EnvironmentSettingsService::SetSkyViewAltMappingMode(int value) -> void
{
  if (sky_view_alt_mapping_mode_ == value) {
    return;
  }
  sky_view_alt_mapping_mode_ = value;
  MarkDirty();
}

auto EnvironmentSettingsService::RequestRegenerateLut() -> void
{
  regenerate_lut_requested_ = true;
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
  NormalizeSkySystems();
  skybox_dirty_ = true;
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
  skybox_dirty_ = true;
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
  skybox_dirty_ = true;
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
  skybox_dirty_ = true;
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
  skybox_dirty_ = true;
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
  skybox_dirty_ = true;
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
  skybox_dirty_ = true;
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
  skybox_dirty_ = true;
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  MarkDirty();
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
  if (sun_present_ && sun_source_ == 1) {
    return;
  }
  sun_present_ = true;
  SetSunSource(1);
}

auto EnvironmentSettingsService::GetUseLut() const -> bool { return use_lut_; }

auto EnvironmentSettingsService::SetUseLut(bool enabled) -> void
{
  if (use_lut_ == enabled) {
    return;
  }
  use_lut_ = enabled;
  MarkDirty();
}

auto EnvironmentSettingsService::ApplyPendingChanges() -> void
{
  if (!pending_changes_ || !config_.scene) {
    return;
  }

  NormalizeSkySystems();

  auto env = config_.scene->GetEnvironment();
  if (!env) {
    config_.scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    env = config_.scene->GetEnvironment();
  }

  auto sun = env->TryGetSystem<scene::environment::Sun>();
  if (sun_present_ && sun_enabled_ && (sun == nullptr)) {
    sun = observer_ptr { &env->AddSystem<scene::environment::Sun>() };
  }
  if (sun) {
    sun->SetEnabled(sun_enabled_);
  }
  if ((sun) && !sun_enabled_) {
    UpdateSunLightCandidate();
    if (sun_light_available_) {
      if (auto light = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
        light->get().SetIsSunLight(false);
        light->get().Common().affects_world = false;
      }
    }

    if (synthetic_sun_light_node_.IsAlive()) {
      if (auto light
        = synthetic_sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
        light->get().SetIsSunLight(false);
        light->get().Common().affects_world = false;
      }
    }

    sun->ClearLightReference();
  }

  if (sun_enabled_ && (sun != nullptr)) {
    const auto sun_source = (sun_source_ == 0)
      ? scene::environment::SunSource::kFromScene
      : scene::environment::SunSource::kSynthetic;
    sun->SetSunSource(sun_source);
    sun->SetAzimuthElevationDegrees(sun_azimuth_deg_, sun_elevation_deg_);
    sun->SetIlluminanceLx(sun_illuminance_lx_);
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
          light->get().SetIntensityLux(sun_illuminance_lx_);
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
          light->get().SetIntensityLux(sun_illuminance_lx_);
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
  } else if (sun) {
    sun->ClearLightReference();
  }

  auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>();
  if (sky_atmo_enabled_ && !atmo) {
    atmo
      = observer_ptr { &env->AddSystem<scene::environment::SkyAtmosphere>() };
  }
  if (atmo) {
    atmo->SetEnabled(sky_atmo_enabled_);
  }
  if (sky_atmo_enabled_ && atmo) {
    atmo->SetPlanetRadiusMeters(planet_radius_km_ * kKmToMeters);
    atmo->SetAtmosphereHeightMeters(atmosphere_height_km_ * kKmToMeters);
    atmo->SetGroundAlbedoRgb(ground_albedo_);
    atmo->SetRayleighScaleHeightMeters(rayleigh_scale_height_km_ * kKmToMeters);
    atmo->SetMieScaleHeightMeters(mie_scale_height_km_ * kKmToMeters);
    atmo->SetMieScaleHeightMeters(mie_scale_height_km_ * kKmToMeters);
    atmo->SetMieAnisotropy(mie_anisotropy_);
    atmo->SetMieAbsorptionRgb(mie_absorption_scale_ * glm::vec3(2.33e-6F));
    // We now control absorption explicitly via the new parameters.
    atmo->SetOzoneAbsorptionRgb(ozone_rgb_);

    // Apply the 2-layer ozone density profile as-authored (UI/settings).
    atmo->SetOzoneDensityProfile(ozone_profile_);

    atmo->SetMultiScatteringFactor(multi_scattering_);
    atmo->SetSunDiskEnabled(sun_disk_enabled_);
    atmo->SetAerialPerspectiveDistanceScale(aerial_perspective_scale_);
    atmo->SetAerialScatteringStrength(aerial_scattering_strength_);

    if (config_.on_atmosphere_params_changed) {
      config_.on_atmosphere_params_changed();
      LOG_F(INFO,
        "EnvironmentSettingsService: atmosphere parameters changed "
        "(SunDiskEnabled={})",
        sun_disk_enabled_);
    }
  }

  auto fog = env->TryGetSystem<scene::environment::Fog>();
  if (fog_enabled_ && !fog) {
    fog = observer_ptr { &env->AddSystem<scene::environment::Fog>() };
  }
  if (fog) {
    fog->SetEnabled(fog_enabled_);
  }
  if (fog_enabled_ && fog) {
    fog->SetModel(static_cast<scene::environment::FogModel>(fog_model_));
    fog->SetExtinctionSigmaTPerMeter(fog_extinction_sigma_t_per_m_);
    fog->SetHeightFalloffPerMeter(fog_height_falloff_per_m_);
    fog->SetHeightOffsetMeters(fog_height_offset_m_);
    fog->SetStartDistanceMeters(fog_start_distance_m_);
    fog->SetMaxOpacity(fog_max_opacity_);
    fog->SetSingleScatteringAlbedoRgb(fog_single_scattering_albedo_rgb_);
  }

  auto sky = env->TryGetSystem<scene::environment::SkySphere>();
  if (sky_sphere_enabled_ && !sky) {
    sky = observer_ptr { &env->AddSystem<scene::environment::SkySphere>() };
  }
  if (sky) {
    sky->SetEnabled(sky_sphere_enabled_);
  }
  if (sky_sphere_enabled_ && sky) {
    sky->SetSource(
      static_cast<scene::environment::SkySphereSource>(sky_sphere_source_));
    sky->SetSolidColorRgb(sky_sphere_solid_color_);
    sky->SetIntensity(sky_intensity_);
    sky->SetRotationRadians(sky_sphere_rotation_deg_ * kDegToRad);
  }

  auto light = env->TryGetSystem<scene::environment::SkyLight>();
  if (sky_light_enabled_ && !light) {
    light = observer_ptr { &env->AddSystem<scene::environment::SkyLight>() };
  }
  if (light) {
    light->SetEnabled(sky_light_enabled_);
  }
  if (sky_light_enabled_ && light) {
    light->SetSource(
      static_cast<scene::environment::SkyLightSource>(sky_light_source_));
    light->SetTintRgb(sky_light_tint_);
    light->SetIntensityMul(sky_light_intensity_mul_);
    light->SetDiffuseIntensity(sky_light_diffuse_);
    light->SetSpecularIntensity(sky_light_specular_);
  }

  MaybeAutoLoadSkybox();

  if (config_.renderer) {
    // Apply sky-view LUT slice configuration to the LUT manager.
    if (auto lut_mgr = config_.renderer->GetSkyAtmosphereLutManager()) {
      lut_mgr->SetSkyViewLutSlices(static_cast<uint32_t>(sky_view_lut_slices_));
      lut_mgr->SetAltMappingMode(
        static_cast<uint32_t>(sky_view_alt_mapping_mode_));

      // Honor explicit "Regenerate LUT" button press.
      if (regenerate_lut_requested_) {
        lut_mgr->MarkDirty();
        regenerate_lut_requested_ = false;
      }
    }
  }

  SaveSettings();
  applied_changes_this_frame_ = true;
  pending_changes_ = false;
  saved_sun_source_ = sun_source_;
}

auto EnvironmentSettingsService::SyncFromScene() -> void
{
  if (!config_.scene) {
    return;
  }

  auto env = config_.scene->GetEnvironment();
  if (!env) {
    if (apply_saved_sun_on_next_sync_) {
      ApplySavedSunSourcePreference();
      apply_saved_sun_on_next_sync_ = false;
    }
    return;
  }

  if (auto atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>()) {
    sky_atmo_enabled_ = atmo->IsEnabled();
    planet_radius_km_ = atmo->GetPlanetRadiusMeters() * kMetersToKm;
    atmosphere_height_km_ = atmo->GetAtmosphereHeightMeters() * kMetersToKm;
    ground_albedo_ = atmo->GetGroundAlbedoRgb();
    rayleigh_scale_height_km_
      = atmo->GetRayleighScaleHeightMeters() * kMetersToKm;
    mie_scale_height_km_ = atmo->GetMieScaleHeightMeters() * kMetersToKm;
    mie_anisotropy_ = atmo->GetMieAnisotropy();
    // Compute scale relative to the engine's Earth-like default.
    const auto absorption = atmo->GetMieAbsorptionRgb();
    const auto base_absorption = engine::atmos::kDefaultMieAbsorptionRgb;
    const float base_avg
      = (base_absorption.x + base_absorption.y + base_absorption.z) / 3.0F;
    mie_absorption_scale_ = (base_avg > 0.0F)
      ? (absorption.x + absorption.y + absorption.z) / (3.0F * base_avg)
      : 0.0F;
    multi_scattering_ = atmo->GetMultiScatteringFactor();
    sun_disk_enabled_ = atmo->GetSunDiskEnabled();
    aerial_perspective_scale_ = atmo->GetAerialPerspectiveDistanceScale();
    aerial_scattering_strength_ = atmo->GetAerialScatteringStrength();

    // Sync ozone parameters.
    ozone_rgb_ = atmo->GetAbsorptionRgb();

    // Keep the full two-layer profile in sync so the UI can reflect
    // scene-authored values (e.g., from loaded assets).
    ozone_profile_ = atmo->GetOzoneDensityProfile();
  }

  if (auto fog = env->TryGetSystem<scene::environment::Fog>()) {
    fog_enabled_ = fog->IsEnabled();
    fog_model_ = static_cast<int>(fog->GetModel());
    fog_extinction_sigma_t_per_m_ = fog->GetExtinctionSigmaTPerMeter();
    fog_height_falloff_per_m_ = fog->GetHeightFalloffPerMeter();
    fog_height_offset_m_ = fog->GetHeightOffsetMeters();
    fog_start_distance_m_ = fog->GetStartDistanceMeters();
    fog_max_opacity_ = fog->GetMaxOpacity();
    fog_single_scattering_albedo_rgb_ = fog->GetSingleScatteringAlbedoRgb();
  }

  // Sync LUT slice configuration from the renderer's LUT manager.
  if (config_.renderer) {
    if (auto lut_mgr = config_.renderer->GetSkyAtmosphereLutManager()) {
      sky_view_lut_slices_ = static_cast<int>(lut_mgr->GetSkyViewLutSlices());
      sky_view_alt_mapping_mode_
        = static_cast<int>(lut_mgr->GetAltMappingMode());
    }
  }

  if (auto sky = env->TryGetSystem<scene::environment::SkySphere>()) {
    sky_sphere_enabled_ = sky->IsEnabled();
    sky_sphere_source_ = static_cast<int>(sky->GetSource());
    sky_sphere_solid_color_ = sky->GetSolidColorRgb();
    sky_intensity_ = sky->GetIntensity();
    sky_sphere_rotation_deg_ = sky->GetRotationRadians() * kRadToDeg;
  }

  if (auto light = env->TryGetSystem<scene::environment::SkyLight>()) {
    sky_light_enabled_ = light->IsEnabled();
    sky_light_source_ = static_cast<int>(light->GetSource());
    sky_light_tint_ = light->GetTintRgb();
    sky_light_intensity_mul_ = light->GetIntensityMul();
    sky_light_diffuse_ = light->GetDiffuseIntensity();
    sky_light_specular_ = light->GetSpecularIntensity();
  }

  if (auto sun = env->TryGetSystem<scene::environment::Sun>()) {
    sun_present_ = true;
    sun_enabled_ = sun->IsEnabled();
    const bool from_scene
      = (sun->GetSunSource() == scene::environment::SunSource::kFromScene);
    sun_source_ = from_scene ? 0 : 1;
    sun_azimuth_deg_ = sun->GetAzimuthDegrees();
    sun_elevation_deg_ = sun->GetElevationDegrees();
    sun_color_rgb_ = sun->GetColorRgb();
    sun_illuminance_lx_ = sun->GetIlluminanceLx();
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

  NormalizeSkySystems();
  epoch_++;
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

auto EnvironmentSettingsService::LoadSettings() -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

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
  any_loaded
    |= load_float(kAerialPerspectiveScaleKey, aerial_perspective_scale_);
  any_loaded
    |= load_float(kAerialScatteringStrengthKey, aerial_scattering_strength_);
  any_loaded |= load_vec3(kOzoneRgbKey, ozone_rgb_);

  engine::atmos::DensityProfile loaded_profile = ozone_profile_;
  bool ozone_profile_loaded = false;
  ozone_profile_loaded |= load_float(
    kOzoneProfileLayer0WidthMKey, loaded_profile.layers[0].width_m);
  ozone_profile_loaded |= load_float(
    kOzoneProfileLayer0LinearTermKey, loaded_profile.layers[0].linear_term);
  ozone_profile_loaded |= load_float(
    kOzoneProfileLayer0ConstantTermKey, loaded_profile.layers[0].constant_term);
  ozone_profile_loaded |= load_float(
    kOzoneProfileLayer1LinearTermKey, loaded_profile.layers[1].linear_term);
  ozone_profile_loaded |= load_float(
    kOzoneProfileLayer1ConstantTermKey, loaded_profile.layers[1].constant_term);

  if (ozone_profile_loaded) {
    loaded_profile.layers[0].exp_term = 0.0F;
    loaded_profile.layers[1].width_m = 0.0F;
    loaded_profile.layers[1].exp_term = 0.0F;
    ozone_profile_ = loaded_profile;
  }

  any_loaded |= load_int(kSkyViewLutSlicesKey, sky_view_lut_slices_);
  any_loaded |= load_int(kSkyViewAltMappingModeKey, sky_view_alt_mapping_mode_);

  any_loaded |= load_bool(kSkySphereEnabledKey, sky_sphere_enabled_);
  any_loaded |= load_int(kSkySphereSourceKey, sky_sphere_source_);
  any_loaded |= load_vec3(kSkySphereSolidColorKey, sky_sphere_solid_color_);
  any_loaded |= load_float(kSkySphereRotationKey, sky_sphere_rotation_deg_);

  const bool sky_intensity_loaded
    = load_float(kSkySphereIntensityKey, sky_intensity_);
  const bool sky_light_intensity_mul_loaded
    = load_float(kSkyLightIntensityMulKey, sky_light_intensity_mul_);

  any_loaded |= sky_intensity_loaded || sky_light_intensity_mul_loaded;

  bool skybox_settings_loaded = false;
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
  any_loaded |= load_float(kSkyLightIntensityMulKey, sky_light_intensity_mul_);
  any_loaded |= load_float(kSkyLightDiffuseKey, sky_light_diffuse_);
  any_loaded |= load_float(kSkyLightSpecularKey, sky_light_specular_);

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

  any_loaded |= load_int(kEnvironmentPresetKey, preset_index_);

  any_loaded |= load_bool(kSunEnabledKey, sun_enabled_);
  const bool sun_source_loaded = load_int(kSunSourceKey, sun_source_);
  any_loaded |= sun_source_loaded;
  any_loaded |= load_float(kSunAzimuthKey, sun_azimuth_deg_);
  any_loaded |= load_float(kSunElevationKey, sun_elevation_deg_);
  any_loaded |= load_vec3(kSunColorKey, sun_color_rgb_);
  any_loaded |= load_float(kSunIlluminanceKey, sun_illuminance_lx_);
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

  if (any_loaded) {
    settings_loaded_ = true;
    needs_sync_ = false;
    pending_changes_ = true;
    skybox_dirty_ = skybox_settings_loaded;
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

  save_bool(kSkyAtmoEnabledKey, sky_atmo_enabled_);
  save_float(kPlanetRadiusKey, planet_radius_km_);
  save_float(kAtmosphereHeightKey, atmosphere_height_km_);
  save_vec3(kGroundAlbedoKey, ground_albedo_);
  save_float(kRayleighScaleHeightKey, rayleigh_scale_height_km_);
  save_float(kMieScaleHeightKey, mie_scale_height_km_);
  save_float(kMieAnisotropyKey, mie_anisotropy_);
  save_float(kMultiScatteringKey, multi_scattering_);
  save_bool(kSunDiskEnabledKey, sun_disk_enabled_);
  save_float(kAerialPerspectiveScaleKey, aerial_perspective_scale_);
  save_float(kAerialPerspectiveScaleKey, aerial_perspective_scale_);
  save_float(kAerialScatteringStrengthKey, aerial_scattering_strength_);

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

  save_int(kSkyViewLutSlicesKey, sky_view_lut_slices_);
  save_int(kSkyViewAltMappingModeKey, sky_view_alt_mapping_mode_);

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

  save_bool(kFogEnabledKey, fog_enabled_);
  save_int(kFogModelKey, fog_model_);
  save_float(kFogExtinctionSigmaTKey, fog_extinction_sigma_t_per_m_);
  save_float(kFogHeightFalloffKey, fog_height_falloff_per_m_);
  save_float(kFogHeightOffsetKey, fog_height_offset_m_);
  save_float(kFogStartDistanceKey, fog_start_distance_m_);
  save_float(kFogMaxOpacityKey, fog_max_opacity_);
  save_vec3(kFogSingleScatteringAlbedoKey, fog_single_scattering_albedo_rgb_);
  save_int(kEnvironmentPresetKey, preset_index_);

  save_bool(kSunEnabledKey, sun_enabled_);
  save_int(kSunSourceKey, sun_source_);
  save_float(kSunAzimuthKey, sun_azimuth_deg_);
  save_float(kSunElevationKey, sun_elevation_deg_);
  save_vec3(kSunColorKey, sun_color_rgb_);
  save_float(kSunIlluminanceKey, sun_illuminance_lx_);
  save_bool(kSunUseTemperatureKey, sun_use_temperature_);
  save_float(kSunTemperatureKey, sun_temperature_kelvin_);
  save_float(kSunDiskRadiusKey, sun_component_disk_radius_deg_);
}

auto EnvironmentSettingsService::MarkDirty() -> void
{
  pending_changes_ = true;
  if (sky_light_enabled_
    && sky_light_source_
      == static_cast<int>(scene::environment::SkyLightSource::kCapturedScene)) {
    needs_sky_capture_ = true;
  }
  if (update_depth_ == 0) {
    SaveSettings();
    epoch_++;
  }
}

auto EnvironmentSettingsService::MaybeRequestSkyCapture() -> void
{
  if (!needs_sky_capture_ || applied_changes_this_frame_ || pending_changes_) {
    return;
  }

  if (!config_.renderer) {
    return;
  }

  if (!sky_light_enabled_
    || sky_light_source_
      != static_cast<int>(scene::environment::SkyLightSource::kCapturedScene)) {
    return;
  }

  if (sky_atmo_enabled_) {
    if (const auto lut_mgr = config_.renderer->GetSkyAtmosphereLutManager()) {
      if (!lut_mgr->HasBeenGenerated() || lut_mgr->IsDirty()) {
        DLOG_F(
          INFO, "Skipping sky capture because LUT Manager will regenerate");
        return;
      }
    }
  }

  DLOG_F(INFO,
    "Requesting sky capture from renderer because user clicked the button or "
    "changed a setting that requires it");
  config_.renderer->RequestSkyCapture();
  needs_sky_capture_ = false;
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
  sun_illuminance_lx_ = defaults.GetIlluminanceLx();
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
  sun_illuminance_lx_ = settings.illuminance_lx;
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
  settings.illuminance_lx = sun_illuminance_lx_;
  settings.use_temperature = sun_use_temperature_;
  settings.temperature_kelvin = sun_temperature_kelvin_;
  settings.disk_radius_deg = sun_component_disk_radius_deg_;
}

} // namespace oxygen::examples
