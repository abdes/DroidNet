//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportWheelZoomFeature.h"

#include "EditorModule/EditorViewportInputHelpers.h"
#include "EditorModule/EditorViewportMathHelpers.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>

namespace oxygen::interop::module {

  namespace {

    struct WheelZoomParams {
      float zoom_sensitivity_units_per_tick = 0.6f;
      float ortho_zoom_scale_per_tick = 0.12f;
      float ortho_min_half_height = 0.001f;
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
    float& ortho_half_height,
    float dt_seconds) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    const WheelZoomParams params {};
    const float max_radius = ComputeMaxRadius(camera_node, params);

    auto transform = camera_node.GetTransform();
    const float wheel_ticks =
      viewport::AccumulateAxis1DFromTransitionsOrZero(input_snapshot,
        "Editor.Camera.Zoom");

    if (std::abs(wheel_ticks) <= 0.0f) {
      return;
    }

    if (camera_node.GetCameraAs<scene::OrthographicCamera>()) {
      if (!std::isfinite(ortho_half_height) || ortho_half_height <= 0.0f) {
        ortho_half_height = 10.0f;
      }

      // Multiplicative zoom feels more stable for orthographic cameras.
      const float scale = std::exp(-wheel_ticks * params.ortho_zoom_scale_per_tick);
      const float new_half_height = ortho_half_height * scale;
      if (std::isfinite(new_half_height)) {
        ortho_half_height = std::max(params.ortho_min_half_height, new_half_height);
      }
      return;
    }

    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3 {});

    if (!viewport::IsFinite(position) || !viewport::IsFinite(focus_point)) {
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

    const glm::vec3 dir = viewport::NormalizeSafe(offset,
      glm::vec3(0.0f, 0.0f, 1.0f));
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
