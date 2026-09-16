//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::scene::environment {

//! Scene-global post processing parameters.
/*!
 This is a minimal, renderer-agnostic post process parameter set inspired by
 UE/Unity volume workflows.

 Exposure is authored in EV100 and resolved atomically per view using
 ResolveExposureSettings. Requested fields may be edited independently; an
 invalid combined revision does not replace the renderer's last valid revision.
 Manual scale is 2^(compensation-EV100) * (key/12.5). The key is not an EV.
 Physical camera exposure consumes the active camera's EV100.
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

  //! Replace requested exposure intent; the renderer validates it atomically.
  auto SetExposureSettings(const ExposureSettings& settings) -> void
  {
    exposure_ = settings;
  }

  //! Inspect the complete requested exposure revision, including resources.
  [[nodiscard]] auto GetExposureSettings() const noexcept
    -> const ExposureSettings&
  {
    return exposure_;
  }

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
    exposure_.mode = mode;
  }

  //! Gets exposure mode.
  [[nodiscard]] auto GetExposureMode() const noexcept -> engine::ExposureMode
  {
    return exposure_.mode;
  }

  //! Enables or disables exposure application.
  auto SetExposureEnabled(const bool enabled) noexcept -> void
  {
    exposure_.enabled = enabled;
  }

  //! Returns whether exposure is enabled.
  [[nodiscard]] auto GetExposureEnabled() const noexcept -> bool
  {
    return exposure_.enabled;
  }

  //! Sets exposure compensation in EV (stops).
  auto SetExposureCompensationEv(const float ev) noexcept -> void
  {
    exposure_.compensation_ev = ev;
  }

  //! Gets exposure compensation in EV.
  [[nodiscard]] auto GetExposureCompensationEv() const noexcept -> float
  {
    return exposure_.compensation_ev;
  }

  //! Sets the display key scale applied after calibration.
  auto SetExposureKey(const float exposure_key) noexcept -> void
  {
    exposure_.key = exposure_key;
  }

  //! Gets the display key scale applied after calibration.
  [[nodiscard]] auto GetExposureKey() const noexcept -> float
  {
    return exposure_.key;
  }

  //! Sets manual exposure EV value (EV100, ISO 100 reference).
  auto SetManualExposureEv(const float ev) noexcept -> void
  {
    exposure_.manual_ev = ev;
  }

  //! Gets manual exposure EV value (EV100, ISO 100 reference).
  [[nodiscard]] auto GetManualExposureEv() const noexcept -> float
  {
    return exposure_.manual_ev;
  }

  //! Sets auto-exposure min/max EV.
  auto SetAutoExposureRangeEv(const float min_ev, const float max_ev) noexcept
    -> void
  {
    exposure_.min_ev = min_ev;
    exposure_.max_ev = max_ev;
  }

  //! Gets auto-exposure minimum EV.
  [[nodiscard]] auto GetAutoExposureMinEv() const noexcept -> float
  {
    return exposure_.min_ev;
  }

  //! Gets auto-exposure maximum EV.
  [[nodiscard]] auto GetAutoExposureMaxEv() const noexcept -> float
  {
    return exposure_.max_ev;
  }

  //! Sets auto-exposure adaptation speeds (EV per second).
  auto SetAutoExposureAdaptationSpeeds(
    const float up_ev_per_s, const float down_ev_per_s) noexcept -> void
  {
    exposure_.speed_up = up_ev_per_s;
    exposure_.speed_down = down_ev_per_s;
  }

  //! Gets auto-exposure speed up (EV per second).
  [[nodiscard]] auto GetAutoExposureSpeedUp() const noexcept -> float
  {
    return exposure_.speed_up;
  }

  //! Gets auto-exposure speed down (EV per second).
  [[nodiscard]] auto GetAutoExposureSpeedDown() const noexcept -> float
  {
    return exposure_.speed_down;
  }

  //! Sets the auto-exposure metering mode.
  auto SetAutoExposureMeteringMode(const engine::MeteringMode mode) noexcept
    -> void
  {
    exposure_.metering_mode = mode;
  }

  //! Gets the auto-exposure metering mode.
  [[nodiscard]] auto GetAutoExposureMeteringMode() const noexcept
    -> engine::MeteringMode
  {
    return exposure_.metering_mode;
  }

  //! Sets the histogram percentiles used by auto exposure.
  auto SetAutoExposureHistogramPercentiles(
    const float low_percentile, const float high_percentile) noexcept -> void
  {
    exposure_.low_percentile = low_percentile;
    exposure_.high_percentile = high_percentile;
  }

  //! Gets the low histogram percentile used by auto exposure.
  [[nodiscard]] auto GetAutoExposureLowPercentile() const noexcept -> float
  {
    return exposure_.low_percentile;
  }

  //! Gets the high histogram percentile used by auto exposure.
  [[nodiscard]] auto GetAutoExposureHighPercentile() const noexcept -> float
  {
    return exposure_.high_percentile;
  }

  //! Sets the histogram luminance window used by auto exposure.
  auto SetAutoExposureHistogramWindow(
    const float min_log_luminance, const float log_luminance_range) noexcept
    -> void
  {
    exposure_.min_log_luminance = min_log_luminance;
    exposure_.log_luminance_range = log_luminance_range;
  }

  //! Gets the minimum log2 luminance used by auto exposure.
  [[nodiscard]] auto GetAutoExposureMinLogLuminance() const noexcept -> float
  {
    return exposure_.min_log_luminance;
  }

  //! Gets the log2 luminance range used by auto exposure.
  [[nodiscard]] auto GetAutoExposureLogLuminanceRange() const noexcept
    -> float
  {
    return exposure_.log_luminance_range;
  }

  //! Sets the target average luminance used by auto exposure.
  auto SetAutoExposureTargetLuminance(const float target_luminance) noexcept
    -> void
  {
    exposure_.target_luminance = target_luminance;
  }

  //! Gets the target average luminance used by auto exposure.
  [[nodiscard]] auto GetAutoExposureTargetLuminance() const noexcept -> float
  {
    return exposure_.target_luminance;
  }

  //! Sets the spot-meter radius used by auto exposure.
  auto SetAutoExposureSpotMeterRadius(const float radius) noexcept -> void
  {
    exposure_.spot_meter_radius = radius;
  }

  //! Gets the spot-meter radius used by auto exposure.
  [[nodiscard]] auto GetAutoExposureSpotMeterRadius() const noexcept -> float
  {
    return exposure_.spot_meter_radius;
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
  ExposureSettings exposure_ {};

  float bloom_intensity_ = 0.0F;
  float bloom_threshold_ = 1.0F;

  float saturation_ = 1.0F;
  float contrast_ = 1.0F;

  float vignette_intensity_ = 0.0F;
  float display_gamma_ = 2.2F;
};

} // namespace oxygen::scene::environment
