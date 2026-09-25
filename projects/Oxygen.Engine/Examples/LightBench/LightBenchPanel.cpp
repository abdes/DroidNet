//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "LightBench/LightBenchPanel.h"
#include "LightBench/LightBenchSettings.h"
#include "LightBench/LightScene.h"
#include "LightBench/ReferenceScene.h"
#include <imgui.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Styles/Spectrum.h>

namespace oxygen::examples::light_bench {

namespace {
  constexpr Vec3 kAxisColorX { 1.0F, 0.2F, 0.2F };
  constexpr Vec3 kAxisColorY { 0.2F, 1.0F, 0.2F };
  constexpr Vec3 kAxisColorZ { 0.2F, 0.4F, 1.0F };

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
  Actions actions, const std::filesystem::path& settings_path)
  : actions_(std::move(actions))
  , light_scene_(light_scene)
  , icon_(std::string(imgui::icons::kIconDemoPanel) + "##LightBench")
{
  const auto text = settings_path.string();
  if (text.size() >= settings_path_.size()) {
    throw std::length_error("LightBench settings path is too long");
  }
  std::ranges::copy(text, settings_path_.begin());
}

auto LightBenchPanel::DrawPresetOverlay() -> void
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
    return ImGui::CalcTextSize(label).x + 2.0F * style.FramePadding.x;
  };
  float label_width = 0;
  for (const auto& preset : GetPresets()) {
    label_width = std::max(label_width, ImGui::CalcTextSize(preset.name).x);
  }
  const auto reset_width = button_width("Reset");
  const auto selector_width
    = label_width + ImGui::GetFrameHeight() + 2.0F * style.FramePadding.x;
  const auto step_width = std::max(
    { button_width("Dim"), button_width("Normal"), button_width("Bright") });
  const auto steps_width = 3.0F * step_width + 2.0F * style.ItemSpacing.x;
  const auto desired_width = 2.0F * padding
    + std::max(selector_width + reset_width + style.ItemSpacing.x, steps_width);
  const auto width
    = std::min(desired_width, std::max(1.0F, viewport->Size.x - 2.0F * margin));
  ImGui::SetNextWindowPos(ImVec2 { viewport->Pos.x + viewport->Size.x * .5F,
                            viewport->Pos.y + margin },
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
  const auto popup_height = std::ceil(2.0F * style.WindowPadding.y
    + ImGui::GetTextLineHeightWithSpacing()
      * static_cast<float>(GetPresets().size())
    + 2.0F * style.PopupBorderSize);
  ImGui::SetNextWindowSizeConstraints(
    ImVec2 { 0, popup_height }, ImVec2 { FLT_MAX, FLT_MAX });
  if (ImGui::BeginCombo(
        "##Scenario", info.name, ImGuiComboFlags_HeightLargest)) {
    for (const auto& preset : GetPresets()) {
      const auto is_selected = preset.preset == selected;
      if (ImGui::Selectable(preset.name, is_selected)) {
        actions_.select(preset.preset);
      }
      if (is_selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset", ImVec2 { reset_width, 0 })) {
    actions_.reset();
    file_status_.clear();
  }
  DrawPresetControls(std::max(1.0F,
    (ImGui::GetWindowWidth() - 2.0F * padding - 2.0F * style.ItemSpacing.x)
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
      const auto result
        = actions_.save(std::filesystem::path(settings_path_.data()));
      file_status_ = result ? "Settings saved." : result.error();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load settings")) {
      const auto result
        = actions_.load(std::filesystem::path(settings_path_.data()));
      file_status_
        = result ? "Settings validated; applying next frame." : result.error();
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
        light_scene_->GetPointLightState().position = { 0.0F, distance, 1.0F };
      }
      if (distance != 4.0F) {
        ImGui::SameLine();
      }
    }

  } else if (actions_.selected() == LightBenchPreset::kAutoAdaptation) {
    constexpr std::array steps { std::pair { "Dim", 100.0F },
      std::pair { "Normal", 1000.0F }, std::pair { "Bright", 10000.0F } };
    for (const auto& [label, lux] : steps) {
      auto& current_lux
        = light_scene_->GetDirectionalLightState().illuminance_lux;
      if (PresetButton(label, current_lux == lux, button_width)) {
        current_lux = lux;
      }
      if (lux != steps.back().second) {
        ImGui::SameLine();
      }
    }
  }
}

auto LightBenchPanel::DrawSceneObjectControls(std::string_view label,
  LightScene::SceneObjectState& state, const bool allow_rotation) -> void
{
  ImGui::Text("%s", std::string(label).c_str());
  ImGui::Indent();
  const std::string id(label);
  static_cast<void>(
    ImGui::Checkbox(("Enabled##" + id).c_str(), &state.enabled));
  ImGui::SameLine();
  if (ImGui::Button(("Reset##" + id).c_str())) {
    light_scene_->ResetSceneObject(label);
  }
  DrawVector3Table(
    id + "_pos", "Position", state.position, 0.05F, -100000.0F, 100000.0F);
  if (allow_rotation) {
    DrawVector3Table(
      id + "_rot", "Rotation (deg)", state.rotation_deg, 0.5F, -360.0F, 360.0F);
  }
  DrawVector3Table(id + "_scl", "Scale", state.scale, 0.05F, 0.01F, 100.0F);
  ImGui::Unindent();
}

auto LightBenchPanel::DrawSceneAdvancedSection() -> void
{
  DrawSceneObjectControls(
    "18% Gray Card", light_scene_->GetGrayCardState(), true);
  ImGui::Spacing();
  DrawSceneObjectControls(
    "White Card", light_scene_->GetWhiteCardState(), true);
  ImGui::Spacing();
  DrawSceneObjectControls(
    "Black Card", light_scene_->GetBlackCardState(), true);
  ImGui::Spacing();
  DrawSceneObjectControls(
    "Matte Sphere", light_scene_->GetMatteSphereState(), false);
  ImGui::Spacing();
  DrawSceneObjectControls(
    "Glossy Sphere", light_scene_->GetGlossySphereState(), false);
  ImGui::Spacing();
  DrawSceneObjectControls(
    "Ground Plane", light_scene_->GetGroundPlaneState(), false);
}

auto LightBenchPanel::DrawVector3Table(const std::string& id, const char* label,
  Vec3& value, const float speed, const float min_value, const float max_value)
  -> void
{
  constexpr ImGuiTableFlags kFlags
    = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV;
  if (!ImGui::BeginTable(id.c_str(), 4, kFlags)) {
    return;
  }

  ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0F);
  ImGui::TableSetupColumn("X");
  ImGui::TableSetupColumn("Y");
  ImGui::TableSetupColumn("Z");

  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);

  ImGui::TableSetColumnIndex(1);
  DrawAxisFloatCell(
    id + "_x", kAxisColorX, value.x, speed, min_value, max_value);

  ImGui::TableSetColumnIndex(2);
  DrawAxisFloatCell(
    id + "_y", kAxisColorY, value.y, speed, min_value, max_value);

  ImGui::TableSetColumnIndex(3);
  DrawAxisFloatCell(
    id + "_z", kAxisColorZ, value.z, speed, min_value, max_value);

  ImGui::EndTable();
}

