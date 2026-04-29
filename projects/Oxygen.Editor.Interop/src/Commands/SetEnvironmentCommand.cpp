//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <memory>

#include <Commands/SetEnvironmentCommand.h>

#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Scene.h>

namespace {

auto MapExposureMode(const int value) noexcept -> oxygen::engine::ExposureMode
{
  switch (value) {
  case 0: return oxygen::engine::ExposureMode::kManual;
  case 1: return oxygen::engine::ExposureMode::kManualCamera;
  case 2:
  default: return oxygen::engine::ExposureMode::kAuto;
  }
}

auto MapMeteringMode(const int value) noexcept -> oxygen::engine::MeteringMode
{
  switch (value) {
  case 1: return oxygen::engine::MeteringMode::kCenterWeighted;
  case 2: return oxygen::engine::MeteringMode::kSpot;
  case 0:
  default: return oxygen::engine::MeteringMode::kAverage;
  }
}

auto MapToneMapper(const int value) noexcept -> oxygen::engine::ToneMapper
{
  switch (value) {
  case 0: return oxygen::engine::ToneMapper::kNone;
  case 2: return oxygen::engine::ToneMapper::kFilmic;
  case 3: return oxygen::engine::ToneMapper::kReinhard;
  case 1:
  default: return oxygen::engine::ToneMapper::kAcesFitted;
  }
}

auto EnsureEnvironment(oxygen::scene::Scene& scene)
  -> oxygen::scene::SceneEnvironment*
{
  auto environment = scene.GetEnvironment();
  if (environment) {
    return environment.get();
  }

  auto owned = std::make_unique<oxygen::scene::SceneEnvironment>();
  scene.SetEnvironment(std::move(owned));
  return scene.GetEnvironment().get();
}

} // namespace

namespace oxygen::interop::module {

  void SetEnvironmentCommand::Execute(CommandContext& context)
  {
    if (!context.Scene) {
      return;
    }

    auto* const environment = EnsureEnvironment(*context.Scene);
    if (environment == nullptr) {
      return;
    }

    auto atmosphere
      = environment->TryGetSystem<scene::environment::SkyAtmosphere>();
    if (!atmosphere) {
      (void)environment->AddSystem<scene::environment::SkyAtmosphere>();
      atmosphere
        = environment->TryGetSystem<scene::environment::SkyAtmosphere>();
    }

    if (!atmosphere) {
      return;
    }

    atmosphere->SetEnabled(atmosphere_.enabled);
    atmosphere->SetSunDiskEnabled(atmosphere_.sun_disk_enabled);
    atmosphere->SetPlanetRadiusMeters(atmosphere_.planet_radius_m);
    atmosphere->SetAtmosphereHeightMeters(atmosphere_.atmosphere_height_m);
    atmosphere->SetGroundAlbedoRgb(atmosphere_.ground_albedo_rgb);
    atmosphere->SetRayleighScaleHeightMeters(
      atmosphere_.rayleigh_scale_height_m);
    atmosphere->SetMieScaleHeightMeters(atmosphere_.mie_scale_height_m);
    atmosphere->SetMieAnisotropy(atmosphere_.mie_anisotropy);
    atmosphere->SetSkyLuminanceFactorRgb(
      atmosphere_.sky_luminance_factor_rgb);
    atmosphere->SetSkyAndAerialPerspectiveLuminanceFactorRgb(
      atmosphere_.sky_luminance_factor_rgb);
    atmosphere->SetAerialPerspectiveDistanceScale(
      atmosphere_.aerial_perspective_distance_scale);
    atmosphere->SetAerialScatteringStrength(
      atmosphere_.aerial_scattering_strength);
    atmosphere->SetAerialPerspectiveStartDepthMeters(
      atmosphere_.aerial_perspective_start_depth_m);
    atmosphere->SetHeightFogContribution(atmosphere_.height_fog_contribution);

    auto post_process
      = environment->TryGetSystem<scene::environment::PostProcessVolume>();
    if (!post_process) {
      (void)environment->AddSystem<scene::environment::PostProcessVolume>();
      post_process
        = environment->TryGetSystem<scene::environment::PostProcessVolume>();
    }

    if (!post_process) {
      return;
    }

    post_process->SetToneMapper(MapToneMapper(post_process_.tone_mapper));
    post_process->SetExposureMode(MapExposureMode(post_process_.exposure_mode));
    post_process->SetExposureEnabled(post_process_.exposure_enabled);
    post_process->SetExposureCompensationEv(
      post_process_.exposure_compensation_ev);
    post_process->SetExposureKey(post_process_.exposure_key);
    post_process->SetManualExposureEv(post_process_.manual_exposure_ev);
    post_process->SetAutoExposureRangeEv(post_process_.auto_exposure_min_ev,
      post_process_.auto_exposure_max_ev);
    post_process->SetAutoExposureAdaptationSpeeds(
      post_process_.auto_exposure_speed_up,
      post_process_.auto_exposure_speed_down);
    post_process->SetAutoExposureMeteringMode(
      MapMeteringMode(post_process_.auto_exposure_metering_mode));
    post_process->SetAutoExposureHistogramPercentiles(
      post_process_.auto_exposure_low_percentile,
      post_process_.auto_exposure_high_percentile);
    post_process->SetAutoExposureHistogramWindow(
      post_process_.auto_exposure_min_log_luminance,
      post_process_.auto_exposure_log_luminance_range);
    post_process->SetAutoExposureTargetLuminance(
      post_process_.auto_exposure_target_luminance);
    post_process->SetAutoExposureSpotMeterRadius(
      post_process_.auto_exposure_spot_meter_radius);
    post_process->SetBloomIntensity(post_process_.bloom_intensity);
    post_process->SetBloomThreshold(post_process_.bloom_threshold);
    post_process->SetSaturation(post_process_.saturation);
    post_process->SetContrast(post_process_.contrast);
    post_process->SetVignetteIntensity(post_process_.vignette_intensity);
    post_process->SetDisplayGamma(post_process_.display_gamma);
  }

} // namespace oxygen::interop::module
