//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "DemoShell/PanelCommands.h"
#include "LightBench/LightBenchPanel.h"
#include "LightBench/LightBenchSettings.h"
#include "LightBench/LightScene.h"
#include "LightBench/ReferenceScene.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Styles/Spectrum.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise) -
// ImGui

namespace oxygen::examples::light_bench {

namespace {
  constexpr Vec3 kAxisColorX { 1.0F, 0.2F, 0.2F };
  constexpr Vec3 kAxisColorY { 0.2F, 1.0F, 0.2F };
  constexpr Vec3 kAxisColorZ { 0.2F, 0.4F, 1.0F };

  // Owned by LightBenchConsoleBindings.
  constexpr std::string_view kPreset = "lightbench.preset";
  constexpr std::string_view kReset = "lightbench.reset";

  constexpr std::string_view kSave = "lightbench.save";
  constexpr std::string_view kLoad = "lightbench.load";
  constexpr std::string_view kObjectEnabled = "lightbench.object.enabled";
  constexpr std::string_view kObjectPosition = "lightbench.object.position";
  constexpr std::string_view kObjectRotation = "lightbench.object.rotation";
  constexpr std::string_view kObjectScale = "lightbench.object.scale";
  constexpr std::string_view kObjectReset = "lightbench.object.reset";

  //! A scene object: its command value, its label and its state.
  struct SceneObject {
    std::string_view id;
    std::string_view label;
    LightScene::SceneObjectState& (LightScene::*state)();
    bool allow_rotation;
  };
  constexpr auto kSceneObjects = std::array {
    SceneObject {
      .id = "gray_card",
      .label = "18% Gray Card",
      .state = &LightScene::GetGrayCardState,
      .allow_rotation = true,
    },
    SceneObject {
      .id = "white_card",
      .label = "White Card",
      .state = &LightScene::GetWhiteCardState,
      .allow_rotation = true,
    },
    SceneObject {
      .id = "black_card",
      .label = "Black Card",
      .state = &LightScene::GetBlackCardState,
      .allow_rotation = true,
    },
    SceneObject {
      .id = "matte_sphere",
      .label = "Matte Sphere",
      .state = &LightScene::GetMatteSphereState,
      .allow_rotation = false,
    },
    SceneObject {
      .id = "glossy_sphere",
      .label = "Glossy Sphere",
      .state = &LightScene::GetGlossySphereState,
      .allow_rotation = false,
    },
    SceneObject {
      .id = "ground_plane",
      .label = "Ground Plane",
      .state = &LightScene::GetGroundPlaneState,
      .allow_rotation = false,
    },
  };

  auto Name(const std::string_view name) -> std::string
  {
    return std::string(name);
  }

  auto Number(std::string name, std::string help) -> commands::CommandParam
  {
    return commands::CommandParam {
      .name = std::move(name),
      .type = commands::ParamType::kDouble,
      .help = std::move(help),
      .required = true,
    };
  }

  auto ObjectParam() -> commands::CommandParam
  {
    commands::CommandParam param {
      .name = "object",
      .type = commands::ParamType::kEnum,
      .help = "Scene object",
      .required = true,
    };
    for (const auto& object : kSceneObjects) {
      param.allowed_values.emplace_back(object.id);
    }
    return param;
  }

  auto FindObject(const std::string_view id) -> const SceneObject&
  {
    for (const auto& object : kSceneObjects) {
      if (object.id == id) {
        return object;
      }
    }
    return kSceneObjects.front();
  }

  auto ToVec3(const commands::CommandArgs& args) -> Vec3
  {
    return Vec3 {
      static_cast<float>(args.GetDouble("x")),
      static_cast<float>(args.GetDouble("y")),
      static_cast<float>(args.GetDouble("z")),
    };
  }

