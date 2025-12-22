//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportDollyFeature.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <limits>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::interop::module {

  namespace {

    struct DollyParams {
      float zoom_log_per_pixel = 0.0025f;
      float min_radius = 0.25f;
      float max_radius = 100000.0f;
      float max_abs_pixels_per_frame = 500.0f;
      float max_abs_log_zoom_per_frame = 1.5f;
      glm::vec3 up = oxygen::space::move::Up;
    };

    [[nodiscard]] auto ComputeMaxRadius(scene::SceneNode camera_node,
      const DollyParams& params,
      const float min_radius) noexcept -> float {
      float max_radius = params.max_radius;

      // Keep the focus point within the camera frustum.
      if (auto cam_ref = camera_node.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
        const float far_plane = cam_ref->get().GetFarPlane();
        if (std::isfinite(far_plane) && far_plane > min_radius) {
          // Leave a small margin so the focus doesn't sit exactly on the far plane.
          max_radius = std::min(max_radius, far_plane * 0.95f);
        }
      }

      return std::max(min_radius, max_radius);
    }

    [[nodiscard]] auto IsFinite(const glm::vec3& v) noexcept -> bool {
      return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

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
    float& /*ortho_half_height*/, float /*dt_seconds*/) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    if (camera_node.GetCameraAs<scene::OrthographicCamera>()) {
      return;
    }

    const DollyParams params{};
    float far_plane = std::numeric_limits<float>::quiet_NaN();
    float near_plane = std::numeric_limits<float>::quiet_NaN();

    if (auto cam_ref = camera_node.GetCameraAs<scene::PerspectiveCamera>(); cam_ref) {
      far_plane = cam_ref->get().GetFarPlane();
      near_plane = cam_ref->get().GetNearPlane();
    }

    float min_radius = params.min_radius;
    if (std::isfinite(near_plane) && near_plane > 0.0f) {
      // Keep the focus point comfortably outside the near plane.
      min_radius = std::max(min_radius, near_plane * 2.0f);
    }

    float max_radius = ComputeMaxRadius(camera_node, params, min_radius);

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
    const glm::quat rotation =
      transform.GetLocalRotation().value_or(glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f });

    if (!IsFinite(position) || !IsFinite(focus_point)) {
      // If we ever reach non-finite state (e.g., runaway radius overflow),
      // reset to a sane view so the user can recover.
      focus_point = glm::vec3(0.0f, 0.0f, 0.0f);

      const glm::vec3 safe_position = focus_point + glm::vec3(0.0f, 0.0f, 5.0f);
      (void)transform.SetLocalPosition(safe_position);

      const glm::quat look_rotation =
        LookRotationFromPositionToTarget(safe_position, focus_point, params.up);
      (void)transform.SetLocalRotation(look_rotation);
      return;
    }

    const glm::vec3 fallback_dir = NormalizeSafe(
      -(rotation * glm::vec3(0.0f, 0.0f, -1.0f)),
      glm::vec3(0.0f, 0.0f, 1.0f));

    const glm::vec3 offset = position - focus_point;
    float radius = std::sqrt(glm::dot(offset, offset));
    if (!std::isfinite(radius)) {
      radius = max_radius;
    }

    glm::vec3 dir = fallback_dir;
    if (radius > std::numeric_limits<float>::epsilon()) {
      dir = offset / radius;
    }
    dir = NormalizeSafe(dir, fallback_dir);

    radius = std::clamp(radius, min_radius, max_radius);

    // Drag up should dolly in (reduce radius).
    const float dy = std::clamp(mouse_delta.y,
      -params.max_abs_pixels_per_frame,
      params.max_abs_pixels_per_frame);
    float log_zoom = dy * params.zoom_log_per_pixel;
    log_zoom = std::clamp(log_zoom,
      -params.max_abs_log_zoom_per_frame,
      params.max_abs_log_zoom_per_frame);

    const float unclamped_new_radius = radius * std::exp(log_zoom);
    float new_radius = unclamped_new_radius;
    if (!std::isfinite(new_radius)) {
      new_radius = max_radius;
    }
    new_radius = std::clamp(new_radius, min_radius, max_radius);

    const glm::vec3 new_position = focus_point + dir * new_radius;
    (void)transform.SetLocalPosition(new_position);

    const glm::quat look_rotation =
      LookRotationFromPositionToTarget(new_position, focus_point, params.up);
    (void)transform.SetLocalRotation(look_rotation);
  }

} // namespace oxygen::interop::module
