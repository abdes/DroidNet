//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportResetFeature.h"

#include "EditorModule/EditorViewportMathHelpers.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <Oxygen/Core/Constants.h>

namespace oxygen::interop::module {

  namespace {

    struct ResetParams {
      glm::vec3 default_focus_point = glm::vec3(0.0f, 0.0f, 0.0f);
      glm::vec3 default_camera_position = glm::vec3(10.0f, -10.0f, 7.0f);
      glm::vec3 up = oxygen::space::move::Up;
    };

  } // namespace

  auto EditorViewportResetFeature::RegisterBindings(
    oxygen::engine::InputSystem& input_system,
    const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
    -> void {
    using oxygen::input::Action;
    using oxygen::input::ActionTriggerDown;
    using oxygen::input::ActionValueType;
    using oxygen::input::InputActionMapping;
    using platform::InputSlots;

    if (reset_action_) {
      return;
    }

    reset_action_ = std::make_shared<Action>(
      "Editor.Camera.Reset", ActionValueType::kBool);
    input_system.AddAction(reset_action_);

    const auto trig = std::make_shared<ActionTriggerDown>();
    trig->MakeExplicit();

    const auto reset = std::make_shared<InputActionMapping>(
      reset_action_, InputSlots::Home);
    reset->AddTrigger(trig);
    ctx->AddMapping(reset);
  }

  auto EditorViewportResetFeature::Apply(scene::SceneNode camera_node,
    const input::InputSnapshot& input_snapshot,
    glm::vec3& focus_point,
    float& /*ortho_half_height*/,
    float /*dt_seconds*/) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    if (!input_snapshot.DidActionTrigger("Editor.Camera.Reset")) {
      return;
    }

    const ResetParams params {};

    focus_point = params.default_focus_point;

    auto transform = camera_node.GetTransform();
    (void)transform.SetLocalPosition(params.default_camera_position);

    const glm::quat look_rotation = viewport::LookRotationFromPositionToTarget(
      params.default_camera_position,
      focus_point,
      params.up);
    (void)transform.SetLocalRotation(look_rotation);
  }

} // namespace oxygen::interop::module