  //! Registers `name object x y z` writing the vector to `member`.
  auto AddObjectVector(PanelCommands& commands, LightScene& scene,
    const std::string_view name, std::string label, std::string help,
    Vec3 LightScene::SceneObjectState::* member) -> void
  {
    commands.Add({
      .name = Name(name),
      .label = std::move(label),
      .help = std::move(help),
      .params
      = { ObjectParam(), Number("x", "X"), Number("y", "Y"), Number("z", "Z") },
      .handler = [&scene, member](const commands::CommandArgs& args,
                   const commands::CommandContext&) -> commands::CommandResult {
        const auto& object = FindObject(args.GetString("object"));
        (scene.*object.state)().*member = ToVec3(args);
        return commands::CommandResult::Success();
      },
    });
  }

  auto RunObjectVector(const PanelCommands& commands,
    const std::string_view name, const std::string_view object,
    const Vec3& value) -> void
  {
    commands.Run(name,
      {
        { "object", object },
        { "x", value.x },
        { "y", value.y },
        { "z", value.z },
      });
  }

  //! A drag field for one axis, behind a bar of the axis color.
  auto AxisFloatCell(const std::string& id, const Vec3& color, float& value,
    const float speed, const float min_value, const float max_value) -> bool
  {
    const float height = ImGui::GetFrameHeight();
    const float rect_width = 6.0F;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 rect_color
      = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0F));
    draw_list->AddRectFilled(cursor,
      ImVec2(cursor.x + rect_width, cursor.y + height), rect_color, 2.0F);

    ImGui::SetCursorScreenPos(ImVec2(cursor.x + rect_width + 6.0F, cursor.y));
    ImGui::PushItemWidth(-1.0F);
    const bool changed = ImGui::DragFloat(id.c_str(), &value, speed, min_value,
      max_value, "%.3F", ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopItemWidth();
    return changed;
  }

  //! A labelled row of three axis fields; true when `value` changed.
  auto Vector3Table(const std::string& id, const char* label, Vec3& value,
    const float speed, const float min_value, const float max_value) -> bool
  {
    constexpr ImGuiTableFlags kFlags
      = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable(id.c_str(), 4, kFlags)) {
      return false;
    }
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0F);
    ImGui::TableSetupColumn("X");
    ImGui::TableSetupColumn("Y");
    ImGui::TableSetupColumn("Z");
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    bool changed = false;
    ImGui::TableSetColumnIndex(1);
    changed |= AxisFloatCell(
      id + "_x", kAxisColorX, value.x, speed, min_value, max_value);
    ImGui::TableSetColumnIndex(2);
    changed |= AxisFloatCell(
      id + "_y", kAxisColorY, value.y, speed, min_value, max_value);
    ImGui::TableSetColumnIndex(3);
    changed |= AxisFloatCell(
      id + "_z", kAxisColorZ, value.z, speed, min_value, max_value);
    ImGui::EndTable();
    return changed;
  }

  auto PresetButton(const char* label, bool selected, float width) -> bool
  {
    ImGui::PushStyleColor(ImGuiCol_Button,
      ImGui::GetStyleColorVec4(selected ? ImGuiCol_Header : ImGuiCol_Button));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
      ImGui::GetStyleColorVec4(
        selected ? ImGuiCol_HeaderHovered : ImGuiCol_ButtonHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
      ImGui::GetStyleColorVec4(
        selected ? ImGuiCol_HeaderActive : ImGuiCol_ButtonActive));
    const auto clicked = ImGui::Button(label, ImVec2 { width, 0 });
    ImGui::PopStyleColor(3);
    return clicked;
  }
} // namespace

