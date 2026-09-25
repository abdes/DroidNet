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

namespace oxygen::examples::light_bench {

//! Demo panel for LightBench-specific controls.
class LightBenchPanel final : public DemoPanel {
public:
  struct Actions {
    std::function<void()> reset;
    std::function<void(LightBenchPreset)> select;
    std::function<LightBenchPreset()> selected;
    std::function<bool()> is_reference;
    std::function<Result<void, std::string>(const std::filesystem::path&)> save;
    std::function<Result<void, std::string>(const std::filesystem::path&)> load;
  };
  LightBenchPanel(observer_ptr<LightScene> light_scene, Actions actions,
    const std::filesystem::path& settings_path);

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
  //! Persistent preset controls, independent of the active sidebar panel.
  auto DrawPresetOverlay() -> void;
  auto OnLoaded() -> void override { }
  auto OnUnloaded() -> void override { }

private:
  auto DrawSceneSection() -> void;
  auto DrawPresetControls(float button_width) -> void;
  auto DrawSceneAdvancedSection() -> void;
  auto DrawSceneObjectControls(std::string_view label,
    LightScene::SceneObjectState& state, bool allow_rotation) -> void;
  auto DrawVector3Table(const std::string& id, const char* label, Vec3& value,
    float speed, float min_value, float max_value) -> void;
  auto DrawAxisFloatCell(const std::string& id, const Vec3& color, float& value,
    float speed, float min_value, float max_value) -> void;
  auto DrawLightsSection() -> void;
  auto DrawDirectionalLightControls() -> void;
  auto DrawPointLightControls() -> void;
  auto DrawSpotLightControls() -> void;

  Actions actions_;
  std::array<char, 1024> settings_path_ {};
  std::string file_status_;
  observer_ptr<LightScene> light_scene_ { nullptr };
  std::string icon_ {};
};

} // namespace oxygen::examples::light_bench
