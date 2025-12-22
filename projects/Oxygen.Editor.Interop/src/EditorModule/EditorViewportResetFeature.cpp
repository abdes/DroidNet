//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportResetFeature.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

namespace oxygen::interop::module {

  namespace {

    struct ResetParams {
      glm::vec3 default_focus_point = glm::vec3(0.0f, 0.0f, 0.0f);
      glm::vec3 default_camera_position = glm::vec3(10.0f, -10.0f, 7.0f);
      glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    };

    [[nodiscard]] auto NormalizeSafe(const glm::vec3 v,
      const glm::vec3 fallback) noexcept -> glm::vec3 {
      const float len2 = glm::dot(v, v);
      if (len2 <= std::numeric_limits<float>::epsilon()) {
        return fallback;
      }
      return v / std::sqrt(len2);
    }

    [[nodiscard]] auto LookRotationFromPositionToTarget(
      const glm::vec3& position,
      const glm::vec3& target_position,
      const glm::vec3& up_direction) noexcept -> glm::quat {
      const glm::vec3 forward =
        NormalizeSafe(target_position - position, glm::vec3(0.0f, 0.0f, -1.0f));
      const glm::vec3 right =
        NormalizeSafe(glm::cross(forward, up_direction), glm::vec3(1.0f, 0.0f, 0.0f));
      const glm::vec3 up = glm::cross(right, forward);

      glm::mat4 look_matrix(1.0f);
      look_matrix[0] = glm::vec4(right, 0.0f);
      look_matrix[1] = glm::vec4(up, 0.0f);
      look_matrix[2] = glm::vec4(-forward, 0.0f);

      return glm::quat_cast(look_matrix);
    }

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

    const glm::quat look_rotation = LookRotationFromPositionToTarget(
      params.default_camera_position,
      focus_point,
      params.up);
    (void)transform.SetLocalRotation(look_rotation);
  }

} // namespace oxygen::interop::module
