//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include "DemoShell/UI/DemoPanel.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Input/Action.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {
class CameraRigController;
} // namespace oxygen::examples::ui

namespace oxygen::examples::physics_demo {

//! The demo's commands; the module registers them.
inline constexpr std::string_view kLaunchCommand = "physics.launch";
inline constexpr std::string_view kResetCommand = "physics.reset";
inline constexpr std::string_view kLaunchImpulseCommand
  = "physics.launch_impulse";

//! What the panel shows; it changes nothing except through commands.
struct PhysicsDemoPanelConfig {
  observer_ptr<oxygen::examples::ui::CameraRigController> camera_rig {
    nullptr
  };
  observer_ptr<PanelCommands> commands { nullptr };

  std::shared_ptr<oxygen::input::Action> launch_action { nullptr };
  std::shared_ptr<oxygen::input::Action> reset_action { nullptr };
  std::shared_ptr<oxygen::input::Action> nudge_left_action { nullptr };
  std::shared_ptr<oxygen::input::Action> nudge_right_action { nullptr };

  observer_ptr<const float> launch_impulse { nullptr };
  observer_ptr<const float> player_speed { nullptr };
  observer_ptr<const bool> player_settled { nullptr };

  observer_ptr<const uint64_t> launches_count { nullptr };
  observer_ptr<const uint64_t> resets_count { nullptr };
  observer_ptr<const uint64_t> contact_events_count { nullptr };
  observer_ptr<const uint64_t> flipper_trigger_count { nullptr };

  observer_ptr<const float> settle_progress { nullptr };
  float settle_target { 1.0F };
};

class PhysicsDemoPanel final : public DemoPanel {
public:
  PhysicsDemoPanel() = default;
  ~PhysicsDemoPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PhysicsDemoPanel)
  OXYGEN_DEFAULT_MOVABLE(PhysicsDemoPanel)

  void Initialize(const PhysicsDemoPanelConfig& config);

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
