//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <glm/vec3.hpp>

#include <memory>

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

  //! A composable unit of editor viewport camera navigation.
  /*!
   Implementations register input bindings/mappings and apply camera transforms
   based on the current input snapshot.
  */
  class IEditorViewportNavigationFeature {
  public:
    IEditorViewportNavigationFeature() = default;
    IEditorViewportNavigationFeature(const IEditorViewportNavigationFeature&) = delete;
    IEditorViewportNavigationFeature(IEditorViewportNavigationFeature&&) = delete;
    auto operator=(const IEditorViewportNavigationFeature&)
      -> IEditorViewportNavigationFeature& = delete;
    auto operator=(IEditorViewportNavigationFeature&&)
      -> IEditorViewportNavigationFeature& = delete;

    virtual ~IEditorViewportNavigationFeature() = default;

    //! Registers input bindings and mappings for this feature.
    virtual auto RegisterBindings(oxygen::engine::InputSystem& input_system,
      const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
      -> void = 0;

    //! Applies this feature to the given camera node for the current frame.
    virtual auto Apply(oxygen::scene::SceneNode camera_node,
      const oxygen::input::InputSnapshot& input_snapshot,
      glm::vec3& focus_point,
      float dt_seconds) noexcept -> void = 0;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
