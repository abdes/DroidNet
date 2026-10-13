//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/CameraControlPanel.h"
#include "DemoShell/UI/CameraVm.h"
#include "DemoShell/UI/OrbitCameraController.h"
#include "DemoShell/UI/UiSettingsVm.h"
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

namespace oxygen::examples::ui {

// ImGui takes printf-style text and literal layout metrics.
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, *-magic-numbers)

namespace {

  constexpr std::string_view kMode = "demo.camera.mode";
  constexpr std::string_view kOrbitMode = "demo.camera.orbit_mode";
  constexpr std::string_view kFlySpeed = "demo.camera.fly_speed";
  constexpr std::string_view kDroneRunning = "demo.camera.drone.running";
  constexpr std::string_view kDroneSpeed = "demo.camera.drone.speed";
  constexpr std::string_view kDroneSmoothing = "demo.camera.drone.smoothing";
  constexpr std::string_view kDroneFocusHeight
    = "demo.camera.drone.focus_height";
  constexpr std::string_view kDroneFocusOffset
    = "demo.camera.drone.focus_offset";
  constexpr std::string_view kDroneBobAmplitude
    = "demo.camera.drone.bob_amplitude";
  constexpr std::string_view kDroneBobFrequency
    = "demo.camera.drone.bob_frequency";
  constexpr std::string_view kDroneNoise = "demo.camera.drone.noise";
  constexpr std::string_view kDroneBank = "demo.camera.drone.bank";
  constexpr std::string_view kDronePoiRadius = "demo.camera.drone.poi_radius";
  constexpr std::string_view kDronePoiMinSpeed
    = "demo.camera.drone.poi_min_speed";
  constexpr std::string_view kDroneShowPath = "demo.camera.drone.show_path";
  constexpr std::string_view kProjection = "demo.camera.projection";
  constexpr std::string_view kFov = "demo.camera.fov";
  constexpr std::string_view kOrthoHeight = "demo.camera.ortho_height";
  constexpr std::string_view kAspectMode = "demo.camera.aspect_mode";
  constexpr std::string_view kAspectRatio = "demo.camera.aspect_ratio";
  constexpr std::string_view kNear = "demo.camera.near";
  constexpr std::string_view kFar = "demo.camera.far";
  constexpr std::string_view kReset = "demo.camera.reset";
  constexpr std::string_view kAxes = "demo.camera.axes";

  constexpr auto kModeNames = std::array { "orbit", "fly", "drone" };
  constexpr auto kModes = std::array {
    CameraControlMode::kOrbit,
    CameraControlMode::kFly,
    CameraControlMode::kDrone,
  };

  auto Name(const std::string_view name) -> std::string
  {
    return std::string(name);
  }

