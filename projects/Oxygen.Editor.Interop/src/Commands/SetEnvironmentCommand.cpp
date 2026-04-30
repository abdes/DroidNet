//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <memory>

#include <Commands/SetEnvironmentCommand.h>

#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Scene.h>

namespace {

template <typename T>
auto EnsureSystem(oxygen::scene::SceneEnvironment& environment) -> T*
{
  auto system = environment.TryGetSystem<T>();
  if (!system) {
    system = oxygen::observer_ptr { &environment.AddSystem<T>() };
  }

  return system.get();
}

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

auto EnsureSkyAtmosphereWhenEnabled(
  oxygen::scene::SceneEnvironment& environment, const bool enabled)
  -> oxygen::scene::environment::SkyAtmosphere*
{
  auto atmosphere
    = environment.TryGetSystem<oxygen::scene::environment::SkyAtmosphere>();
  if (enabled && !atmosphere) {
    atmosphere = oxygen::observer_ptr {
      &environment.AddSystem<oxygen::scene::environment::SkyAtmosphere>()
    };
  }

  return atmosphere.get();
}

auto ApplySkyAtmosphere(
  oxygen::scene::environment::SkyAtmosphere& atmosphere,
  const oxygen::interop::module::SkyAtmosphereParams& params) -> void
{
  namespace atmos = oxygen::engine::atmos;
  namespace env = oxygen::scene::environment;

  atmosphere.SetEnabled(params.enabled);
  if (!params.enabled) {
    return;
  }

  atmosphere.SetTransformMode(
    env::SkyAtmosphereTransformMode::kPlanetTopAtAbsoluteWorldOrigin);
  atmosphere.SetRenderInMainPass(true);
  atmosphere.SetPlanetRadiusMeters(params.planet_radius_m);
  atmosphere.SetAtmosphereHeightMeters(params.atmosphere_height_m);
  atmosphere.SetGroundAlbedoRgb(params.ground_albedo_rgb);
  atmosphere.SetRayleighScatteringRgb(atmos::kDefaultRayleighScatteringRgb);
  atmosphere.SetRayleighScaleHeightMeters(params.rayleigh_scale_height_m);
  atmosphere.SetMieScatteringRgb(atmos::kDefaultMieScatteringRgb);
  atmosphere.SetMieAbsorptionRgb(atmos::kDefaultMieAbsorptionRgb);
  atmosphere.SetMieScaleHeightMeters(params.mie_scale_height_m);
  atmosphere.SetMieAnisotropy(params.mie_anisotropy);
  atmosphere.SetOzoneAbsorptionRgb(atmos::kDefaultOzoneAbsorptionRgb);
  atmosphere.SetOzoneDensityProfile(atmos::kDefaultOzoneDensityProfile);
  atmosphere.SetMultiScatteringFactor(1.0F);
  atmosphere.SetSkyLuminanceFactorRgb(params.sky_luminance_factor_rgb);
  atmosphere.SetSkyAndAerialPerspectiveLuminanceFactorRgb(
    params.sky_luminance_factor_rgb);
  atmosphere.SetSunDiskEnabled(params.sun_disk_enabled);
  atmosphere.SetAerialPerspectiveDistanceScale(
    params.aerial_perspective_distance_scale);
  atmosphere.SetAerialScatteringStrength(params.aerial_scattering_strength);
  atmosphere.SetAerialPerspectiveStartDepthMeters(
    params.aerial_perspective_start_depth_m);
  atmosphere.SetHeightFogContribution(params.height_fog_contribution);
  atmosphere.SetTraceSampleCountScale(1.0F);
  atmosphere.SetTransmittanceMinLightElevationDeg(-90.0F);
  atmosphere.SetHoldout(false);
}

auto ApplySkyLight(oxygen::scene::SceneEnvironment& environment) -> void
{
  namespace env = oxygen::scene::environment;

  auto* const sky_light = EnsureSystem<env::SkyLight>(environment);
  if (sky_light == nullptr) {
    return;
  }

  sky_light->SetEnabled(true);
  sky_light->SetSource(env::SkyLightSource::kCapturedScene);
  sky_light->SetIntensityMul(1.0F);
  sky_light->SetTintRgb({ 1.0F, 1.0F, 1.0F });
  sky_light->SetDiffuseIntensity(1.0F);
  sky_light->SetSpecularIntensity(1.0F);
  sky_light->SetRealTimeCaptureEnabled(true);
  sky_light->SetLowerHemisphereColor({ 0.02F, 0.02F, 0.03F });
  sky_light->SetVolumetricScatteringIntensity(1.0F);
  sky_light->SetAffectReflections(true);
}

auto ApplyPostProcess(oxygen::scene::SceneEnvironment& environment,
  const oxygen::interop::module::PostProcessParams& params) -> void
{
  namespace env = oxygen::scene::environment;

  auto* const post_process = EnsureSystem<env::PostProcessVolume>(environment);
  if (post_process == nullptr) {
    return;
  }

  post_process->SetToneMapper(MapToneMapper(params.tone_mapper));
  post_process->SetExposureMode(MapExposureMode(params.exposure_mode));
  post_process->SetExposureEnabled(params.exposure_enabled);
  post_process->SetExposureCompensationEv(params.exposure_compensation_ev);
  post_process->SetExposureKey(params.exposure_key);
  post_process->SetManualExposureEv(params.manual_exposure_ev);
  post_process->SetAutoExposureRangeEv(
    params.auto_exposure_min_ev, params.auto_exposure_max_ev);
  post_process->SetAutoExposureAdaptationSpeeds(
    params.auto_exposure_speed_up, params.auto_exposure_speed_down);
  post_process->SetAutoExposureMeteringMode(
    MapMeteringMode(params.auto_exposure_metering_mode));
  post_process->SetAutoExposureHistogramPercentiles(
    params.auto_exposure_low_percentile, params.auto_exposure_high_percentile);
  post_process->SetAutoExposureHistogramWindow(
    params.auto_exposure_min_log_luminance,
    params.auto_exposure_log_luminance_range);
  post_process->SetAutoExposureTargetLuminance(
    params.auto_exposure_target_luminance);
  post_process->SetAutoExposureSpotMeterRadius(
    params.auto_exposure_spot_meter_radius);
  post_process->SetBloomIntensity(params.bloom_intensity);
  post_process->SetBloomThreshold(params.bloom_threshold);
  post_process->SetSaturation(params.saturation);
  post_process->SetContrast(params.contrast);
  post_process->SetVignetteIntensity(params.vignette_intensity);
  post_process->SetDisplayGamma(params.display_gamma);
}

auto EnsureDisabledFog(oxygen::scene::SceneEnvironment& environment) -> void
{
  namespace env = oxygen::scene::environment;

  auto* const fog = EnsureSystem<env::Fog>(environment);
  if (fog == nullptr) {
    return;
  }

  fog->SetEnabled(false);
  fog->SetEnableHeightFog(false);
  fog->SetEnableVolumetricFog(false);
  fog->SetRenderInMainPass(true);
  fog->SetVisibleInReflectionCaptures(true);
  fog->SetVisibleInRealTimeSkyCaptures(true);
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

    auto* const atmosphere
      = EnsureSkyAtmosphereWhenEnabled(*environment, atmosphere_.enabled);
    if (atmosphere != nullptr) {
      ApplySkyAtmosphere(*atmosphere, atmosphere_);
    }

    ApplySkyLight(*environment);
    ApplyPostProcess(*environment, post_process_);
    EnsureDisabledFog(*environment);

    context.Scene->Update(false);
  }

} // namespace oxygen::interop::module
