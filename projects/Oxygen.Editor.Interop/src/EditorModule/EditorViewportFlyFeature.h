//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <memory>

#include "EditorModule/EditorViewportNavigationFeature.h"

namespace oxygen::input {
  class Action;
} // namespace oxygen::input

namespace oxygen::engine {
  class InputSystem;
} // namespace oxygen::engine

namespace oxygen::interop::module {

  //! Free-fly perspective navigation in every orbit style: holding RMB looks
  //! with the mouse and moves with WASD/QE; the arrow keys move without RMB.
  //! The orbit focus point travels with the camera at its kept distance.
  class EditorViewportFlyFeature final : public IEditorViewportNavigationFeature {
  public:
    EditorViewportFlyFeature() = default;

    auto RegisterBindings(oxygen::engine::InputSystem& input_system,
      const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
      -> void override;

    auto Apply(oxygen::scene::SceneNode camera_node,
      const oxygen::input::InputSnapshot& input_snapshot,
      EditorViewportCameraControlMode control_mode,
      glm::vec3& focus_point,
      float& ortho_half_height,
      float movement_speed_units_per_second,
      float dt_seconds) noexcept -> void override;

  private:
    std::shared_ptr<oxygen::input::Action> w_action_;
    std::shared_ptr<oxygen::input::Action> a_action_;
    std::shared_ptr<oxygen::input::Action> s_action_;
    std::shared_ptr<oxygen::input::Action> d_action_;
    std::shared_ptr<oxygen::input::Action> q_action_;
    std::shared_ptr<oxygen::input::Action> e_action_;
    std::shared_ptr<oxygen::input::Action> up_arrow_action_;
    std::shared_ptr<oxygen::input::Action> down_arrow_action_;
    std::shared_ptr<oxygen::input::Action> left_arrow_action_;
    std::shared_ptr<oxygen::input::Action> right_arrow_action_;
    std::shared_ptr<oxygen::input::Action> shift_action_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
