//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportDollyFeature.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

namespace oxygen::interop::module {

  namespace {

    struct DollyParams {
      float units_per_pixel_at_unit_distance = 0.01f;
      float min_radius = 0.25f;
      glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    };

    [[nodiscard]] auto HasAction(const input::InputSnapshot& snapshot,
      std::string_view name) noexcept -> bool {
      return snapshot.GetActionStateFlags(name) != input::ActionState::kNone;
    }

    [[nodiscard]] auto GetAxis2DOrZero(const input::InputSnapshot& snapshot,
      std::string_view name) noexcept -> Axis2D {
      if (!HasAction(snapshot, name)) {
        return Axis2D{ .x = 0.0f, .y = 0.0f };
      }
      const auto v = snapshot.GetActionValue(name).GetAs<Axis2D>();
      return v;
    }

    [[nodiscard]] auto AccumulateAxis2DFromTransitionsOrZero(
      const input::InputSnapshot& snapshot,
      std::string_view name) noexcept -> Axis2D {
      if (!HasAction(snapshot, name)) {
        return Axis2D{ .x = 0.0f, .y = 0.0f };
      }

      Axis2D delta{ .x = 0.0f, .y = 0.0f };
      bool saw_non_zero = false;
      for (const auto& tr : snapshot.GetActionTransitions(name)) {
        const auto& v = tr.value_at_transition.GetAs<Axis2D>();
        if ((std::abs(v.x) > 0.0f) || (std::abs(v.y) > 0.0f)) {
          delta.x += v.x;
          delta.y += v.y;
          saw_non_zero = true;
        }
      }

      if (saw_non_zero) {
        return delta;
      }

      return GetAxis2DOrZero(snapshot, name);
    }

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

  auto EditorViewportDollyFeature::RegisterBindings(
    oxygen::engine::InputSystem& input_system,
    const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
    -> void {
    using oxygen::input::Action;
    using oxygen::input::ActionTriggerDown;
    using oxygen::input::ActionValueType;
    using oxygen::input::InputActionMapping;
    using platform::InputSlots;

    if (rmb_action_) {
      return;
    }

    rmb_action_ = std::make_shared<Action>(
      "Editor.Mouse.RightButton", ActionValueType::kBool);
    input_system.AddAction(rmb_action_);

    const auto trig = std::make_shared<ActionTriggerDown>();
    trig->MakeExplicit();

    const auto rmb = std::make_shared<InputActionMapping>(
      rmb_action_, InputSlots::RightMouseButton);
    rmb->AddTrigger(trig);
    ctx->AddMapping(rmb);
  }

  auto EditorViewportDollyFeature::Apply(scene::SceneNode camera_node,
    const input::InputSnapshot& input_snapshot,
    glm::vec3& focus_point,
    float /*dt_seconds*/) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    const DollyParams params{};

    const bool alt_held = input_snapshot.IsActionOngoing("Editor.Modifier.Alt");
    const bool rmb_held =
      input_snapshot.IsActionOngoing("Editor.Mouse.RightButton");
    if (!alt_held || !rmb_held) {
      return;
    }

    const auto mouse_delta =
      AccumulateAxis2DFromTransitionsOrZero(input_snapshot, "Editor.Mouse.Delta");
    if ((std::abs(mouse_delta.x) <= 0.0f) && (std::abs(mouse_delta.y) <= 0.0f)) {
      return;
    }

    auto transform = camera_node.GetTransform();

    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3{});

    glm::vec3 offset = position - focus_point;
    float radius = std::sqrt(glm::dot(offset, offset));
    if (radius < params.min_radius) {
      offset = glm::vec3(0.0f, 0.0f, params.min_radius);
      radius = params.min_radius;
    }

    const glm::vec3 dir = NormalizeSafe(offset, glm::vec3(0.0f, 0.0f, 1.0f));

    // Drag up should dolly in (reduce radius).
    const float dr = mouse_delta.y * params.units_per_pixel_at_unit_distance * radius;
    const float new_radius = std::max(params.min_radius, radius + dr);

    const glm::vec3 new_position = focus_point + dir * new_radius;
    (void)transform.SetLocalPosition(new_position);

    const glm::quat look_rotation =
      LookRotationFromPositionToTarget(new_position, focus_point, params.up);
    (void)transform.SetLocalRotation(look_rotation);
  }

} // namespace oxygen::interop::module
