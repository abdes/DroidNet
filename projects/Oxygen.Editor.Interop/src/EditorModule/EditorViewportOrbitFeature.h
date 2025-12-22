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

  //! Orbit the viewport camera around the focus point.
  class EditorViewportOrbitFeature final
    : public IEditorViewportNavigationFeature {
  public:
    EditorViewportOrbitFeature() = default;

    auto RegisterBindings(oxygen::engine::InputSystem& input_system,
      const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
      -> void override;

    auto Apply(oxygen::scene::SceneNode camera_node,
      const oxygen::input::InputSnapshot& input_snapshot,
      glm::vec3& focus_point,
      float dt_seconds) noexcept -> void override;

  private:
    std::shared_ptr<oxygen::input::Action> alt_action_;
    std::shared_ptr<oxygen::input::Action> lmb_action_;
    std::shared_ptr<oxygen::input::Action> mouse_delta_action_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
