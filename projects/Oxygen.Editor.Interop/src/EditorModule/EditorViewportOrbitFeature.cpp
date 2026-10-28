//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include "EditorModule/EditorViewportOrbitFeature.h"

#include "EditorModule/EditorViewportInputHelpers.h"
#include "EditorModule/EditorViewportMathHelpers.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::interop::module {

  namespace {

    struct OrbitParams {
      float radians_per_pixel = 0.005f;
      float min_radius = 0.25f;
      glm::vec3 up = oxygen::space::move::Up;
    };

    struct OrbitState {
      bool was_active = false;
      EditorViewportCameraControlMode mode =
        EditorViewportCameraControlMode::kOrbitTurntable;
      float yaw_radians = 0.0f;
      float pitch_radians = 0.0f;
      float radius = 1.0f;
      glm::quat orbit_rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    };


    [[nodiscard]] auto ClampPitchRadians(const OrbitParams& params,
      float pitch_radians) noexcept -> float {
      (void)params;
      constexpr float kTurntablePitchLimitEpsilon = 0.001f;
      constexpr float max_pitch =
        (std::numbers::pi_v<float> * 0.5f) - kTurntablePitchLimitEpsilon;
      return std::clamp(pitch_radians, -max_pitch, max_pitch);
    }

    [[nodiscard]] auto BuildLookRotation(const glm::vec3& forward_ws,
      const float yaw_radians,
      const glm::vec3& world_up) noexcept -> glm::quat {
      const glm::vec3 forward = viewport::NormalizeSafe(
        forward_ws, oxygen::space::node::Forward);

      glm::vec3 right = glm::cross(forward, world_up);
      const float right_len2 = glm::dot(right, right);
      if (right_len2 <= 1e-8f) {
        // At the poles, world-up is colinear with the forward vector. Use yaw
        // to preserve a stable horizontal screen axis instead of snapping.
        right = viewport::NormalizeSafe(
          glm::vec3(-std::cos(yaw_radians), -std::sin(yaw_radians), 0.0f),
          oxygen::space::node::Right);
      } else {
        right /= std::sqrt(right_len2);
      }

      return oxygen::transforms::LookRotation(
        { .forward = forward, .up = glm::cross(right, forward) });
    }

    [[nodiscard]] auto GetOrInitOrbitState(
      std::unordered_map<scene::NodeHandle, OrbitState>& states,
      const scene::SceneNode& camera_node) noexcept -> OrbitState& {
      return states[camera_node.GetHandle()];
    }

  } // namespace

  auto EditorViewportOrbitFeature::RegisterBindings(
    oxygen::engine::InputSystem& /*input_system*/,
    const std::shared_ptr<oxygen::input::InputMappingContext>& /*ctx*/,
    const EditorViewportInputActions& inputs) noexcept -> void {
    inputs_ = inputs;
  }

  auto EditorViewportOrbitFeature::Apply(scene::SceneNode camera_node,
    const input::InputSnapshot& input_snapshot,
    EditorViewportCameraControlMode control_mode,
    glm::vec3& focus_point,
    float& /*ortho_half_height*/,
    float /*movement_speed_units_per_second*/,
    float /*dt_seconds*/) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    if (camera_node.GetCameraAs<scene::OrthographicCamera>()) {
      return;
    }

    const bool trackball =
      control_mode == EditorViewportCameraControlMode::kOrbitTrackball;
    const bool turntable =
      control_mode == EditorViewportCameraControlMode::kOrbitTurntable;
    if (!trackball && !turntable) {
      return;
    }

    const OrbitParams params{};

    static std::unordered_map<scene::NodeHandle, OrbitState> orbit_states;
    auto& state = GetOrInitOrbitState(orbit_states, camera_node);

    const bool alt_held
      = input_snapshot.IsActionOngoing(inputs_.alt->GetAssetKey());
    const bool lmb_held
      = input_snapshot.IsActionOngoing(inputs_.left_button->GetAssetKey());
    const bool active = alt_held && lmb_held;
    if (!active) {
      state.was_active = false;
      return;
    }

    const bool mode_changed = state.mode != control_mode;
    const bool just_activated = !state.was_active || mode_changed;

    auto transform = camera_node.GetTransform();

    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3{});

    // Initialize orbit angles from current camera position when the drag starts.
    if (just_activated) {
      glm::vec3 offset = position - focus_point;
      float radius = std::sqrt(glm::dot(offset, offset));
      if (radius < params.min_radius) {
        offset = glm::vec3(0.0f, params.min_radius, 0.0f);
        radius = params.min_radius;
      }

      state.radius = radius;
      state.mode = control_mode;
      state.orbit_rotation =
        transform.GetLocalRotation().value_or(glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f });
      state.was_active = true;

      const glm::vec3 forward = viewport::NormalizeSafe(
        focus_point - position, oxygen::space::node::Forward);
      state.yaw_radians = std::atan2(forward.x, -forward.y);
      state.pitch_radians = ClampPitchRadians(params,
        std::asin(std::clamp(forward.z, -1.0f, 1.0f)));

      // Consume the activation frame so a stale mouse delta doesn't cause a jump.
      return;
    }

    const auto mouse_delta =
      viewport::GetAxis2DOrZero(input_snapshot,
        inputs_.mouse_delta->GetAssetKey());

    if ((std::abs(mouse_delta.x) > 0.0f) || (std::abs(mouse_delta.y) > 0.0f)) {
      if (trackball) {
        const float pitch_delta = -mouse_delta.y * params.radians_per_pixel;
        const float yaw_delta = -mouse_delta.x * params.radians_per_pixel;
        const glm::vec3 view_x = viewport::NormalizeSafe(
          state.orbit_rotation * oxygen::space::node::Right,
          oxygen::space::node::Right);
        const glm::vec3 view_y = viewport::NormalizeSafe(
          state.orbit_rotation * oxygen::space::node::Up,
          oxygen::space::node::Up);
        const glm::vec3 axis_angle =
          (view_x * pitch_delta) + (view_y * yaw_delta);
        const float angle = glm::length(axis_angle);
        if (angle > std::numeric_limits<float>::epsilon()) {
          const glm::quat delta_rotation =
            glm::angleAxis(angle, axis_angle / angle);
          state.orbit_rotation =
            glm::normalize(delta_rotation * state.orbit_rotation);
        }
      } else {
        state.yaw_radians += -mouse_delta.x * params.radians_per_pixel;
        state.pitch_radians = ClampPitchRadians(params,
          state.pitch_radians + (-mouse_delta.y * params.radians_per_pixel));
      }
    }

    if (trackball) {
      const glm::vec3 forward =
        state.orbit_rotation * oxygen::space::node::Forward;
      const glm::vec3 new_position =
        focus_point - (forward * std::max(state.radius, params.min_radius));
      (void)transform.SetLocalPosition(new_position);
      (void)transform.SetLocalRotation(state.orbit_rotation);
      return;
    }

    const float cos_pitch = std::cos(state.pitch_radians);
    const float sin_pitch = std::sin(state.pitch_radians);
    const float cos_yaw = std::cos(state.yaw_radians);
    const float sin_yaw = std::sin(state.yaw_radians);
    const glm::vec3 forward_ws(
      sin_yaw * cos_pitch,
      -cos_yaw * cos_pitch,
      sin_pitch);
    const glm::vec3 new_position =
      focus_point - (forward_ws * std::max(state.radius, params.min_radius));

    state.orbit_rotation =
      BuildLookRotation(forward_ws, state.yaw_radians, params.up);
    (void)transform.SetLocalPosition(new_position);
    (void)transform.SetLocalRotation(state.orbit_rotation);
  }

} // namespace oxygen::interop::module
