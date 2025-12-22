//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportFlyFeature.h"

#include <algorithm>
#include <unordered_map>

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::interop::module {

  namespace {

    struct FlyParams {
      float look_radians_per_pixel = 0.0035f;
      float base_speed_units_per_second = 5.0f;
      float fast_multiplier = 4.0f;
      float max_up_dot = 0.99f;
      glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    };

    struct FlyState {
      bool was_active = false;
      float yaw_radians = 0.0f;
      float pitch_radians = 0.0f;
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

    [[nodiscard]] auto ClampPitchRadians(const FlyParams& params,
      float pitch_radians) noexcept -> float {
      const float max_pitch = std::asin(std::clamp(params.max_up_dot, 0.0f, 1.0f));
      return std::clamp(pitch_radians, -max_pitch, max_pitch);
    }

    [[nodiscard]] auto LookRotationFromForwardUp(const glm::vec3 forward,
      const glm::vec3 up_direction) noexcept -> glm::quat {
      const glm::vec3 f = NormalizeSafe(forward, glm::vec3(0.0f, 0.0f, -1.0f));
      const glm::vec3 r =
        NormalizeSafe(glm::cross(f, up_direction), glm::vec3(1.0f, 0.0f, 0.0f));
      const glm::vec3 u = glm::cross(r, f);

      glm::mat4 look_matrix(1.0f);
      look_matrix[0] = glm::vec4(r, 0.0f);
      look_matrix[1] = glm::vec4(u, 0.0f);
      look_matrix[2] = glm::vec4(-f, 0.0f);
      return glm::quat_cast(look_matrix);
    }

    [[nodiscard]] auto GetOrInitFlyState(
      std::unordered_map<scene::NodeHandle, FlyState>& states,
      const scene::SceneNode& camera_node) noexcept -> FlyState& {
      return states[camera_node.GetHandle()];
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

    const auto add_key = [&](const std::shared_ptr<Action>& act, const auto& slot) {
      auto m = std::make_shared<InputActionMapping>(act, slot);
      m->AddTrigger(make_down());
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
    glm::vec3& /*focus_point*/,
    float dt_seconds) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    if (dt_seconds <= 0.0f) {
      return;
    }

    const FlyParams params{};

    // Fly is RMB (without Alt). Alt+RMB is reserved for dolly.
    const bool rmb_held = input_snapshot.IsActionOngoing("Editor.Mouse.RightButton");
    const bool alt_held = input_snapshot.IsActionOngoing("Editor.Modifier.Alt");
    const bool active = rmb_held && !alt_held;

    static std::unordered_map<scene::NodeHandle, FlyState> fly_states;
    auto& state = GetOrInitFlyState(fly_states, camera_node);

    if (!active) {
      state.was_active = false;
      return;
    }

    auto transform = camera_node.GetTransform();
    const glm::quat current_rot =
      transform.GetLocalRotation().value_or(glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f });

    const glm::vec3 current_forward =
      NormalizeSafe(current_rot * glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f));

    if (!state.was_active) {
      // Derive yaw/pitch from the current forward vector.
      // For base forward (0,0,-1), yaw=0. Match the orbit convention.
      state.yaw_radians = std::atan2(-current_forward.x, -current_forward.z);
      state.pitch_radians = ClampPitchRadians(params, std::asin(current_forward.y));
      state.was_active = true;
    }

    const auto mouse_delta =
      AccumulateAxis2DFromTransitionsOrZero(input_snapshot, "Editor.Mouse.Delta");
    if ((std::abs(mouse_delta.x) > 0.0f) || (std::abs(mouse_delta.y) > 0.0f)) {
      state.yaw_radians += -mouse_delta.x * params.look_radians_per_pixel;
      state.pitch_radians = ClampPitchRadians(params,
        state.pitch_radians + (-mouse_delta.y * params.look_radians_per_pixel));
    }

    const glm::quat yaw_q = glm::angleAxis(state.yaw_radians, params.up);
    const glm::vec3 right = yaw_q * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::quat pitch_q = glm::angleAxis(state.pitch_radians, right);

    const glm::vec3 forward = NormalizeSafe(pitch_q * (yaw_q * glm::vec3(0.0f, 0.0f, -1.0f)),
      glm::vec3(0.0f, 0.0f, -1.0f));

    const glm::quat new_rot = LookRotationFromForwardUp(forward, params.up);
    (void)transform.SetLocalRotation(new_rot);

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

    const glm::vec3 fly_right = new_rot * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 fly_forward = new_rot * glm::vec3(0.0f, 0.0f, -1.0f);

    const glm::vec3 delta =
      (fly_forward * forward_axis + fly_right * right_axis + params.up * up_axis)
      * (speed * dt_seconds);

    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3{});
    (void)transform.SetLocalPosition(position + delta);
  }

} // namespace oxygen::interop::module
