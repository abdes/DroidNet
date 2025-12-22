//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportPanFeature.h"

#include "EditorModule/EditorViewportInputHelpers.h"

#include <algorithm>
#include <unordered_map>

#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::interop::module {

  namespace {

    struct PanParams {
      float units_per_pixel_at_unit_distance = 0.0025f;
      float min_radius = 0.25f;
    };

    struct PanState {
      bool was_active = false;
    };

    [[nodiscard]] auto GetOrInitPanState(
      std::unordered_map<scene::NodeHandle, PanState>& states,
      const scene::SceneNode& camera_node) noexcept -> PanState& {
      return states[camera_node.GetHandle()];
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
    float& /*ortho_half_height*/,
    float /*dt_seconds*/) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    const PanParams params{};

    static std::unordered_map<scene::NodeHandle, PanState> pan_states;
    auto& state = GetOrInitPanState(pan_states, camera_node);

    const bool alt_held = input_snapshot.IsActionOngoing("Editor.Modifier.Alt");
    const bool mmb_held =
      input_snapshot.IsActionOngoing("Editor.Mouse.MiddleButton");
    const bool active = alt_held && mmb_held;
    if (!active) {
      state.was_active = false;
      return;
    }

    if (!state.was_active) {
      state.was_active = true;
      // Consume the activation frame so a stale mouse delta doesn't cause a jump.
      return;
    }

    const auto mouse_delta =
      viewport::AccumulateAxis2DFromTransitionsOrZero(input_snapshot,
        "Editor.Mouse.Delta");
    if ((std::abs(mouse_delta.x) <= 0.0f) && (std::abs(mouse_delta.y) <= 0.0f)) {
      return;
    }

    auto transform = camera_node.GetTransform();

    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3{});
    const glm::quat rotation =
      transform.GetLocalRotation().value_or(glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f });

    const glm::vec3 offset = position - focus_point;
    float pan_scale_x = 0.0f;
    float pan_scale_y = 0.0f;

    if (auto cam_ref = camera_node.GetCameraAs<scene::OrthographicCamera>(); cam_ref) {
      const auto ext = cam_ref->get().GetExtents();
      const auto vp = cam_ref->get().ActiveViewport();

      const float width_world = std::abs(ext[1] - ext[0]);
      const float height_world = std::abs(ext[3] - ext[2]);

      const float vp_w = std::max(1.0f, vp.width);
      const float vp_h = std::max(1.0f, vp.height);

      pan_scale_x = width_world / vp_w;
      pan_scale_y = height_world / vp_h;
    }
    else {
      const float radius = std::max(params.min_radius,
        std::sqrt(glm::dot(offset, offset)));
      const float pan_scale = params.units_per_pixel_at_unit_distance * radius;
      pan_scale_x = pan_scale;
      pan_scale_y = pan_scale;
    }

    const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);

    // Pan convention: the scene follows the drag direction.
    // Drag right => scene moves right => camera moves left.
    // Drag up    => scene moves up    => camera moves down.
    const glm::vec3 delta_world =
      (right * (-mouse_delta.x) * pan_scale_x) +
      (up * (mouse_delta.y) * pan_scale_y);

    const glm::vec3 new_position = position + delta_world;
    focus_point += delta_world;

    (void)transform.SetLocalPosition(new_position);
  }

} // namespace oxygen::interop::module
