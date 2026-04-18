//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

namespace oxygen::scene::environment {

//! Scene-global post processing parameters.
/*!
 This is a minimal, renderer-agnostic post process parameter set inspired by
 UE/Unity volume workflows.

 Exposure uses EV (EV100, ISO 100) and is resolved by the renderer into a
 linear
 scale using the ISO 2720 calibration formula
 $exposure = \frac{1}{12.5} \cdot 2^{-EV100}$. A display key scale is applied
 after calibration to map mid-gray to a display-friendly level.
 Manual mode uses the authored EV value, while manual camera mode consumes EV
 derived from camera aperture/shutter/ISO.

 The engine can later extend this with per-camera overrides or local volumes;
 for now it represents scene-global authored intent.
*/
class PostProcessVolume final : public EnvironmentSystem {
  OXYGEN_COMPONENT(PostProcessVolume)

public:
  //! Constructs post process settings with neutral defaults.
  PostProcessVolume() = default;

  //! Virtual destructor.
  ~PostProcessVolume() override = default;

  OXYGEN_DEFAULT_COPYABLE(PostProcessVolume)
  OXYGEN_DEFAULT_MOVABLE(PostProcessVolume)

  //! Sets the tone mapper.
  auto SetToneMapper(const engine::ToneMapper mapper) noexcept -> void
  {
    tone_mapper_ = mapper;
  }

  //! Gets the tone mapper.
  [[nodiscard]] auto GetToneMapper() const noexcept -> engine::ToneMapper
  {
    return tone_mapper_;
  }

  //! Sets exposure mode (manual, auto, or manual camera EV).
  auto SetExposureMode(const engine::ExposureMode mode) noexcept -> void
  {
    exposure_mode_ = mode;
  }

  //! Gets exposure mode.
  [[nodiscard]] auto GetExposureMode() const noexcept -> engine::ExposureMode
  {
    return exposure_mode_;
  }

  //! Enables or disables exposure application.
  auto SetExposureEnabled(const bool enabled) noexcept -> void
  {
    exposure_enabled_ = enabled;
  }

  //! Returns whether exposure is enabled.
  [[nodiscard]] auto GetExposureEnabled() const noexcept -> bool
  {
    return exposure_enabled_;
  }

  //! Sets exposure compensation in EV (stops).
  auto SetExposureCompensationEv(const float ev) noexcept -> void
  {
    exposure_compensation_ev_ = ev;
  }

  //! Gets exposure compensation in EV.
  [[nodiscard]] auto GetExposureCompensationEv() const noexcept -> float
  {
    return exposure_compensation_ev_;
  }

  //! Sets the display key scale applied after calibration.
  auto SetExposureKey(const float exposure_key) noexcept -> void
  {
    exposure_key_ = exposure_key;
  }

  //! Gets the display key scale applied after calibration.
  [[nodiscard]] auto GetExposureKey() const noexcept -> float
  {
    return exposure_key_;
  }

  //! Sets manual exposure EV value (EV100, ISO 100 reference).
  auto SetManualExposureEv(const float ev) noexcept -> void
  {
    manual_exposure_ev_ = ev;
  }

  //! Gets manual exposure EV value (EV100, ISO 100 reference).
  [[nodiscard]] auto GetManualExposureEv() const noexcept -> float
  {
    return manual_exposure_ev_;
  }

  //! Sets auto-exposure min/max EV.
  auto SetAutoExposureRangeEv(const float min_ev, const float max_ev) noexcept
    -> void
  {
    auto_exposure_min_ev_ = min_ev;
    auto_exposure_max_ev_ = max_ev;
  }

  //! Gets auto-exposure minimum EV.
  [[nodiscard]] auto GetAutoExposureMinEv() const noexcept -> float
  {
    return auto_exposure_min_ev_;
  }

  //! Gets auto-exposure maximum EV.
  [[nodiscard]] auto GetAutoExposureMaxEv() const noexcept -> float
  {
    return auto_exposure_max_ev_;
  }

