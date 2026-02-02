//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <string_view>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Input/Action.h>

#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraVm.h"

namespace oxygen::examples::ui {

CameraControlPanel::CameraControlPanel(observer_ptr<CameraVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "CameraControlPanel requires CameraVm");
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
  return imgui::icons::kIconCameraControls;
}

auto CameraControlPanel::OnLoaded() -> void { }

auto CameraControlPanel::OnUnloaded() -> void
{
  vm_->PersistActiveCameraSettings();
}

void CameraControlPanel::DrawCameraModeTab()
{
  ImGui::SeparatorText("Control Mode");

  const auto current_mode = vm_->GetControlMode();
  const bool is_orbit = current_mode == CameraControlMode::kOrbit;
  const bool is_fly = current_mode == CameraControlMode::kFly;

  if (ImGui::RadioButton("Orbit", is_orbit)) {
    vm_->SetControlMode(CameraControlMode::kOrbit);
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Fly", is_fly)) {
    vm_->SetControlMode(CameraControlMode::kFly);
  }

  if (vm_->IsDroneAvailable()) {
    ImGui::SameLine();
    const bool is_drone = current_mode == CameraControlMode::kDrone;
    if (ImGui::RadioButton("Drone", is_drone)) {
      vm_->SetControlMode(CameraControlMode::kDrone);
    }
  }

  ImGui::Spacing();

  if (current_mode == CameraControlMode::kOrbit) {
    ImGui::SeparatorText("Orbit Settings");

    const auto orbit_mode = vm_->GetOrbitMode();
    const bool is_trackball = orbit_mode == OrbitMode::kTrackball;
    const bool is_turntable = orbit_mode == OrbitMode::kTurntable;

    if (ImGui::RadioButton("Trackball", is_trackball)) {
      vm_->SetOrbitMode(OrbitMode::kTrackball);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Turntable", is_turntable)) {
      vm_->SetOrbitMode(OrbitMode::kTurntable);
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Controls: Hold Right Mouse Button and drag to orbit. "
                       "Mouse wheel to zoom in/out.");

  } else if (current_mode == CameraControlMode::kDrone) {
    ImGui::SeparatorText("Drone Survey");

    // Play/Pause + Progress
    bool running = vm_->GetDroneRunning();
    if (ImGui::Button(running ? "Pause" : "Play")) {
      vm_->SetDroneRunning(!running);
    }
    ImGui::SameLine();
    ImGui::ProgressBar(static_cast<float>(vm_->GetDroneProgress()),
      ImVec2(-1.0F, 0.0F), "Path Progress");

    ImGui::Spacing();

    // Flight Settings
    float speed = vm_->GetDroneSpeed();
    if (ImGui::SliderFloat("Speed", &speed, 0.5F, 30.0F, "%.1F u/s")) {
      vm_->SetDroneSpeed(speed);
    }

    float damping = vm_->GetDroneDamping();
    if (ImGui::SliderFloat("Smoothing", &damping, 1.0F, 20.0F, "%.1F")) {
      vm_->SetDroneDamping(damping);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Focus Tracking");
    ImGui::Indent();
    float height = vm_->GetDroneFocusHeight();
    if (ImGui::SliderFloat("Height", &height, -5.0F, 15.0F)) {
      vm_->SetDroneFocusHeight(height);
    }
    glm::vec2 offset = vm_->GetDroneFocusOffset();
    if (ImGui::DragFloat2("Offset X/Z", &offset.x, 0.1F)) {
      vm_->SetDroneFocusOffset(offset);
    }
    ImGui::Unindent();

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Cinematics")) {
      float bob_amp = vm_->GetDroneBobAmplitude();
      if (ImGui::SliderFloat("Bob Amp", &bob_amp, 0.0F, 0.5F)) {
        vm_->SetDroneBobAmplitude(bob_amp);
      }
      float bob_freq = vm_->GetDroneBobFrequency();
      if (ImGui::SliderFloat("Bob Freq", &bob_freq, 0.1F, 5.0F, "%.1F Hz")) {
        vm_->SetDroneBobFrequency(bob_freq);
      }
      float noise = vm_->GetDroneNoiseAmplitude();
      if (ImGui::SliderFloat("Noise", &noise, 0.0F, 0.2F)) {
        vm_->SetDroneNoiseAmplitude(noise);
      }
      float bank = vm_->GetDroneBankFactor();
      if (ImGui::SliderFloat("Bank", &bank, 0.0F, 0.2F)) {
        vm_->SetDroneBankFactor(bank);
      }
    }

    if (ImGui::CollapsingHeader("POI Slowdown")) {
      float radius = vm_->GetDronePOISlowdownRadius();
      if (ImGui::SliderFloat("Radius", &radius, 1.0F, 20.0F)) {
        vm_->SetDronePOISlowdownRadius(radius);
      }
      float min_speed = vm_->GetDronePOIMinSpeed();
      if (ImGui::SliderFloat("Min Speed", &min_speed, 0.1F, 1.0F, "%.2Fx")) {
        vm_->SetDronePOIMinSpeed(min_speed);
      }
    }

    ImGui::Spacing();
    bool show_path = vm_->GetDroneShowPath();
    if (ImGui::Checkbox("Show flight path", &show_path)) {
      vm_->SetDroneShowPath(show_path);
    }

    if (show_path) {
      ImGui::Spacing();
      ImGui::SeparatorText("Minimap");
      DrawDroneMinimap();
    }

  } else {
    ImGui::SeparatorText("Fly Settings");

    float speed = vm_->GetFlyMoveSpeed();
    if (ImGui::SliderFloat("Move Speed", &speed, 0.1F, 100.0F, "%.2F",
          ImGuiSliderFlags_Logarithmic)) {
      vm_->SetFlyMoveSpeed(speed);
    }

    ImGui::Spacing();
    ImGui::TextWrapped(
      "Controls: WASD to move, Q/E for down/up. "
      "Hold Right Mouse Button and drag to look around. "
      "Hold Shift to boost speed. Hold Space to lock to horizontal plane. "
      "Mouse wheel to adjust speed.");
  }

  ImGui::Spacing();

  ImGui::SeparatorText("Actions");

  if (ImGui::Button("Reset Camera Position", ImVec2(-1, 0))) {
    vm_->RequestReset();
  }

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reset camera to initial position and rotation");
  }
}

