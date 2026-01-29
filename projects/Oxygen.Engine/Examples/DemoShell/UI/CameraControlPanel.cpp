//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <string_view>

#include <glm/geometric.hpp>
#include <imgui.h>

#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Input/Action.h>

#include "DemoShell/UI/CameraControlPanel.h"

namespace oxygen::examples::ui {

void CameraControlPanel::Initialize(const CameraControlConfig& config)
{
  config_ = config;
  current_mode_ = config_.current_mode;
}

void CameraControlPanel::UpdateConfig(const CameraControlConfig& config)
{
  config_ = config;
  current_mode_ = config_.current_mode;
}
auto CameraControlPanel::DrawContents() -> void
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

auto CameraControlPanel::GetName() const noexcept -> std::string_view
{
  return "Camera Controls";
}

auto CameraControlPanel::GetPreferredWidth() const noexcept -> float
{
  return 360.0F;
}

auto CameraControlPanel::GetIcon() const noexcept -> std::string_view
{
  return oxygen::imgui::icons::kIconCameraControls;
}

auto CameraControlPanel::OnLoaded() -> void
{
  current_mode_ = config_.current_mode;
}

auto CameraControlPanel::OnUnloaded() -> void
{
  if (config_.on_panel_closed) {
    config_.on_panel_closed();
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

  // Mode-specific controls
  if (current_mode_ == CameraControlMode::kOrbit) {
    ImGui::SeparatorText("Orbit Settings");

    if (config_.orbit_controller) {
      // Orbit mode selection
      const auto current_orbit_mode = config_.orbit_controller->GetMode();
      const bool is_trackball
        = (current_orbit_mode == ui::OrbitMode::kTrackball);
      const bool is_turntable
        = (current_orbit_mode == ui::OrbitMode::kTurntable);

      if (ImGui::RadioButton("Trackball", is_trackball)) {
        if (!is_trackball) {
          config_.orbit_controller->SetMode(ui::OrbitMode::kTrackball);
          if (config_.active_camera && config_.active_camera->IsAlive()) {
            config_.orbit_controller->SyncFromTransform(*config_.active_camera);
          }
        }
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Turntable", is_turntable)) {
        if (!is_turntable) {
          config_.orbit_controller->SetMode(ui::OrbitMode::kTurntable);
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
  if (current_mode_ == CameraControlMode::kFly) {
    ImGui::Spacing();
    DrawInputDebugInfo();
  }
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

  // Basis vectors
  const glm::vec3 forward = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
  const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
  const glm::vec3 right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);

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

  const float forward_dot_pos_y = glm::dot(forward_normalized, world_pos_y);
  const float forward_dot_neg_y = glm::dot(forward_normalized, world_neg_y);
  const float up_dot_pos_z = glm::dot(up_normalized, world_pos_z);

  ImGui::PushID("CameraPoseTable");
  if (ImGui::BeginTable(
        "##CameraPoseTable", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    constexpr float kValueFieldWidth = 240.0f;
    const auto right_align = [kValueFieldWidth]() {
      const float start = ImGui::GetCursorPosX();
      const float col_width = ImGui::GetColumnWidth();
      const float offset = std::max(0.0f, col_width - kValueFieldWidth);
      ImGui::SetCursorPosX(start + offset);
    };

    auto row_vec3 = [kValueFieldWidth, right_align](
                      const char* label, const glm::vec3& value) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      float data[3] = { value.x, value.y, value.z };
      ImGui::BeginDisabled();
      const std::string id = std::string("##") + label;
      right_align();
      ImGui::SetNextItemWidth(kValueFieldWidth);
      ImGui::InputFloat3(
        id.c_str(), data, "%.3f", ImGuiInputTextFlags_ReadOnly);
      ImGui::EndDisabled();
    };

    auto row_float
      = [kValueFieldWidth, right_align](const char* label, float value) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(label);
          ImGui::TableNextColumn();
          float data = value;
          ImGui::BeginDisabled();
          const std::string id = std::string("##") + label;
          right_align();
          ImGui::SetNextItemWidth(kValueFieldWidth);
          ImGui::InputFloat(id.c_str(), &data, 0.0f, 0.0f, "%.3f",
            ImGuiInputTextFlags_ReadOnly);
          ImGui::EndDisabled();
        };

    row_vec3("Position", position);
    row_vec3("Forward", forward);
    row_vec3("Up", up);
    row_vec3("Right", right);
    row_float("forward · +Y", forward_dot_pos_y);
    row_float("forward · -Y", forward_dot_neg_y);
    row_float("up · +Z", up_dot_pos_z);

    ImGui::EndTable();
  }
  ImGui::PopID();
}

void CameraControlPanel::DrawInputDebugInfo()
{
  ImGui::SeparatorText("Input State");

  const auto& io = ImGui::GetIO();
  const ImVec4 kActiveColor { 0.2f, 0.8f, 0.2f, 1.0f };
  const ImVec4 kInactiveColor { 0.6f, 0.6f, 0.6f, 1.0f };
  ImGui::PushID("InputStateTable");
  if (ImGui::BeginTable(
        "##InputStateTable", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 260.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    auto row_bool
      = [kActiveColor, kInactiveColor](const char* label, bool value) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(label);
          ImGui::TableNextColumn();
          const ImVec4 color = value ? kActiveColor : kInactiveColor;
          ImGui::TextColored(color, "%s", value ? "Active" : "Inactive");
        };

    row_bool("ImGui WantCaptureKeyboard", io.WantCaptureKeyboard);
    row_bool("ImGui WantCaptureMouse", io.WantCaptureMouse);
    ImGui::EndTable();
  }
  ImGui::PopID();

  ImGui::Spacing();
  ImGui::TextUnformatted("Action States:");
  ImGui::Separator();

  if (ImGui::BeginTable(
        "##ActionStatesTable", 3, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthStretch);

    const auto show_action
      = [this](const char* label,
          const std::shared_ptr<oxygen::input::Action>& action) {
          const char* state = GetActionStateString(action);
          const bool ongoing = action && action->IsOngoing();
          const bool triggered = action && action->WasTriggeredThisFrame();
          const bool released = action && action->WasReleasedThisFrame();
          const bool is_active = std::string_view(state) != "Idle"
            && std::string_view(state) != "<null>";
          const ImVec4 color = is_active
            ? ImVec4(1.0f, 0.75f, 0.2f, 1.0f)
            : ImGui::GetStyleColorVec4(ImGuiCol_Text);

          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(label);
          ImGui::TableNextColumn();
          ImGui::TextColored(color, "%s", state);
          ImGui::TableNextColumn();
          ImGui::TextColored(color, "O:%d  T:%d  R:%d", ongoing ? 1 : 0,
            triggered ? 1 : 0, released ? 1 : 0);
        };

    show_action("W (Fwd)", config_.move_fwd_action);
    show_action("S (Bwd)", config_.move_bwd_action);
    show_action("A (Left)", config_.move_left_action);
    show_action("D (Right)", config_.move_right_action);
    show_action("Shift", config_.fly_boost_action);
    show_action("Space", config_.fly_plane_lock_action);
    show_action("RMB", config_.rmb_action);
    ImGui::EndTable();
  }

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
    ImGui::PushID("MouseDeltaTable");
    if (ImGui::BeginTable(
          "##MouseDeltaTable", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn(
        "Label", ImGuiTableColumnFlags_WidthFixed, 160.0f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Mouse Delta");
      ImGui::TableNextColumn();
      float delta[2] = { mouse_delta.x, mouse_delta.y };
      ImGui::BeginDisabled();
      ImGui::SetNextItemWidth(220.0f);
      ImGui::InputFloat2(
        "##MouseDelta", delta, "%.2f", ImGuiInputTextFlags_ReadOnly);
      ImGui::EndDisabled();
      ImGui::EndTable();
    }
    ImGui::PopID();
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

} // namespace oxygen::examples::ui