LightBenchPanel::LightBenchPanel(observer_ptr<LightScene> light_scene,
  Actions actions, const std::filesystem::path& settings_path,
  observer_ptr<PanelCommands> commands)
  : actions_(std::move(actions))
  , light_scene_(light_scene)
  , commands_(commands)
  , icon_(std::string(imgui::icons::kIconDemoPanel) + "##LightBench")
{
  CHECK_NOTNULL_F(light_scene_, "LightBenchPanel requires a LightScene");
  CHECK_NOTNULL_F(commands_, "LightBenchPanel requires PanelCommands");
  const auto text = settings_path.string();
  if (text.size() >= settings_path_.size()) {
    throw std::length_error("LightBench settings path is too long");
  }
  std::ranges::copy(text, settings_path_.begin());
  RegisterCommands();
}

auto LightBenchPanel::RegisterCommands() -> void
{
  auto& commands = *commands_;
  auto& scene = *light_scene_;
  const auto path_param = commands::CommandParam {
    .name = "path",
    .type = commands::ParamType::kPath,
    .help = "Settings file",
    .required = true,
  };
  commands.Add({
    .name = Name(kSave),
    .label = "Save LightBench Settings",
    .help = "Save the current LightBench settings to a file",
    .params = { path_param },
    .handler = [this](const commands::CommandArgs& args,
                 const commands::CommandContext&) -> commands::CommandResult {
      const auto result = actions_.save(args.GetString("path"));
      file_status_ = result ? "Settings saved." : result.error();
      return result ? commands::CommandResult::Success(file_status_)
                    : commands::CommandResult::Failure(
                        commands::ExecutionStatus::kError, file_status_);
    },
  });
  commands.Add({
    .name = Name(kLoad),
    .label = "Load LightBench Settings",
    .help = "Validate a settings file and apply it at the next frame",
    .params = { path_param },
    .handler = [this](const commands::CommandArgs& args,
                 const commands::CommandContext&) -> commands::CommandResult {
      const auto result = actions_.load(args.GetString("path"));
      file_status_
        = result ? "Settings validated; applying next frame." : result.error();
      return result
        ? commands::CommandResult::Success(file_status_)
        : commands::CommandResult::Failure(
            commands::ExecutionStatus::kInvalidArguments, file_status_);
    },
  });
  commands.Add({
    .name = Name(kObjectEnabled),
    .label = "Show Scene Object",
    .help = "Show or hide a LightBench scene object",
    .params = { ObjectParam(),
      commands::CommandParam {
        .name = "value",
        .type = commands::ParamType::kBool,
        .help = "true or false",
        .required = true,
      }, },
    .handler = [&scene](const commands::CommandArgs& args,
                 const commands::CommandContext&) -> commands::CommandResult {
      const auto& object = FindObject(args.GetString("object"));
      (scene.*object.state)().enabled = args.GetBool("value");
      return commands::CommandResult::Success();
    },
  });
  AddObjectVector(commands, scene, kObjectPosition, "Scene Object Position",
    "Position of a LightBench scene object",
    &LightScene::SceneObjectState::position);
  AddObjectVector(commands, scene, kObjectRotation, "Scene Object Rotation",
    "Rotation of a LightBench card, in degrees",
    &LightScene::SceneObjectState::rotation_deg);
  AddObjectVector(commands, scene, kObjectScale, "Scene Object Scale",
    "Scale of a LightBench scene object", &LightScene::SceneObjectState::scale);
  commands.Add({
    .name = Name(kObjectReset),
    .label = "Reset Scene Object",
    .help = "Put a LightBench scene object back where its preset has it",
    .params = { ObjectParam() },
    .handler = [&scene](const commands::CommandArgs& args,
                 const commands::CommandContext&) -> commands::CommandResult {
      scene.ResetSceneObject(FindObject(args.GetString("object")).label);
      return commands::CommandResult::Success();
    },
  });

  // Directional light.
  commands.AddBool("lightbench.directional.enabled", "Directional Light",
    "Turn the directional light on or off", [&scene](const bool v) -> void {
      scene.GetDirectionalLightState().enabled = v;
    });
  commands.AddFloat(
    "lightbench.directional.lux", "Directional Illuminance",
    "Illuminance of the directional light, in lux",
    [&scene](const float v) -> void {
      scene.GetDirectionalLightState().illuminance_lux = v;
    },
    0.0F, 200000.0F);
  commands.AddRgb("lightbench.directional.color", "Directional Color",
    "Color of the directional light", [&scene](const Vec3& v) -> void {
      scene.GetDirectionalLightState().color_rgb = v;
    });
  commands.AddVec3("lightbench.directional.direction", "Directional Direction",
    "World direction the directional light's rays travel",
    [&scene](const Vec3& v) -> void {
      scene.GetDirectionalLightState().direction_ws = v;
    });
  commands.AddBool("lightbench.directional.shadows", "Directional Shadows",
    "Whether the directional light casts shadows",
    [&scene](const bool v) -> void {
      scene.GetDirectionalLightState().casts_shadows = v;
    });

  // Point light.
  commands.AddBool("lightbench.point.enabled", "Point Light",
    "Turn the point light on or off",
    [&scene](const bool v) -> void { scene.GetPointLightState().enabled = v; });
  commands.AddBool("lightbench.point.shadows", "Point Shadows",
    "Whether the point light casts shadows", [&scene](const bool v) -> void {
      scene.GetPointLightState().casts_shadows = v;
    });
  commands.AddVec3("lightbench.point.position", "Point Position",
    "Position of the point light", [&scene](const Vec3& v) -> void {
      scene.GetPointLightState().position = v;
    });
  commands.AddRgb("lightbench.point.color", "Point Color",
    "Color of the point light", [&scene](const Vec3& v) -> void {
      scene.GetPointLightState().color_rgb = v;
    });
  commands.AddFloat(
    "lightbench.point.lumens", "Point Intensity",
    "Luminous power of the point light, in lumens",
    [&scene](
      const float v) -> void { scene.GetPointLightState().intensity = v; },
    0.0F, 200000.0F);
  commands.AddFloat(
    "lightbench.point.range", "Point Range",
    "Range of the point light, in meters",
    [&scene](const float v) -> void { scene.GetPointLightState().range = v; },
    0.1F, 500.0F);
  commands.AddFloat(
    "lightbench.point.radius", "Point Radius",
    "Source radius of the point light, in meters",
    [&scene](
      const float v) -> void { scene.GetPointLightState().source_radius = v; },
    0.0F, 10.0F);

  // Spot light.
  commands.AddBool("lightbench.spot.enabled", "Spot Light",
    "Turn the spot light on or off",
    [&scene](const bool v) -> void { scene.GetSpotLightState().enabled = v; });
  commands.AddBool("lightbench.spot.shadows", "Spot Shadows",
    "Whether the spot light casts shadows", [&scene](const bool v) -> void {
      scene.GetSpotLightState().casts_shadows = v;
    });
  commands.AddVec3("lightbench.spot.position", "Spot Position",
    "Position of the spot light", [&scene](const Vec3& v) -> void {
      scene.GetSpotLightState().position = v;
    });
  commands.AddVec3("lightbench.spot.direction", "Spot Direction",
    "World direction of the spot light", [&scene](const Vec3& v) -> void {
      scene.GetSpotLightState().direction_ws = v;
    });
  commands.AddRgb("lightbench.spot.color", "Spot Color",
    "Color of the spot light", [&scene](const Vec3& v) -> void {
      scene.GetSpotLightState().color_rgb = v;
    });
  commands.AddFloat(
    "lightbench.spot.lumens", "Spot Intensity",
    "Luminous power of the spot light, in lumens",
    [&scene](
      const float v) -> void { scene.GetSpotLightState().intensity = v; },
    0.0F, 200000.0F);
  commands.AddFloat(
    "lightbench.spot.range", "Spot Range", "Range of the spot light, in meters",
    [&scene](const float v) -> void { scene.GetSpotLightState().range = v; },
    0.1F, 500.0F);
  commands.AddFloat(
    "lightbench.spot.inner", "Spot Inner Angle",
    "Inner cone angle of the spot light, in degrees",
    [&scene](const float v) -> void {
      auto& spot = scene.GetSpotLightState();
      spot.inner_angle_deg = v;
      spot.outer_angle_deg = (std::max)(spot.outer_angle_deg, v);
    },
    0.0F, 89.0F);
  commands.AddFloat(
    "lightbench.spot.outer", "Spot Outer Angle",
    "Outer cone angle of the spot light, in degrees; at least the inner one",
    [&scene](const float v) -> void {
      auto& spot = scene.GetSpotLightState();
      spot.outer_angle_deg = (std::max)(v, spot.inner_angle_deg);
    },
    0.1F, 89.9F);
  commands.AddFloat(
    "lightbench.spot.radius", "Spot Radius",
    "Source radius of the spot light, in meters",
    [&scene](
      const float v) -> void { scene.GetSpotLightState().source_radius = v; },
    0.0F, 10.0F);
}

