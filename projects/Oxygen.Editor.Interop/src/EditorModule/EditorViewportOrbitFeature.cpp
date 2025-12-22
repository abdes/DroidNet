//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/EditorViewportOrbitFeature.h"

#include "EditorModule/EditorViewportInputHelpers.h"
#include "EditorModule/EditorViewportMathHelpers.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <unordered_map>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::interop::module {

  namespace {

    struct OrbitParams {
      float radians_per_pixel = 0.005f;
      float min_radius = 0.25f;
      float max_up_dot = 0.99f;
      glm::vec3 up = oxygen::space::move::Up;
    };

    struct OrbitState {
      bool was_active = false;
      float yaw_radians = 0.0f;
      float pitch_radians = 0.0f;
      float radius = 1.0f;
    };


    [[nodiscard]] auto ClampPitchRadians(const OrbitParams& params,
      float pitch_radians) noexcept -> float {
      const float max_pitch = std::asin(std::clamp(params.max_up_dot, 0.0f, 1.0f));
      return std::clamp(pitch_radians, -max_pitch, max_pitch);
    }

    [[nodiscard]] auto GetOrInitOrbitState(
      std::unordered_map<scene::NodeHandle, OrbitState>& states,
      const scene::SceneNode& camera_node) noexcept -> OrbitState& {
      return states[camera_node.GetHandle()];
    }

  } // namespace

  auto EditorViewportOrbitFeature::RegisterBindings(
    oxygen::engine::InputSystem& input_system,
    const std::shared_ptr<oxygen::input::InputMappingContext>& ctx) noexcept
    -> void {
    using oxygen::input::Action;
    using oxygen::input::ActionTriggerDown;
    using oxygen::input::ActionValueType;
    using oxygen::input::InputActionMapping;
    using platform::InputSlots;

    if (mouse_delta_action_) {
      return;
    }

    alt_action_ = std::make_shared<Action>(
      "Editor.Modifier.Alt", ActionValueType::kBool);
    lmb_action_ = std::make_shared<Action>(
      "Editor.Mouse.LeftButton", ActionValueType::kBool);
    mouse_delta_action_ = std::make_shared<Action>(
      "Editor.Mouse.Delta", ActionValueType::kAxis2D);

    input_system.AddAction(alt_action_);
    input_system.AddAction(lmb_action_);
    input_system.AddAction(mouse_delta_action_);

    const auto make_explicit_down = [] {
      auto trig = std::make_shared<ActionTriggerDown>();
      trig->MakeExplicit();
      return trig;
    };

    const auto make_mouse_move_down = [] {
      auto trig = std::make_shared<ActionTriggerDown>();
      trig->SetActuationThreshold(0.0F);
      trig->MakeExplicit();
      return trig;
    };

    const auto alt_left = std::make_shared<InputActionMapping>(
      alt_action_, InputSlots::LeftAlt);
    alt_left->AddTrigger(make_explicit_down());
    ctx->AddMapping(alt_left);

    const auto alt_right = std::make_shared<InputActionMapping>(
      alt_action_, InputSlots::RightAlt);
    alt_right->AddTrigger(make_explicit_down());
    ctx->AddMapping(alt_right);

    const auto lmb = std::make_shared<InputActionMapping>(
      lmb_action_, InputSlots::LeftMouseButton);
    lmb->AddTrigger(make_explicit_down());
    ctx->AddMapping(lmb);

    const auto mouse_xy = std::make_shared<InputActionMapping>(
      mouse_delta_action_, InputSlots::MouseXY);
    mouse_xy->AddTrigger(make_mouse_move_down());
    ctx->AddMapping(mouse_xy);
  }

  auto EditorViewportOrbitFeature::Apply(scene::SceneNode camera_node,
    const input::InputSnapshot& input_snapshot,
    glm::vec3& focus_point,
    float& /*ortho_half_height*/,
    float /*dt_seconds*/) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    if (camera_node.GetCameraAs<scene::OrthographicCamera>()) {
      return;
    }

    const OrbitParams params{};

    static std::unordered_map<scene::NodeHandle, OrbitState> orbit_states;
    auto& state = GetOrInitOrbitState(orbit_states, camera_node);

    const bool alt_held = input_snapshot.IsActionOngoing("Editor.Modifier.Alt");
    const bool lmb_held =
      input_snapshot.IsActionOngoing("Editor.Mouse.LeftButton");
    const bool active = alt_held && lmb_held;
    if (!active) {
      state.was_active = false;
      return;
    }

    const bool just_activated = !state.was_active;

    auto transform = camera_node.GetTransform();

    const glm::vec3 position =
      transform.GetLocalPosition().value_or(glm::vec3{});

    // Initialize orbit angles from current camera position when the drag starts.
    if (just_activated) {
      glm::vec3 offset = position - focus_point;
      float radius = std::sqrt(glm::dot(offset, offset));
      if (radius < params.min_radius) {
        offset = glm::vec3(0.0f, 0.0f, params.min_radius);
        radius = params.min_radius;
      }

      // yaw: atan2(x, z) for offset = yawRot * (0,0,r)
      state.yaw_radians = std::atan2(offset.x, offset.z);

      // pitch: defined such that positive pitch rotates camera downward.
      const float s = std::clamp(offset.y / radius, -1.0f, 1.0f);
      state.pitch_radians = ClampPitchRadians(params, -std::asin(s));

      state.radius = radius;
      state.was_active = true;

      // Consume the activation frame so a stale mouse delta doesn't cause a jump.
      return;
    }

    const auto mouse_delta =
      viewport::AccumulateAxis2DFromTransitionsOrZero(input_snapshot,
        "Editor.Mouse.Delta");

    if ((std::abs(mouse_delta.x) > 0.0f) || (std::abs(mouse_delta.y) > 0.0f)) {
      state.yaw_radians += -mouse_delta.x * params.radians_per_pixel;
      state.pitch_radians = ClampPitchRadians(params,
        state.pitch_radians + (-mouse_delta.y * params.radians_per_pixel));
    }

    const glm::quat yaw_q = glm::angleAxis(state.yaw_radians, params.up);
    const glm::vec3 right = yaw_q * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::quat pitch_q = glm::angleAxis(state.pitch_radians, right);

    const glm::vec3 base_offset(0.0f, 0.0f, std::max(state.radius, params.min_radius));
    const glm::vec3 offset = pitch_q * (yaw_q * base_offset);

    const glm::vec3 new_position = focus_point + offset;
    (void)transform.SetLocalPosition(new_position);

    const glm::quat look_rotation =
      viewport::LookRotationFromPositionToTarget(new_position, focus_point,
        params.up);
    (void)transform.SetLocalRotation(look_rotation);
  }

} // namespace oxygen::interop::module