  //! Sets auto-exposure adaptation speeds (EV per second).
  auto SetAutoExposureAdaptationSpeeds(
    const float up_ev_per_s, const float down_ev_per_s) noexcept -> void
  {
    auto_exposure_speed_up_ = up_ev_per_s;
    auto_exposure_speed_down_ = down_ev_per_s;
  }

  //! Gets auto-exposure speed up (EV per second).
  [[nodiscard]] auto GetAutoExposureSpeedUp() const noexcept -> float
  {
    return auto_exposure_speed_up_;
  }

  //! Gets auto-exposure speed down (EV per second).
  [[nodiscard]] auto GetAutoExposureSpeedDown() const noexcept -> float
  {
    return auto_exposure_speed_down_;
  }

  //! Sets the auto-exposure metering mode.
  auto SetAutoExposureMeteringMode(const engine::MeteringMode mode) noexcept
    -> void
  {
    auto_exposure_metering_mode_ = mode;
  }

  //! Gets the auto-exposure metering mode.
  [[nodiscard]] auto GetAutoExposureMeteringMode() const noexcept
    -> engine::MeteringMode
  {
    return auto_exposure_metering_mode_;
  }

  //! Sets the histogram percentiles used by auto exposure.
  auto SetAutoExposureHistogramPercentiles(
    const float low_percentile, const float high_percentile) noexcept -> void
  {
    auto_exposure_low_percentile_ = low_percentile;
    auto_exposure_high_percentile_ = high_percentile;
  }

  //! Gets the low histogram percentile used by auto exposure.
  [[nodiscard]] auto GetAutoExposureLowPercentile() const noexcept -> float
  {
    return auto_exposure_low_percentile_;
  }

  //! Gets the high histogram percentile used by auto exposure.
  [[nodiscard]] auto GetAutoExposureHighPercentile() const noexcept -> float
  {
    return auto_exposure_high_percentile_;
  }

  //! Sets the histogram luminance window used by auto exposure.
  auto SetAutoExposureHistogramWindow(
    const float min_log_luminance, const float log_luminance_range) noexcept
    -> void
  {
    auto_exposure_min_log_luminance_ = min_log_luminance;
    auto_exposure_log_luminance_range_ = log_luminance_range;
  }

  //! Gets the minimum log2 luminance used by auto exposure.
  [[nodiscard]] auto GetAutoExposureMinLogLuminance() const noexcept -> float
  {
    return auto_exposure_min_log_luminance_;
  }

  //! Gets the log2 luminance range used by auto exposure.
  [[nodiscard]] auto GetAutoExposureLogLuminanceRange() const noexcept
    -> float
  {
    return auto_exposure_log_luminance_range_;
  }

  //! Sets the target average luminance used by auto exposure.
  auto SetAutoExposureTargetLuminance(const float target_luminance) noexcept
    -> void
  {
    auto_exposure_target_luminance_ = target_luminance;
  }

  //! Gets the target average luminance used by auto exposure.
  [[nodiscard]] auto GetAutoExposureTargetLuminance() const noexcept -> float
  {
    return auto_exposure_target_luminance_;
  }

  //! Sets the spot-meter radius used by auto exposure.
  auto SetAutoExposureSpotMeterRadius(const float radius) noexcept -> void
  {
    auto_exposure_spot_meter_radius_ = radius;
  }

  //! Gets the spot-meter radius used by auto exposure.
  [[nodiscard]] auto GetAutoExposureSpotMeterRadius() const noexcept -> float
  {
    return auto_exposure_spot_meter_radius_;
  }

  //! Sets bloom intensity (unitless).
  auto SetBloomIntensity(const float intensity) noexcept -> void
  {
    bloom_intensity_ = intensity;
  }

  //! Gets bloom intensity.
  [[nodiscard]] auto GetBloomIntensity() const noexcept -> float
  {
    return bloom_intensity_;
  }

  //! Sets bloom threshold (linear HDR).
  auto SetBloomThreshold(const float threshold) noexcept -> void
  {
    bloom_threshold_ = threshold;
  }

