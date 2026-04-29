//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportFlyFeature.h"

#include "EditorModule/EditorViewportInputHelpers.h"
#include "EditorModule/EditorViewportMathHelpers.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::interop::module {

  namespace {

    struct FlyParams {
      float look_radians_per_pixel = 0.0025f;
      float base_speed_units_per_second = 5.0f;
      float fast_multiplier = 4.0f;
      glm::vec3 up = oxygen::space::move::Up;
    };

    struct FlyState {
      bool initialized = false;
      bool was_look_active = false;
      float yaw_radians = 0.0f;
      float pitch_radians = 0.0f;
    };

    [[nodiscard]] auto ClampPitchRadians(const FlyParams& params,
      float pitch_radians) noexcept -> float {
      (void)params;
      constexpr float kMaxPitch =
        (std::numbers::pi_v<float> * 0.5f) - 0.001f;
      return std::clamp(pitch_radians, -kMaxPitch, kMaxPitch);
    }

    [[nodiscard]] auto BuildOrientation(
      float yaw_radians,
      float pitch_radians,
      const glm::vec3& world_up) noexcept -> glm::quat {
      const float cos_pitch = std::cos(pitch_radians);
      const float sin_pitch = std::sin(pitch_radians);
      const float cos_yaw = std::cos(yaw_radians);
      const float sin_yaw = std::sin(yaw_radians);

      const glm::vec3 forward_ws(
        sin_yaw * cos_pitch,
        -cos_yaw * cos_pitch,
        sin_pitch);

      glm::vec3 right_ws = glm::cross(forward_ws, world_up);
      const float right_len2 = glm::dot(right_ws, right_ws);
      if (right_len2 <= 1e-8f) {
        right_ws = glm::vec3(std::cos(yaw_radians), std::sin(yaw_radians), 0.0f);
      } else {
        right_ws /= std::sqrt(right_len2);
      }

      const glm::vec3 up_ws = glm::normalize(glm::cross(right_ws, forward_ws));
      glm::mat4 view_basis(1.0f);
      view_basis[0] = glm::vec4(right_ws, 0.0f);
      view_basis[1] = glm::vec4(up_ws, 0.0f);
      view_basis[2] = glm::vec4(-glm::normalize(forward_ws), 0.0f);
      return glm::normalize(glm::quat_cast(view_basis));
    }

    [[nodiscard]] auto GetOrInitFlyState(
      std::unordered_map<scene::NodeHandle, FlyState>& states,
      const scene::SceneNode& camera_node) noexcept -> FlyState& {
      return states[camera_node.GetHandle()];
    }

    auto SyncStateFromTransform(FlyState& state,
      const auto& transform,
      const FlyParams& params) noexcept -> void {
      const glm::quat current_rot =
        transform.GetLocalRotation().value_or(
          glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f });
      const glm::vec3 current_forward =
        viewport::NormalizeSafe(current_rot * oxygen::space::look::Forward,
          oxygen::space::look::Forward);
      state.pitch_radians = ClampPitchRadians(params,
        std::asin(std::clamp(current_forward.z, -1.0f, 1.0f)));
      state.yaw_radians = std::atan2(current_forward.x, -current_forward.y);
      state.initialized = true;
    }

    [[nodiscard]] auto BoolToAxis(const bool positive, const bool negative) noexcept
      -> float {
      return (positive ? 1.0f : 0.0f) + (negative ? -1.0f : 0.0f);
    }

  } // namespace

  auto EditorViewportFlyFeature::RegisterBindings(
    oxygen::engine::InputSystem& input_system,
    const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
    -> void {
    using oxygen::input::Action;
    using oxygen::input::ActionTriggerDown;
    using oxygen::input::ActionTriggerPulse;
    using oxygen::input::ActionValueType;
    using oxygen::input::InputActionMapping;
    using platform::InputSlots;

    if (w_action_) {
      return;
    }

    w_action_ = std::make_shared<Action>("Editor.Fly.W", ActionValueType::kBool);
    a_action_ = std::make_shared<Action>("Editor.Fly.A", ActionValueType::kBool);
    s_action_ = std::make_shared<Action>("Editor.Fly.S", ActionValueType::kBool);
    d_action_ = std::make_shared<Action>("Editor.Fly.D", ActionValueType::kBool);
    q_action_ = std::make_shared<Action>("Editor.Fly.Q", ActionValueType::kBool);
    e_action_ = std::make_shared<Action>("Editor.Fly.E", ActionValueType::kBool);
    shift_action_ =
      std::make_shared<Action>("Editor.Fly.Shift", ActionValueType::kBool);

    input_system.AddAction(w_action_);
    input_system.AddAction(a_action_);
    input_system.AddAction(s_action_);
    input_system.AddAction(d_action_);
    input_system.AddAction(q_action_);
    input_system.AddAction(e_action_);
    input_system.AddAction(shift_action_);

    const auto make_down = [] {
      auto t = std::make_shared<ActionTriggerDown>();
      t->MakeExplicit();
      return t;
    };

    const auto make_pulse = [] {
      auto t = std::make_shared<ActionTriggerPulse>();
      t->MakeExplicit();
      t->SetActuationThreshold(0.1F);
      return t;
    };

    const auto add_key = [&](const std::shared_ptr<Action>& act, const auto& slot) {
      auto m = std::make_shared<InputActionMapping>(act, slot);
      m->AddTrigger(make_pulse());
      ctx->AddMapping(m);
    };

    add_key(w_action_, InputSlots::W);
    add_key(a_action_, InputSlots::A);
    add_key(s_action_, InputSlots::S);
    add_key(d_action_, InputSlots::D);
    add_key(q_action_, InputSlots::Q);
    add_key(e_action_, InputSlots::E);

    add_key(shift_action_, InputSlots::LeftShift);
    add_key(shift_action_, InputSlots::RightShift);
  }

  auto EditorViewportFlyFeature::Apply(scene::SceneNode camera_node,
    const input::InputSnapshot& input_snapshot,
    EditorViewportCameraControlMode control_mode,
    glm::vec3& /*focus_point*/,
    float& /*ortho_half_height*/,
    float dt_seconds) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    if (camera_node.GetCameraAs<scene::OrthographicCamera>()) {
      return;
    }

    if (control_mode != EditorViewportCameraControlMode::kFly) {
      return;
    }

    if (dt_seconds <= 0.0f) {
      return;
    }

    const FlyParams params{};

    // Fly is RMB (without Alt). Alt+RMB is reserved for dolly.
    const bool rmb_held = input_snapshot.IsActionOngoing("Editor.Mouse.RightButton");
    const bool alt_held = input_snapshot.IsActionOngoing("Editor.Modifier.Alt");
    const bool look_active = rmb_held && !alt_held;

    static std::unordered_map<scene::NodeHandle, FlyState> fly_states;
    auto& state = GetOrInitFlyState(fly_states, camera_node);

    auto transform = camera_node.GetTransform();
    if (!state.initialized || (look_active && !state.was_look_active)) {
      SyncStateFromTransform(state, transform, params);
    }

    if (!look_active) {
      state.was_look_active = false;
    }

    const auto mouse_delta =
      viewport::AccumulateAxis2DFromTransitionsOrZero(input_snapshot,
        "Editor.Mouse.Delta");
    const bool mouse_moved = (std::abs(mouse_delta.x) > 0.0f)
      || (std::abs(mouse_delta.y) > 0.0f);

    if (look_active && mouse_moved) {
      state.yaw_radians += -mouse_delta.x * params.look_radians_per_pixel;
      state.pitch_radians = ClampPitchRadians(params,
        state.pitch_radians + (-mouse_delta.y * params.look_radians_per_pixel));
    }
    state.was_look_active = look_active;

    const glm::quat applied_rot =
      BuildOrientation(state.yaw_radians, state.pitch_radians, params.up);
    (void)transform.SetLocalRotation(applied_rot);

    const bool w = input_snapshot.IsActionOngoing("Editor.Fly.W");
    const bool a = input_snapshot.IsActionOngoing("Editor.Fly.A");
    const bool s = input_snapshot.IsActionOngoing("Editor.Fly.S");
    const bool d = input_snapshot.IsActionOngoing("Editor.Fly.D");
    const bool q = input_snapshot.IsActionOngoing("Editor.Fly.Q");
    const bool e = input_snapshot.IsActionOngoing("Editor.Fly.E");

    const float forward_axis = BoolToAxis(w, s);
    const float right_axis = BoolToAxis(d, a);
    const float up_axis = BoolToAxis(e, q);

    if ((std::abs(forward_axis) <= 0.0f) && (std::abs(right_axis) <= 0.0f)
      && (std::abs(up_axis) <= 0.0f)) {
      return;
    }

    const bool fast = input_snapshot.IsActionOngoing("Editor.Fly.Shift");
    const float speed = params.base_speed_units_per_second
      * (fast ? params.fast_multiplier : 1.0f);

    const glm::vec3 fly_right = applied_rot * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 fly_forward = applied_rot * glm::vec3(0.0f, 0.0f, -1.0f);

    const glm::vec3 delta =
      (fly_forward * forward_axis + fly_right * right_axis + params.up * up_axis)
      * (speed * dt_seconds);

    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3{});
    (void)transform.SetLocalPosition(position + delta);
  }

} // namespace oxygen::interop::module