auto LightBenchPanel::DrawPresetOverlay(const float top_inset) -> void
{
  if (!light_scene_) {
    return;
  }
  const auto* viewport = ImGui::GetMainViewport();
  const auto& style = ImGui::GetStyle();
  const auto font_size = ImGui::GetFontSize();
  const auto padding = std::ceil(font_size * .6F);
  const auto margin = std::ceil(font_size * .8F);
  const auto button_width = [&style](const char* label) {
    return ImGui::CalcTextSize(label).x + (2.0F * style.FramePadding.x);
  };
  float label_width = 0;
  for (const auto& preset : GetPresets()) {
    label_width = std::max(label_width, ImGui::CalcTextSize(preset.name).x);
  }
  const auto reset_width = button_width("Reset");
  const auto selector_width
    = label_width + ImGui::GetFrameHeight() + (2.0F * style.FramePadding.x);
  const auto step_width = std::max(
    { button_width("Dim"), button_width("Normal"), button_width("Bright") });
  const auto steps_width = (3.0F * step_width) + (2.0F * style.ItemSpacing.x);
  const auto desired_width = (2.0F * padding)
    + std::max(selector_width + reset_width + style.ItemSpacing.x, steps_width);
  const auto width = std::min(
    desired_width, std::max(1.0F, viewport->Size.x - (2.0F * margin)));
  ImGui::SetNextWindowPos(ImVec2 { viewport->Pos.x + (viewport->Size.x * .5F),
                            viewport->Pos.y + top_inset + margin },
    ImGuiCond_Always, ImVec2 { .5F, 0 });
  ImGui::SetNextWindowSize(ImVec2 { width, 0 }, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(.8F);
  const auto modified = !actions_.is_reference();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, modified ? 2.0F : 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, font_size * .5F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2 { padding, padding });
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, font_size * .22F);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
  ImGui::PushStyleColor(ImGuiCol_Border,
    ImGui::ColorConvertU32ToFloat4(imgui::spectrum::Static::kOrange400));
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
    | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
    | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
  const auto visible = ImGui::Begin("##LightBenchPresets", nullptr, flags);
  // Begin draws this window's border. Restore the normal color before any
  // popup or child control is drawn; only the preset panel signals dirtiness.
  ImGui::PopStyleColor();
  if (!visible) {
    ImGui::End();
    ImGui::PopStyleVar(5);
    return;
  }
  const auto selected = actions_.selected();
  const auto& info = GetPresetInfo(selected);
  ImGui::SetNextItemWidth(std::max(1.0F,
    ImGui::GetContentRegionAvail().x - reset_width - style.ItemSpacing.x));
  const auto popup_height = std::ceil((2.0F * style.WindowPadding.y)
    + (ImGui::GetTextLineHeightWithSpacing()
      * static_cast<float>(GetPresets().size()))
    + (2.0F * style.PopupBorderSize));
  ImGui::SetNextWindowSizeConstraints(
    ImVec2 { 0, popup_height }, ImVec2 { FLT_MAX, FLT_MAX });
  if (ImGui::BeginCombo(
        "##Scenario", info.name, ImGuiComboFlags_HeightLargest)) {
    for (const auto& preset : GetPresets()) {
      const auto is_selected = preset.preset == selected;
      if (ImGui::Selectable(preset.name, is_selected)) {
        commands_->Run(kPreset, { { "id", preset.id } });
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset", ImVec2 { reset_width, 0 })) {
    commands_->Run(kReset);
    file_status_.clear();
  }
  DrawPresetControls(std::max(1.0F,
    (ImGui::GetWindowWidth() - (2.0F * padding) - (2.0F * style.ItemSpacing.x))
      / 3.0F));
  ImGui::End();
  ImGui::PopStyleVar(5);
}

auto LightBenchPanel::DrawContents() -> void
{
  if (!light_scene_) {
    return;
  }
  const auto selected = actions_.selected();
  if (selected == LightBenchPreset::kNeutralReference) {
    ImGui::SeparatorText("Reference (cd/m2)");
    if (ImGui::BeginTable(
          "ReferenceValues", 3, ImGuiTableFlags_SizingStretchSame)) {
      constexpr std::array names { "18% Gray", "90% White", "2% Black" };
      for (std::size_t i = 0; i < names.size(); ++i) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(names.at(i));
        ImGui::Text("%.3f", reference::kExpectedLuminance.at(i));
      }
      ImGui::EndTable();
    }
  }
  if (ImGui::CollapsingHeader("Save / Load settings")) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText(
      "##settings_path", settings_path_.data(), settings_path_.size());
    if (ImGui::Button("Save settings")) {
      commands_->Run(
        kSave, { { "path", std::string_view(settings_path_.data()) } });
    }
    ImGui::SameLine();
    if (ImGui::Button("Load settings")) {
      commands_->Run(
        kLoad, { { "path", std::string_view(settings_path_.data()) } });
    }
    if (!file_status_.empty()) {
      ImGui::TextWrapped("%s", file_status_.c_str());
    }
  }
  ImGui::Separator();
  if (ImGui::CollapsingHeader("Geometry (advanced)")) {
    DrawSceneSection();
  }

  if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawLightsSection();
  }
}

