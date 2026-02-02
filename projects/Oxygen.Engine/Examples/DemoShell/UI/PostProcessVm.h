//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <mutex>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>

#include "DemoShell/Services/PostProcessSettingsService.h"

namespace oxygen::examples::ui {

//! View model for Post Process panel state.
class PostProcessVm {
public:
  explicit PostProcessVm(observer_ptr<PostProcessSettingsService> service);

  // Compositing
  [[nodiscard]] auto GetCompositingEnabled() -> bool;
  auto SetCompositingEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetCompositingAlpha() -> float;
  auto SetCompositingAlpha(float alpha) -> void;

  // Exposure
  [[nodiscard]] auto GetExposureMode() -> engine::ExposureMode;
  auto SetExposureMode(engine::ExposureMode mode) -> void;

  [[nodiscard]] auto GetManualExposureEV100() -> float;
  auto SetManualExposureEV100(float ev100) -> void;

  [[nodiscard]] auto GetExposureCompensation() -> float;
  auto SetExposureCompensation(float stops) -> void;

  // Tonemapping
  [[nodiscard]] auto GetTonemappingEnabled() -> bool;
  auto SetTonemappingEnabled(bool enabled) -> void;

  [[nodiscard]] auto GetToneMapper() -> engine::ToneMapper;
  auto SetToneMapper(engine::ToneMapper mode) -> void;

private:
  auto Refresh() -> void;
  [[nodiscard]] auto IsStale() const -> bool;

  mutable std::mutex mutex_ {};
  observer_ptr<PostProcessSettingsService> service_;
  std::uint64_t epoch_ { 0 };

  // Cached state
  bool compositing_enabled_ { true };
  float compositing_alpha_ { 1.0F };
  engine::ExposureMode exposure_mode_ { engine::ExposureMode::kManual };
  float manual_ev100_ { 9.7F };
  float exposure_compensation_ { 0.0F };
  bool tonemapping_enabled_ { true };
  engine::ToneMapper tonemapping_mode_ { engine::ToneMapper::kAcesFitted };
};

} // namespace oxygen::examples::ui
