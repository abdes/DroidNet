//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

namespace oxygen::scene::environment {

//! Tonemapper selection.
enum class ToneMapper {
  kAcesFitted,
  kReinhard,
  kNone,
};

//! Exposure behavior.
enum class ExposureMode {
  kManual,
  kAuto,
  kManualCamera,
};

//! Scene-global post processing parameters.
/*!
 This is a minimal, renderer-agnostic post process parameter set inspired by
 UE/Unity volume workflows.

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
  auto SetToneMapper(const ToneMapper mapper) noexcept -> void
  {
    tone_mapper_ = mapper;
  }

  //! Gets the tone mapper.
  [[nodiscard]] auto GetToneMapper() const noexcept -> ToneMapper
  {
    return tone_mapper_;
  }

  //! Sets exposure mode.
  auto SetExposureMode(const ExposureMode mode) noexcept -> void
  {
    exposure_mode_ = mode;
  }

  //! Gets exposure mode.
  [[nodiscard]] auto GetExposureMode() const noexcept -> ExposureMode
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

  //! Sets manual exposure EV100 value.
  auto SetManualExposureEv100(const float ev100) noexcept -> void
  {
    manual_exposure_ev100_ = ev100;
  }

  //! Gets manual exposure EV100 value.
  [[nodiscard]] auto GetManualExposureEv100() const noexcept -> float
  {
    return manual_exposure_ev100_;
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

private:
  ToneMapper tone_mapper_ = ToneMapper::kAcesFitted;

  ExposureMode exposure_mode_ = ExposureMode::kAuto;
  bool exposure_enabled_ = true;
  float exposure_compensation_ev_ = 0.0F;
  float manual_exposure_ev100_ = 9.7F;

  float auto_exposure_min_ev_ = -6.0F;
  float auto_exposure_max_ev_ = 16.0F;
  float auto_exposure_speed_up_ = 3.0F;
  float auto_exposure_speed_down_ = 1.0F;

  float bloom_intensity_ = 0.0F;
  float bloom_threshold_ = 1.0F;

  float saturation_ = 1.0F;
  float contrast_ = 1.0F;

  float vignette_intensity_ = 0.0F;
};

} // namespace oxygen::scene::environment
