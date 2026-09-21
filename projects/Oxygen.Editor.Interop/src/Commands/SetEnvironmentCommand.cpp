//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <memory>
#include <cmath>
#include <stdexcept>
#include <string>
#include <Oxygen/Scene/ExposureSettings.h>

#include <Commands/SetEnvironmentCommand.h>
#include <Commands/SetBackgroundColorCommand.h>

#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/Background.h>
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

auto MapExposureMode(const int value) -> oxygen::engine::ExposureMode
{
  switch (value) {
  case 0: return oxygen::engine::ExposureMode::kManual;
  case 1: return oxygen::engine::ExposureMode::kManualCamera;
  case 2: return oxygen::engine::ExposureMode::kAuto;
  default: throw std::invalid_argument("Unknown exposure mode");
  }
}

auto MapMeteringMode(const int value) -> oxygen::engine::MeteringMode
{
  switch (value) {
  case 1: return oxygen::engine::MeteringMode::kCenterWeighted;
  case 2: return oxygen::engine::MeteringMode::kSpot;
  case 0: return oxygen::engine::MeteringMode::kAverage;
  default: throw std::invalid_argument("Unknown exposure metering mode");
  }
}

auto MapToneMapper(const int value) -> oxygen::engine::ToneMapper
{
  switch (value) {
  case 0: return oxygen::engine::ToneMapper::kNone;
  case 2: return oxygen::engine::ToneMapper::kFilmic;
  case 3: return oxygen::engine::ToneMapper::kReinhard;
  case 1: return oxygen::engine::ToneMapper::kAcesFitted;
  default: throw std::invalid_argument("Unknown tone mapper");
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

auto ResolvePostProcessExposure(const oxygen::interop::module::PostProcessParams& params)
  -> oxygen::scene::ExposureSettings
{
  auto exposure = oxygen::scene::ExposureSettings {};
  exposure.enabled = params.exposure_enabled;
  exposure.mode = MapExposureMode(params.exposure_mode);
  exposure.metering_mode = MapMeteringMode(params.auto_exposure_metering_mode);
  exposure.key = params.exposure_key;
  exposure.manual_ev = params.manual_exposure_ev;
  exposure.compensation_ev = params.exposure_compensation_ev;
  exposure.min_ev = params.auto_exposure_min_ev;
  exposure.max_ev = params.auto_exposure_max_ev;
  exposure.speed_up = params.auto_exposure_speed_up;
  exposure.speed_down = params.auto_exposure_speed_down;
  exposure.low_percentile = params.auto_exposure_low_percentile;
  exposure.high_percentile = params.auto_exposure_high_percentile;
  exposure.min_log_luminance = params.auto_exposure_min_log_luminance;
  exposure.log_luminance_range = params.auto_exposure_log_luminance_range;
  exposure.target_luminance = params.auto_exposure_target_luminance;
  exposure.spot_meter_radius = params.auto_exposure_spot_meter_radius;
  exposure.black_influence = params.auto_exposure_black_influence;
  exposure.transition_distance = params.auto_exposure_transition_distance_ev;
  exposure.compensation_curve = params.auto_exposure_compensation_curve;
  const auto resolved = oxygen::scene::ResolveExposureSettings(exposure);
  if (!resolved && resolved.error() != oxygen::scene::ExposureSettingsError::kMissingCameraEv) {
    throw std::invalid_argument("Exposure edit rejected: "
      + std::string(oxygen::scene::to_string(resolved.error())));
  }
  static_cast<void>(MapToneMapper(params.tone_mapper));
  for (const auto value : { params.bloom_intensity, params.bloom_threshold,
    params.saturation, params.contrast, params.vignette_intensity, params.display_gamma }) {
    if (!std::isfinite(value) || value < 0.0F) {
      throw std::invalid_argument("Post-process values must be finite and nonnegative");
    }
  }
  if (params.vignette_intensity > 1.0F || params.display_gamma < oxygen::engine::kMinDisplayGamma) {
    throw std::invalid_argument("Post-process vignette or display gamma is outside its valid range");
  }
  return exposure;
}

auto ApplyPostProcess(oxygen::scene::SceneEnvironment& environment,
  const oxygen::interop::module::PostProcessParams& params,
  const oxygen::scene::ExposureSettings& exposure) -> void
{
  namespace env = oxygen::scene::environment;

  auto* const post_process = EnsureSystem<env::PostProcessVolume>(environment);
  if (post_process == nullptr) {
    return;
  }

  post_process->SetToneMapper(MapToneMapper(params.tone_mapper));
  post_process->SetExposureSettings(exposure);
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

  /*!
   Applies the authored background through the engine-owned sky system.
   Atmosphere keeps precedence in the renderer; this command does not change
   that independent authored setting. No tone mapping is applied here.

   @param context The scene-mutation context.
   @throw std::invalid_argument If any color channel is non-finite.
   @throw std::logic_error If there is no active scene.
  */
  void SetBackgroundColorCommand::Execute(CommandContext& context)
  {
    if (!std::isfinite(color_.x) || !std::isfinite(color_.y)
      || !std::isfinite(color_.z)) {
      throw std::invalid_argument("Background color must be finite.");
    }
    if (!context.Scene) {
      throw std::logic_error("Background requires an active scene.");
    }
    auto* environment = EnsureEnvironment(*context.Scene);
    auto* background
      = EnsureSystem<scene::environment::Background>(*environment);
    background->SetColorRgb(color_);
    background->SetEnabled(true);
    context.Scene->Update(false);
  }

  /*!
   Reads native background state after preceding scene mutations.

   @param context The scene-mutation context.
   @note A captured value does not establish that a frame was presented.
  */
  void ObserveBackgroundCommand::Execute(CommandContext& context)
  {
    BackgroundObservation observation;
    if (context.Scene) {
      if (auto environment = context.Scene->GetEnvironment()) {
        if (auto background
          = environment->TryGetSystem<scene::environment::Background>()) {
          observation.exists = background->IsEnabled();
          observation.color = background->GetColorRgb();
        }
        if (auto atmosphere
          = environment->TryGetSystem<scene::environment::SkyAtmosphere>()) {
          observation.atmosphere_enabled = atmosphere->IsEnabled();
        }
      }
    }
    complete_(observation);
  }

  void SetEnvironmentCommand::Execute(CommandContext& context)
  {
    if (!context.Scene) {
      return;
    }

    auto apply = [atmosphere_params = atmosphere_, post_process = post_process_,
        exposure = ResolvePostProcessExposure(post_process_)]
        (scene::Scene& scene, content::ResourceKey mask) mutable {
      auto* const environment = EnsureEnvironment(scene);
      auto* const atmosphere
        = EnsureSkyAtmosphereWhenEnabled(*environment, atmosphere_params.enabled);
      if (atmosphere != nullptr) {
        ApplySkyAtmosphere(*atmosphere, atmosphere_params);
      }
      exposure.metering_mask = mask;
      ApplySkyLight(*environment);
      ApplyPostProcess(*environment, post_process, exposure);
      EnsureDisabledFog(*environment);
      scene.Update(false);
    };
    if (context.AssetRequests) {
      context.AssetRequests->SetExposureMask(*context.Scene,
        post_process_.auto_exposure_metering_mask, std::move(apply),
        std::move(failure_callback_), std::move(success_callback_));
    } else if (post_process_.auto_exposure_metering_mask) {
      throw std::logic_error("Exposure mask requires scene asset request state");
    } else {
      apply(*context.Scene, {});
    }
  }

} // namespace oxygen::interop::module