auto LightBenchPanel::DrawAxisFloatCell(const std::string& id,
  const Vec3& color, float& value, const float speed, const float min_value,
  const float max_value) -> void
{
  const float height = ImGui::GetFrameHeight();
  const float rect_width = 6.0F;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImU32 rect_color
    = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0F));
  draw_list->AddRectFilled(
    cursor, ImVec2(cursor.x + rect_width, cursor.y + height), rect_color, 2.0F);

  ImGui::SetCursorScreenPos(ImVec2(cursor.x + rect_width + 6.0F, cursor.y));
  ImGui::PushItemWidth(-1.0F);
  static_cast<void>(ImGui::DragFloat(id.c_str(), &value, speed, min_value,
    max_value, "%.3F", ImGuiSliderFlags_AlwaysClamp));
  ImGui::PopItemWidth();
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
  auto& key = light_scene_->GetDirectionalLightState();
  static_cast<void>(ImGui::Checkbox("Enabled##directional", &key.enabled));
  static_cast<void>(ImGui::DragFloat("Lux", &key.illuminance_lux, 10.0F, 0.0F,
    200000.0F, "%.1f", ImGuiSliderFlags_AlwaysClamp));
  static_cast<void>(ImGui::ColorEdit3("Color##directional", &key.color_rgb.x));
  static_cast<void>(ImGui::DragFloat3(
    "Ray direction##directional", &key.direction_ws.x, .01F, -1.0F, 1.0F));
  static_cast<void>(
    ImGui::Checkbox("Cast shadows##directional", &key.casts_shadows));
}

