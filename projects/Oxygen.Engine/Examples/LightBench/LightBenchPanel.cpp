//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <string>

#include <imgui.h>

#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/Services/SettingsService.h"
#include "LightBench/LightBenchPanel.h"

namespace oxygen::examples::light_bench {

namespace {
  constexpr Vec3 kAxisColorX { 1.0F, 0.2F, 0.2F };
  constexpr Vec3 kAxisColorY { 0.2F, 1.0F, 0.2F };
  constexpr Vec3 kAxisColorZ { 0.2F, 0.4F, 1.0F };
} // namespace

LightBenchPanel::LightBenchPanel(observer_ptr<LightScene> light_scene)
  : light_scene_(light_scene)
  , icon_(std::string(imgui::icons::kIconDemoPanel) + "##LightBench")
{
  LoadSettings();
}

auto LightBenchPanel::DrawContents() -> void
{
  if (!light_scene_) {
    return;
  }

  if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSceneSection();
  }

  if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawLightsSection();
  }
}

auto LightBenchPanel::OnLoaded() -> void { LoadSettings(); }

auto LightBenchPanel::OnUnloaded() -> void { SaveSettings(); }

auto LightBenchPanel::DrawSceneSection() -> void
{
  DrawScenePresets();

  if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSceneAdvancedSection();
  }
}

auto LightBenchPanel::DrawScenePresets() -> void
{
  ImGui::Text("Presets");
  if (ImGui::Button("Baseline")) {
    light_scene_->ApplyScenePreset(LightScene::ScenePreset::kBaseline);
    MarkChanged();
  }
  ImGui::SameLine();
  if (ImGui::Button("3 Cards")) {
    light_scene_->ApplyScenePreset(LightScene::ScenePreset::kThreeCards);
    MarkChanged();
  }
  ImGui::SameLine();
  if (ImGui::Button("Specular")) {
    light_scene_->ApplyScenePreset(LightScene::ScenePreset::kSpecular);
    MarkChanged();
  }
  ImGui::SameLine();
  if (ImGui::Button("Full")) {
    light_scene_->ApplyScenePreset(LightScene::ScenePreset::kFull);
    MarkChanged();
  }
}

