//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/AutoExposurePass.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>

#include "DemoShell/Services/PostProcessSettingsService.h"

namespace oxygen::examples::ui {

//! View model for Post Process panel state.
class PostProcessVm {
public:
  explicit PostProcessVm(observer_ptr<PostProcessSettingsService> service);

  // Exposure
  [[nodiscard]] auto GetExposureEnabled() -> bool;
  auto SetExposureEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetExposureMode() -> engine::ExposureMode;
  auto SetExposureMode(engine::ExposureMode mode) -> void;

  [[nodiscard]] auto GetManualExposureEv() -> float;
  auto SetManualExposureEv(float ev) -> void;

  [[nodiscard]] auto GetManualCameraAperture() -> float;
  auto SetManualCameraAperture(float aperture) -> void;

  [[nodiscard]] auto GetManualCameraShutterRate() -> float;
  auto SetManualCameraShutterRate(float shutter_rate) -> void;

  [[nodiscard]] auto GetManualCameraIso() -> float;
  auto SetManualCameraIso(float iso) -> void;

  [[nodiscard]] auto GetManualCameraEv() -> float;

  [[nodiscard]] auto GetExposureCompensation() -> float;
  auto SetExposureCompensation(float stops) -> void;

  [[nodiscard]] auto GetExposureKey() -> float;
  auto SetExposureKey(float exposure_key) -> void;

  // Auto Exposure
  [[nodiscard]] auto GetAutoExposureAdaptationSpeedUp() -> float;
  auto SetAutoExposureAdaptationSpeedUp(float speed) -> void;

  [[nodiscard]] auto GetAutoExposureAdaptationSpeedDown() -> float;
  auto SetAutoExposureAdaptationSpeedDown(float speed) -> void;

  [[nodiscard]] auto GetAutoExposureLowPercentile() -> float;
  auto SetAutoExposureLowPercentile(float percentile) -> void;

  [[nodiscard]] auto GetAutoExposureHighPercentile() -> float;
  auto SetAutoExposureHighPercentile(float percentile) -> void;

  [[nodiscard]] auto GetAutoExposureMinLogLuminance() -> float;
  auto SetAutoExposureMinLogLuminance(float min_log_lum) -> void;

  [[nodiscard]] auto GetAutoExposureLogLuminanceRange() -> float;
  auto SetAutoExposureLogLuminanceRange(float range) -> void;

  [[nodiscard]] auto GetAutoExposureTargetLuminance() -> float;
  auto SetAutoExposureTargetLuminance(float target_lum) -> void;

  [[nodiscard]] auto GetAutoExposureMeteringMode() -> engine::MeteringMode;
  auto SetAutoExposureMeteringMode(engine::MeteringMode mode) -> void;

  // Tonemapping
  [[nodiscard]] auto GetTonemappingEnabled() -> bool;
  auto SetTonemappingEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetToneMapper() -> engine::ToneMapper;
  auto SetToneMapper(engine::ToneMapper mode) -> void;

  auto ResetToDefaults() -> void;
  auto ResetAutoExposureDefaults() -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  mutable std::mutex mutex_;
  observer_ptr<PostProcessSettingsService> service_;
  std::uint64_t epoch_ { 0 };

  // Cached state
  bool exposure_enabled_ { true };
  engine::ExposureMode exposure_mode_ { engine::ExposureMode::kManual };
  float manual_ev_ { 9.7F };
  float manual_camera_aperture_ { 11.0F };
  float manual_camera_shutter_rate_ { 125.0F };
  float manual_camera_iso_ { 100.0F };
  float exposure_compensation_ { 0.0F };
  float exposure_key_ { 10.0F };

  float auto_exposure_speed_up_ {
    engine::AutoExposurePassConfig::kDefaultAdaptationSpeedUp
  };
  float auto_exposure_speed_down_ {
    engine::AutoExposurePassConfig::kDefaultAdaptationSpeedDown
  };
  float auto_exposure_low_percentile_ {
    engine::AutoExposurePassConfig::kDefaultLowPercentile
  };
  float auto_exposure_high_percentile_ {
    engine::AutoExposurePassConfig::kDefaultHighPercentile
  };
  float auto_exposure_min_log_lum_ {
    engine::AutoExposurePassConfig::kDefaultMinLogLuminance
  };
  float auto_exposure_log_lum_range_ {
    engine::AutoExposurePassConfig::kDefaultLogLuminanceRange
  };
  float auto_exposure_target_lum_ {
    engine::AutoExposurePassConfig::kDefaultTargetLuminance
  };
  engine::MeteringMode auto_exposure_metering_mode_ {
    engine::AutoExposurePassConfig::kDefaultMeteringMode
  };

  bool tonemapping_enabled_ { true };
  engine::ToneMapper tonemapping_mode_ { engine::ToneMapper::kAcesFitted };
};

} // namespace oxygen::examples::ui