auto LightBenchPanel::DrawSceneSection() -> void { DrawSceneAdvancedSection(); }

auto LightBenchPanel::DrawPresetControls(const float button_width) -> void
{
  if (actions_.selected() == LightBenchPreset::kPointFalloff) {
    for (const auto distance : { 1.0F, 2.0F, 4.0F }) {
      const auto label = std::to_string(static_cast<int>(distance)) + " m";
      const auto selected = light_scene_->GetPointLightState().position
        == Vec3 { 0.0F, distance, 1.0F };
      if (PresetButton(label.c_str(), selected, button_width)) {
        commands_->RunVec3(
          "lightbench.point.position", Vec3 { 0.0F, distance, 1.0F });
      }
      if (distance != 4.0F) {
        ImGui::SameLine();
      }
    }

  } else if (actions_.selected() == LightBenchPreset::kAutoAdaptation) {
    constexpr std::array steps {
      std::pair { "Dim", 100.0F },
      std::pair { "Normal", 1000.0F },
      std::pair { "Bright", 10000.0F },
    };
    for (const auto& [label, lux] : steps) {
      const auto current_lux
        = light_scene_->GetDirectionalLightState().illuminance_lux;
      if (PresetButton(label, current_lux == lux, button_width)) {
        commands_->RunFloat("lightbench.directional.lux", lux);
      }
      if (lux != steps.back().second) {
        ImGui::SameLine();
      }
    }
  }
}

