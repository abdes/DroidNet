//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"
#include <LightBench/LightScene.h>

namespace oxygen::examples::light_bench {

//! Demo panel for LightBench-specific controls.
class LightBenchPanel final : public DemoPanel {
public:
  explicit LightBenchPanel(observer_ptr<LightScene> light_scene);

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
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  auto DrawSceneSection() -> void;
  auto DrawScenePresets() -> void;
  auto DrawSceneAdvancedSection() -> void;
  auto DrawSceneObjectControls(std::string_view label,
    LightScene::SceneObjectState& state, bool allow_rotation) -> void;
  auto DrawVector3Table(const std::string& id, const char* label, Vec3& value,
    float speed, float min_value, float max_value) -> void;
  auto DrawAxisFloatCell(const std::string& id, const Vec3& color, float& value,
    float speed, float min_value, float max_value) -> void;
  auto DrawLightsSection() -> void;
  auto DrawPointLightControls() -> void;
  auto DrawSpotLightControls() -> void;

  auto LoadSettings() -> void;
  auto SaveSettings() -> void;
  auto MarkChanged() -> void;
  observer_ptr<LightScene> light_scene_ { nullptr };
  std::string icon_ {};
  bool settings_loaded_ { false };
  bool pending_changes_ { false };
};

} // namespace oxygen::examples::light_bench
