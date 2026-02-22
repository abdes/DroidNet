//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Input/Action.h>

#include "DemoShell/UI/DemoPanel.h"

namespace oxygen::examples::ui {
class CameraRigController;
} // namespace oxygen::examples::ui

namespace oxygen::examples::physics_demo {

struct PhysicsDemoPanelConfig {
  observer_ptr<oxygen::examples::ui::CameraRigController> camera_rig {
    nullptr
  };

  std::shared_ptr<oxygen::input::Action> launch_action { nullptr };
  std::shared_ptr<oxygen::input::Action> reset_action { nullptr };
  std::shared_ptr<oxygen::input::Action> nudge_left_action { nullptr };
  std::shared_ptr<oxygen::input::Action> nudge_right_action { nullptr };

  bool* pending_launch { nullptr };
  bool* pending_reset { nullptr };
  float* launch_impulse { nullptr };
  float* player_speed { nullptr };
  bool* player_settled { nullptr };

  uint64_t* launches_count { nullptr };
  uint64_t* resets_count { nullptr };
  uint64_t* contact_events_count { nullptr };
  uint64_t* flipper_trigger_count { nullptr };

  float* settle_progress { nullptr };
  float settle_target { 1.0F };
};

class PhysicsDemoPanel final : public DemoPanel {
public:
  PhysicsDemoPanel() = default;
  ~PhysicsDemoPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PhysicsDemoPanel)
  OXYGEN_DEFAULT_MOVABLE(PhysicsDemoPanel)

  void Initialize(const PhysicsDemoPanelConfig& config);
  void UpdateConfig(const PhysicsDemoPanelConfig& config);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto DrawContents() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  [[nodiscard]] static auto ActionState(
    const std::shared_ptr<oxygen::input::Action>& action) -> const char*;

  PhysicsDemoPanelConfig config_ {};
};

} // namespace oxygen::examples::physics_demo
