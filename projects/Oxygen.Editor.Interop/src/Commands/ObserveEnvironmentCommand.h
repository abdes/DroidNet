//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <functional>
#include <utility>

#include <Commands/SetEnvironmentCommand.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/SkyLightRuntimeState.h>

namespace oxygen::interop::module {

//! Scene-owned environment values sampled after preceding mutations.
struct EnvironmentObservation {
  bool exists = false;
  bool atmosphere_exists = false;
  bool post_process_exists = false;
  bool fog_exists = false;
  SkyAtmosphereParams atmosphere;
  PostProcessParams post_process;
  FogParams fog;
  content::ResourceKey metering_mask {};
  bool metering_mask_pending { false };
  std::string metering_mask_error;
  //! Last rendered state for this scene, preceding this mutation boundary.
  vortex::SkyLightRuntimeState sky_light;
};

//! Reads the live scene's authored environment properties in the mutation phase.
class ObserveEnvironmentCommand final : public EditorCommand {
public:
  //! Retains completion until the queued read executes or is discarded.
  explicit ObserveEnvironmentCommand(
    std::function<void(EnvironmentObservation)> complete)
    : EditorCommand(core::PhaseId::kSceneMutation)
    , complete_(std::move(complete)) {}

  //! Returns native component presence and stored values.
  //! Only reads scene state; changes nothing a pane shows.
  [[nodiscard]] auto GetInvalidation() const noexcept
    -> CommandInvalidation override
  {
    return CommandInvalidation::None();
  }