auto LightBenchPanel::DrawSceneObjectControls(const SceneObjectHeading& heading,
  const LightScene::SceneObjectState& state, const bool allow_rotation) -> void
{
  const std::string_view object = heading.id;
  const std::string_view label = heading.label;
  ImGui::Text("%s", std::string(label).c_str());
  ImGui::Indent();
  const std::string id(label);
  bool enabled = state.enabled;
  if (ImGui::Checkbox(("Enabled##" + id).c_str(), &enabled)) {
    commands_->Run(
      kObjectEnabled, { { "object", object }, { "value", enabled } });
  }
  ImGui::SameLine();
  if (ImGui::Button(("Reset##" + id).c_str())) {
    commands_->Run(kObjectReset, { { "object", object } });
  }
  auto position = state.position;
  if (Vector3Table(
        id + "_pos", "Position", position, 0.05F, -100000.0F, 100000.0F)) {
    RunObjectVector(*commands_, kObjectPosition, object, position);
  }
  if (allow_rotation) {
    auto rotation = state.rotation_deg;
    if (Vector3Table(
          id + "_rot", "Rotation (deg)", rotation, 0.5F, -360.0F, 360.0F)) {
      RunObjectVector(*commands_, kObjectRotation, object, rotation);
    }
  }
  auto scale = state.scale;
  if (Vector3Table(id + "_scl", "Scale", scale, 0.05F, 0.01F, 100.0F)) {
    RunObjectVector(*commands_, kObjectScale, object, scale);
  }
  ImGui::Unindent();
}

