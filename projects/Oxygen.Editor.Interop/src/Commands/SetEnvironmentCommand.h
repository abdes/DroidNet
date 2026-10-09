//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <EditorModule/EditorCommand.h>
#include <EditorModule/SceneAssetRequests.h>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::interop::module {

  struct SkyAtmosphereParams {
    bool enabled = true;
    bool sun_disk_enabled = true;
    float planet_radius_m = oxygen::engine::atmos::kDefaultPlanetRadiusM;
    float atmosphere_height_m
      = oxygen::engine::atmos::kDefaultAtmosphereHeightM;
    Vec3 ground_albedo_rgb { 0.4F, 0.4F, 0.4F };
    float rayleigh_scale_height_m
      = oxygen::engine::atmos::kDefaultRayleighScaleHeightM;
    float mie_scale_height_m
      = oxygen::engine::atmos::kDefaultMieScaleHeightM;
    float mie_anisotropy = oxygen::engine::atmos::kDefaultMieAnisotropyG;
    Vec3 sky_luminance_factor_rgb { 1.0F, 1.0F, 1.0F };
    float aerial_perspective_distance_scale = 1.0F;
    float aerial_scattering_strength = 1.0F;
    float aerial_perspective_start_depth_m = 100.0F;
    float height_fog_contribution = 1.0F;
  };

  struct PostProcessParams {
    int tone_mapper = 1;
    int exposure_mode = 0;
    bool exposure_enabled = true;
    float exposure_compensation_ev = 0.0F;
    float exposure_key = oxygen::engine::kExposureCalibrationKey;
    float manual_exposure_ev = 13.0F;
    float auto_exposure_min_ev = -6.0F;
    float auto_exposure_max_ev = 16.0F;
    float auto_exposure_speed_up = 3.0F;
    float auto_exposure_speed_down = 1.0F;
    int auto_exposure_metering_mode = 0;
    float auto_exposure_low_percentile = 0.1F;
    float auto_exposure_high_percentile = 0.9F;
    float auto_exposure_min_log_luminance = -12.0F;
    float auto_exposure_log_luminance_range = 25.0F;
    float auto_exposure_target_luminance = 0.18F;
    float auto_exposure_spot_meter_radius = 0.2F;
    float auto_exposure_black_influence = 0.0F;
    float auto_exposure_transition_distance_ev = engine::kDefaultExposureTransitionDistance;
    std::vector<scene::ExposureCompensationKey> auto_exposure_compensation_curve;
    std::optional<content::TextureResourceLocator> auto_exposure_metering_mask;
    std::optional<std::wstring> auto_exposure_metering_mask_mount;
    float bloom_intensity = 0.0F;
    float bloom_threshold = 1.0F;
    float saturation = 1.0F;
    float contrast = 1.0F;
    float vignette_intensity = 0.0F;
    float display_gamma = 2.2F;
  };

  //! Authored height fog and volumetric fog. Defaults match the native Fog
  //! system except `enabled`, which is off for scenes without authored fog.
  struct FogParams {
    bool enabled = false;
    bool height_fog_enabled = true;
    float density = 0.002F;
    float height_falloff = 0.02F;
    float height_offset_meters = 0.0F;
    float max_opacity = 1.0F;
    Vec3 inscattering_luminance_rgb { 0.0F, 0.0F, 0.0F };
    Vec3 sky_ambient_scale_rgb { 1.0F, 1.0F, 1.0F };
    float second_density = 0.0F;
    float second_height_falloff = 0.0F;
    float second_height_offset_meters = 0.0F;
    float start_distance_meters = 0.0F;
    float end_distance_meters = 0.0F;
    float cutoff_distance_meters = 0.0F;
    Vec3 directional_inscattering_luminance_rgb { 0.0F, 0.0F, 0.0F };
    float directional_inscattering_exponent = 4.0F;
    float directional_inscattering_start_distance_meters = 10000.0F;
    bool volumetric_fog_enabled = false;
    float volumetric_scattering_distribution = 0.0F;
    Vec3 volumetric_albedo_rgb { 1.0F, 1.0F, 1.0F };
    Vec3 volumetric_emissive_rgb { 0.0F, 0.0F, 0.0F };
    float volumetric_extinction_scale = 1.0F;
    float volumetric_distance_meters = 0.0F;
    float volumetric_start_distance_meters = 0.0F;
    float volumetric_near_fade_in_distance_meters = 0.0F;
    float volumetric_static_lighting_scattering_intensity = 1.0F;
    bool override_light_colors_with_fog_inscattering = false;
    bool render_in_main_pass = true;
    bool holdout = false;
    bool visible_in_reflection_captures = true;
    bool visible_in_real_time_sky_captures = true;
  };

  //! A cooked cube texture an environment system binds; no locator binds none.
  struct CubemapSource {
    std::optional<content::TextureResourceLocator> locator;
    //! Project mount whose current cooked root replaces the locator's root.
    std::optional<std::wstring> project_mount;
  };

  //! Authored Sky Sphere backdrop. Defaults match the native system except
  //! `enabled`, which is off for scenes without an authored sky sphere.
  struct SkySphereParams {
    bool enabled = false;
    int source = 0; //!< SkySphereSource: 0 cubemap, 1 solid color.
    CubemapSource cubemap;
    Vec3 solid_color_rgb { 0.0F, 0.0F, 0.0F };
    //! Calibrated upward illuminance in lux; zero keeps the imported radiance.
    float illuminance_lux = 0.0F;
    float intensity = 1.0F;
    float rotation_radians = 0.0F;
    Vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
  };

  //! Authored Sky Light image-based lighting; defaults match the native system.
  struct SkyLightParams {
    bool enabled = true;
    int source = 0; //!< SkyLightSource: 0 captured scene, 1 specified cubemap.
    CubemapSource cubemap;
    //! Calibrated upward illuminance of the cubemap in lux; zero keeps it raw.
    float illuminance_lux = 0.0F;
    float intensity = 1.0F;
    Vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
    float diffuse_intensity = 1.0F;
    float specular_intensity = 1.0F;
    float cubemap_angle_radians = 0.0F;
    Vec3 lower_hemisphere_color { 0.0F, 0.0F, 0.0F };
    bool lower_hemisphere_is_solid_color = true;
    float lower_hemisphere_blend_alpha = 1.0F;
    float volumetric_scattering_intensity = 1.0F;
    bool affect_reflections = true;
  };

  //! Authored display-only backdrop color, in linear SDR RGB.
  struct BackgroundParams {
    bool enabled = false;
    Vec3 color_rgb { 0.0F, 0.0F, 0.0F };
  };

  //! What shows behind the scene and how the sky lights it.
  struct SkyParams {
    SkySphereParams sky_sphere;
    SkyLightParams sky_light;
    BackgroundParams background;
  };

  class SetEnvironmentCommand final : public EditorCommand {
  public:
    SetEnvironmentCommand(SkyAtmosphereParams atmosphere,
      PostProcessParams post_process, FogParams fog = {}, SkyParams sky = {})
      : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
      , atmosphere_(atmosphere)
      , post_process_(std::move(post_process))
      , fog_(fog)
      , sky_(std::move(sky))
    {
    }

    void Execute(CommandContext& context) override;
    void SetFailureCallback(SceneAssetRequests::FailureCallback callback) {
      failure_callback_ = std::move(callback);
    }
    void SetSuccessCallback(SceneAssetRequests::SuccessCallback callback) {
      success_callback_ = std::move(callback);
    }

  private:
    SkyAtmosphereParams atmosphere_;
    PostProcessParams post_process_;
    FogParams fog_;
    SkyParams sky_;
    SceneAssetRequests::FailureCallback failure_callback_;
    SceneAssetRequests::SuccessCallback success_callback_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
