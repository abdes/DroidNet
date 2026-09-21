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
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Scene.h>

namespace oxygen::interop::module {

//! Scene-owned environment values sampled after preceding mutations.
struct EnvironmentObservation {
  bool exists = false;
  bool atmosphere_exists = false;
  bool post_process_exists = false;
  SkyAtmosphereParams atmosphere;
  PostProcessParams post_process;
  content::ResourceKey metering_mask {};
  bool metering_mask_pending { false };
  std::string metering_mask_error;
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
  void Execute(CommandContext& context) override
  {
    EnvironmentObservation result;
    if (context.AssetRequests) {
      const auto mask = context.AssetRequests->InspectExposureMask();
      result.metering_mask_pending = mask.pending;
      result.metering_mask_error = mask.error;
    }
    if (context.Scene) {
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