auto LightBenchPanel::DrawSceneAdvancedSection() -> void
{
  bool first = true;
  for (const auto& object : kSceneObjects) {
    if (!first) {
      ImGui::Spacing();
    }
    first = false;
    DrawSceneObjectControls({ .id = object.id, .label = object.label },
      ((*light_scene_).*object.state)(), object.allow_rotation);
  }
}

auto LightBenchPanel::DrawLightsSection() -> void
{
  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * .52F);
  const std::array controls {
    std::pair { light_scene_->GetDirectionalLightState().enabled,
      &LightBenchPanel::DrawDirectionalLightControls },
    std::pair { light_scene_->GetPointLightState().enabled,
      &LightBenchPanel::DrawPointLightControls },
    std::pair { light_scene_->GetSpotLightState().enabled,
      &LightBenchPanel::DrawSpotLightControls },
  };
  // Keep every control accessible; present the active sources first.
  for (const bool active : { true, false }) {
    for (const auto& [enabled, draw] : controls) {
      if (enabled != active) {
        continue;
      }
      (this->*draw)();
      ImGui::Spacing();
      ImGui::Separator();
    }
  }
  ImGui::PopItemWidth();
}

auto LightBenchPanel::DrawDirectionalLightControls() -> void
{
  ImGui::TextUnformatted("Directional Light");
  auto key = light_scene_->GetDirectionalLightState();
  if (ImGui::Checkbox("Enabled##directional", &key.enabled)) {
    commands_->RunBool("lightbench.directional.enabled", key.enabled);
  }
  if (ImGui::DragFloat("Lux", &key.illuminance_lux, 10.0F, 0.0F, 200000.0F,
        "%.1f", ImGuiSliderFlags_AlwaysClamp)) {
    commands_->RunFloat("lightbench.directional.lux", key.illuminance_lux);
  }
  if (ImGui::ColorEdit3("Color##directional", &key.color_rgb.x)) {
    commands_->RunRgb("lightbench.directional.color", key.color_rgb);
  }
  if (ImGui::DragFloat3(
        "Ray direction##directional", &key.direction_ws.x, .01F, -1.0F, 1.0F)) {
    commands_->RunVec3("lightbench.directional.direction", key.direction_ws);
  }
  if (ImGui::Checkbox("Cast shadows##directional", &key.casts_shadows)) {
    commands_->RunBool("lightbench.directional.shadows", key.casts_shadows);
  }
}