auto LightBenchPanel::DrawSceneObjectControls(std::string_view label,
  LightScene::SceneObjectState& state, const bool allow_rotation) -> void
{
  ImGui::Text("%s", std::string(label).c_str());
  ImGui::Indent();
  const std::string id(label);
  if (ImGui::Checkbox(("Enabled##" + id).c_str(), &state.enabled)) {
    MarkChanged();
  }
  ImGui::SameLine();
  if (ImGui::Button(("Reset##" + id).c_str())) {
    light_scene_->ResetSceneObject(label);
    MarkChanged();
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
  if (ImGui::DragFloat(
        id.c_str(), &value, speed, min_value, max_value, "%.3F")) {
    MarkChanged();
  }
  ImGui::PopItemWidth();
}

auto LightBenchPanel::DrawLightsSection() -> void
{
  ImGui::Text("Local Lights");
  ImGui::Separator();
  DrawPointLightControls();
  ImGui::Spacing();
  DrawSpotLightControls();
}

auto LightBenchPanel::DrawPointLightControls() -> void
{
  auto& point = light_scene_->GetPointLightState();

  ImGui::Text("Point Light");
  ImGui::Indent();

  if (ImGui::Checkbox("Enabled##point", &point.enabled)) {
    MarkChanged();
  }
  if (ImGui::DragFloat3("Position##point", &point.position.x, 0.1F)) {
    MarkChanged();
  }
  if (ImGui::ColorEdit3("Color##point", &point.color_rgb.x)) {
    MarkChanged();
  }
  if (ImGui::DragFloat("Luminous Flux (lm)##point", &point.intensity, 1.0F,
        0.0F, 200000.0F, "%.2F", ImGuiSliderFlags_Logarithmic)) {
    MarkChanged();
  }
  if (ImGui::DragFloat("Range##point", &point.range, 0.1F, 0.1F, 500.0F)) {
    MarkChanged();
  }
  if (ImGui::DragFloat(
        "Source Radius##point", &point.source_radius, 0.01F, 0.0F, 10.0F)) {
    MarkChanged();
  }

  point.range = (std::max)(point.range, 0.1F);

  ImGui::Unindent();
}

auto LightBenchPanel::DrawSpotLightControls() -> void
{
  auto& spot = light_scene_->GetSpotLightState();

  ImGui::Text("Spot Light");
  ImGui::Indent();

  if (ImGui::Checkbox("Enabled##spot", &spot.enabled)) {
    MarkChanged();
  }
  if (ImGui::DragFloat3("Position##spot", &spot.position.x, 0.1F)) {
    MarkChanged();
  }
  if (ImGui::DragFloat3("Direction##spot", &spot.direction_ws.x, 0.05F)) {
    MarkChanged();
  }
  if (ImGui::ColorEdit3("Color##spot", &spot.color_rgb.x)) {
    MarkChanged();
  }
  if (ImGui::DragFloat("Luminous Flux (lm)##spot", &spot.intensity, 1.0F, 0.0F,
        200000.0F, "%.2F", ImGuiSliderFlags_Logarithmic)) {
    MarkChanged();
  }
  if (ImGui::DragFloat("Range##spot", &spot.range, 0.1F, 0.1F, 500.0F)) {
    MarkChanged();
  }
  if (ImGui::DragFloat(
        "Inner Angle (deg)##spot", &spot.inner_angle_deg, 0.1F, 0.0F, 89.0F)) {
    MarkChanged();
  }
  if (ImGui::DragFloat(
        "Outer Angle (deg)##spot", &spot.outer_angle_deg, 0.1F, 0.1F, 89.9F)) {
    MarkChanged();
  }
  if (ImGui::DragFloat(
        "Source Radius##spot", &spot.source_radius, 0.01F, 0.0F, 10.0F)) {
    MarkChanged();
  }

  spot.range = (std::max)(spot.range, 0.1F);
  if (spot.outer_angle_deg < spot.inner_angle_deg) {
    spot.outer_angle_deg = spot.inner_angle_deg;
  }

  ImGui::Unindent();
}

auto LightBenchPanel::LoadSettings() -> void
{
  if (settings_loaded_) {
    return;
  }

  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return;
  }

  auto load_bool = [&](std::string_view key, bool& value) {
    if (const auto stored = settings->GetBool(key)) {
      value = *stored;
    }
  };
  auto load_float = [&](std::string_view key, float& value) {
    if (const auto stored = settings->GetFloat(key)) {
      value = *stored;
    }
  };
  auto load_vec3 = [&](std::string_view prefix, Vec3& value) {
    std::string key(prefix);
    key += ".x";
    load_float(key, value.x);
    key.resize(prefix.size());
    key += ".y";
    load_float(key, value.y);
    key.resize(prefix.size());
    key += ".z";
    load_float(key, value.z);
  };

  auto& gray = light_scene_->GetGrayCardState();
  load_bool("lightbench.scene.gray_card.enabled", gray.enabled);
  load_vec3("lightbench.scene.gray_card.position", gray.position);
  load_vec3("lightbench.scene.gray_card.rotation", gray.rotation_deg);
  load_vec3("lightbench.scene.gray_card.scale", gray.scale);

  auto& white = light_scene_->GetWhiteCardState();
  load_bool("lightbench.scene.white_card.enabled", white.enabled);
  load_vec3("lightbench.scene.white_card.position", white.position);
  load_vec3("lightbench.scene.white_card.rotation", white.rotation_deg);
  load_vec3("lightbench.scene.white_card.scale", white.scale);

  auto& black = light_scene_->GetBlackCardState();
  load_bool("lightbench.scene.black_card.enabled", black.enabled);
  load_vec3("lightbench.scene.black_card.position", black.position);
  load_vec3("lightbench.scene.black_card.rotation", black.rotation_deg);
  load_vec3("lightbench.scene.black_card.scale", black.scale);

  auto& matte = light_scene_->GetMatteSphereState();
  load_bool("lightbench.scene.matte_sphere.enabled", matte.enabled);
  load_vec3("lightbench.scene.matte_sphere.position", matte.position);
  load_vec3("lightbench.scene.matte_sphere.rotation", matte.rotation_deg);
  load_vec3("lightbench.scene.matte_sphere.scale", matte.scale);

  auto& glossy = light_scene_->GetGlossySphereState();
  load_bool("lightbench.scene.glossy_sphere.enabled", glossy.enabled);
  load_vec3("lightbench.scene.glossy_sphere.position", glossy.position);
  load_vec3("lightbench.scene.glossy_sphere.rotation", glossy.rotation_deg);
  load_vec3("lightbench.scene.glossy_sphere.scale", glossy.scale);

  auto& ground = light_scene_->GetGroundPlaneState();
  load_bool("lightbench.scene.ground_plane.enabled", ground.enabled);
  load_vec3("lightbench.scene.ground_plane.position", ground.position);
  load_vec3("lightbench.scene.ground_plane.rotation", ground.rotation_deg);
  load_vec3("lightbench.scene.ground_plane.scale", ground.scale);

  auto& point = light_scene_->GetPointLightState();
  load_bool("lightbench.light.point.enabled", point.enabled);
  load_vec3("lightbench.light.point.position", point.position);
  load_vec3("lightbench.light.point.color", point.color_rgb);
  load_float("lightbench.light.point.intensity", point.intensity);
  load_float("lightbench.light.point.range", point.range);
  load_float("lightbench.light.point.source_radius", point.source_radius);

  auto& spot = light_scene_->GetSpotLightState();
  load_bool("lightbench.light.spot.enabled", spot.enabled);
  load_vec3("lightbench.light.spot.position", spot.position);
  load_vec3("lightbench.light.spot.direction", spot.direction_ws);
  load_vec3("lightbench.light.spot.color", spot.color_rgb);
  load_float("lightbench.light.spot.intensity", spot.intensity);
  load_float("lightbench.light.spot.range", spot.range);
  load_float("lightbench.light.spot.inner_angle", spot.inner_angle_deg);
  load_float("lightbench.light.spot.outer_angle", spot.outer_angle_deg);
  load_float("lightbench.light.spot.source_radius", spot.source_radius);

  settings_loaded_ = true;
}

