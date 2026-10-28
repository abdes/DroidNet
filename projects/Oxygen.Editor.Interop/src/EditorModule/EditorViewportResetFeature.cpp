//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include "EditorModule/EditorViewportResetFeature.h"

#include "EditorModule/EditorViewportMathHelpers.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Transforms/NodeFrame.h>

namespace oxygen::interop::module {

  namespace {

    struct ResetParams {
      glm::vec3 fallback_focus_point = glm::vec3(0.0f, 0.0f, 0.0f);
      glm::vec3 camera_offset = glm::vec3(15.0f, -27.0f, 13.0f);
      glm::vec3 up = oxygen::space::move::Up;
    };

  } // namespace

  auto EditorViewportResetFeature::RegisterBindings(
    oxygen::engine::InputSystem& input_system,
    const std::shared_ptr<oxygen::input::InputMappingContext>& ctx,
    const EditorViewportInputActions& /*inputs*/) noexcept -> void {
    using oxygen::input::ActionTriggerDown;
    using oxygen::input::ActionValueType;
    using oxygen::input::InputActionMapping;
    using platform::InputSlots;

    if (reset_action_) {
      return;
    }

    reset_action_ = oxygen::input::MakeCodeAction(
      kEditorViewportInputOwner, "Camera.Reset", ActionValueType::kBool);
    input_system.AddAction(reset_action_);

    const auto trig = std::make_shared<ActionTriggerDown>();
    trig->MakeExplicit();

    const auto reset = std::make_shared<InputActionMapping>(
      reset_action_, InputSlots::Home);
    reset->AddTrigger(trig);
    ctx->AddMapping(reset);
  }

  auto EditorViewportResetFeature::Apply(scene::SceneNode camera_node,
    const input::InputSnapshot& input_snapshot,
    EditorViewportCameraControlMode /*control_mode*/,
    glm::vec3& focus_point,
    float& /*ortho_half_height*/,
    float /*movement_speed_units_per_second*/,
    float /*dt_seconds*/) noexcept -> void {
    if (!camera_node.IsAlive()) {
      return;
    }

    if (!input_snapshot.DidActionTrigger(reset_action_->GetAssetKey())) {
      return;
    }

    const ResetParams params {};

    if (!viewport::IsFinite(focus_point)) {
      focus_point = params.fallback_focus_point;
    }

    auto transform = camera_node.GetTransform();
    const glm::vec3 camera_position = focus_point + params.camera_offset;
    (void)transform.SetLocalPosition(camera_position);

    const glm::quat look_rotation = oxygen::transforms::LookRotation(
      { .forward = focus_point - camera_position, .up = params.up });
    (void)transform.SetLocalRotation(look_rotation);
  }

} // namespace oxygen::interop::module