auto LightBenchPanel::DrawPointLightControls() -> void
{
  auto point = light_scene_->GetPointLightState();

  ImGui::Text("Point Light");
  ImGui::Indent();
  if (ImGui::Checkbox("Enabled##point", &point.enabled)) {
    commands_->RunBool("lightbench.point.enabled", point.enabled);
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Shadows##point", &point.casts_shadows)) {
    commands_->RunBool("lightbench.point.shadows", point.casts_shadows);
  }
  if (ImGui::DragFloat3("Position##point", &point.position.x, 0.1F)) {
    commands_->RunVec3("lightbench.point.position", point.position);
  }
  if (ImGui::ColorEdit3("Color##point", &point.color_rgb.x)) {
    commands_->RunRgb("lightbench.point.color", point.color_rgb);
  }
  if (ImGui::DragFloat("Lumens##point", &point.intensity, 1.0F, 0.0F, 200000.0F,
        "%.2F", ImGuiSliderFlags_Logarithmic)) {
    commands_->RunFloat("lightbench.point.lumens", point.intensity);
  }
  if (ImGui::DragFloat("Range (m)##point", &point.range, 0.1F, 0.1F, 500.0F)) {
    commands_->RunFloat(
      "lightbench.point.range", (std::max)(point.range, 0.1F));
  }
  if (ImGui::DragFloat(
        "Radius (m)##point", &point.source_radius, 0.01F, 0.0F, 10.0F)) {
    commands_->RunFloat("lightbench.point.radius", point.source_radius);
  }
  ImGui::Unindent();
}

auto LightBenchPanel::DrawSpotLightControls() -> void
{
  auto spot = light_scene_->GetSpotLightState();

  ImGui::Text("Spot Light");
  ImGui::Indent();
  if (ImGui::Checkbox("Enabled##spot", &spot.enabled)) {
    commands_->RunBool("lightbench.spot.enabled", spot.enabled);
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Shadows##spot", &spot.casts_shadows)) {
    commands_->RunBool("lightbench.spot.shadows", spot.casts_shadows);
  }
  if (ImGui::DragFloat3("Position##spot", &spot.position.x, 0.1F)) {
    commands_->RunVec3("lightbench.spot.position", spot.position);
  }
  if (ImGui::DragFloat3("Direction##spot", &spot.direction_ws.x, 0.05F)) {
    commands_->RunVec3("lightbench.spot.direction", spot.direction_ws);
  }
  if (ImGui::ColorEdit3("Color##spot", &spot.color_rgb.x)) {
    commands_->RunRgb("lightbench.spot.color", spot.color_rgb);
  }
  if (ImGui::DragFloat("Lumens##spot", &spot.intensity, 1.0F, 0.0F, 200000.0F,
        "%.2F", ImGuiSliderFlags_Logarithmic)) {
    commands_->RunFloat("lightbench.spot.lumens", spot.intensity);
  }
  if (ImGui::DragFloat("Range (m)##spot", &spot.range, 0.1F, 0.1F, 500.0F)) {
    commands_->RunFloat("lightbench.spot.range", (std::max)(spot.range, 0.1F));
  }
  if (ImGui::DragFloat(
        "Inner (deg)##spot", &spot.inner_angle_deg, 0.1F, 0.0F, 89.0F)) {
    commands_->RunFloat("lightbench.spot.inner", spot.inner_angle_deg);
  }
  if (ImGui::DragFloat(
        "Outer (deg)##spot", &spot.outer_angle_deg, 0.1F, 0.1F, 89.9F)) {
    commands_->RunFloat("lightbench.spot.outer", spot.outer_angle_deg);
  }
  if (ImGui::DragFloat(
        "Radius (m)##spot", &spot.source_radius, 0.01F, 0.0F, 10.0F)) {
    commands_->RunFloat("lightbench.spot.radius", spot.source_radius);
  }
  ImGui::Unindent();
}

} // namespace oxygen::examples::light_bench

// NOLINTEND(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise)
