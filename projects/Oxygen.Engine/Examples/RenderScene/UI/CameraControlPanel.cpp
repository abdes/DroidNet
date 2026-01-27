//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>

#include <imgui.h>

#include <glm/geometric.hpp>

#include <Oxygen/Input/Action.h>

#include "RenderScene/FlyCameraController.h"
#include "RenderScene/OrbitCameraController.h"
#include "RenderScene/UI/CameraControlPanel.h"

namespace oxygen::examples::render_scene::ui {

void CameraControlPanel::Initialize(const CameraControlConfig& config)
{
  config_ = config;
}

void CameraControlPanel::UpdateConfig(const CameraControlConfig& config)
{
  config_ = config;
}

void CameraControlPanel::Draw()
{
  ImGui::SetNextWindowPos(ImVec2(550, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Camera Controls", nullptr, ImGuiWindowFlags_None)) {
    ImGui::End();
    return;
  }

  DrawContents();

  ImGui::End();
}

void CameraControlPanel::DrawContents()
{
  if (ImGui::BeginTabBar("CameraControlTabs")) {
    if (ImGui::BeginTabItem("Camera Mode")) {
      DrawCameraModeTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Debug")) {
      DrawDebugTab();
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }
}

void CameraControlPanel::DrawCameraModeTab()
{
  ImGui::SeparatorText("Control Mode");

  // Mode selection with radio buttons
  const bool is_orbit = (current_mode_ == CameraControlMode::kOrbit);
  const bool is_fly = (current_mode_ == CameraControlMode::kFly);

  if (ImGui::RadioButton("Orbit", is_orbit)) {
    if (!is_orbit) {
      current_mode_ = CameraControlMode::kOrbit;
      if (config_.on_mode_changed) {
        config_.on_mode_changed(current_mode_);
      }
    }
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Fly", is_fly)) {
    if (!is_fly) {
      current_mode_ = CameraControlMode::kFly;
      if (config_.on_mode_changed) {
        config_.on_mode_changed(current_mode_);
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Mode-specific controls
  if (current_mode_ == CameraControlMode::kOrbit) {
    ImGui::SeparatorText("Orbit Settings");

    if (config_.orbit_controller) {
      // Orbit mode selection
      const auto current_orbit_mode = config_.orbit_controller->GetMode();
      const bool is_trackball
        = (current_orbit_mode == render_scene::OrbitMode::kTrackball);
      const bool is_turntable
        = (current_orbit_mode == render_scene::OrbitMode::kTurntable);

      if (ImGui::RadioButton("Trackball", is_trackball)) {
        if (!is_trackball) {
          config_.orbit_controller->SetMode(
            render_scene::OrbitMode::kTrackball);
          if (config_.active_camera && config_.active_camera->IsAlive()) {
            config_.orbit_controller->SyncFromTransform(*config_.active_camera);
          }
        }
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Turntable", is_turntable)) {
        if (!is_turntable) {
          config_.orbit_controller->SetMode(
            render_scene::OrbitMode::kTurntable);
          if (config_.active_camera && config_.active_camera->IsAlive()) {
            config_.orbit_controller->SyncFromTransform(*config_.active_camera);
          }
        }
      }

      ImGui::Spacing();
      ImGui::TextWrapped("Controls: Hold Right Mouse Button and drag to orbit. "
                         "Mouse wheel to zoom in/out.");

    } else {
      ImGui::TextDisabled("Orbit controller not available");
    }

  } else {
    ImGui::SeparatorText("Fly Settings");

    if (config_.fly_controller) {
      float speed = config_.fly_controller->GetMoveSpeed();

      if (ImGui::SliderFloat("Move Speed", &speed, 0.1f, 100.0f, "%.2f",
            ImGuiSliderFlags_Logarithmic)) {
        config_.fly_controller->SetMoveSpeed(speed);
      }

      ImGui::Spacing();
      ImGui::TextWrapped(
        "Controls: WASD to move, Q/E for down/up. "
        "Hold Right Mouse Button and drag to look around. "
        "Hold Shift to boost speed. Hold Space to lock to horizontal plane. "
        "Mouse wheel to adjust speed.");

    } else {
      ImGui::TextDisabled("Fly controller not available");
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Reset button
  ImGui::SeparatorText("Actions");

  if (ImGui::Button("Reset Camera Position", ImVec2(-1, 0))) {
    if (config_.on_reset_requested) {
      config_.on_reset_requested();
    }
  }

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reset camera to initial position and rotation");
  }
}

void CameraControlPanel::DrawDebugTab()
{
  DrawCameraPoseInfo();
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  DrawInputDebugInfo();
}

void CameraControlPanel::DrawCameraPoseInfo()
{
  ImGui::SeparatorText("Camera Pose");

  if (!config_.active_camera || !config_.active_camera->IsAlive()) {
    ImGui::TextDisabled("No active camera");
    return;
  }

  auto transform = config_.active_camera->GetTransform();

  glm::vec3 position { 0.0f, 0.0f, 0.0f };
  glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };

  if (auto pos = transform.GetLocalPosition()) {
    position = *pos;
  }
  if (auto rot = transform.GetLocalRotation()) {
    rotation = *rot;
  }

  // Position
  ImGui::Text(
    "Position: (%.2f, %.2f, %.2f)", position.x, position.y, position.z);

  // Basis vectors
  const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
  const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);

  ImGui::Text("Forward:  (%.3f, %.3f, %.3f)", forward.x, forward.y, forward.z);
  ImGui::Text("Up:       (%.3f, %.3f, %.3f)", up.x, up.y, up.z);
  ImGui::Text("Right:    (%.3f, %.3f, %.3f)", right.x, right.y, right.z);

  // World space alignment checks
  const auto safe_normalize = [](const glm::vec3& v) -> glm::vec3 {
    const float len_squared = glm::dot(v, v);
    if (len_squared <= 0.0f) {
      return glm::vec3(0.0f);
    }
    return v / std::sqrt(len_squared);
  };

  const glm::vec3 forward_normalized = safe_normalize(forward);
  const glm::vec3 up_normalized = safe_normalize(up);

  const glm::vec3 world_pos_y(0.0f, 1.0f, 0.0f);
  const glm::vec3 world_neg_y(0.0f, -1.0f, 0.0f);
  const glm::vec3 world_pos_z(0.0f, 0.0f, 1.0f);

  ImGui::Spacing();
  ImGui::Text("Alignment (dot products):");
  ImGui::Text(
    "  forward · +Y: %.3f", glm::dot(forward_normalized, world_pos_y));
  ImGui::Text(
    "  forward · -Y: %.3f", glm::dot(forward_normalized, world_neg_y));
  ImGui::Text("  up · +Z:      %.3f (expect ~1.0 for Z-up)",
    glm::dot(up_normalized, world_pos_z));
}

void CameraControlPanel::DrawInputDebugInfo()
{
  ImGui::SeparatorText("Input State");

  const auto& io = ImGui::GetIO();
  ImGui::Text(
    "ImGui WantCaptureKeyboard: %s", io.WantCaptureKeyboard ? "true" : "false");
  ImGui::Text(
    "ImGui WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");

  ImGui::Spacing();
  ImGui::Text("Action States:");
  ImGui::Separator();

  // Layout: action name, state string, flags
  const auto show_action
    = [this](const char* label,
        const std::shared_ptr<oxygen::input::Action>& action) {
        const char* state = GetActionStateString(action);
        const bool ongoing = action && action->IsOngoing();
        const bool triggered = action && action->WasTriggeredThisFrame();
        const bool released = action && action->WasReleasedThisFrame();

        ImGui::Text("%-12s  %-10s  [O:%d T:%d R:%d]", label, state,
          ongoing ? 1 : 0, triggered ? 1 : 0, released ? 1 : 0);
      };

  show_action("W (Fwd)", config_.move_fwd_action);
  show_action("S (Bwd)", config_.move_bwd_action);
  show_action("A (Left)", config_.move_left_action);
  show_action("D (Right)", config_.move_right_action);
  show_action("Shift", config_.fly_boost_action);
  show_action("Space", config_.fly_plane_lock_action);
  show_action("RMB", config_.rmb_action);

  // Mouse delta
  if (config_.orbit_action
    && config_.orbit_action->GetValueType()
      == oxygen::input::ActionValueType::kAxis2D) {
    glm::vec2 mouse_delta(0.0f);

    for (const auto& transition : config_.orbit_action->GetFrameTransitions()) {
      const auto& value
        = transition.value_at_transition.GetAs<oxygen::Axis2D>();
      mouse_delta.x += value.x;
      mouse_delta.y += value.y;
    }

    ImGui::Spacing();
    ImGui::Text("Mouse Delta: (%.2f, %.2f)", mouse_delta.x, mouse_delta.y);
  }
}

auto CameraControlPanel::GetActionStateString(
  const std::shared_ptr<oxygen::input::Action>& action) const -> const char*
{
  if (!action) {
    return "<null>";
  }

  if (action->WasCanceledThisFrame()) {
    return "Canceled";
  }
  if (action->WasCompletedThisFrame()) {
    return "Completed";
  }
  if (action->WasTriggeredThisFrame()) {
    return "Triggered";
  }
  if (action->WasReleasedThisFrame()) {
    return "Released";
  }
  if (action->IsOngoing()) {
    return "Ongoing";
  }
  if (action->WasValueUpdatedThisFrame()) {
    return "Updated";
  }

  return "Idle";
}

} // namespace oxygen::examples::render_scene::ui