  auto RegisterDroneCommands(PanelCommands& commands, observer_ptr<CameraVm> vm)
    -> void
  {
    commands.AddBool(Name(kDroneRunning), "Drone Flight",
      "Fly or pause the drone along its path",
      [vm](const bool v) -> void { vm->SetDroneRunning(v); });
    commands.AddFloat(
      Name(kDroneSpeed), "Drone Speed", "Drone speed in units per second",
      [vm](const float v) -> void { vm->SetDroneSpeed(v); }, 0.5F, 30.0F);
    commands.AddFloat(
      Name(kDroneSmoothing), "Drone Smoothing", "Drone motion damping",
      [vm](const float v) -> void { vm->SetDroneDamping(v); }, 1.0F, 20.0F);
    commands.AddFloat(
      Name(kDroneFocusHeight), "Drone Focus Height",
      "Height of the point the drone looks at",
      [vm](const float v) -> void { vm->SetDroneFocusHeight(v); }, -5.0F,
      15.0F);
    commands.Add({
      .name = Name(kDroneFocusOffset),
      .label = "Drone Focus Offset",
      .help = "Horizontal offset of the point the drone looks at",
      .params = {
        commands::CommandParam {
          .name = "x",
          .type = commands::ParamType::kDouble,
          .help = "X offset",
          .required = true,
        },
        commands::CommandParam {
          .name = "z",
          .type = commands::ParamType::kDouble,
          .help = "Z offset",
          .required = true,
        },
      },
      .handler = [vm](const commands::CommandArgs& args,
                   const commands::CommandContext&) -> commands::CommandResult {
        vm->SetDroneFocusOffset(glm::vec2 {
          static_cast<float>(args.GetDouble("x")),
          static_cast<float>(args.GetDouble("z")),
        });
        return commands::CommandResult::Success();
      },
    });
    commands.AddFloat(
      Name(kDroneBobAmplitude), "Drone Bob Amplitude",
      "Vertical bobbing amplitude",
      [vm](const float v) -> void { vm->SetDroneBobAmplitude(v); }, 0.0F, 0.5F);
    commands.AddFloat(
      Name(kDroneBobFrequency), "Drone Bob Frequency",
      "Vertical bobbing frequency in Hz",
      [vm](const float v) -> void { vm->SetDroneBobFrequency(v); }, 0.1F, 5.0F);
    commands.AddFloat(
      Name(kDroneNoise), "Drone Noise", "Path noise amplitude",
      [vm](const float v) -> void { vm->SetDroneNoiseAmplitude(v); }, 0.0F,
      0.2F);
    commands.AddFloat(
      Name(kDroneBank), "Drone Bank", "Banking into turns",
      [vm](const float v) -> void { vm->SetDroneBankFactor(v); }, 0.0F, 0.2F);
    commands.AddFloat(
      Name(kDronePoiRadius), "Drone POI Radius",
      "Distance from a point of interest where the drone slows down",
      [vm](const float v) -> void { vm->SetDronePOISlowdownRadius(v); }, 1.0F,
      20.0F);
    commands.AddFloat(
      Name(kDronePoiMinSpeed), "Drone POI Min Speed",
      "Speed factor at a point of interest",
      [vm](const float v) -> void { vm->SetDronePOIMinSpeed(v); }, 0.1F, 1.0F);
    commands.AddBool(Name(kDroneShowPath), "Drone Flight Path",
      "Show the drone's flight path and minimap",
      [vm](const bool v) -> void { vm->SetDroneShowPath(v); });
  }

  auto RegisterProjectionCommands(
    PanelCommands& commands, observer_ptr<CameraVm> vm) -> void
  {
    commands.AddEnum(Name(kProjection), "Camera Projection",
      "Projection of the active camera", { "perspective", "orthographic" },
      [vm](const std::size_t i) -> void { vm->SetOrthographic(i == 1U); });
    commands.AddFloat(
      Name(kFov), "Camera Field of View",
      "Vertical field of view of a perspective camera, in degrees",
      [vm](const float v) -> void { vm->SetPerspectiveFovDegrees(v); }, 10.0F,
      160.0F);
    commands.AddFloat(
      Name(kOrthoHeight), "Camera Ortho Size",
      "Visible height of an orthographic camera, in world units",
      [vm](const float v) -> void { vm->SetOrthoHeight(v); }, 0.0F);
    commands.AddEnum(Name(kAspectMode), "Camera Aspect",
      "auto fills the view; fixed keeps the ratio and adds bars",
      { "auto", "fixed" }, [vm](const std::size_t i) -> void {
        vm->SetAspectMode(
          i == 1U ? CameraAspectMode::kFixed : CameraAspectMode::kAuto);
      });
    commands.AddFloat(
      Name(kAspectRatio), "Camera Aspect Ratio",
      "Width over height of a fixed-aspect camera",
      [vm](const float v) -> void { vm->SetAspectRatio(v); }, 0.0F);
    commands.AddFloat(Name(kNear), "Camera Near Plane",
      "Near clipping distance of the active camera",
      [vm](const float v) -> void {
        if (vm->HasPerspectiveCamera()) {
          vm->SetPerspectiveNearPlane(v);
        } else {
          vm->SetOrthoNearPlane(v);
        }
      });
    commands.AddFloat(Name(kFar), "Camera Far Plane",
      "Far clipping distance of the active camera",
      [vm](const float v) -> void {
        if (vm->HasPerspectiveCamera()) {
          vm->SetPerspectiveFarPlane(v);
        } else {
          vm->SetOrthoFarPlane(v);
        }
      });
  }

