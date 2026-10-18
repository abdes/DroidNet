//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string_view>

#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/DroneCameraController.h"
#include "DemoShell/UI/FlyCameraController.h"
#include "DemoShell/UI/OrbitCameraController.h"
#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/CodeInput.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Scene/SceneNode.h>

namespace {

//! The owner of the inputs created here in code (see
//! oxygen::input::MakeCodeAction).
constexpr std::string_view kInputOwner = "Examples/DemoShell/CameraRig";

} // namespace

namespace oxygen::examples::ui {

CameraRigController::CameraRigController() = default;
CameraRigController::~CameraRigController() = default;

/*!
 Initializes camera input bindings and mapping contexts for orbit and fly
 controls.

 @param input_system The input system to register actions and contexts with.
 @return True if initialization succeeded; false if no input system is
   present.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: Allocates input actions and contexts once per controller instance.
 - Optimization: Reuses existing actions when already initialized.

 @note Calling this method multiple times is safe; subsequent calls are
   no-ops.
*/
auto CameraRigController::Initialize(
  observer_ptr<engine::InputSystem> input_system) -> bool
{
  if (!input_system) {
    return false;
  }

  if (input_system_ == input_system && orbit_controls_ctx_
    && fly_controls_ctx_) {
    return true;
  }

  input_system_ = input_system;

  using input::Action;
  using input::ActionTriggerChain;
  using input::ActionTriggerDown;
  using input::ActionTriggerPulse;
  using input::ActionTriggerTap;
  using input::ActionValueType;
  using input::InputActionMapping;
  using input::InputMappingContext;
  using platform::InputSlots;

  LOG_F(INFO, "CameraRigController: Creating camera input actions");

  zoom_in_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "zoom in", ActionValueType::kBool);
  zoom_out_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "zoom out", ActionValueType::kBool);
  rmb_action_
    = oxygen::input::MakeCodeAction(kInputOwner, "rmb", ActionValueType::kBool);
  orbit_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "camera orbit", ActionValueType::kAxis2D);
  move_fwd_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "move fwd", ActionValueType::kBool);
  move_bwd_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "move bwd", ActionValueType::kBool);
  move_left_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "move left", ActionValueType::kBool);
  move_right_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "move right", ActionValueType::kBool);
  move_up_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "move up", ActionValueType::kBool);
  move_down_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "move down", ActionValueType::kBool);
  fly_plane_lock_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "fly plane lock", ActionValueType::kBool);
  fly_boost_action_ = oxygen::input::MakeCodeAction(
    kInputOwner, "fly boost", ActionValueType::kBool);

  input_system_->AddAction(zoom_in_action_);
  input_system_->AddAction(zoom_out_action_);
  input_system_->AddAction(rmb_action_);
  input_system_->AddAction(orbit_action_);
  input_system_->AddAction(move_fwd_action_);
  input_system_->AddAction(move_bwd_action_);
  input_system_->AddAction(move_left_action_);
  input_system_->AddAction(move_right_action_);
  input_system_->AddAction(move_up_action_);
  input_system_->AddAction(move_down_action_);
  input_system_->AddAction(fly_plane_lock_action_);
  input_system_->AddAction(fly_boost_action_);

  orbit_controls_ctx_
    = oxygen::input::MakeCodeContext(kInputOwner, "camera orbit");
  {
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_in_action_, InputSlots::MouseWheelUp);
      mapping->AddTrigger(trigger);
      orbit_controls_ctx_->AddMapping(mapping);
    }

    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_out_action_, InputSlots::MouseWheelDown);
      mapping->AddTrigger(trigger);
      orbit_controls_ctx_->AddMapping(mapping);
    }

    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      orbit_controls_ctx_->AddMapping(mapping);
    }

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
      orbit_controls_ctx_->AddMapping(mapping);
    }
  }

  fly_controls_ctx_ = oxygen::input::MakeCodeContext(kInputOwner, "camera fly");
  {
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      fly_controls_ctx_->AddMapping(mapping);
    }

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
      fly_controls_ctx_->AddMapping(mapping);
    }

    auto add_bool_mapping = [&](const std::shared_ptr<Action>& action,
                              const auto& slot) {
      const auto mapping = std::make_shared<InputActionMapping>(action, slot);
      const auto trigger = std::make_shared<ActionTriggerPulse>();
      trigger->MakeExplicit();
      trigger->SetActuationThreshold(0.1F);
      mapping->AddTrigger(trigger);
      fly_controls_ctx_->AddMapping(mapping);
    };

    add_bool_mapping(move_fwd_action_, InputSlots::W);
    add_bool_mapping(move_bwd_action_, InputSlots::S);
    add_bool_mapping(move_left_action_, InputSlots::A);
    add_bool_mapping(move_right_action_, InputSlots::D);
    add_bool_mapping(move_up_action_, InputSlots::E);
    add_bool_mapping(move_down_action_, InputSlots::Q);
    add_bool_mapping(fly_plane_lock_action_, InputSlots::Space);
  }

  // Modifier keys context at higher priority (evaluated first), following
  // the InputSystem demo pattern for shift detection.
  fly_modifier_keys_ctx_
    = oxygen::input::MakeCodeContext(kInputOwner, "camera fly modifiers");
  {
    const auto trigger = std::make_shared<ActionTriggerDown>();
    trigger->MakeExplicit();
    const auto mapping = std::make_shared<InputActionMapping>(
      fly_boost_action_, InputSlots::LeftShift);
    mapping->AddTrigger(trigger);
    fly_modifier_keys_ctx_->AddMapping(mapping);
  }

  input_system_->AddMappingContext(orbit_controls_ctx_, 10);
  input_system_->AddMappingContext(fly_controls_ctx_, 10);
  input_system_->AddMappingContext(fly_modifier_keys_ctx_, 1000);
  UpdateActiveCameraInputContext();
  return true;
}

