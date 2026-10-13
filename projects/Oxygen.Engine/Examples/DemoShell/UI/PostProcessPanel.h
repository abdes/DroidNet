//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/PostProcessVm.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

//! Panel for controlling post-process effects (Exposure, Tonemapping).
/*!
 Every control posts a `pp.*` command (`pp.exposure`, `pp.camera`,
 `pp.output`, `pp.curve`, `pp.reset_auto`) to the current `scene:<revision>`
 target, so a person and a remote client edit through the same validated
 path. The compensation curve is edited as a local draft and applied as one
 command.
*/
class PostProcessPanel final : public DemoPanel {
public:
  PostProcessPanel(
    observer_ptr<PostProcessVm> vm, observer_ptr<PanelCommands> commands);

  // DemoPanel interface
  auto DrawContents() -> void override;
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;

  auto OnRegistered() -> void override { }
  auto OnLoaded() -> void override { }
  auto OnUnloaded() -> void override { }

private:
  //! Posts a pp.* command to the current authoring target, if any.
  auto RunOnTarget(std::string_view command, nlohmann::json arguments) const
    -> void;
  auto RunExposure(nlohmann::json fields) const -> void;
  auto RunCamera(float aperture, float shutter_rate, float iso) const -> void;
  auto RunOutput(std::string_view tone_curve, float gamma) const -> void;

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
  observer_ptr<PanelCommands> commands_;
};

} // namespace oxygen::examples::ui