  //! Gets bloom threshold.
  [[nodiscard]] auto GetBloomThreshold() const noexcept -> float
  {
    return bloom_threshold_;
  }

  //! Sets color grading saturation multiplier (unitless).
  auto SetSaturation(const float saturation) noexcept -> void
  {
    saturation_ = saturation;
  }

  //! Gets saturation.
  [[nodiscard]] auto GetSaturation() const noexcept -> float
  {
    return saturation_;
  }

  //! Sets color grading contrast multiplier (unitless).
  auto SetContrast(const float contrast) noexcept -> void
  {
    contrast_ = contrast;
  }

  //! Gets contrast.
  [[nodiscard]] auto GetContrast() const noexcept -> float { return contrast_; }

  //! Sets vignette intensity in [0, 1].
  auto SetVignetteIntensity(const float intensity) noexcept -> void
  {
    vignette_intensity_ = intensity;
  }

  //! Gets vignette intensity.
  [[nodiscard]] auto GetVignetteIntensity() const noexcept -> float
  {
    return vignette_intensity_;
  }

  //! Sets the display gamma applied after tonemapping.
  auto SetDisplayGamma(const float gamma) noexcept -> void
  {
    display_gamma_ = gamma;
  }

  //! Gets the display gamma applied after tonemapping.
  [[nodiscard]] auto GetDisplayGamma() const noexcept -> float
  {
    return display_gamma_;
  }

private:
  engine::ToneMapper tone_mapper_ = engine::ToneMapper::kAcesFitted;
  engine::ExposureMode exposure_mode_ = engine::ExposureMode::kAuto;
  bool exposure_enabled_ = true;

  //! Exposure compensation in stops (EV).
  //! Scale: logarithmic (base 2).
  //! Variation: +/- 1.0 reflects a doubling/halving of final image brightness.
  float exposure_compensation_ev_ = 0.0F;

  //! Display key scale applied after EV-to-linear calibration.
  //! Scale: linear. Maps mid-gray (18%) to a display level.
  //! Variation: Small changes (e.g. 0.1) affect overall image brightness
  //! without changing lighting.
  float exposure_key_ = 10.0F;

  //! Manual exposure value at ISO 100.
  //! Scale: EV (EV100, log base 2). Typical: 13 (daylight), 0 (indoor).
  //! Variation: +/- 1.0 reflects a doubling/halving of sensor sensitivity.
  float manual_exposure_ev_ = 9.7F;

  //! Minimum allowable exposure value for auto-exposure.
  //! Scale: EV (EV100).
  //! Variation: Changes define the lower limit of dark environments.
  float auto_exposure_min_ev_ = -6.0F;

  //! Maximum allowable exposure value for auto-exposure.
  //! Scale: EV (EV100).
  //! Variation: Changes define the upper limit for bright environments.
  float auto_exposure_max_ev_ = 16.0F;

  //! Speed of exposure increase (getting darker/entering light).
  //! Scale: EV per second.
  //! Variation: Small changes affect temporal stability vs responsiveness.
  float auto_exposure_speed_up_ = 3.0F;

  //! Speed of exposure decrease (getting brighter/leaving light).
  //! Scale: EV per second.
  //! Variation: Small changes affect temporal stability vs responsiveness.
  float auto_exposure_speed_down_ = 1.0F;

  engine::MeteringMode auto_exposure_metering_mode_
    = engine::MeteringMode::kAverage;
  float auto_exposure_low_percentile_ = 0.1F;
  float auto_exposure_high_percentile_ = 0.9F;
  float auto_exposure_min_log_luminance_ = -12.0F;
  float auto_exposure_log_luminance_range_ = 25.0F;
  float auto_exposure_target_luminance_ = 0.18F;
  float auto_exposure_spot_meter_radius_ = 0.2F;

  float bloom_intensity_ = 0.0F;
  float bloom_threshold_ = 1.0F;

  float saturation_ = 1.0F;
  float contrast_ = 1.0F;

  float vignette_intensity_ = 0.0F;
  float display_gamma_ = 2.2F;
};

} // namespace oxygen::scene::environment
