//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <glm/vec3.hpp>

#include <memory>
#include <vector>

#include "EditorModule/EditorViewportNavigationFeature.h"

namespace oxygen::input {
  class InputMappingContext;
  class InputSnapshot;
} // namespace oxygen::input

namespace oxygen::scene {
  class SceneNode;
} // namespace oxygen::scene

namespace oxygen::engine {
  class InputSystem;
} // namespace oxygen::engine

namespace oxygen::interop::module {

  //! Composes editor viewport camera navigation from small features.
  class EditorViewportNavigation final {
  public:
    EditorViewportNavigation();

    auto InitializeBindings(oxygen::engine::InputSystem& input_system) noexcept
      -> bool;

    auto Apply(oxygen::scene::SceneNode camera_node,
      const oxygen::input::InputSnapshot& input_snapshot,
      glm::vec3& focus_point,
      float& ortho_half_height,
      float dt_seconds) noexcept -> void;

    auto ApplyNonWheel(oxygen::scene::SceneNode camera_node,
      const oxygen::input::InputSnapshot& input_snapshot,
      glm::vec3& focus_point,
      float& ortho_half_height,
      float dt_seconds) noexcept -> void;

    auto ApplyWheelOnly(oxygen::scene::SceneNode camera_node,
      const oxygen::input::InputSnapshot& input_snapshot,
      glm::vec3& focus_point,
      float& ortho_half_height,
      float dt_seconds) noexcept -> void;

  private:
    std::shared_ptr<oxygen::input::InputMappingContext> ctx_;
    std::vector<std::unique_ptr<IEditorViewportNavigationFeature>> features_;

    // A cached pointer to the wheel-zoom feature instance owned by `features_`.
    // This allows the editor module to route wheel input to the hovered view
    // without re-applying all other navigation features.
    IEditorViewportNavigationFeature* wheel_zoom_feature_{ nullptr };
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