auto LightBenchPanel::DrawPointLightControls() -> void
{
  auto& point = light_scene_->GetPointLightState();

  ImGui::Text("Point Light");
  ImGui::Indent();

  static_cast<void>(ImGui::Checkbox("Enabled##point", &point.enabled));
  ImGui::SameLine();
  static_cast<void>(ImGui::Checkbox("Shadows##point", &point.casts_shadows));
  static_cast<void>(
    ImGui::DragFloat3("Position##point", &point.position.x, 0.1F));
  static_cast<void>(ImGui::ColorEdit3("Color##point", &point.color_rgb.x));
  static_cast<void>(ImGui::DragFloat("Lumens##point", &point.intensity, 1.0F,
    0.0F, 200000.0F, "%.2F", ImGuiSliderFlags_Logarithmic));
  static_cast<void>(
    ImGui::DragFloat("Range (m)##point", &point.range, 0.1F, 0.1F, 500.0F));
  static_cast<void>(ImGui::DragFloat(
    "Radius (m)##point", &point.source_radius, 0.01F, 0.0F, 10.0F));

  point.range = (std::max)(point.range, 0.1F);

  ImGui::Unindent();
}

auto LightBenchPanel::DrawSpotLightControls() -> void
{
  auto& spot = light_scene_->GetSpotLightState();

  ImGui::Text("Spot Light");
  ImGui::Indent();

  static_cast<void>(ImGui::Checkbox("Enabled##spot", &spot.enabled));
  ImGui::SameLine();
  static_cast<void>(ImGui::Checkbox("Shadows##spot", &spot.casts_shadows));
  static_cast<void>(
    ImGui::DragFloat3("Position##spot", &spot.position.x, 0.1F));
  static_cast<void>(
    ImGui::DragFloat3("Direction##spot", &spot.direction_ws.x, 0.05F));
  static_cast<void>(ImGui::ColorEdit3("Color##spot", &spot.color_rgb.x));
  static_cast<void>(ImGui::DragFloat("Lumens##spot", &spot.intensity, 1.0F,
    0.0F, 200000.0F, "%.2F", ImGuiSliderFlags_Logarithmic));
  static_cast<void>(
    ImGui::DragFloat("Range (m)##spot", &spot.range, 0.1F, 0.1F, 500.0F));
  static_cast<void>(ImGui::DragFloat(
    "Inner (deg)##spot", &spot.inner_angle_deg, 0.1F, 0.0F, 89.0F));
  static_cast<void>(ImGui::DragFloat(
    "Outer (deg)##spot", &spot.outer_angle_deg, 0.1F, 0.1F, 89.9F));
  static_cast<void>(ImGui::DragFloat(
    "Radius (m)##spot", &spot.source_radius, 0.01F, 0.0F, 10.0F));

  spot.range = (std::max)(spot.range, 0.1F);
  if (spot.outer_angle_deg < spot.inner_angle_deg) {
    spot.outer_angle_deg = spot.inner_angle_deg;
  }

  ImGui::Unindent();
}

} // namespace oxygen::examples::light_bench