/*!
 Assigns the active camera node and synchronizes controller state from its
 transform.

 @param camera The active camera node handle to manage.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: No additional allocations.
 - Optimization: Reuses existing controller instances.
*/
auto CameraRigController::SetActiveCamera(observer_ptr<scene::SceneNode> camera)
  -> void
{
  active_camera_ = camera;
  SyncFromActiveCamera();
}

auto CameraRigController::GetActiveCamera() const noexcept
  -> observer_ptr<scene::SceneNode>
{
  return active_camera_;
}

/*!
 Updates the current control mode and activates the matching input context.

 @param mode The desired camera control mode.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Avoids redundant input context changes.
*/
auto CameraRigController::SetMode(CameraControlMode mode) -> void
{
  if (current_mode_ == mode) {
    return;
  }

  current_mode_ = mode;
  UpdateActiveCameraInputContext();
}

/*!
 Returns the current camera control mode.

 @return The active camera control mode.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetMode() const noexcept -> CameraControlMode
{
  return current_mode_;
}

/*!
 Applies per-frame input to the active camera using the active controller.

 @param delta_time The frame delta time.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Skips updates if no active camera or controller exists.
*/
auto CameraRigController::Update(time::CanonicalDuration delta_time) -> void
{
  if (!active_camera_ || !active_camera_->IsAlive()) {
    return;
  }

  auto& camera = *active_camera_;

  if (current_mode_ == CameraControlMode::kDrone) {
    if (drone_controller_ && drone_controller_->HasPath()) {
      drone_controller_->Update(camera, delta_time);
    }
    return;
  }

  if (current_mode_ == CameraControlMode::kOrbit) {
    if (!orbit_controller_) {
      return;
    }

    if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
      orbit_controller_->AddZoomInput(1.0F);
    }
    if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
      orbit_controller_->AddZoomInput(-1.0F);
    }

    if (orbit_action_
      && orbit_action_->GetValueType() == input::ActionValueType::kAxis2D) {
      // A relative action's value is the whole-frame mouse delta.
      const auto& v = orbit_action_->GetValue().GetAs<Axis2D>();
      const glm::vec2 orbit_delta(v.x, v.y);

      if (std::abs(orbit_delta.x) > 0.0F || std::abs(orbit_delta.y) > 0.0F) {
        orbit_controller_->AddOrbitInput(orbit_delta);
      }
    }

    orbit_controller_->Update(camera, delta_time);
    return;
  }

  if (current_mode_ != CameraControlMode::kFly) {
    return;
  }

  if (!fly_controller_) {
    return;
  }

  if (fly_boost_action_) {
    fly_controller_->SetBoostActive(fly_boost_action_->IsOngoing());
  }
  if (fly_plane_lock_action_) {
    fly_controller_->SetPlaneLockActive(fly_plane_lock_action_->IsOngoing());
  }

  if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
    const float speed = fly_controller_->GetMoveSpeed();
    fly_controller_->SetMoveSpeed(std::min(speed * 1.2F, 1000.0F));
  }
  if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
    const float speed = fly_controller_->GetMoveSpeed();
    fly_controller_->SetMoveSpeed(std::max(speed / 1.2F, 0.1F));
  }

  if (orbit_action_
    && orbit_action_->GetValueType() == input::ActionValueType::kAxis2D) {
    const auto& v = orbit_action_->GetValue().GetAs<Axis2D>();
    const glm::vec2 look_delta(v.x, v.y);

    if (std::abs(look_delta.x) > 0.0F || std::abs(look_delta.y) > 0.0F) {
      fly_controller_->AddRotationInput(look_delta);
    }
  }

  glm::vec3 move_input(0.0F);
  if (move_fwd_action_ && move_fwd_action_->IsOngoing()) {
    move_input.z += 1.0F;
  }
  if (move_bwd_action_ && move_bwd_action_->IsOngoing()) {
    move_input.z -= 1.0F;
  }
  if (move_left_action_ && move_left_action_->IsOngoing()) {
    move_input.x -= 1.0F;
  }
  if (move_right_action_ && move_right_action_->IsOngoing()) {
    move_input.x += 1.0F;
  }
  if (move_up_action_ && move_up_action_->IsOngoing()) {
    move_input.y += 1.0F;
  }
  if (move_down_action_ && move_down_action_->IsOngoing()) {
    move_input.y -= 1.0F;
  }

  if (glm::length(move_input) > 0.0F) {
    fly_controller_->AddMovementInput(move_input);
  }

  fly_controller_->Update(camera, delta_time);
}

