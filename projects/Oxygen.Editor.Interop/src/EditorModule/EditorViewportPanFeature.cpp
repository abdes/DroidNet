//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportPanFeature.h"

namespace oxygen::interop::module {

  namespace {

    struct PanParams {
      float units_per_pixel_at_unit_distance = 0.0025f;
      float min_radius = 0.25f;
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

  } // namespace

  auto EditorViewportPanFeature::RegisterBindings(
    oxygen::engine::InputSystem& input_system,
    const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
    -> void {
    using oxygen::input::Action;
    using oxygen::input::ActionTriggerDown;
    using oxygen::input::ActionValueType;
    using oxygen::input::InputActionMapping;
    using platform::InputSlots;

    if (mmb_action_) {
      return;
    }

    mmb_action_ = std::make_shared<Action>(
      "Editor.Mouse.MiddleButton", ActionValueType::kBool);
    input_system.AddAction(mmb_action_);

    const auto trig = std::make_shared<ActionTriggerDown>();
    trig->MakeExplicit();

    const auto mmb = std::make_shared<InputActionMapping>(
      mmb_action_, InputSlots::MiddleMouseButton);
    mmb->AddTrigger(trig);
    ctx->AddMapping(mmb);
  }

  auto EditorViewportPanFeature::Apply(scene::SceneNode camera_node,
    const input::InputSnapshot& input_snapshot,
    glm::vec3& focus_point,
    float /*dt_seconds*/) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    const PanParams params{};

    const bool alt_held = input_snapshot.IsActionOngoing("Editor.Modifier.Alt");
    const bool mmb_held =
      input_snapshot.IsActionOngoing("Editor.Mouse.MiddleButton");
    if (!alt_held || !mmb_held) {
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
    const glm::quat rotation =
      transform.GetLocalRotation().value_or(glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f });

    const glm::vec3 offset = position - focus_point;
    const float radius = std::max(params.min_radius,
      std::sqrt(glm::dot(offset, offset)));

    const float pan_scale = params.units_per_pixel_at_unit_distance * radius;

    const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);

    // Pan convention: the scene follows the drag direction.
    // Drag right => scene moves right => camera moves left.
    // Drag up    => scene moves up    => camera moves down.
    const glm::vec3 delta_world =
      (right * (-mouse_delta.x) + up * (mouse_delta.y)) * pan_scale;

    const glm::vec3 new_position = position + delta_world;
    focus_point += delta_world;

    (void)transform.SetLocalPosition(new_position);
  }

} // namespace oxygen::interop::module
