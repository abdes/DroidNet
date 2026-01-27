//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "TexturedCube/CameraController.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Scene/Camera/Perspective.h>

namespace {

auto MakeLookRotationFromPosition(const glm::vec3& position,
  const glm::vec3& target, const glm::vec3& up_direction = { 0.0F, 0.0F, 1.0F })
  -> glm::quat
{
  const auto forward_raw = target - position;
  const float forward_len2 = glm::dot(forward_raw, forward_raw);
  if (forward_len2 <= 1e-8F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  const auto forward = glm::normalize(forward_raw);
  const auto right = glm::normalize(glm::cross(forward, up_direction));
  const auto up = glm::cross(right, forward);

  glm::mat4 look_matrix(1.0F);
  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  look_matrix[0] = glm::vec4(right, 0.0F);
  look_matrix[1] = glm::vec4(up, 0.0F);
  look_matrix[2] = glm::vec4(-forward, 0.0F);
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)

  return glm::quat_cast(look_matrix);
}

} // namespace

namespace oxygen::examples::textured_cube {

CameraController::CameraController(
  oxygen::observer_ptr<oxygen::engine::InputSystem> input_system,
  const Config& config)
  : input_system_(input_system)
  , config_(config)
  , orbit_yaw_rad_(config.initial_yaw_rad)
  , orbit_pitch_rad_(config.initial_pitch_rad)
  , orbit_distance_(config.initial_distance)
{
}

auto CameraController::InitInputBindings() noexcept -> bool
{
  using oxygen::input::Action;
  using oxygen::input::ActionTriggerChain;
  using oxygen::input::ActionTriggerDown;
  using oxygen::input::ActionTriggerTap;
  using oxygen::input::ActionValueType;
  using oxygen::input::InputActionMapping;
  using oxygen::input::InputMappingContext;
  using oxygen::platform::InputSlots;

  if (!input_system_) {
    LOG_F(ERROR, "InputSystem not available; skipping input bindings");
    return false;
  }

  zoom_in_action_ = std::make_shared<Action>("zoom in", ActionValueType::kBool);
  zoom_out_action_
    = std::make_shared<Action>("zoom out", ActionValueType::kBool);
  rmb_action_ = std::make_shared<Action>("rmb", ActionValueType::kBool);
  orbit_action_
    = std::make_shared<Action>("camera orbit", ActionValueType::kAxis2D);

  input_system_->AddAction(zoom_in_action_);
  input_system_->AddAction(zoom_out_action_);
  input_system_->AddAction(rmb_action_);
  input_system_->AddAction(orbit_action_);

  camera_controls_ctx_ = std::make_shared<InputMappingContext>("camera");
  {
    // Zoom in: Mouse wheel up
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_in_action_, InputSlots::MouseWheelUp);
      mapping->AddTrigger(trigger);
      camera_controls_ctx_->AddMapping(mapping);
    }

    // Zoom out: Mouse wheel down
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_out_action_, InputSlots::MouseWheelDown);
      mapping->AddTrigger(trigger);
      camera_controls_ctx_->AddMapping(mapping);
    }

    // RMB helper mapping
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      camera_controls_ctx_->AddMapping(mapping);
    }

    // Orbit mapping: MouseXY with an implicit chain requiring RMB.
    {
      const auto trig_move = std::make_shared<ActionTriggerDown>();
      trig_move->MakeExplicit();
      trig_move->SetActuationThreshold(0.0F);

      const auto rmb_chain = std::make_shared<ActionTriggerChain>();
      rmb_chain->SetLinkedAction(rmb_action_);
      rmb_chain->MakeImplicit();
      rmb_chain->RequirePrerequisiteHeld(true);

      const auto mapping = std::make_shared<InputActionMapping>(
        orbit_action_, InputSlots::MouseXY);
      mapping->AddTrigger(trig_move);
      mapping->AddTrigger(rmb_chain);
      camera_controls_ctx_->AddMapping(mapping);
    }

    input_system_->AddMappingContext(camera_controls_ctx_, 10);
    input_system_->ActivateMappingContext(camera_controls_ctx_);
  }

  return true;
}

auto CameraController::EnsureCamera(
  std::shared_ptr<scene::Scene>& scene, int width, int height) -> void
{
  using scene::PerspectiveCamera;

  if (!scene) {
    return;
  }

  if (!camera_node_.IsAlive()) {
    camera_node_ = scene->CreateNode("MainCamera");
  }

  if (!camera_node_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
    const bool attached = camera_node_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  }

  const auto cam_ref = camera_node_.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = height > 0
      ? (static_cast<float>(width) / static_cast<float>(height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetFieldOfView(glm::radians(config_.fov_degrees));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(config_.near_plane);
    cam.SetFarPlane(config_.far_plane);
    cam.SetViewport(oxygen::ViewPort { .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F });
  }

  ApplyOrbitAndZoom();
}

auto CameraController::Update() -> void { ApplyOrbitAndZoom(); }

auto CameraController::ApplyOrbitAndZoom() -> void
{
  if (!camera_node_.IsAlive()) {
    return;
  }

  // Zoom via mouse wheel actions
  if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
    orbit_distance_
      = (std::max)(orbit_distance_ - config_.zoom_step, config_.min_distance);
  }
  if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
    orbit_distance_
      = (std::min)(orbit_distance_ + config_.zoom_step, config_.max_distance);
  }

  // Orbit via MouseXY deltas for this frame
  if (orbit_action_
    && orbit_action_->GetValueType()
      == oxygen::input::ActionValueType::kAxis2D) {
    glm::vec2 orbit_delta(0.0f);
    for (const auto& tr : orbit_action_->GetFrameTransitions()) {
      const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
      orbit_delta.x += v.x;
      orbit_delta.y += v.y;
    }

    if (std::abs(orbit_delta.x) > 0.0f || std::abs(orbit_delta.y) > 0.0f) {
      orbit_yaw_rad_ += orbit_delta.x * config_.orbit_sensitivity;
      orbit_pitch_rad_ += orbit_delta.y * config_.orbit_sensitivity * -1.0f;

      const float kMinPitch = -glm::half_pi<float>() + 0.05f;
      const float kMaxPitch = glm::half_pi<float>() - 0.05f;
      orbit_pitch_rad_ = std::clamp(orbit_pitch_rad_, kMinPitch, kMaxPitch);
    }
  }

  const float cp = std::cos(orbit_pitch_rad_);
  const float sp = std::sin(orbit_pitch_rad_);
  const float cy = std::cos(orbit_yaw_rad_);
  const float sy = std::sin(orbit_yaw_rad_);

  const glm::vec3 offset = orbit_distance_ * glm::vec3(cp * cy, cp * sy, sp);
  const glm::vec3 cam_pos = config_.target + offset;

  auto tf = camera_node_.GetTransform();
  tf.SetLocalPosition(cam_pos);
  tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, config_.target));
}

} // namespace oxygen::examples::textured_cube
