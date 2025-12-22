//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportWheelZoomFeature.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::interop::module {

  namespace {

    struct WheelZoomParams {
      float zoom_sensitivity_units_per_tick = 0.6f;
      float min_radius = 0.25f;
      float max_radius = 100000.0f;
    };

    [[nodiscard]] auto ComputeMaxRadius(scene::SceneNode camera_node,
      const WheelZoomParams& params) noexcept -> float {
      float max_radius = params.max_radius;

      if (auto cam_ref = camera_node.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
        const float far_plane = cam_ref->get().GetFarPlane();
        if (std::isfinite(far_plane) && far_plane > params.min_radius) {
          max_radius = std::min(max_radius, far_plane * 0.95f);
        }
      }

      return std::max(params.min_radius, max_radius);
    }

    [[nodiscard]] auto IsFinite(const glm::vec3& v) noexcept -> bool {
      return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    [[nodiscard]] auto NormalizeSafe(glm::vec3 v, glm::vec3 fallback) noexcept
      -> glm::vec3 {
      const float len2 = glm::dot(v, v);
      if (len2 <= std::numeric_limits<float>::epsilon()) {
        return fallback;
      }
      return v / std::sqrt(len2);
    }

    [[nodiscard]] auto HasAction(const input::InputSnapshot& snapshot,
      std::string_view name) noexcept -> bool {
      return snapshot.GetActionStateFlags(name) != input::ActionState::kNone;
    }

    [[nodiscard]] auto GetAxis1DOrZero(const input::InputSnapshot& snapshot,
      std::string_view name) noexcept -> float {
      if (!HasAction(snapshot, name)) {
        return 0.0f;
      }
      const auto v = snapshot.GetActionValue(name).GetAs<Axis1D>();
      return v.x;
    }

    [[nodiscard]] auto AccumulateAxis1DFromTransitionsOrZero(
      const input::InputSnapshot& snapshot,
      std::string_view name) noexcept -> float {
      if (!HasAction(snapshot, name)) {
        return 0.0f;
      }

      float delta = 0.0f;
      bool saw_non_zero = false;
      for (const auto& tr : snapshot.GetActionTransitions(name)) {
        const auto& v = tr.value_at_transition.GetAs<Axis1D>();
        if (std::abs(v.x) > 0.0f) {
          delta += v.x;
          saw_non_zero = true;
        }
      }

      if (saw_non_zero) {
        return delta;
      }

      return GetAxis1DOrZero(snapshot, name);
    }

  } // namespace

  auto EditorViewportWheelZoomFeature::RegisterBindings(
    oxygen::engine::InputSystem& input_system,
    const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
    -> void {
    using oxygen::input::Action;
    using oxygen::input::ActionTriggerDown;
    using oxygen::input::ActionValueType;
    using oxygen::input::InputActionMapping;
    using platform::InputSlots;

    if (zoom_action_) {
      return;
    }

    zoom_action_ = std::make_shared<Action>(
      "Editor.Camera.Zoom", ActionValueType::kAxis1D);
    input_system.AddAction(zoom_action_);

    const auto trig = std::make_shared<ActionTriggerDown>();
    trig->MakeExplicit();

    const auto zoom = std::make_shared<InputActionMapping>(
      zoom_action_, InputSlots::MouseWheelY);
    zoom->AddTrigger(trig);
    ctx->AddMapping(zoom);
  }

  auto EditorViewportWheelZoomFeature::Apply(scene::SceneNode camera_node,
    const input::InputSnapshot& input_snapshot,
    glm::vec3& focus_point,
    float dt_seconds) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    const WheelZoomParams params {};
    const float max_radius = ComputeMaxRadius(camera_node, params);

    auto transform = camera_node.GetTransform();
    const float wheel_ticks =
      AccumulateAxis1DFromTransitionsOrZero(input_snapshot, "Editor.Camera.Zoom");

    if (std::abs(wheel_ticks) <= 0.0f) {
      return;
    }

    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3 {});

    if (!IsFinite(position) || !IsFinite(focus_point)) {
      // Recover from non-finite state by resetting to a sane view.
      focus_point = glm::vec3(0.0f, 0.0f, 0.0f);
      const glm::vec3 safe_position = focus_point + glm::vec3(0.0f, 0.0f, 5.0f);
      (void)transform.SetLocalPosition(safe_position);
      return;
    }

    glm::vec3 offset = position - focus_point;
    float radius = std::sqrt(glm::dot(offset, offset));
    if (!std::isfinite(radius)) {
      radius = max_radius;
    }

    radius = std::clamp(radius, params.min_radius, max_radius);
    if (radius <= std::numeric_limits<float>::epsilon()) {
      offset = glm::vec3(0.0f, 0.0f, params.min_radius);
      radius = params.min_radius;
    }

    const glm::vec3 dir = NormalizeSafe(offset, glm::vec3(0.0f, 0.0f, 1.0f));
    const float dr = wheel_ticks * params.zoom_sensitivity_units_per_tick;
    float new_radius = radius - dr;
    if (!std::isfinite(new_radius)) {
      new_radius = max_radius;
    }
    new_radius = std::clamp(new_radius, params.min_radius, max_radius);
    const glm::vec3 new_position = focus_point + dir * new_radius;
    (void)transform.SetLocalPosition(new_position);

    const auto new_pos = transform.GetLocalPosition().value_or(position);
    const glm::vec3 dp = new_pos - position;
    if (glm::dot(dp, dp) > 1e-6f) {
      DLOG_F(INFO,
        "Editor camera moved: dpos=({:.3f},{:.3f},{:.3f}) newPos=({:.3f},{:.3f},{:.3f})",
        dp.x, dp.y, dp.z, new_pos.x, new_pos.y, new_pos.z);
    }
  }

} // namespace oxygen::interop::module
