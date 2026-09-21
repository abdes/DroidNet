//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/PostProcessVm.h"

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples::ui {

//! Panel for controlling post-process effects (Exposure, Tonemapping).
class PostProcessPanel final : public DemoPanel {
public:
  explicit PostProcessPanel(observer_ptr<PostProcessVm> vm);

  // DemoPanel interface
  auto DrawContents() -> void override;
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;

  auto OnRegistered() -> void override { }
  auto OnLoaded() -> void override { }
  auto OnUnloaded() -> void override { }

private:
  void DrawExposureSection();
  void DrawTonemappingSection();
  void DrawAutoExposureControls();
  void DrawAdvancedExposureControls();
  void DrawCompensationCurve(const scene::ExposureSettings& requested);
  void DrawExposureStatus(
    const std::optional<vortex::ExposureSettingsStatus>& status);
  std::vector<scene::ExposureCompensationKey> curve_draft_;
  std::uint64_t curve_epoch_ { 0U };
  std::uint64_t curve_scene_revision_ { 0U };
  bool curve_initialized_ { false };
  bool curve_dirty_ { false };

  observer_ptr<PostProcessVm> vm_;
};

} // namespace oxygen::examples::ui