  auto RegisterCommands(PanelCommands& commands, observer_ptr<CameraVm> vm)
    -> void
  {
    commands.AddEnum(Name(kMode), "Camera Mode", "Camera control mode",
      { kModeNames.begin(), kModeNames.end() },
      [vm](const std::size_t i) -> void { vm->SetControlMode(kModes.at(i)); });
    commands.AddEnum(Name(kOrbitMode), "Orbit Mode",
      "Trackball rotates freely; turntable keeps the horizon level",
      { "trackball", "turntable" }, [vm](const std::size_t i) -> void {
        vm->SetOrbitMode(
          i == 0U ? OrbitMode::kTrackball : OrbitMode::kTurntable);
      });
    commands.AddFloat(
      Name(kFlySpeed), "Fly Speed", "Fly camera speed in units per second",
      [vm](const float v) -> void { vm->SetFlyMoveSpeed(v); }, 0.1F, 1000.0F);
    commands.AddAction(Name(kReset), "Reset Camera",
      "Reset the camera to its initial position and rotation",
      [vm] -> void { vm->RequestReset(); });
    RegisterDroneCommands(commands, vm);
    RegisterProjectionCommands(commands, vm);
  }

  auto ModeName(const CameraControlMode mode) -> std::string_view
  {
    for (std::size_t i = 0; i < kModes.size(); ++i) {
      if (kModes.at(i) == mode) {
        return kModeNames.at(i);
      }
    }
    return kModeNames.front();
  }

} // namespace

