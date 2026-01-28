//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/UI/CameraRigController.h"
#include "DemoShell/UI/FlyCameraController.h"
#include "DemoShell/UI/OrbitCameraController.h"

namespace oxygen::examples::ui {

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

  using oxygen::input::Action;
  using oxygen::input::ActionTriggerChain;
  using oxygen::input::ActionTriggerDown;
  using oxygen::input::ActionTriggerPulse;
  using oxygen::input::ActionTriggerTap;
  using oxygen::input::ActionValueType;
  using oxygen::input::InputActionMapping;
  using oxygen::input::InputMappingContext;
  using oxygen::platform::InputSlots;

  LOG_F(INFO, "CameraRigController: Creating camera input actions");

  zoom_in_action_ = std::make_shared<Action>("zoom in", ActionValueType::kBool);
  zoom_out_action_
    = std::make_shared<Action>("zoom out", ActionValueType::kBool);
  rmb_action_ = std::make_shared<Action>("rmb", ActionValueType::kBool);
  orbit_action_
    = std::make_shared<Action>("camera orbit", ActionValueType::kAxis2D);
  move_fwd_action_
    = std::make_shared<Action>("move fwd", ActionValueType::kBool);
  move_bwd_action_
    = std::make_shared<Action>("move bwd", ActionValueType::kBool);
  move_left_action_
    = std::make_shared<Action>("move left", ActionValueType::kBool);
  move_right_action_
    = std::make_shared<Action>("move right", ActionValueType::kBool);
  move_up_action_ = std::make_shared<Action>("move up", ActionValueType::kBool);
  move_down_action_
    = std::make_shared<Action>("move down", ActionValueType::kBool);
  fly_plane_lock_action_
    = std::make_shared<Action>("fly plane lock", ActionValueType::kBool);
  fly_boost_action_
    = std::make_shared<Action>("fly boost", ActionValueType::kBool);

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

  orbit_controls_ctx_ = std::make_shared<InputMappingContext>("camera orbit");
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

  fly_controls_ctx_ = std::make_shared<InputMappingContext>("camera fly");
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
    add_bool_mapping(fly_boost_action_, InputSlots::LeftShift);
  }

  input_system_->AddMappingContext(orbit_controls_ctx_, 10);
  input_system_->AddMappingContext(fly_controls_ctx_, 10);
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

/*!
 Retrieves the active camera handle currently controlled by the rig.

 @return Observer pointer to the active camera node, or null if none is set.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
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
      && orbit_action_->GetValueType()
        == oxygen::input::ActionValueType::kAxis2D) {
      glm::vec2 orbit_delta(0.0F);
      for (const auto& tr : orbit_action_->GetFrameTransitions()) {
        const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
        orbit_delta.x += v.x;
        orbit_delta.y += v.y;
      }

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
    && orbit_action_->GetValueType()
      == oxygen::input::ActionValueType::kAxis2D) {
    glm::vec2 look_delta(0.0F);
    for (const auto& tr : orbit_action_->GetFrameTransitions()) {
      const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
      look_delta.x += v.x;
      look_delta.y += v.y;
    }

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
}

/*!
 Retrieves the orbit controller handle.

 @return Observer pointer to the orbit controller, or null when unavailable.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetOrbitController() const noexcept
  -> observer_ptr<OrbitCameraController>
{
  return observer_ptr { orbit_controller_.get() };
}

/*!
 Retrieves the fly controller handle.

 @return Observer pointer to the fly controller, or null when unavailable.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetFlyController() const noexcept
  -> observer_ptr<FlyCameraController>
{
  return observer_ptr { fly_controller_.get() };
}

/*!
 Retrieves the zoom-in action used for camera control.

 @return Shared pointer to the zoom-in action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetZoomInAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return zoom_in_action_;
}

/*!
 Retrieves the zoom-out action used for camera control.

 @return Shared pointer to the zoom-out action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetZoomOutAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return zoom_out_action_;
}

/*!
 Retrieves the right mouse button action used for camera control.

 @return Shared pointer to the right mouse button action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetRmbAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return rmb_action_;
}

/*!
 Retrieves the orbit/look action used for mouse deltas.

 @return Shared pointer to the orbit/look action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetOrbitAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return orbit_action_;
}

/*!
 Retrieves the forward movement action.

 @return Shared pointer to the forward movement action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetMoveForwardAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return move_fwd_action_;
}

/*!
 Retrieves the backward movement action.

 @return Shared pointer to the backward movement action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetMoveBackwardAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return move_bwd_action_;
}

/*!
 Retrieves the left movement action.

 @return Shared pointer to the left movement action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetMoveLeftAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return move_left_action_;
}

/*!
 Retrieves the right movement action.

 @return Shared pointer to the right movement action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetMoveRightAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return move_right_action_;
}

/*!
 Retrieves the up movement action.

 @return Shared pointer to the up movement action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetMoveUpAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return move_up_action_;
}

/*!
 Retrieves the down movement action.

 @return Shared pointer to the down movement action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetMoveDownAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return move_down_action_;
}

/*!
 Retrieves the plane-lock action for fly mode.

 @return Shared pointer to the plane-lock action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetFlyPlaneLockAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return fly_plane_lock_action_;
}

/*!
 Retrieves the boost action for fly mode.

 @return Shared pointer to the boost action.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: None.
*/
auto CameraRigController::GetFlyBoostAction() const noexcept
  -> std::shared_ptr<input::Action>
{
  return fly_boost_action_;
}

auto CameraRigController::UpdateActiveCameraInputContext() -> void
{
  if (!input_system_) {
    return;
  }

  if (current_mode_ == CameraControlMode::kOrbit) {
    if (orbit_controls_ctx_) {
      input_system_->ActivateMappingContext(orbit_controls_ctx_);
    }
    if (fly_controls_ctx_) {
      input_system_->DeactivateMappingContext(fly_controls_ctx_);
    }
    return;
  }

  if (orbit_controls_ctx_) {
    input_system_->DeactivateMappingContext(orbit_controls_ctx_);
  }
  if (fly_controls_ctx_) {
    input_system_->ActivateMappingContext(fly_controls_ctx_);
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
}

} // namespace oxygen::examples::ui
