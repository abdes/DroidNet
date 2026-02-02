//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"

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

namespace oxygen::examples::ui {
class CameraRigController;
} // namespace oxygen::examples::ui

namespace oxygen::examples::input_system {

//! Configuration for the InputSystem debug panel.
struct InputDebugPanelConfig {
  observer_ptr<engine::InputSystem> input_system { nullptr };
  observer_ptr<oxygen::examples::ui::CameraRigController> camera_rig {
    nullptr
  };

  std::shared_ptr<oxygen::input::Action> shift_action { nullptr };
  std::shared_ptr<oxygen::input::Action> jump_action { nullptr };
  std::shared_ptr<oxygen::input::Action> jump_higher_action { nullptr };
  std::shared_ptr<oxygen::input::Action> swim_up_action { nullptr };

  std::shared_ptr<oxygen::input::InputMappingContext> ground_movement_ctx {
    nullptr,
  };
  std::shared_ptr<oxygen::input::InputMappingContext> swimming_ctx { nullptr };

  bool* swimming_mode { nullptr };
  bool* pending_ground_reset { nullptr };
};

//! Demo panel that visualizes InputSystem actions and mapping state.
/*!
 Shows action state badges, analog values, and sparklines for the InputSystem
 example. It also exposes the ground/swimming toggle used by the demo.

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
  InputDebugPanel(InputDebugPanel&&) = default;
  auto operator=(InputDebugPanel&&) -> InputDebugPanel& = default;

  //! Initialize the panel configuration.
  void Initialize(const InputDebugPanelConfig& config);

  //! Update the panel configuration.
  void UpdateConfig(const InputDebugPanelConfig& config);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto DrawContents() -> void override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  struct History {
    static constexpr int kCapacity = 64;
    float values[kCapacity] = {};
    int head = 0;
    int count = 0;

    void Push(float v) noexcept;
  };

  InputDebugPanelConfig config_ {};
  std::unordered_map<const char*, History> histories_ {};
  std::unordered_map<const char*, double> last_trigger_time_ {};
  bool show_inactive_ { true };
};

} // namespace oxygen::examples::input_system