/*!
 Synchronizes controller state with the active camera transform.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Avoids work when the active camera is invalid.
*/
auto CameraRigController::SyncFromActiveCamera() -> void
{
  if (!active_camera_ || !active_camera_->IsAlive()) {
    return;
  }

  EnsureControllers();
  orbit_controller_->SyncFromTransform(*active_camera_);
  fly_controller_->SyncFromTransform(*active_camera_);
  drone_controller_->SyncFromTransform(*active_camera_);
}

auto CameraRigController::GetOrbitController() noexcept
  -> observer_ptr<OrbitCameraController>
{
  EnsureControllers();
  return observer_ptr { orbit_controller_.get() };
}

auto CameraRigController::GetFlyController() noexcept
  -> observer_ptr<FlyCameraController>
{
  EnsureControllers();
  return observer_ptr { fly_controller_.get() };
}

auto CameraRigController::GetZoomInAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return zoom_in_action_;
}

auto CameraRigController::GetZoomOutAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return zoom_out_action_;
}

auto CameraRigController::GetRmbAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return rmb_action_;
}

auto CameraRigController::GetOrbitAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return orbit_action_;
}

auto CameraRigController::UpdateActiveCameraInputContext() -> void
{
  if (!input_system_) {
    return;
  }

  // Deactivate all first
  if (orbit_controls_ctx_) {
    input_system_->DeactivateMappingContext(orbit_controls_ctx_);
  }
  if (fly_controls_ctx_) {
    input_system_->DeactivateMappingContext(fly_controls_ctx_);
  }
  if (fly_modifier_keys_ctx_) {
    input_system_->DeactivateMappingContext(fly_modifier_keys_ctx_);
  }

  if (current_mode_ == CameraControlMode::kOrbit) {
    if (orbit_controls_ctx_) {
      input_system_->ActivateMappingContext(orbit_controls_ctx_);
    }
  } else if (current_mode_ == CameraControlMode::kFly) {
    if (fly_controls_ctx_) {
      input_system_->ActivateMappingContext(fly_controls_ctx_);
    }
    if (fly_modifier_keys_ctx_) {
      input_system_->ActivateMappingContext(fly_modifier_keys_ctx_);
    }
  }
}

auto CameraRigController::EnsureControllers() -> void
{
  if (!orbit_controller_) {
    orbit_controller_ = std::make_unique<OrbitCameraController>();
  }

  if (!fly_controller_) {
    fly_controller_ = std::make_unique<FlyCameraController>();
    fly_controller_->SetLookSensitivity(0.0015F);
  }

  if (!drone_controller_) {
    drone_controller_ = std::make_unique<DroneCameraController>();
  }
}

/*!
 Retrieves the drone controller handle.

 @return Observer pointer to the drone controller, or null when unavailable.
*/
auto CameraRigController::GetDroneController() noexcept
  -> observer_ptr<DroneCameraController>
{
  EnsureControllers();
  return observer_ptr { drone_controller_.get() };
}

/*!
 Checks if drone mode is available (valid path configured).

 @return True if drone has a path, false otherwise.
*/
auto CameraRigController::IsDroneAvailable() const noexcept -> bool
{
  return drone_controller_ && drone_controller_->HasPath();
}

} // namespace oxygen::examples::ui