CameraControlPanel::CameraControlPanel(observer_ptr<CameraVm> vm,
  observer_ptr<UiSettingsVm> ui_settings, observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , ui_settings_(ui_settings)
  , commands_(commands)
{
  DCHECK_NOTNULL_F(vm_, "CameraControlPanel requires CameraVm");
  DCHECK_NOTNULL_F(ui_settings_, "CameraControlPanel requires UiSettingsVm");
  DCHECK_NOTNULL_F(commands_, "CameraControlPanel requires PanelCommands");
  RegisterCommands(*commands_, vm_);
  commands_->AddBool(std::string(kAxes), "Show Axes",
    "Show the orientation axes in the corner of the window",
    [settings = ui_settings_](
      const bool visible) -> void { settings->SetAxesVisible(visible); });
}

auto CameraControlPanel::DrawContents() -> void
{
  if (ImGui::BeginTabBar("CameraControlTabs")) {
    if (ImGui::BeginTabItem("Camera Mode")) {
      DrawCameraModeTab();
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Orientation")) {
      DrawOrientationTab();
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
    commands_->RunText(kMode, ModeName(CameraControlMode::kOrbit));
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Fly", is_fly)) {
    commands_->RunText(kMode, ModeName(CameraControlMode::kFly));
  }

  if (vm_->IsDroneAvailable()) {
    ImGui::SameLine();
    const bool is_drone = current_mode == CameraControlMode::kDrone;
    if (ImGui::RadioButton("Drone", is_drone)) {
      commands_->RunText(kMode, ModeName(CameraControlMode::kDrone));
    }
  }

  ImGui::Spacing();

  if (current_mode == CameraControlMode::kOrbit) {
    ImGui::SeparatorText("Orbit Settings");

    const auto orbit_mode = vm_->GetOrbitMode();
    const bool is_trackball = orbit_mode == OrbitMode::kTrackball;
    const bool is_turntable = orbit_mode == OrbitMode::kTurntable;

    if (ImGui::RadioButton("Trackball", is_trackball)) {
      commands_->RunText(kOrbitMode, "trackball");
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Turntable", is_turntable)) {
      commands_->RunText(kOrbitMode, "turntable");
    }

    ImGui::Spacing();
    ImGui::TextWrapped("Controls: Hold Right Mouse Button and drag to orbit. "
                       "Mouse wheel to zoom in/out.");

  } else if (current_mode == CameraControlMode::kDrone) {
    ImGui::SeparatorText("Drone Survey");

    // Play/Pause + Progress
    bool running = vm_->GetDroneRunning();
    if (ImGui::Button(running ? "Pause" : "Play")) {
      commands_->RunBool(kDroneRunning, !running);
    }
    ImGui::SameLine();
    ImGui::ProgressBar(static_cast<float>(vm_->GetDroneProgress()),
      ImVec2(-1.0F, 0.0F), "Path Progress");

    ImGui::Spacing();

    // Flight Settings
    float speed = vm_->GetDroneSpeed();
    if (ImGui::SliderFloat("Speed", &speed, 0.5F, 30.0F, "%.1F u/s")) {
      commands_->RunFloat(kDroneSpeed, speed);
    }

    float damping = vm_->GetDroneDamping();
    if (ImGui::SliderFloat("Smoothing", &damping, 1.0F, 20.0F, "%.1F")) {
      commands_->RunFloat(kDroneSmoothing, damping);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Focus Tracking");
    ImGui::Indent();
    float height = vm_->GetDroneFocusHeight();
    if (ImGui::SliderFloat("Height", &height, -5.0F, 15.0F)) {
      commands_->RunFloat(kDroneFocusHeight, height);
    }
    glm::vec2 offset = vm_->GetDroneFocusOffset();
    if (ImGui::DragFloat2("Offset X/Z", &offset.x, 0.1F)) {
      commands_->Run(
        kDroneFocusOffset, { { "x", offset.x }, { "z", offset.y } });
    }
    ImGui::Unindent();

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Cinematics")) {
      float bob_amp = vm_->GetDroneBobAmplitude();
      if (ImGui::SliderFloat("Bob Amp", &bob_amp, 0.0F, 0.5F)) {
        commands_->RunFloat(kDroneBobAmplitude, bob_amp);
      }
      float bob_freq = vm_->GetDroneBobFrequency();
      if (ImGui::SliderFloat("Bob Freq", &bob_freq, 0.1F, 5.0F, "%.1F Hz")) {
        commands_->RunFloat(kDroneBobFrequency, bob_freq);
      }
      float noise = vm_->GetDroneNoiseAmplitude();
      if (ImGui::SliderFloat("Noise", &noise, 0.0F, 0.2F)) {
        commands_->RunFloat(kDroneNoise, noise);
      }
      float bank = vm_->GetDroneBankFactor();
      if (ImGui::SliderFloat("Bank", &bank, 0.0F, 0.2F)) {
        commands_->RunFloat(kDroneBank, bank);
      }
    }

    if (ImGui::CollapsingHeader("POI Slowdown")) {
      float radius = vm_->GetDronePOISlowdownRadius();
      if (ImGui::SliderFloat("Radius", &radius, 1.0F, 20.0F)) {
        commands_->RunFloat(kDronePoiRadius, radius);
      }
      float min_speed = vm_->GetDronePOIMinSpeed();
      if (ImGui::SliderFloat("Min Speed", &min_speed, 0.1F, 1.0F, "%.2Fx")) {
        commands_->RunFloat(kDronePoiMinSpeed, min_speed);
      }
    }

    ImGui::Spacing();
    bool show_path = vm_->GetDroneShowPath();
    if (ImGui::Checkbox("Show flight path", &show_path)) {
      commands_->RunBool(kDroneShowPath, show_path);
    }

    if (show_path) {
      ImGui::Spacing();
      ImGui::SeparatorText("Minimap");
      DrawDroneMinimap();
    }

  } else {
    ImGui::SeparatorText("Fly Settings");

    float speed = vm_->GetFlyMoveSpeed();
    if (ImGui::SliderFloat("Move Speed", &speed, 0.1F, 1000.0F, "%.2F",
          ImGuiSliderFlags_Logarithmic)) {
      commands_->RunFloat(kFlySpeed, speed);
    }

    ImGui::Spacing();
    ImGui::TextWrapped(
      "Controls: WASD to move, Q/E for down/up. "
      "Hold Right Mouse Button and drag to look around. "
      "Hold Shift to boost speed. Hold Space to lock to horizontal plane. "
      "Mouse wheel to adjust speed.");
  }

  ImGui::Spacing();

  ImGui::SeparatorText("Projection");
  if (!vm_->HasActiveCamera()) {
    ImGui::TextDisabled("No active camera");
  } else if (!vm_->HasPerspectiveCamera() && !vm_->HasOrthographicCamera()) {
    ImGui::TextDisabled("Active camera has no projection component");
  } else {
    DrawProjectionSettings();
  }

  ImGui::SeparatorText("Actions");

  if (ImGui::Button("Reset Camera Position", ImVec2(-1, 0))) {
    commands_->Run(kReset);
  }

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Reset camera to initial position and rotation");
  }
}

void CameraControlPanel::DrawProjectionSettings()
{
  struct RatioPreset {
    const char* label;
    float ratio;
  };
  constexpr std::array kRatioPresets {
    RatioPreset { .label = "16:9", .ratio = 16.0F / 9.0F },
    RatioPreset { .label = "4:3", .ratio = 4.0F / 3.0F },
    RatioPreset { .label = "21:9", .ratio = 21.0F / 9.0F },
    RatioPreset { .label = "1:1", .ratio = 1.0F },
    RatioPreset { .label = "9:16", .ratio = 9.0F / 16.0F },
  };
  constexpr std::array kProjectionItems { "Perspective", "Orthographic" };
  constexpr std::array kAspectItems { "Auto", "Fixed" };

  ImGui::PushID("Projection");
  ImGui::Indent();

  int projection = vm_->HasOrthographicCamera() ? 1 : 0;
  if (ImGui::Combo("Type", &projection, kProjectionItems.data(),
        static_cast<int>(kProjectionItems.size()))) {
    commands_->RunText(
      kProjection, projection == 1 ? "orthographic" : "perspective");
  }

  if (vm_->HasPerspectiveCamera()) {
    float fov_deg = vm_->GetPerspectiveFovDegrees();
    if (ImGui::SliderFloat("FOV", &fov_deg, 10.0F, 160.0F, "%.1F deg")) {
      commands_->RunFloat(kFov, fov_deg);
    }
  } else {
    float height = vm_->GetOrthoHeight();
    if (ImGui::InputFloat("Size", &height, 0.1F, 1.0F, "%.3F")) {
      commands_->RunFloat(kOrthoHeight, height);
    }
    ImGui::SetItemTooltip("Visible height in world units");
  }

  // Auto fills the view; Fixed keeps the ratio and adds bars as needed.
  const auto mode = vm_->GetAspectMode();
  int mode_index = mode == CameraAspectMode::kFixed ? 1 : 0;
  if (ImGui::Combo("Aspect", &mode_index, kAspectItems.data(),
        static_cast<int>(kAspectItems.size()))) {
    commands_->RunText(kAspectMode, mode_index == 1 ? "fixed" : "auto");
  }
  if (mode == CameraAspectMode::kFixed) {
    float ratio = vm_->GetAspectRatio();
    const auto* current = "Custom";
    for (const auto& preset : kRatioPresets) {
      if (std::abs(preset.ratio - ratio) < 1.0e-3F) {
        current = preset.label;
      }
    }
    if (ImGui::BeginCombo("Ratio", current)) {
      for (const auto& preset : kRatioPresets) {
        if (ImGui::Selectable(preset.label, current == preset.label)) {
          commands_->RunFloat(kAspectRatio, preset.ratio);
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::InputFloat("Width / Height", &ratio, 0.01F, 0.1F, "%.4F")) {
      commands_->RunFloat(kAspectRatio, ratio);
    }
  }

  float near_plane = vm_->HasPerspectiveCamera()
    ? vm_->GetPerspectiveNearPlane()
    : vm_->GetOrthoNearPlane();
  if (ImGui::InputFloat("Near", &near_plane, 0.01F, 0.1F, "%.4F")) {
    commands_->RunFloat(kNear, near_plane);
  }
  float far_plane = vm_->HasPerspectiveCamera() ? vm_->GetPerspectiveFarPlane()
                                                : vm_->GetOrthoFarPlane();
  if (ImGui::InputFloat("Far", &far_plane, 1.0F, 10.0F, "%.2F")) {
    commands_->RunFloat(kFar, far_plane);
  }

  ImGui::Unindent();
  ImGui::PopID();
}

void CameraControlPanel::DrawOrientationTab()
{
  bool axes = ui_settings_->GetAxesVisible();
  if (ImGui::Checkbox("Show Axes", &axes)) {
    commands_->RunBool(kAxes, axes);
  }
  DrawCameraPoseInfo();
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
  const glm::vec3 forward = rotation * space::look::Forward;
  const glm::vec3 up = rotation * space::look::Up;
  const glm::vec3 right = rotation * space::look::Right;

  ImGui::PushID("CameraPoseTable");
  if (ImGui::BeginTable(
        "##CameraPoseTable", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 160.0F);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    constexpr float kValueFieldWidth = 240.0F;
    const auto right_align = [] -> void {
      const float start = ImGui::GetCursorPosX();
      const float col_width = ImGui::GetColumnWidth();
      const float offset = std::max(0.0F, col_width - kValueFieldWidth);
      ImGui::SetCursorPosX(start + offset);
    };

    auto row_vec3
      = [right_align](const char* label, const glm::vec3& value) -> void {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      std::array data { value.x, value.y, value.z };
      ImGui::BeginDisabled();
      const std::string id = std::string("##") + label;
      right_align();
      ImGui::SetNextItemWidth(kValueFieldWidth);
      ImGui::InputFloat3(
        id.c_str(), data.data(), "%.3F", ImGuiInputTextFlags_ReadOnly);
      ImGui::EndDisabled();
    };

    row_vec3("Position", position);
    row_vec3("Forward", forward);
    row_vec3("Up", up);
    row_vec3("Right", right);

    ImGui::EndTable();
  }
  ImGui::PopID();
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

  ImGui::BeginChild("DroneMinimap", size, 1, ImGuiWindowFlags_NoScrollbar);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  auto* draw_list = ImGui::GetWindowDrawList();

  float min_x = points.front().x;
  float max_x = points.front().x;
  float min_y = points.front().y;
  float max_y = points.front().y;
  for (const auto& p : points) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }

  const float path_width = std::max(max_x - min_x, 0.001F);
  const float height = std::max(max_y - min_y, 0.001F);

  constexpr float kPadding = 22.0F;
  const float scale_x = (size.x - (2.0F * kPadding)) / path_width;
  const float scale_y = (size.y - (2.0F * kPadding)) / height;
  const float scale = std::min(scale_x, scale_y);

  const float offset_x = origin.x + kPadding
    + (0.5F * (size.x - (2.0F * kPadding) - (path_width * scale)));
  const float offset_y = origin.y + kPadding
    + (0.5F * (size.y - (2.0F * kPadding) - (height * scale)));

  const auto to_minimap = [&](const glm::vec3& p) -> ImVec2 {
    const float x = offset_x + ((p.x - min_x) * scale);
    const float y = offset_y + ((p.y - min_y) * scale);
    return { x, y };
  };

  constexpr ImU32 line_color = IM_COL32(0, 255, 255, 255);
  constexpr float kThickness = 1.5F;
  // Closed loop: the last point joins the first.
  auto previous = points.back();
  for (const auto& point : points) {
    draw_list->AddLine(
      to_minimap(previous), to_minimap(point), line_color, kThickness);
    previous = point;
  }

  const double progress = vm_->GetDroneProgress();
  if (!points.empty()) {
    const double wrapped = std::fmod(progress, 1.0);
    const double safe = wrapped < 0.0 ? wrapped + 1.0 : wrapped;
    const auto idx
      = static_cast<std::size_t>(safe * static_cast<double>(points.size()))
      % points.size();
    constexpr float kDotRadius = 6.0F;
    draw_list->AddCircleFilled(to_minimap(points.subspan(idx, 1).front()),
      kDotRadius, IM_COL32(255, 255, 0, 255));
  }

  ImGui::EndChild();
}

// NOLINTEND(cppcoreguidelines-pro-type-vararg, *-magic-numbers)

} // namespace oxygen::examples::ui