  void Execute(CommandContext& context) override
  {
    EnvironmentObservation result;
    if (context.AssetRequests) {
      const auto mask = context.AssetRequests->InspectExposureMask();
      result.metering_mask_pending = mask.pending;
      result.metering_mask_error = mask.error;
    }
    if (context.Scene) {
      if (context.Renderer) {
        result.sky_light = context.Renderer->InspectSkyLight(*context.Scene);
      }
      const auto environment = context.Scene->GetEnvironment();
      result.exists = environment != nullptr;
      if (environment) {
        namespace env = oxygen::scene::environment;
        if (const auto sky = environment->TryGetSystem<env::SkyAtmosphere>()) {
          result.atmosphere_exists = true;
          result.atmosphere.enabled = sky->IsEnabled();
          result.atmosphere.sun_disk_enabled = sky->GetSunDiskEnabled();
          result.atmosphere.planet_radius_m = sky->GetPlanetRadiusMeters();
          result.atmosphere.atmosphere_height_m = sky->GetAtmosphereHeightMeters();
          result.atmosphere.ground_albedo_rgb = sky->GetGroundAlbedoRgb();
          result.atmosphere.rayleigh_scale_height_m = sky->GetRayleighScaleHeightMeters();
          result.atmosphere.mie_scale_height_m = sky->GetMieScaleHeightMeters();
          result.atmosphere.mie_anisotropy = sky->GetMieAnisotropy();
          result.atmosphere.sky_luminance_factor_rgb = sky->GetSkyLuminanceFactorRgb();
          result.atmosphere.aerial_perspective_distance_scale = sky->GetAerialPerspectiveDistanceScale();
          result.atmosphere.aerial_scattering_strength = sky->GetAerialScatteringStrength();
          result.atmosphere.aerial_perspective_start_depth_m = sky->GetAerialPerspectiveStartDepthMeters();
          result.atmosphere.height_fog_contribution = sky->GetHeightFogContribution();
        }
        if (const auto fog = environment->TryGetSystem<env::Fog>()) {
          result.fog_exists = true;
          result.fog.enabled = fog->IsEnabled();
          result.fog.height_fog_enabled = fog->GetEnableHeightFog();
          result.fog.density = fog->GetExtinctionSigmaTPerMeter();
          result.fog.height_falloff = fog->GetHeightFalloffPerMeter();
          result.fog.height_offset_meters = fog->GetHeightOffsetMeters();
          result.fog.max_opacity = fog->GetMaxOpacity();
          result.fog.inscattering_luminance_rgb = fog->GetFogInscatteringLuminance();
          result.fog.sky_ambient_scale_rgb = fog->GetSkyAtmosphereAmbientContributionColorScale();
          result.fog.second_density = fog->GetSecondFogDensity();
          result.fog.second_height_falloff = fog->GetSecondFogHeightFalloff();
          result.fog.second_height_offset_meters = fog->GetSecondFogHeightOffset();
          result.fog.start_distance_meters = fog->GetStartDistanceMeters();
          result.fog.end_distance_meters = fog->GetEndDistanceMeters();
          result.fog.cutoff_distance_meters = fog->GetFogCutoffDistanceMeters();
          result.fog.directional_inscattering_luminance_rgb = fog->GetDirectionalInscatteringLuminance();
          result.fog.directional_inscattering_exponent = fog->GetDirectionalInscatteringExponent();
          result.fog.directional_inscattering_start_distance_meters = fog->GetDirectionalInscatteringStartDistance();
          result.fog.volumetric_fog_enabled = fog->GetEnableVolumetricFog();
          result.fog.volumetric_scattering_distribution = fog->GetVolumetricFogScatteringDistribution();
          result.fog.volumetric_albedo_rgb = fog->GetVolumetricFogAlbedo();
          result.fog.volumetric_emissive_rgb = fog->GetVolumetricFogEmissive();
          result.fog.volumetric_extinction_scale = fog->GetVolumetricFogExtinctionScale();
          result.fog.volumetric_distance_meters = fog->GetVolumetricFogDistance();
          result.fog.volumetric_start_distance_meters = fog->GetVolumetricFogStartDistance();
          result.fog.volumetric_near_fade_in_distance_meters = fog->GetVolumetricFogNearFadeInDistance();
          result.fog.volumetric_static_lighting_scattering_intensity = fog->GetVolumetricFogStaticLightingScatteringIntensity();
          result.fog.override_light_colors_with_fog_inscattering = fog->GetOverrideLightColorsWithFogInscatteringColors();
          result.fog.render_in_main_pass = fog->GetRenderInMainPass();
          result.fog.holdout = fog->GetHoldout();
          result.fog.visible_in_reflection_captures = fog->GetVisibleInReflectionCaptures();
          result.fog.visible_in_real_time_sky_captures = fog->GetVisibleInRealTimeSkyCaptures();
        }
        if (const auto post = environment->TryGetSystem<env::PostProcessVolume>()) {
          result.post_process_exists = true;
          result.post_process.exposure_mode = static_cast<int>(post->GetExposureMode());
          result.post_process.exposure_enabled = post->GetExposureEnabled();
          result.post_process.exposure_key = post->GetExposureKey();
          result.post_process.manual_exposure_ev = post->GetManualExposureEv();
          result.post_process.exposure_compensation_ev = post->GetExposureCompensationEv();
          result.post_process.tone_mapper = static_cast<int>(post->GetToneMapper());
          result.post_process.auto_exposure_metering_mode = static_cast<int>(post->GetAutoExposureMeteringMode());
          result.post_process.auto_exposure_min_ev = post->GetAutoExposureMinEv();
          result.post_process.auto_exposure_max_ev = post->GetAutoExposureMaxEv();
          result.post_process.auto_exposure_speed_up = post->GetAutoExposureSpeedUp();
          result.post_process.auto_exposure_speed_down = post->GetAutoExposureSpeedDown();
          result.post_process.auto_exposure_low_percentile = post->GetAutoExposureLowPercentile();
          result.post_process.auto_exposure_high_percentile = post->GetAutoExposureHighPercentile();
          result.post_process.auto_exposure_min_log_luminance = post->GetAutoExposureMinLogLuminance();
          result.post_process.auto_exposure_log_luminance_range = post->GetAutoExposureLogLuminanceRange();
          result.post_process.auto_exposure_target_luminance = post->GetAutoExposureTargetLuminance();
          result.post_process.auto_exposure_spot_meter_radius = post->GetAutoExposureSpotMeterRadius();
          const auto& exposure = post->GetExposureSettings();
          result.metering_mask = exposure.metering_mask;
          result.post_process.auto_exposure_black_influence = exposure.black_influence;
          result.post_process.auto_exposure_transition_distance_ev = exposure.transition_distance;
          result.post_process.auto_exposure_compensation_curve = exposure.compensation_curve;
          result.post_process.bloom_intensity = post->GetBloomIntensity();
          result.post_process.bloom_threshold = post->GetBloomThreshold();
          result.post_process.saturation = post->GetSaturation();
          result.post_process.contrast = post->GetContrast();
          result.post_process.vignette_intensity = post->GetVignetteIntensity();
          result.post_process.display_gamma = post->GetDisplayGamma();
        }
      }
    }
    complete_(result);
  }

private:
  std::function<void(EnvironmentObservation)> complete_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
