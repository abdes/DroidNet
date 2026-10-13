//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "DemoShell/UI/DemoPanel.h"
#include "LightBench/LightBenchSettings.h"
#include "LightBench/LightScene.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::light_bench {

//! Demo panel for LightBench-specific controls.
/*!
 Every control runs a command: the presets run `lightbench.preset` and
 `lightbench.reset`, the light and geometry controls the `lightbench.*`
 commands this panel registers. The settings path is a local draft.
*/
class LightBenchPanel final : public DemoPanel {
public:
  struct Actions {
    std::function<LightBenchPreset()> selected;
    std::function<bool()> is_reference;
    std::function<Result<void, std::string>(const std::filesystem::path&)> save;
    std::function<Result<void, std::string>(const std::filesystem::path&)> load;
  };
  LightBenchPanel(observer_ptr<LightScene> light_scene, Actions actions,
    const std::filesystem::path& settings_path,
    observer_ptr<PanelCommands> commands);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "LightBench";
  }

  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override
  {
    return 520.0F;
  }

  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override
  {
    return icon_;
  }

  auto DrawContents() -> void override;
  //! Persistent preset controls, independent of the active sidebar panel,
  //! centered below the UI that covers `top_inset` of the window.
  auto DrawPresetOverlay(float top_inset) -> void;
  auto OnLoaded() -> void override { }
  auto OnUnloaded() -> void override { }

private:
  auto DrawSceneSection() -> void;
  auto DrawPresetControls(float button_width) -> void;
  auto DrawSceneAdvancedSection() -> void;
  auto DrawSceneObjectControls(std::string_view object, std::string_view label,
    const LightScene::SceneObjectState& state, bool allow_rotation) -> void;
  auto RegisterCommands() -> void;
  auto DrawLightsSection() -> void;
  auto DrawDirectionalLightControls() -> void;
  auto DrawPointLightControls() -> void;
  auto DrawSpotLightControls() -> void;

  Actions actions_;
  std::array<char, 1024> settings_path_ {};
  std::string file_status_;
  observer_ptr<LightScene> light_scene_ { nullptr };
  observer_ptr<PanelCommands> commands_;
  std::string icon_;
};

} // namespace oxygen::examples::light_bench