auto LightBenchPanel::SaveSettings() -> void
{
  if (!pending_changes_) {
    return;
  }

  const auto settings = SettingsService::ForDemoApp();
  if (!settings) {
    return;
  }

  auto save_bool
    = [&](std::string_view key, bool value) { settings->SetBool(key, value); };
  auto save_float = [&](std::string_view key, float value) {
    settings->SetFloat(key, value);
  };
  auto save_vec3 = [&](std::string_view prefix, const Vec3& value) {
    std::string key(prefix);
    key += ".x";
    save_float(key, value.x);
    key.resize(prefix.size());
    key += ".y";
    save_float(key, value.y);
    key.resize(prefix.size());
    key += ".z";
    save_float(key, value.z);
  };

  const auto& gray = light_scene_->GetGrayCardState();
  save_bool("lightbench.scene.gray_card.enabled", gray.enabled);
  save_vec3("lightbench.scene.gray_card.position", gray.position);
  save_vec3("lightbench.scene.gray_card.rotation", gray.rotation_deg);
  save_vec3("lightbench.scene.gray_card.scale", gray.scale);

  const auto& white = light_scene_->GetWhiteCardState();
  save_bool("lightbench.scene.white_card.enabled", white.enabled);
  save_vec3("lightbench.scene.white_card.position", white.position);
  save_vec3("lightbench.scene.white_card.rotation", white.rotation_deg);
  save_vec3("lightbench.scene.white_card.scale", white.scale);

  const auto& black = light_scene_->GetBlackCardState();
  save_bool("lightbench.scene.black_card.enabled", black.enabled);
  save_vec3("lightbench.scene.black_card.position", black.position);
  save_vec3("lightbench.scene.black_card.rotation", black.rotation_deg);
  save_vec3("lightbench.scene.black_card.scale", black.scale);

  const auto& matte = light_scene_->GetMatteSphereState();
  save_bool("lightbench.scene.matte_sphere.enabled", matte.enabled);
  save_vec3("lightbench.scene.matte_sphere.position", matte.position);
  save_vec3("lightbench.scene.matte_sphere.rotation", matte.rotation_deg);
  save_vec3("lightbench.scene.matte_sphere.scale", matte.scale);

  const auto& glossy = light_scene_->GetGlossySphereState();
  save_bool("lightbench.scene.glossy_sphere.enabled", glossy.enabled);
  save_vec3("lightbench.scene.glossy_sphere.position", glossy.position);
  save_vec3("lightbench.scene.glossy_sphere.rotation", glossy.rotation_deg);
  save_vec3("lightbench.scene.glossy_sphere.scale", glossy.scale);

  const auto& ground = light_scene_->GetGroundPlaneState();
  save_bool("lightbench.scene.ground_plane.enabled", ground.enabled);
  save_vec3("lightbench.scene.ground_plane.position", ground.position);
  save_vec3("lightbench.scene.ground_plane.rotation", ground.rotation_deg);
  save_vec3("lightbench.scene.ground_plane.scale", ground.scale);

  const auto& point = light_scene_->GetPointLightState();
  save_bool("lightbench.light.point.enabled", point.enabled);
  save_vec3("lightbench.light.point.position", point.position);
  save_vec3("lightbench.light.point.color", point.color_rgb);
  save_float("lightbench.light.point.intensity", point.intensity);
  save_float("lightbench.light.point.range", point.range);
  save_float("lightbench.light.point.source_radius", point.source_radius);

  const auto& spot = light_scene_->GetSpotLightState();
  save_bool("lightbench.light.spot.enabled", spot.enabled);
  save_vec3("lightbench.light.spot.position", spot.position);
  save_vec3("lightbench.light.spot.direction", spot.direction_ws);
  save_vec3("lightbench.light.spot.color", spot.color_rgb);
  save_float("lightbench.light.spot.intensity", spot.intensity);
  save_float("lightbench.light.spot.range", spot.range);
  save_float("lightbench.light.spot.inner_angle", spot.inner_angle_deg);
  save_float("lightbench.light.spot.outer_angle", spot.outer_angle_deg);
  save_float("lightbench.light.spot.source_radius", spot.source_radius);

  pending_changes_ = false;
}

auto LightBenchPanel::MarkChanged() -> void { pending_changes_ = true; }

} // namespace oxygen::examples::light_bench
