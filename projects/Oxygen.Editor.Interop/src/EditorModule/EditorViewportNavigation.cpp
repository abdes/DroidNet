//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include "EditorModule/EditorViewportNavigation.h"
#include "EditorModule/EditorViewportDollyFeature.h"
#include "EditorModule/EditorViewportFlyFeature.h"
#include "EditorModule/EditorViewportOrbitFeature.h"
#include "EditorModule/EditorViewportPanFeature.h"
#include "EditorModule/EditorViewportResetFeature.h"
#include "EditorModule/EditorViewportWheelZoomFeature.h"

namespace oxygen::interop::module {

  EditorViewportNavigation::EditorViewportNavigation() {
    features_.push_back(std::make_unique<EditorViewportResetFeature>());
    features_.push_back(std::make_unique<EditorViewportOrbitFeature>());
    features_.push_back(std::make_unique<EditorViewportPanFeature>());
    features_.push_back(std::make_unique<EditorViewportDollyFeature>());
    features_.push_back(std::make_unique<EditorViewportFlyFeature>());

    auto wheel = std::make_unique<EditorViewportWheelZoomFeature>();
    wheel_zoom_feature_ = wheel.get();
    features_.push_back(std::move(wheel));
  }

  auto EditorViewportNavigation::RegisterSharedInputs(
    oxygen::engine::InputSystem& input_system) -> void {
    using oxygen::input::Action;
    using oxygen::input::ActionTriggerDown;
    using oxygen::input::ActionValueType;
    using oxygen::input::InputActionMapping;
    using platform::InputSlots;

    const auto make_action = [&](const char* name, ActionValueType type) {
      auto action
        = oxygen::input::MakeCodeAction(kEditorViewportInputOwner, name, type);
      input_system.AddAction(action);
      return action;
    };
    inputs_ = EditorViewportInputActions {
      .alt = make_action("Modifier.Alt", ActionValueType::kBool),
      .left_button = make_action("Mouse.LeftButton", ActionValueType::kBool),
      .middle_button = make_action("Mouse.MiddleButton", ActionValueType::kBool),
      .right_button = make_action("Mouse.RightButton", ActionValueType::kBool),
      .mouse_delta = make_action("Mouse.Delta", ActionValueType::kAxis2D),
    };

    const auto map_down = [&](const std::shared_ptr<Action>& action,
                            const platform::InputSlot& slot,
                            const std::shared_ptr<ActionTriggerDown>& trigger) {
      trigger->MakeExplicit();
      auto mapping = std::make_shared<InputActionMapping>(action, slot);
      mapping->AddTrigger(trigger);
      ctx_->AddMapping(mapping);
    };
    const auto down = [] { return std::make_shared<ActionTriggerDown>(); };
    map_down(inputs_.alt, InputSlots::LeftAlt, down());
    map_down(inputs_.alt, InputSlots::RightAlt, down());
    map_down(inputs_.left_button, InputSlots::LeftMouseButton, down());
    map_down(inputs_.middle_button, InputSlots::MiddleMouseButton, down());
    map_down(inputs_.right_button, InputSlots::RightMouseButton, down());
    // Any mouse motion counts, however small.
    const auto any_motion = down();
    any_motion->SetActuationThreshold(0.0F);
    map_down(inputs_.mouse_delta, InputSlots::MouseXY, any_motion);
  }

  auto EditorViewportNavigation::InitializeBindings(
    oxygen::engine::InputSystem& input_system) noexcept -> bool {
    if (ctx_) {
      return true;
    }

    ctx_ = oxygen::input::MakeCodeContext(
      kEditorViewportInputOwner, "IMC_Editor_Viewport");
    RegisterSharedInputs(input_system);

    for (auto& feature : features_) {
      feature->RegisterBindings(input_system, ctx_, inputs_);
    }

    // Priority matches the design doc (IMC_Editor_Viewport = 50).
    input_system.AddMappingContext(ctx_, 50);
    input_system.ActivateMappingContext(ctx_);

    LOG_F(INFO, "Initialized editor viewport navigation input mapping context");
    return true;
  }

  auto EditorViewportNavigation::Apply(oxygen::scene::SceneNode camera_node,
    const oxygen::input::InputSnapshot& input_snapshot,
    EditorViewportCameraControlMode control_mode,
    glm::vec3& focus_point,
    float& ortho_half_height,
    float movement_speed_units_per_second,
    float dt_seconds) noexcept -> void {
    if (!ctx_) {
      return;
    }
    for (auto& feature : features_) {
      feature->Apply(camera_node, input_snapshot, control_mode, focus_point,
        ortho_half_height, movement_speed_units_per_second, dt_seconds);
    }
  }

  auto EditorViewportNavigation::ApplyNonWheel(
    oxygen::scene::SceneNode camera_node,
    const oxygen::input::InputSnapshot& input_snapshot,
    EditorViewportCameraControlMode control_mode,
    glm::vec3& focus_point,
    float& ortho_half_height,
    float movement_speed_units_per_second,
    float dt_seconds) noexcept -> void {
    if (!ctx_) {
      return;
    }
    for (auto& feature : features_) {
      if (feature.get() == wheel_zoom_feature_) {
        continue;
      }
      feature->Apply(camera_node, input_snapshot, control_mode, focus_point,
        ortho_half_height, movement_speed_units_per_second, dt_seconds);
    }
  }

  auto EditorViewportNavigation::ApplyWheelOnly(
    oxygen::scene::SceneNode camera_node,
    const oxygen::input::InputSnapshot& input_snapshot,
    EditorViewportCameraControlMode control_mode,
    glm::vec3& focus_point,
    float& ortho_half_height,
    float movement_speed_units_per_second,
    float dt_seconds) noexcept -> void {
    if (!ctx_ || wheel_zoom_feature_ == nullptr) {
      return;
    }

    wheel_zoom_feature_->Apply(camera_node, input_snapshot, control_mode,
      focus_point, ortho_half_height, movement_speed_units_per_second,
      dt_seconds);
  }

} // namespace oxygen::interop::module