void CameraControlPanel::DrawDebugTab()
{
  DrawCameraPoseInfo();
  if (vm_->GetControlMode() == CameraControlMode::kFly) {
    ImGui::Spacing();
    DrawInputDebugInfo();
  }
}

void CameraControlPanel::DrawCameraPoseInfo()
{
  ImGui::SeparatorText("Camera Pose");

  if (!vm_->HasActiveCamera()) {
    ImGui::TextDisabled("No active camera");
    return;
  }

  const glm::vec3 position = vm_->GetCameraPosition();
  const glm::quat rotation = vm_->GetCameraRotation();

  // Basis vectors
  const glm::vec3 forward = rotation * glm::vec3(0.0F, 0.0F, -1.0F);
  const glm::vec3 up = rotation * glm::vec3(0.0F, 1.0F, 0.0F);
  const glm::vec3 right = rotation * glm::vec3(1.0F, 0.0F, 0.0F);

  // World space alignment checks
  const auto safe_normalize = [](const glm::vec3& v) -> glm::vec3 {
    const float len_squared = glm::dot(v, v);
    if (len_squared <= 0.0F) {
      return glm::vec3(0.0F);
    }
    return v / std::sqrt(len_squared);
  };

  const glm::vec3 forward_normalized = safe_normalize(forward);
  const glm::vec3 up_normalized = safe_normalize(up);

  constexpr glm::vec3 world_pos_y(0.0F, 1.0F, 0.0F);
  constexpr glm::vec3 world_neg_y(0.0F, -1.0F, 0.0F);
  constexpr glm::vec3 world_pos_z(0.0F, 0.0F, 1.0F);

  const float forward_dot_pos_y = glm::dot(forward_normalized, world_pos_y);
  const float forward_dot_neg_y = glm::dot(forward_normalized, world_neg_y);
  const float up_dot_pos_z = glm::dot(up_normalized, world_pos_z);

  ImGui::PushID("CameraPoseTable");
  if (ImGui::BeginTable(
        "##CameraPoseTable", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 160.0F);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    constexpr float kValueFieldWidth = 240.0F;
    const auto right_align = [] {
      const float start = ImGui::GetCursorPosX();
      const float col_width = ImGui::GetColumnWidth();
      const float offset = std::max(0.0F, col_width - kValueFieldWidth);
      ImGui::SetCursorPosX(start + offset);
    };

    auto row_vec3 = [right_align](const char* label, const glm::vec3& value) {
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
        id.c_str(), data, "%.3F", ImGuiInputTextFlags_ReadOnly);
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
          ImGui::InputFloat(id.c_str(), &data, 0.0F, 0.0F, "%.3F",
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
  constexpr ImVec4 kActiveColor { 0.2F, 0.8F, 0.2F, 1.0F };
  constexpr ImVec4 kInactiveColor { 0.6F, 0.6F, 0.6F, 1.0F };
  ImGui::PushID("InputStateTable");
  if (ImGui::BeginTable(
        "##InputStateTable", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 260.0F);
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
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 120.0F);
    ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 120.0F);
    ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthStretch);

    const auto show_action = [this](const char* label,
                               const std::shared_ptr<input::Action>& action) {
      const char* state = vm_->GetActionStateString(action);
      const bool ongoing = action && action->IsOngoing();
      const bool triggered = action && action->WasTriggeredThisFrame();
      const bool released = action && action->WasReleasedThisFrame();
      const bool is_active = std::string_view(state) != "Idle"
        && std::string_view(state) != "<null>";
      const ImVec4 color = is_active ? ImVec4(1.0F, 0.75F, 0.2F, 1.0F)
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

    show_action("W (Fwd)", vm_->GetMoveForwardAction());
    show_action("S (Bwd)", vm_->GetMoveBackwardAction());
    show_action("A (Left)", vm_->GetMoveLeftAction());
    show_action("D (Right)", vm_->GetMoveRightAction());
    show_action("Shift", vm_->GetFlyBoostAction());
    show_action("Space", vm_->GetFlyPlaneLockAction());
    show_action("RMB", vm_->GetRmbAction());
    ImGui::EndTable();
  }

  // Mouse delta
  auto orbit_action = vm_->GetOrbitAction();
  if (orbit_action
    && orbit_action->GetValueType() == input::ActionValueType::kAxis2D) {
    glm::vec2 mouse_delta(0.0F);

    for (const auto& transition : orbit_action->GetFrameTransitions()) {
      const auto& value = transition.value_at_transition.GetAs<Axis2D>();
      mouse_delta.x += value.x;
      mouse_delta.y += value.y;
    }

    ImGui::Spacing();
    ImGui::PushID("MouseDeltaTable");
    if (ImGui::BeginTable(
          "##MouseDeltaTable", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn(
        "Label", ImGuiTableColumnFlags_WidthFixed, 160.0F);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted("Mouse Delta");
      ImGui::TableNextColumn();
      float delta[2] = { mouse_delta.x, mouse_delta.y };
      ImGui::BeginDisabled();
      ImGui::SetNextItemWidth(220.0F);
      ImGui::InputFloat2(
        "##MouseDelta", delta, "%.2F", ImGuiInputTextFlags_ReadOnly);
      ImGui::EndDisabled();
      ImGui::EndTable();
    }
    ImGui::PopID();
  }
}

void CameraControlPanel::DrawDroneMinimap()
{
  if (!vm_) {
    return;
  }
  if (!vm_->IsDroneAvailable()) {
    return;
  }
  if (!vm_->GetDroneShowPath()) {
    return;
  }

  const auto points = vm_->GetDronePathPoints();
  if (points.size() < 2U) {
    return;
  }

  constexpr float kMinimapHeight = 180.0F;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float minimap_width = std::max(avail.x, 1.0F);
  const ImVec2 size(minimap_width, kMinimapHeight);

  ImGui::BeginChild("DroneMinimap", size, true, ImGuiWindowFlags_NoScrollbar);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  auto* draw_list = ImGui::GetWindowDrawList();

  float min_x = points[0].x;
  float max_x = points[0].x;
  float min_z = points[0].z;
  float max_z = points[0].z;
  for (const auto& p : points) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_z = std::min(min_z, p.z);
    max_z = std::max(max_z, p.z);
  }

  const float path_width = std::max(max_x - min_x, 0.001F);
  const float height = std::max(max_z - min_z, 0.001F);

  constexpr float kPadding = 22.0F;
  const float scale_x = (size.x - 2.0F * kPadding) / path_width;
  const float scale_y = (size.y - 2.0F * kPadding) / height;
  const float scale = std::min(scale_x, scale_y);

  const float offset_x = origin.x + kPadding
    + 0.5F * (size.x - 2.0F * kPadding - path_width * scale);
  const float offset_y
    = origin.y + kPadding + 0.5F * (size.y - 2.0F * kPadding - height * scale);

  const auto to_minimap = [&](const glm::vec3& p) -> ImVec2 {
    const float x = offset_x + (p.x - min_x) * scale;
    const float y = offset_y + (p.z - min_z) * scale;
    return ImVec2(x, y);
  };

  constexpr ImU32 line_color = IM_COL32(0, 255, 255, 255);
  constexpr float kThickness = 1.5F;
  for (size_t i = 0; i < points.size(); ++i) {
    const size_t j = (i + 1) % points.size();
    draw_list->AddLine(
      to_minimap(points[i]), to_minimap(points[j]), line_color, kThickness);
  }

  const double progress = vm_->GetDroneProgress();
  if (!points.empty()) {
    const double wrapped = std::fmod(progress, 1.0);
    const double safe = wrapped < 0.0 ? wrapped + 1.0 : wrapped;
    const size_t idx
      = static_cast<size_t>(safe * points.size()) % points.size();
    constexpr float kDotRadius = 6.0F;
    draw_list->AddCircleFilled(
      to_minimap(points[idx]), kDotRadius, IM_COL32(255, 255, 0, 255));
  }

  ImGui::EndChild();
}

} // namespace oxygen::examples::ui
