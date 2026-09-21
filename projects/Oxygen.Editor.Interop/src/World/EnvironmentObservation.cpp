//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma managed

#include <functional>
#include <memory>
#include <msclr/gcroot.h>

#include <Oxygen/EditorInterface/EngineContext.h>
#include <Oxygen/Engine/AsyncEngine.h>

#include <Commands/ObserveEnvironmentCommand.h>
#include <EditorModule/EditorModule.h>
#include <World/OxygenWorld.h>

using namespace System::Threading::Tasks;
using namespace oxygen::interop::module;

namespace Oxygen::Interop::World {
namespace {

class EnvironmentObservationCompletion final {
public:
  explicit EnvironmentObservationCompletion(
    TaskCompletionSource<EnvironmentStateManaged>^ completion)
    : completion_(completion) {}

  ~EnvironmentObservationCompletion()
  {
    // A discarded native command must also finish its managed observation.
    completion_->TrySetCanceled();
  }

  void Complete(const EnvironmentObservation& value) const
  {
    EnvironmentStateManaged result;
    result.Exists = value.exists;
    result.AtmosphereExists = value.atmosphere_exists;
    result.PostProcessExists = value.post_process_exists;
    result.AtmosphereEnabled = value.atmosphere.enabled;
    result.SunDiskEnabled = value.atmosphere.sun_disk_enabled;
    result.PlanetRadiusMeters = value.atmosphere.planet_radius_m;
    result.AtmosphereHeightMeters = value.atmosphere.atmosphere_height_m;
    result.GroundAlbedoRgb = System::Numerics::Vector3(value.atmosphere.ground_albedo_rgb.x,
      value.atmosphere.ground_albedo_rgb.y, value.atmosphere.ground_albedo_rgb.z);
    result.RayleighScaleHeightMeters = value.atmosphere.rayleigh_scale_height_m;
    result.MieScaleHeightMeters = value.atmosphere.mie_scale_height_m;
    result.MieAnisotropy = value.atmosphere.mie_anisotropy;
    result.SkyLuminanceFactorRgb = System::Numerics::Vector3(value.atmosphere.sky_luminance_factor_rgb.x,
      value.atmosphere.sky_luminance_factor_rgb.y, value.atmosphere.sky_luminance_factor_rgb.z);
    result.AerialPerspectiveDistanceScale = value.atmosphere.aerial_perspective_distance_scale;
    result.AerialScatteringStrength = value.atmosphere.aerial_scattering_strength;
    result.AerialPerspectiveStartDepthMeters = value.atmosphere.aerial_perspective_start_depth_m;
    result.HeightFogContribution = value.atmosphere.height_fog_contribution;
    result.ExposureMode = value.post_process.exposure_mode;
    result.ExposureEnabled = value.post_process.exposure_enabled;
    result.ExposureKey = value.post_process.exposure_key;
    result.ManualExposureEv = value.post_process.manual_exposure_ev;
    result.ExposureCompensation = value.post_process.exposure_compensation_ev;
    result.ToneMapping = value.post_process.tone_mapper;
    result.AutoExposureMeteringMode = value.post_process.auto_exposure_metering_mode;
    result.AutoExposureMinEv = value.post_process.auto_exposure_min_ev;
    result.AutoExposureMaxEv = value.post_process.auto_exposure_max_ev;
    result.AutoExposureSpeedUp = value.post_process.auto_exposure_speed_up;
    result.AutoExposureSpeedDown = value.post_process.auto_exposure_speed_down;
    result.AutoExposureLowPercentile = value.post_process.auto_exposure_low_percentile;
    result.AutoExposureHighPercentile = value.post_process.auto_exposure_high_percentile;
    result.AutoExposureMinLogLuminance = value.post_process.auto_exposure_min_log_luminance;
    result.AutoExposureLogLuminanceRange = value.post_process.auto_exposure_log_luminance_range;
    result.AutoExposureTargetLuminance = value.post_process.auto_exposure_target_luminance;
    result.AutoExposureSpotMeterRadius = value.post_process.auto_exposure_spot_meter_radius;
    result.AutoExposureBlackInfluence = value.post_process.auto_exposure_black_influence;
    result.AutoExposureMeteringMask = value.metering_mask.get();
    result.ExposureMaskPending = value.metering_mask_pending;
    result.ExposureMaskError = gcnew System::String(value.metering_mask_error.c_str());
    result.AutoExposureTransitionDistanceEv = value.post_process.auto_exposure_transition_distance_ev;
    const auto& curve = value.post_process.auto_exposure_compensation_curve;
    result.AutoExposureCompensationCurve = gcnew cli::array<ExposureCompensationKeyManaged>(static_cast<int>(curve.size()));
    for (size_t index = 0; index < curve.size(); ++index) {
      ExposureCompensationKeyManaged key;
      key.MeteredEv = curve[index].metered_ev;
      key.CompensationEv = curve[index].compensation_ev;
      result.AutoExposureCompensationCurve[static_cast<int>(index)] = key;
    }
    result.BloomIntensity = value.post_process.bloom_intensity;
    result.BloomThreshold = value.post_process.bloom_threshold;
    result.Saturation = value.post_process.saturation;
    result.Contrast = value.post_process.contrast;
    result.VignetteIntensity = value.post_process.vignette_intensity;
    result.DisplayGamma = value.post_process.display_gamma;
    completion_->TrySetResult(result);
  }

private:
  msclr::gcroot<TaskCompletionSource<EnvironmentStateManaged>^> completion_;
};

auto MakeEnvironmentObservationCallback(
  TaskCompletionSource<EnvironmentStateManaged>^ completion)
  -> std::function<void(EnvironmentObservation)>
{
  auto observer = std::make_shared<EnvironmentObservationCompletion>(completion);
  return [observer](EnvironmentObservation value) { observer->Complete(value); };
}

} // namespace

Task<EnvironmentStateManaged>^ OxygenWorld::ObserveEnvironmentAsync()
{
  auto native_context = context_->NativePtr();
  if (!native_context || !native_context->engine) {
    throw gcnew System::InvalidOperationException(
      "Environment observation has no engine context.");
  }
  auto module = native_context->engine->GetModule<EditorModule>();
  if (!module) {
    throw gcnew System::InvalidOperationException(
      "Environment observation has no editor module.");
  }
  auto completion = gcnew TaskCompletionSource<EnvironmentStateManaged>(
    TaskCreationOptions::RunContinuationsAsynchronously);
  module->get().Enqueue(std::make_unique<ObserveEnvironmentCommand>(
    MakeEnvironmentObservationCallback(completion)));
  return completion->Task;
}

} // namespace Oxygen::Interop::World
