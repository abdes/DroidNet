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

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Pipeline/CompositionView.h>
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
  constexpr std::string_view kEnvironmentSettingsSchemaVersionKey
    = "env.settings.schema_version";
  constexpr std::string_view kEnvironmentCustomStatePresentKey
    = "env.settings.custom_state_present";
  constexpr float kCurrentSettingsSchemaVersion = 2.0F;
  constexpr int kPresetUseScene = -2;
  constexpr int kPresetCustom = -1;
  // Demo policy: UI environment settings are authoritative and always
  // override scene environment data.
  constexpr bool kForceEnvironmentOverride = true;

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
      sun_present_ = true;
      return;
    }

    if (preset_index_ == kPresetUseScene) {
      pending_changes_ = false;
      dirty_domains_ = ToMask(DirtyDomain::kNone);
      batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
      needs_sync_ = true;
      apply_saved_sun_on_next_sync_ = true;
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
        apply_saved_sun_on_next_sync_ = true;
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

auto EnvironmentSettingsService::OnSceneActivated(scene::Scene& scene)
  -> void
{
  PersistSettingsIfDirty();
  config_.scene = observer_ptr { &scene };
  // Ensure the next runtime config update runs scene-transition logic even
  // though config_.scene is pre-bound here for immediate HasScene() correctness.
  force_scene_rebind_ = true;
  config_.skybox_service = nullptr;
  main_view_id_.reset();
  needs_sync_ = true;
  apply_saved_sun_on_next_sync_ = true;
  sun_light_available_ = false;
  sun_light_node_ = {};
  synthetic_sun_light_node_ = {};
  synthetic_sun_light_created_ = false;
  pending_changes_ = false;
  dirty_domains_ = ToMask(DirtyDomain::kNone);
  batched_dirty_domains_ = ToMask(DirtyDomain::kNone);
  epoch_++;
}

auto EnvironmentSettingsService::OnMainViewReady(
  const engine::FrameContext& /*context*/,
  const renderer::CompositionView& view) -> void
{
  main_view_id_ = view.id;
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
  DLOG_F(INFO,
    "EnvironmentSettingsService: preset index changed to {} (rev={})",
    preset_index_, settings_revision_);
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
  apply_saved_sun_on_next_sync_ = true;
  SyncFromSceneIfNeeded();
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
      "EnvironmentSettingsService: deferring scene sync while pending UI "
      "changes exist (mask=0x{:X} rev={})",
      dirty_domains_, settings_revision_);
    return;
  }
  if (update_depth_ > 0) {
    DLOG_F(WARNING,
      "EnvironmentSettingsService: deferring scene sync while update "
      "transaction is active (depth={})",
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
    if (auto lut_mgr
      = config_.renderer->GetSkyAtmosphereLutManagerForView(*main_view_id_)) {
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  MarkDirty(ToMask(DirtyDomain::kAtmosphere));
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
  DLOG_F(INFO,
    "EnvironmentSettingsService: SkyView LUT slices are renderer-owned; "
    "ignoring UI write {} (current={})",
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
  DLOG_F(INFO,
    "EnvironmentSettingsService: SkyView mapping mode is renderer-owned; "
    "ignoring UI write {} (current={})",
    value, sky_view_alt_mapping_mode_);
}

auto EnvironmentSettingsService::RequestRegenerateLut() -> void
{
  DLOG_F(INFO,
    "EnvironmentSettingsService: RequestRegenerateLut ignored; renderer owns "
    "LUT regeneration");
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
  MarkDirty(ToMask(DirtyDomain::kFog));
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
  MarkDirty(ToMask(DirtyDomain::kFog));
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
  MarkDirty(ToMask(DirtyDomain::kFog));
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
  MarkDirty(ToMask(DirtyDomain::kFog));
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
  MarkDirty(ToMask(DirtyDomain::kFog));
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
  MarkDirty(ToMask(DirtyDomain::kFog));
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
  MarkDirty(ToMask(DirtyDomain::kFog));
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
  MarkDirty(ToMask(DirtyDomain::kFog));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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
  MarkDirty(ToMask(DirtyDomain::kSun));
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

  DLOG_F(INFO,
    "EnvironmentSettingsService: applying pending changes mask=0x{:X} rev={}",
    dirty_domains_, settings_revision_);
  ValidateAndClampState();
  NormalizeSkySystems();
  const bool apply_atmosphere
    = HasDirty(dirty_domains_, DirtyDomain::kAtmosphere);
  const bool apply_sun = HasDirty(dirty_domains_, DirtyDomain::kSun);
  const bool apply_fog = HasDirty(dirty_domains_, DirtyDomain::kFog);
  const bool apply_sky_sphere
    = HasDirty(dirty_domains_, DirtyDomain::kSkySphere);
  const bool apply_sky_light = HasDirty(dirty_domains_, DirtyDomain::kSkyLight);
  const bool apply_skybox = HasDirty(dirty_domains_, DirtyDomain::kSkybox);

  const auto cache_atmo_before = CaptureAtmosphereCanonicalState();
  const auto scene_atmo_before = CaptureSceneAtmosphereCanonicalState();
  if (apply_atmosphere) {
    DLOG_F(INFO,
      "EnvironmentSettingsService: atmosphere hash before apply cache=0x{:X} "
      "scene=0x{:X}",
      HashAtmosphereState(cache_atmo_before),
      scene_atmo_before.has_value() ? HashAtmosphereState(*scene_atmo_before)
                                    : 0ULL);
  }

  auto env = config_.scene->GetEnvironment();
  if (!env) {
    config_.scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    env = config_.scene->GetEnvironment();
  }

  auto sun = env->TryGetSystem<scene::environment::Sun>();
  if (apply_sun && sun_present_ && sun_enabled_ && (sun == nullptr)) {
    sun = observer_ptr { &env->AddSystem<scene::environment::Sun>() };
  }
  if (apply_sun && sun) {
    sun->SetEnabled(sun_enabled_);
  }
  if (apply_sun && (sun) && !sun_enabled_) {
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

  if (apply_sun && sun_enabled_ && (sun != nullptr)) {
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
  } else if (apply_sun && sun) {
    sun->ClearLightReference();
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
    atmo->SetSunDiskEnabled(atmosphere_state.sun_disk_enabled);
    atmo->SetAerialPerspectiveDistanceScale(
      atmosphere_state.aerial_perspective_scale);
    atmo->SetAerialScatteringStrength(
      atmosphere_state.aerial_scattering_strength);

    if (config_.on_atmosphere_params_changed) {
      config_.on_atmosphere_params_changed();
      LOG_F(INFO,
        "EnvironmentSettingsService: atmosphere parameters changed "
        "(SunDiskEnabled={})",
        sun_disk_enabled_);
    }
  }

  auto fog = env->TryGetSystem<scene::environment::Fog>();
  if (apply_fog && fog_enabled_ && !fog) {
    fog = observer_ptr { &env->AddSystem<scene::environment::Fog>() };
  }
  if (apply_fog && fog) {
    fog->SetEnabled(fog_enabled_);
  }
  if (apply_fog && fog_enabled_ && fog) {
    fog->SetModel(static_cast<scene::environment::FogModel>(fog_model_));
    fog->SetExtinctionSigmaTPerMeter(fog_extinction_sigma_t_per_m_);
    fog->SetHeightFalloffPerMeter(fog_height_falloff_per_m_);
    fog->SetHeightOffsetMeters(fog_height_offset_m_);
    fog->SetStartDistanceMeters(fog_start_distance_m_);
    fog->SetMaxOpacity(fog_max_opacity_);
    fog->SetSingleScatteringAlbedoRgb(fog_single_scattering_albedo_rgb_);
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
  }

  if (apply_skybox || apply_sky_sphere) {
    MaybeAutoLoadSkybox();
  }

  if (apply_atmosphere) {
    const auto cache_atmo_after = CaptureAtmosphereCanonicalState();
    const auto scene_atmo_after = CaptureSceneAtmosphereCanonicalState();
    DLOG_F(INFO,
      "EnvironmentSettingsService: atmosphere hash after apply cache=0x{:X} "
      "scene=0x{:X}",
      HashAtmosphereState(cache_atmo_after),
      scene_atmo_after.has_value() ? HashAtmosphereState(*scene_atmo_after)
                                   : 0ULL);
    if (scene_atmo_before.has_value() && scene_atmo_after.has_value()) {
      LogAtmosphereStateDiff(
        "EnvironmentSettingsService: scene atmosphere diff", *scene_atmo_before,
        *scene_atmo_after);
    }
  }

  settings_persist_dirty_ = true;
  applied_changes_this_frame_ = true;
  pending_changes_ = false;
  dirty_domains_ = ToMask(DirtyDomain::kNone);
  saved_sun_source_ = sun_source_;
}

auto EnvironmentSettingsService::SyncFromScene() -> void
{
  if (!config_.scene) {
    return;
  }
  const auto cache_atmo_before = CaptureAtmosphereCanonicalState();

  auto env = config_.scene->GetEnvironment();
  if (!env) {
    if (apply_saved_sun_on_next_sync_) {
      ApplySavedSunSourcePreference();
      apply_saved_sun_on_next_sync_ = false;
    }
    pending_changes_ = false;
    dirty_domains_ = ToMask(DirtyDomain::kNone);
    return;
  }

  if (const auto atmosphere_from_scene = CaptureSceneAtmosphereCanonicalState();
    atmosphere_from_scene.has_value()) {
    const auto& atmo_state = *atmosphere_from_scene;
    sky_atmo_enabled_ = atmo_state.enabled;
    planet_radius_km_ = atmo_state.planet_radius_km;
    atmosphere_height_km_ = atmo_state.atmosphere_height_km;
    ground_albedo_ = atmo_state.ground_albedo;
    rayleigh_scale_height_km_ = atmo_state.rayleigh_scale_height_km;
    mie_scale_height_km_ = atmo_state.mie_scale_height_km;
    mie_anisotropy_ = atmo_state.mie_anisotropy;
    mie_absorption_scale_
      = std::clamp(atmo_state.mie_absorption_scale, 0.0F, 5.0F);
    multi_scattering_ = atmo_state.multi_scattering;
    sun_disk_enabled_ = atmo_state.sun_disk_enabled;
    aerial_perspective_scale_ = atmo_state.aerial_perspective_scale;
    aerial_scattering_strength_ = atmo_state.aerial_scattering_strength;
    ozone_rgb_ = atmo_state.ozone_rgb;
    ozone_profile_ = atmo_state.ozone_profile;
  } else {
    sky_atmo_enabled_ = false;
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
  } else {
    fog_enabled_ = false;
  }

  // Sync LUT slice configuration from the renderer's LUT manager.
  if (config_.renderer && main_view_id_.has_value()) {
    if (auto lut_mgr
      = config_.renderer->GetSkyAtmosphereLutManagerForView(*main_view_id_)) {
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
  } else {
    sky_light_enabled_ = false;
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
  } else {
    sun_present_ = false;
    sun_light_available_ = false;
  }

  if (apply_saved_sun_on_next_sync_) {
    ApplySavedSunSourcePreference();
    apply_saved_sun_on_next_sync_ = false;
  }

  const auto cache_atmo_after = CaptureAtmosphereCanonicalState();
  if (HashAtmosphereState(cache_atmo_before)
    != HashAtmosphereState(cache_atmo_after)) {
    LogAtmosphereStateDiff(
      "EnvironmentSettingsService: scene sync overwrote UI cache",
      cache_atmo_before, cache_atmo_after);
  }

  ValidateAndClampState();
  NormalizeSkySystems();
  pending_changes_ = false;
  dirty_domains_ = ToMask(DirtyDomain::kNone);
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

auto EnvironmentSettingsService::CaptureAtmosphereCanonicalState() const
  -> AtmosphereCanonicalState
{
  AtmosphereCanonicalState state {};
  state.enabled = sky_atmo_enabled_;
  state.planet_radius_km = planet_radius_km_;
  state.atmosphere_height_km = atmosphere_height_km_;
  state.ground_albedo = ground_albedo_;
  state.rayleigh_scale_height_km = rayleigh_scale_height_km_;
  state.mie_scale_height_km = mie_scale_height_km_;
  state.mie_anisotropy = mie_anisotropy_;
  state.mie_absorption_scale = mie_absorption_scale_;
  state.multi_scattering = multi_scattering_;
  state.ozone_rgb = ozone_rgb_;
  state.ozone_profile = ozone_profile_;
  state.sun_disk_enabled = sun_disk_enabled_;
  state.aerial_perspective_scale = aerial_perspective_scale_;
  state.aerial_scattering_strength = aerial_scattering_strength_;
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
  state.ozone_rgb = atmo->GetAbsorptionRgb();
  state.ozone_profile = atmo->GetOzoneDensityProfile();
  state.sun_disk_enabled = atmo->GetSunDiskEnabled();
  state.aerial_perspective_scale = atmo->GetAerialPerspectiveDistanceScale();
  state.aerial_scattering_strength = atmo->GetAerialScatteringStrength();
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
  seed = HashCombineU64(seed, FloatBits(state.aerial_scattering_strength));
  return seed;
}

auto EnvironmentSettingsService::LogAtmosphereStateDiff(std::string_view prefix,
  const AtmosphereCanonicalState& before, const AtmosphereCanonicalState& after)
  -> void
{
  if (before.enabled != after.enabled) {
    DLOG_F(INFO, "{} atmosphere.enabled: {} -> {}", prefix, before.enabled,
      after.enabled);
  }
  if (before.planet_radius_km != after.planet_radius_km) {
    DLOG_F(INFO, "{} atmosphere.planet_radius_km: {} -> {}", prefix,
      before.planet_radius_km, after.planet_radius_km);
  }
  if (before.atmosphere_height_km != after.atmosphere_height_km) {
    DLOG_F(INFO, "{} atmosphere.atmosphere_height_km: {} -> {}", prefix,
      before.atmosphere_height_km, after.atmosphere_height_km);
  }
  if (before.mie_absorption_scale != after.mie_absorption_scale) {
    DLOG_F(INFO, "{} atmosphere.mie_absorption_scale: {} -> {}", prefix,
      before.mie_absorption_scale, after.mie_absorption_scale);
  }
  if (before.multi_scattering != after.multi_scattering) {
    DLOG_F(INFO, "{} atmosphere.multi_scattering: {} -> {}", prefix,
      before.multi_scattering, after.multi_scattering);
  }
  if (before.sun_disk_enabled != after.sun_disk_enabled) {
    DLOG_F(INFO, "{} atmosphere.sun_disk_enabled: {} -> {}", prefix,
      before.sun_disk_enabled, after.sun_disk_enabled);
  }
}

auto EnvironmentSettingsService::ValidateAndClampState() -> void
{
  auto clamp_float
    = [](std::string_view key, float& value, float min_v, float max_v) {
        const float before = value;
        value = std::clamp(value, min_v, max_v);
        if (before != value) {
          DLOG_F(WARNING, "EnvironmentSettingsService: clamped {}: {} -> {}",
            key, before, value);
        }
      };
  auto clamp_int = [](std::string_view key, int& value, int min_v, int max_v) {
    const int before = value;
    value = std::clamp(value, min_v, max_v);
    if (before != value) {
      DLOG_F(WARNING, "EnvironmentSettingsService: clamped {}: {} -> {}", key,
        before, value);
    }
  };
  auto clamp_vec3 = [](std::string_view key, glm::vec3& value, float min_v,
                      float max_v) {
    const glm::vec3 before = value;
    value = ClampVec3(value, min_v, max_v);
    if (before != value) {
      DLOG_F(WARNING,
        "EnvironmentSettingsService: clamped {}: ({}, {}, {}) -> ({}, {}, {})",
        key, before.x, before.y, before.z, value.x, value.y, value.z);
    }
  };
  auto clamp_vec3_min = [](
                          std::string_view key, glm::vec3& value, float min_v) {
    const glm::vec3 before = value;
    value = glm::max(value, glm::vec3(min_v));
    if (before != value) {
      DLOG_F(WARNING,
        "EnvironmentSettingsService: clamped {}: ({}, {}, {}) -> ({}, {}, {})",
        key, before.x, before.y, before.z, value.x, value.y, value.z);
    }
  };

  clamp_float("env.atmo.planet_radius_km", planet_radius_km_, 1.0F, 100000.0F);
  clamp_float(
    "env.atmo.atmosphere_height_km", atmosphere_height_km_, 0.1F, 1000.0F);
  clamp_vec3("env.atmo.ground_albedo", ground_albedo_, 0.0F, 1.0F);
  clamp_float("env.atmo.rayleigh_scale_height_km", rayleigh_scale_height_km_,
    0.01F, 100.0F);
  clamp_float(
    "env.atmo.mie_scale_height_km", mie_scale_height_km_, 0.01F, 50.0F);
  clamp_float("env.atmo.mie_anisotropy", mie_anisotropy_, 0.0F, 0.999F);
  clamp_float(
    "env.atmo.mie_absorption_scale", mie_absorption_scale_, 0.0F, 5.0F);
  clamp_float("env.atmo.multi_scattering", multi_scattering_, 0.0F, 5.0F);
  clamp_vec3_min("env.atmo.ozone_rgb", ozone_rgb_, 0.0F);
  clamp_float("env.atmo.aerial_perspective_scale", aerial_perspective_scale_,
    0.0F, 16.0F);
  clamp_float("env.atmo.aerial_scattering_strength",
    aerial_scattering_strength_, 0.0F, 16.0F);

  clamp_float("env.atmo.ozone_profile.layer0.width_m",
    ozone_profile_.layers[0].width_m, 0.0F, 200000.0F);
  clamp_float("env.atmo.ozone_profile.layer0.linear_term",
    ozone_profile_.layers[0].linear_term, -1.0F, 1.0F);
  clamp_float("env.atmo.ozone_profile.layer0.constant_term",
    ozone_profile_.layers[0].constant_term, -1.0F, 1.0F);
  ozone_profile_.layers[0].exp_term = 0.0F;
  ozone_profile_.layers[1].width_m = 0.0F;
  ozone_profile_.layers[1].exp_term = 0.0F;
  clamp_float("env.atmo.ozone_profile.layer1.linear_term",
    ozone_profile_.layers[1].linear_term, -1.0F, 1.0F);
  // For the canonical two-layer ozone profile, this term is commonly > 1
  // (Earth defaults to ~2.6667), so [-1, 1] causes false clamping.
  clamp_float("env.atmo.ozone_profile.layer1.constant_term",
    ozone_profile_.layers[1].constant_term, -1.0F, 8.0F);

  clamp_int("env.atmo.sky_view_lut_slices", sky_view_lut_slices_, 1, 128);
  clamp_int(
    "env.atmo.sky_view_alt_mapping_mode", sky_view_alt_mapping_mode_, 0, 1);

  clamp_int("env.sky_sphere.source", sky_sphere_source_, 0, 1);
  clamp_vec3_min("env.sky_sphere.solid_color", sky_sphere_solid_color_, 0.0F);
  clamp_float("env.sky_sphere.intensity", sky_intensity_, 0.0F, 1000.0F);
  clamp_float(
    "env.sky_sphere.rotation_deg", sky_sphere_rotation_deg_, -3600.0F, 3600.0F);

  clamp_int("env.skybox.layout", skybox_layout_idx_, 0, 4);
  clamp_int("env.skybox.output", skybox_output_format_idx_, 0, 3);
  clamp_int("env.skybox.face_size", skybox_face_size_, 16, 4096);
  clamp_float(
    "env.skybox.hdr_exposure_ev", skybox_hdr_exposure_ev_, 0.0F, 24.0F);

  clamp_int("env.sky_light.source", sky_light_source_, 0, 1);
  clamp_vec3_min("env.sky_light.tint", sky_light_tint_, 0.0F);
  clamp_float(
    "env.sky_light.intensity_mul", sky_light_intensity_mul_, 0.0F, 100.0F);
  clamp_float("env.sky_light.diffuse", sky_light_diffuse_, 0.0F, 100.0F);
  clamp_float("env.sky_light.specular", sky_light_specular_, 0.0F, 100.0F);

  clamp_int("env.fog.model", fog_model_, 0, 1);
  clamp_float("env.fog.extinction_sigma_t_per_m", fog_extinction_sigma_t_per_m_,
    0.0F, 10.0F);
  clamp_float(
    "env.fog.height_falloff_per_m", fog_height_falloff_per_m_, 0.0F, 10.0F);
  clamp_float(
    "env.fog.height_offset_m", fog_height_offset_m_, -100000.0F, 100000.0F);
  clamp_float(
    "env.fog.start_distance_m", fog_start_distance_m_, 0.0F, 1000000.0F);
  clamp_float("env.fog.max_opacity", fog_max_opacity_, 0.0F, 1.0F);
  clamp_vec3("env.fog.single_scattering_albedo_rgb",
    fog_single_scattering_albedo_rgb_, 0.0F, 1.0F);

  clamp_int("env.sun.source", sun_source_, 0, 1);
  clamp_float("env.sun.azimuth_deg", sun_azimuth_deg_, -720.0F, 720.0F);
  clamp_float("env.sun.elevation_deg", sun_elevation_deg_, -90.0F, 90.0F);
  clamp_vec3_min("env.sun.color", sun_color_rgb_, 0.0F);
  clamp_float("env.sun.illuminance_lx", sun_illuminance_lx_, 0.0F, 250000.0F);
  clamp_float(
    "env.sun.temperature_kelvin", sun_temperature_kelvin_, 1000.0F, 40000.0F);
  clamp_float(
    "env.sun.disk_radius_deg", sun_component_disk_radius_deg_, 0.01F, 2.0F);

  clamp_int("environment_preset_index", preset_index_, -2, 64);
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
  bool sun_source_loaded = false;
  if (load_custom_state) {
    any_loaded |= load_bool(kSkyAtmoEnabledKey, sky_atmo_enabled_);
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
    sun_source_loaded = load_int(kSunSourceKey, sun_source_);
    any_loaded |= sun_source_loaded;
    any_loaded |= load_float(kSunAzimuthKey, sun_azimuth_deg_);
    any_loaded |= load_float(kSunElevationKey, sun_elevation_deg_);
    any_loaded |= load_vec3(kSunColorKey, sun_color_rgb_);
    any_loaded |= load_float(kSunIlluminanceKey, sun_illuminance_lx_);
    any_loaded |= load_bool(kSunUseTemperatureKey, sun_use_temperature_);
    any_loaded |= load_float(kSunTemperatureKey, sun_temperature_kelvin_);
    any_loaded |= load_float(kSunDiskRadiusKey, sun_component_disk_radius_deg_);
  }

  if (sun_source_loaded) {
    saved_sun_source_ = sun_source_;
    apply_saved_sun_on_next_sync_ = true;
    SaveSunSettingsToProfile(sun_source_);
    if (sun_source_ == 1) {
      sun_present_ = true;
    }
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
  if (kForceEnvironmentOverride) {
    sun_present_ = true;
  }
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

  if (preset_index_ != kPresetCustom) {
    return;
  }

  save_bool(kSkyAtmoEnabledKey, sky_atmo_enabled_);
  save_float(kPlanetRadiusKey, planet_radius_km_);
  save_float(kAtmosphereHeightKey, atmosphere_height_km_);
  save_vec3(kGroundAlbedoKey, ground_albedo_);
  save_float(kRayleighScaleHeightKey, rayleigh_scale_height_km_);
  save_float(kMieScaleHeightKey, mie_scale_height_km_);
  save_float(kMieAnisotropyKey, mie_anisotropy_);
  save_float(kMieAbsorptionScaleKey, mie_absorption_scale_);
  save_float(kMultiScatteringKey, multi_scattering_);
  save_bool(kSunDiskEnabledKey, sun_disk_enabled_);
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

auto EnvironmentSettingsService::MarkDirty(uint32_t dirty_domains) -> void
{
  ValidateAndClampState();
  uint32_t effective_domains = dirty_domains;
  if ((dirty_domains & ToMask(DirtyDomain::kSun)) != 0U && sky_atmo_enabled_) {
    // Sky-atmosphere LUT generation depends on sun state (not just atmosphere
    // material params), so sun edits must also drive atmosphere
    // apply/invalidate.
    effective_domains |= ToMask(DirtyDomain::kAtmosphere);
  }
  if (update_depth_ > 0) {
    batched_dirty_domains_ |= effective_domains;
    settings_persist_dirty_ = true;
    DLOG_F(INFO,
      "EnvironmentSettingsService: batched dirty domains=0x{:X} "
      "pending_batch=0x{:X} depth={}",
      dirty_domains, batched_dirty_domains_, update_depth_);
    return;
  }
  pending_changes_ = true;
  dirty_domains_ |= effective_domains;
  settings_persist_dirty_ = true;
  settings_revision_++;
  DLOG_F(INFO,
    "EnvironmentSettingsService: marked dirty domains=0x{:X} effective=0x{:X} "
    "pending_mask=0x{:X} rev={}",
    dirty_domains, effective_domains, dirty_domains_, settings_revision_);
  if (update_depth_ == 0) {
    epoch_++;
  }
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
    MarkDirty(ToMask(DirtyDomain::kSun));
    return;
  }

  if (sun_source_ != desired_source) {
    sun_source_ = desired_source;
    LoadSunSettingsFromProfile(sun_source_);
    MarkDirty(ToMask(DirtyDomain::kSun));
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
