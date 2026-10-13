//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "DemoShell/UI/DemoPanel.h"

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::engine {
class InputSystem;
} // namespace oxygen::engine

namespace oxygen::input {
class Action;
class InputMappingContext;
} // namespace oxygen::input

namespace oxygen::scene {
class SceneNode;
} // namespace oxygen::scene

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {
class CameraRigController;
} // namespace oxygen::examples::ui

namespace oxygen::examples::input_system {

//! The swimming mode command; the module registers it.
inline constexpr std::string_view kSwimmingCommand = "input.swimming";

//! Configuration for the InputSystem debug panel.
struct InputDebugPanelConfig {
  observer_ptr<oxygen::examples::ui::CameraRigController> camera_rig {
    nullptr
  };
  //! Runs the swimming mode command.
  observer_ptr<PanelCommands> commands { nullptr };

  std::shared_ptr<oxygen::input::Action> shift_action { nullptr };
  std::shared_ptr<oxygen::input::Action> jump_action { nullptr };
  std::shared_ptr<oxygen::input::Action> jump_higher_action { nullptr };
  std::shared_ptr<oxygen::input::Action> swim_up_action { nullptr };

  observer_ptr<const bool> swimming_mode { nullptr };
};

//! Demo panel that visualizes InputSystem actions and mapping state.
/*!
 Shows action state badges, analog values, and sparklines for the InputSystem
 example. Its ground/swimming toggle runs `input.swimming`.

 ### Key Features

 - **Action Visualization**: Badges, bars, and sparklines per action.
 - **Mode Controls**: Toggle ground/swimming mapping contexts.
 - **Live Camera Info**: Displays the current demo camera position.

 @see DemoPanel
*/
class InputDebugPanel final : public DemoPanel {
public:
  InputDebugPanel() = default;
  ~InputDebugPanel() override = default;

  InputDebugPanel(const InputDebugPanel&) = delete;
  auto operator=(const InputDebugPanel&) -> InputDebugPanel& = delete;
  InputDebugPanel(InputDebugPanel&&) noexcept = default;
  auto operator=(InputDebugPanel&&) -> InputDebugPanel& = default;

  auto Initialize(const InputDebugPanelConfig& config) -> void;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto DrawContents() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  //! The latest values of an action, oldest overwritten first.
  class History {
  public:
    auto Push(float value) noexcept -> void;
    //! The values held, oldest first.
    [[nodiscard]] auto InOrder() const -> std::vector<float>;

  private:
    static constexpr std::size_t kCapacity = 64;
    std::array<float, kCapacity> values_ {};
    std::size_t next_ { 0 };
    std::size_t count_ { 0 };
  };

  InputDebugPanelConfig config_ {};
  std::unordered_map<const char*, History> histories_;
  std::unordered_map<const char*, double> last_trigger_time_;
  bool show_inactive_ { true };
};

} // namespace oxygen::examples::input_system
