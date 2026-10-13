//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <span>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/EnvironmentDebugPanel.h"
#include "DemoShell/UI/EnvironmentPanelCommands.h"
#include "DemoShell/UI/EnvironmentVm.h"
#include <glm/ext/vector_float3.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Scene/Environment/Fog.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

namespace oxygen::examples::ui {

// ImGui panels use literal layout metrics and slider ranges, and ImGui flags
// are signed int enums combined with bitwise or.
// NOLINTBEGIN(*-magic-numbers, bugprone-signed-bitwise)

namespace {

  constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0F;
  constexpr float kMaxSkySphereIntensity = 100000.0F;
  constexpr float kMinSkySphereExposureEv = -16.0F;
  constexpr float kMaxSkySphereExposureEv = 16.6F;
  constexpr std::array kShadowResolutionLabels = {
    "Low",
    "Medium",
    "High",
    "Ultra",
  };
  constexpr std::array kShadowSplitModeLabels = {
    "Generated",
    "Manual Distances",
  };

  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - azimuth, elevation
  auto DirectionFromAzimuthElevation(float azimuth_deg, float elevation_deg)
    -> glm::vec3
  {
    const float az_rad = azimuth_deg * kDegToRad;
    const float el_rad = elevation_deg * kDegToRad;

    // Z-up coordinate system:
    // Azimuth: angle from +X toward +Y (0° = +X, 90° = +Y)
    // Elevation: angle from horizontal plane toward +Z
    const float cos_el = std::cos(el_rad);
    return {
      cos_el * std::cos(az_rad),
      cos_el * std::sin(az_rad),
      std::sin(el_rad),
    };
  }

  auto SkySphereRadianceScaleToEv(float scale) -> float
  {
    if (!(scale > 0.0F)) {
      return kMinSkySphereExposureEv;
    }
    scale = std::clamp(scale, 0.0F, kMaxSkySphereIntensity);
    return std::clamp(
      std::log2(scale), kMinSkySphereExposureEv, kMaxSkySphereExposureEv);
  }

  auto SkySphereExposureEvToRadianceScale(float exposure_ev) -> float
  {
    exposure_ev = std::clamp(
      exposure_ev, kMinSkySphereExposureEv, kMaxSkySphereExposureEv);
    return std::clamp(std::exp2(exposure_ev), 0.0F, kMaxSkySphereIntensity);
  }

  auto KelvinToLinearRgb(float kelvin) -> glm::vec3
  {
    kelvin = std::clamp(kelvin, 1000.0F, 40000.0F);
    const float temp = kelvin / 100.0F;

    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;

    if (temp <= 66.0F) {
      red = 1.0F;
      green = std::clamp(
        (0.39008157877F * std::log(temp)) - 0.63184144378F, 0.0F, 1.0F);
      if (temp <= 19.0F) {
        blue = 0.0F;
      } else {
        blue = std::clamp(
          (0.54320678911F * std::log(temp - 10.0F)) - 1.19625408914F, 0.0F,
          1.0F);
      }
    } else {
      red = std::clamp(
        1.29293618606F * std::pow(temp - 60.0F, -0.1332047592F), 0.0F, 1.0F);
      green = std::clamp(
        1.1298908609F * std::pow(temp - 60.0F, -0.0755148492F), 0.0F, 1.0F);
      blue = 1.0F;
    }

    return { red, green, blue };
  }

  auto CopyPathToBuffer(
    const std::filesystem::path& path, std::span<char> buffer) -> void
  {
    if (buffer.empty()) {
      return;
    }
    const std::string s = path.string();
    const std::size_t to_copy = std::min(buffer.size() - 1, s.size());
    std::memcpy(buffer.data(), s.data(), to_copy);
    buffer.subspan(to_copy, 1).front() = '\0';
  }

} // namespace

EnvironmentDebugPanel::EnvironmentDebugPanel(
  observer_ptr<EnvironmentVm> vm, observer_ptr<PanelCommands> commands)
  : environment_vm_(vm)
  , commands_(commands)
{
  CHECK_NOTNULL_F(
    environment_vm_, "EnvironmentDebugPanel requires an EnvironmentVm");
  CHECK_NOTNULL_F(commands_, "EnvironmentDebugPanel requires PanelCommands");
  RegisterEnvironmentCommands(*commands_, environment_vm_);
  environment_vm_->SyncFromSceneIfNeeded();
}

auto EnvironmentDebugPanel::DrawContents() -> void
{
  environment_vm_->SyncFromSceneIfNeeded();
  const bool has_scene = environment_vm_->HasScene();

  if (!has_scene) {
    ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F),
      "No scene loaded. Load a scene to edit environment settings.");
    return;
  }

  if (!collapse_state_loaded_) {
    if (const auto settings = examples::SettingsService::ForDemoApp()) {
      if (const auto saved_value
        = settings->GetBool("demo_shell.panels.environment.sun.open")) {
        sun_section_open_ = *saved_value;
      }
      if (const auto saved_value
        = settings->GetBool("demo_shell.panels.environment.sky_atmo.open")) {
        sky_atmo_section_open_ = *saved_value;
      }
      if (const auto saved_value
        = settings->GetBool("demo_shell.panels.environment.sky_sphere.open")) {
        sky_sphere_section_open_ = *saved_value;
      } else {
        sky_sphere_section_open_ = environment_vm_->GetSkySphereEnabled();
      }
      if (const auto saved_value
        = settings->GetBool("demo_shell.panels.environment.sky_light.open")) {
        sky_light_section_open_ = *saved_value;
      } else {
        sky_light_section_open_ = environment_vm_->GetSkyLightEnabled();
      }
    }
    collapse_state_loaded_ = true;
  }

  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08F, 0.09F, 0.11F, 1.0F));
  ImGui::BeginChild("##EnvironmentProfile", ImVec2(0.0F, 0.0F),
    ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::SeparatorText("Environment");
  const auto preset_label = environment_vm_->GetPresetLabel();
  ImGui::SetNextItemWidth(220.0F);
  if (ImGui::BeginCombo("Profile", std::string(preset_label).c_str())) {
    const int current_index = environment_vm_->GetPresetIndex();
    const int preset_count = environment_vm_->GetPresetCount();
    for (int i = 0; i < preset_count; ++i) {
      const auto name = environment_vm_->GetPresetName(i);
      if (ImGui::Selectable(std::string(name).c_str(), i == current_index)) {
        commands_->RunText(
          env_commands::kPreset, EnvironmentVm::GetPresetKey(i));
      }
    }
    ImGui::EndCombo();
  }

  if (environment_vm_->SupportsPreviewSun()) {
    const bool available = environment_vm_->CanEnablePreviewSun();
    bool preview = environment_vm_->GetPreviewSunEnabled();
    ImGui::BeginDisabled(!available);
    if (ImGui::Checkbox("Preview sun", &preview)) {
      commands_->RunBool(env_commands::kPreviewSunEnabled, preview);
    }
    ImGui::EndDisabled();
    const auto help = environment_vm_->GetPreviewSunHelpText();
    ImGui::TextWrapped("%s", help.c_str());
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextDisabled("Scene files stay unchanged.");
    ImGui::PopTextWrapPos();
  } else {
    const auto sun_source = environment_vm_->GetSunSourceDescription();
    ImGui::TextWrapped("%s", sun_source.c_str());
  }

  ImGui::EndChild();
  ImGui::PopStyleColor();

  if (ImGui::CollapsingHeader("Runtime details")) {
    DrawRuntimeStateSection();
  }

  ImGui::Separator();

  // Sun controls
  ImGui::SetNextItemOpen(sun_section_open_, ImGuiCond_Always);
  const bool sun_section_open = ImGui::CollapsingHeader("Sun");
  if (sun_section_open != sun_section_open_) {
    sun_section_open_ = sun_section_open;
    if (const auto settings = examples::SettingsService::ForDemoApp()) {
      settings->SetBool(
        "demo_shell.panels.environment.sun.open", sun_section_open_);
    }
  }
  if (sun_section_open) {
    DrawSunSection();
  }

  DrawFog();
  DrawLocalFogVolumes();
  // Environment system sections
  ImGui::SetNextItemOpen(sky_atmo_section_open_, ImGuiCond_Always);
  const bool sky_atmo_section_open = ImGui::CollapsingHeader("Sky Atmosphere");
  if (sky_atmo_section_open != sky_atmo_section_open_) {
    sky_atmo_section_open_ = sky_atmo_section_open;
    if (const auto settings = examples::SettingsService::ForDemoApp()) {
      settings->SetBool(
        "demo_shell.panels.environment.sky_atmo.open", sky_atmo_section_open_);
    }
  }
  if (sky_atmo_section_open) {
    DrawSkyAtmosphereSection();
  }

  ImGui::SetNextItemOpen(sky_sphere_section_open_, ImGuiCond_Always);
  const bool sky_sphere_section_open = ImGui::CollapsingHeader("Sky Sphere");
  if (sky_sphere_section_open != sky_sphere_section_open_) {
    sky_sphere_section_open_ = sky_sphere_section_open;
    if (const auto settings = examples::SettingsService::ForDemoApp()) {
      settings->SetBool("demo_shell.panels.environment.sky_sphere.open",
        sky_sphere_section_open_);
    }
  }
  if (sky_sphere_section_open) {
    DrawSkySphereSection();
  }

  ImGui::SetNextItemOpen(sky_light_section_open_, ImGuiCond_Always);
  const bool sky_light_section_open
    = ImGui::CollapsingHeader("Sky Light (IBL)");
  if (sky_light_section_open != sky_light_section_open_) {
    sky_light_section_open_ = sky_light_section_open;
    if (const auto settings = examples::SettingsService::ForDemoApp()) {
      settings->SetBool("demo_shell.panels.environment.sky_light.open",
        sky_light_section_open_);
    }
  }
  if (sky_light_section_open) {
    DrawSkyLightSection();
  }
}

void EnvironmentDebugPanel::DrawFog()
{
  if (!ImGui::CollapsingHeader("Height Fog")) {
    return;
  }

  ImGui::TextDisabled(
    "Controls Vortex exponential height fog for the main scene view.");

  // Height and volumetric fog are independent: toggling one keeps the other.
  bool fog_enabled = environment_vm_->GetFogEnabled();
  if (ImGui::Checkbox("Enable Height Fog", &fog_enabled)) {
    commands_->RunBool(env_commands::kFogEnabled, fog_enabled);
  }
  bool volumetric_fog = environment_vm_->GetFogModel()
    == static_cast<int>(scene::environment::FogModel::kVolumetric);
  if (ImGui::Checkbox("Enable Volumetric Fog", &volumetric_fog)) {
    commands_->RunText(env_commands::kFogModel,
      env_commands::ValueAt(env_commands::kFogModels,
        static_cast<int>(volumetric_fog
            ? scene::environment::FogModel::kVolumetric
            : scene::environment::FogModel::kExponentialHeight)));
  }

  bool fog_main_pass = environment_vm_->GetFogRenderInMainPass();
  if (ImGui::Checkbox("Render In Main Pass##Fog", &fog_main_pass)) {
    commands_->RunBool(env_commands::kFogRenderInMainPass, fog_main_pass);
  }

  ImGui::BeginDisabled(!fog_enabled);

  ImGui::SeparatorText("Primary Layer");
  float extinction_sigma_t_per_m
    = environment_vm_->GetFogExtinctionSigmaTPerMeter();
  if (ImGui::SliderFloat("Density / Extinction (1/m)",
        &extinction_sigma_t_per_m, 0.0F, 10.0F, "%.6f",
        ImGuiSliderFlags_Logarithmic)) {
    commands_->RunFloat(
      env_commands::kFogExtinctionSigmaTPerMeter, extinction_sigma_t_per_m);
  }

  float height_falloff_per_m = environment_vm_->GetFogHeightFalloffPerMeter();
  if (ImGui::DragFloat("Height Falloff (1/m)", &height_falloff_per_m, 0.0001F,
        0.0F, 10.0F, "%.4f")) {
    commands_->RunFloat(
      env_commands::kFogHeightFalloffPerMeter, height_falloff_per_m);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(
      "Controls how extinction increases with decreasing height below Height "
      "Offset.\n"
      "sigma_t(z) = base_sigma_t * exp(-falloff * (z - offset)).\n"
      "0 = uniform with height; higher = fog hugs the ground.\n"
      "Units: 1/m (inverse meters).\n"
      "Tip: small changes can have large visual impact.");
  }

  float height_offset_m = environment_vm_->GetFogHeightOffsetMeters();
  if (ImGui::DragFloat("Height Offset (m)", &height_offset_m, 0.25F, -100000.0F,
        100000.0F, "%.1f")) {
    commands_->RunFloat(env_commands::kFogHeightOffsetMeters, height_offset_m);
  }

  ImGui::SeparatorText("Distance and Opacity");
  float start_distance_m = environment_vm_->GetFogStartDistanceMeters();
  if (ImGui::DragFloat("Start Distance (m)", &start_distance_m, 1.0F, 0.0F,
        1000000.0F, "%.1f")) {
    commands_->RunFloat(
      env_commands::kFogStartDistanceMeters, std::max(start_distance_m, 0.0F));
  }
  float end_distance = environment_vm_->GetFogEndDistanceMeters();
  if (ImGui::DragFloat(
        "End Distance (m)", &end_distance, 1.0F, 0.0F, 1000000.0F, "%.1f")) {
    commands_->RunFloat(env_commands::kFogEndDistanceMeters, end_distance);
  }
  float cutoff_distance = environment_vm_->GetFogCutoffDistanceMeters();
  if (ImGui::DragFloat("Cutoff Distance (m)", &cutoff_distance, 10.0F, 0.0F,
        1000000.0F, "%.1f")) {
    commands_->RunFloat(
      env_commands::kFogCutoffDistanceMeters, cutoff_distance);
  }
  float max_opacity = environment_vm_->GetFogMaxOpacity();
  if (ImGui::SliderFloat("Max Opacity", &max_opacity, 0.0F, 1.0F, "%.3f")) {
    commands_->RunFloat(env_commands::kFogMaxOpacity, max_opacity);
  }

  ImGui::SeparatorText("Secondary Layer");
  float second_fog_density = environment_vm_->GetSecondFogDensity();
  if (ImGui::DragFloat("Second Density (1/m)", &second_fog_density, 0.001F,
        0.0F, 10.0F, "%.4f")) {
    commands_->RunFloat(env_commands::kSecondFogDensity, second_fog_density);
  }
  float second_fog_height_falloff
    = environment_vm_->GetSecondFogHeightFalloff();
  if (ImGui::DragFloat("Second Height Falloff (1/m)",
        &second_fog_height_falloff, 0.0001F, 0.0F, 10.0F, "%.4f")) {
    commands_->RunFloat(
      env_commands::kSecondFogHeightFalloff, second_fog_height_falloff);
  }
  float second_fog_height_offset = environment_vm_->GetSecondFogHeightOffset();
  if (ImGui::DragFloat("Second Height Offset (m)", &second_fog_height_offset,
        0.25F, -100000.0F, 100000.0F, "%.1f")) {
    commands_->RunFloat(
      env_commands::kSecondFogHeightOffset, second_fog_height_offset);
  }

  ImGui::SeparatorText("Inscattering");
  auto fog_inscattering = environment_vm_->GetFogInscatteringLuminance();
  if (ImGui::DragFloat3("Fog Inscattering Luminance", &fog_inscattering.x, 1.0F,
        0.0F, 100000.0F, "%.3f")) {
    commands_->RunRgb(
      env_commands::kFogInscatteringLuminance, fog_inscattering);
  }
  auto ambient_scale
    = environment_vm_->GetSkyAtmosphereAmbientContributionColorScale();
  if (ImGui::DragFloat3("Sky Atmosphere Ambient Scale", &ambient_scale.x, 0.01F,
        0.0F, 16.0F, "%.3f")) {
    commands_->RunRgb(
      env_commands::kSkyAtmosphereAmbientContributionColorScale, ambient_scale);
  }
  auto directional_luminance
    = environment_vm_->GetDirectionalInscatteringLuminance();
  if (ImGui::DragFloat3("Directional Inscattering Luminance",
        &directional_luminance.x, 1.0F, 0.0F, 100000.0F, "%.3f")) {
    commands_->RunRgb(
      env_commands::kDirectionalInscatteringLuminance, directional_luminance);
  }
  float directional_exponent
    = environment_vm_->GetDirectionalInscatteringExponent();
  if (ImGui::DragFloat("Directional Exponent", &directional_exponent, 0.1F,
        0.0F, 128.0F, "%.2f")) {
    commands_->RunFloat(
      env_commands::kDirectionalInscatteringExponent, directional_exponent);
  }
  float directional_start_distance
    = environment_vm_->GetDirectionalInscatteringStartDistance();
  if (ImGui::DragFloat("Directional Start Distance (m)",
        &directional_start_distance, 1.0F, 0.0F, 1000000.0F, "%.1f")) {
    commands_->RunFloat(env_commands::kDirectionalInscatteringStartDistance,
      directional_start_distance);
  }

  ImGui::SeparatorText("Visibility");
  bool fog_holdout = environment_vm_->GetFogHoldout();
  if (ImGui::Checkbox("Holdout##Fog", &fog_holdout)) {
    commands_->RunBool(env_commands::kFogHoldout, fog_holdout);
  }
  bool fog_reflection_captures
    = environment_vm_->GetFogVisibleInReflectionCaptures();
  if (ImGui::Checkbox(
        "Visible In Reflection Captures", &fog_reflection_captures)) {
    commands_->RunBool(
      env_commands::kFogVisibleInReflectionCaptures, fog_reflection_captures);
  }
  bool fog_realtime_sky = environment_vm_->GetFogVisibleInRealTimeSkyCaptures();
  if (ImGui::Checkbox("Include in Sky Lighting", &fog_realtime_sky)) {
    commands_->RunBool(
      env_commands::kFogVisibleInRealTimeSkyCaptures, fog_realtime_sky);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip("Includes height fog in the Captured Scene skylight, "
                      "even when hidden in the main pass.");
  }

  ImGui::EndDisabled();
}

void EnvironmentDebugPanel::DrawLocalFogVolumes()
{
  if (!ImGui::CollapsingHeader("Local Fog Volumes")) {
    return;
  }

  const int count = environment_vm_->GetLocalFogVolumeCount();
  ImGui::Text("Count: %d", count);
  if (ImGui::Button("Add Local Fog Volume")) {
    commands_->Run(env_commands::kAddLocalFogVolume);
  }
  ImGui::SameLine();
  const bool has_selection = count > 0;
  ImGui::BeginDisabled(!has_selection);
  if (ImGui::Button("Remove Selected")) {
    commands_->Run(env_commands::kRemoveLocalFogVolume);
  }
  ImGui::EndDisabled();

  if (!has_selection) {
    return;
  }

  int selected_index = environment_vm_->GetSelectedLocalFogVolumeIndex();
  if (ImGui::SliderInt(
        "Selected", &selected_index, 0, std::max(count - 1, 0))) {
    commands_->RunInt(
      env_commands::kSelectedLocalFogVolumeIndex, selected_index);
  }

  bool enabled = environment_vm_->GetSelectedLocalFogVolumeEnabled();
  if (ImGui::Checkbox("Enabled##LocalFog", &enabled)) {
    commands_->RunBool(env_commands::kSelectedLocalFogVolumeEnabled, enabled);
  }

  float radial_extinction
    = environment_vm_->GetSelectedLocalFogVolumeRadialFogExtinction();
  if (ImGui::DragFloat("Radial Extinction (1/m)##LocalFog", &radial_extinction,
        0.001F, 0.0F, 10.0F, "%.4f")) {
    commands_->RunFloat(
      env_commands::kSelectedLocalFogVolumeRadialFogExtinction,
      radial_extinction);
  }

  float height_extinction
    = environment_vm_->GetSelectedLocalFogVolumeHeightFogExtinction();
  if (ImGui::DragFloat("Height Extinction (1/m)##LocalFog", &height_extinction,
        0.001F, 0.0F, 10.0F, "%.4f")) {
    commands_->RunFloat(
      env_commands::kSelectedLocalFogVolumeHeightFogExtinction,
      height_extinction);
  }

  float height_falloff
    = environment_vm_->GetSelectedLocalFogVolumeHeightFogFalloff();
  if (ImGui::DragFloat("Height Falloff (1/m)##LocalFog", &height_falloff,
        0.001F, 0.0F, 10.0F, "%.4f")) {
    commands_->RunFloat(
      env_commands::kSelectedLocalFogVolumeHeightFogFalloff, height_falloff);
  }

  float height_offset
    = environment_vm_->GetSelectedLocalFogVolumeHeightFogOffset();
  if (ImGui::DragFloat("Height Offset (m)##LocalFog", &height_offset, 0.05F,
        -1000.0F, 1000.0F, "%.2f")) {
    commands_->RunFloat(
      env_commands::kSelectedLocalFogVolumeHeightFogOffset, height_offset);
  }

  float fog_phase_g = environment_vm_->GetSelectedLocalFogVolumeFogPhaseG();
  if (ImGui::SliderFloat(
        "Phase g##LocalFog", &fog_phase_g, -0.99F, 0.99F, "%.2f")) {
    commands_->RunFloat(
      env_commands::kSelectedLocalFogVolumeFogPhaseG, fog_phase_g);
  }

  auto fog_albedo = environment_vm_->GetSelectedLocalFogVolumeFogAlbedo();
  if (ImGui::ColorEdit3(
        "Albedo##LocalFog", &fog_albedo.x, ImGuiColorEditFlags_Float)) {
    commands_->RunRgb(
      env_commands::kSelectedLocalFogVolumeFogAlbedo, fog_albedo);
  }

  auto fog_emissive = environment_vm_->GetSelectedLocalFogVolumeFogEmissive();
  if (ImGui::ColorEdit3(
        "Emissive##LocalFog", &fog_emissive.x, ImGuiColorEditFlags_Float)) {
    commands_->RunRgb(
      env_commands::kSelectedLocalFogVolumeFogEmissive, fog_emissive);
  }

  int sort_priority = environment_vm_->GetSelectedLocalFogVolumeSortPriority();
  if (ImGui::DragInt("Sort Priority##LocalFog", &sort_priority, 1, -128, 128)) {
    commands_->RunInt(
      env_commands::kSelectedLocalFogVolumeSortPriority, sort_priority);
  }
}

auto EnvironmentDebugPanel::GetName() const noexcept -> std::string_view
{
  return "Environment";
}

auto EnvironmentDebugPanel::GetPreferredWidth() const noexcept -> float
{
  return 420.0F;
}

auto EnvironmentDebugPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconEnvironment;
}

auto EnvironmentDebugPanel::OnRegistered() -> void { }

auto EnvironmentDebugPanel::OnLoaded() -> void
{
  if (environment_vm_) {
    environment_vm_->RequestResync();
  }
}

auto EnvironmentDebugPanel::OnUnloaded() -> void { }

void EnvironmentDebugPanel::DrawRuntimeStateSection()
{
  ImGui::SeparatorText("Runtime State");

  const bool atmosphere_enabled = environment_vm_->GetSkyAtmosphereEnabled();
  const bool sky_sphere_enabled = environment_vm_->GetSkySphereEnabled();
  const int sky_sphere_source = environment_vm_->GetSkySphereSource();
  const bool sky_sphere_has_cubemap
    = !environment_vm_->GetSkySphereCubemapResourceKey().IsPlaceholder();
  const bool sky_sphere_visible = sky_sphere_enabled && !atmosphere_enabled
    && (sky_sphere_source == 1 || sky_sphere_has_cubemap);

  ImGui::Text("Visible sky:");
  ImGui::SameLine();
  if (atmosphere_enabled) {
    ImGui::TextColored(ImVec4(0.4F, 0.9F, 1.0F, 1.0F), "Sky Atmosphere");
  } else if (sky_sphere_visible) {
    ImGui::TextColored(ImVec4(0.4F, 0.9F, 1.0F, 1.0F),
      sky_sphere_source == 0 ? "Sky Sphere Cubemap" : "Solid Sky Color");
  } else {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F), "None");
  }

  const bool height_fog_enabled = environment_vm_->GetFogEnabled();
  const bool height_fog_requested
    = environment_vm_->GetHeightFogPassRequested();
  ImGui::Text("Height fog:");
  ImGui::SameLine();
  if (height_fog_enabled && height_fog_requested) {
    ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1.0F), "Active");
  } else if (height_fog_enabled) {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F), "Enabled, not routed");
  } else {
    ImGui::TextDisabled("Off");
  }

  if (!sky_light_section_open_) {
    ImGui::TextUnformatted("Sky lighting:");
    ImGui::SameLine();
    DrawSkyLightFeedback();
  }
}

void EnvironmentDebugPanel::DrawSunSection()
{
  environment_vm_->UpdateSunLightCandidate();
  if (!environment_vm_->GetSunLightAvailable()) {
    ImGui::TextWrapped(environment_vm_->SupportsPreviewSun()
        ? "Enable Preview sun above to adjust sunlight."
        : "This scene has no sun to adjust.");
    return;
  }
  bool sun_enabled = environment_vm_->GetSunEnabled();
  if (ImGui::Checkbox("Enabled##Sun", &sun_enabled)) {
    commands_->RunBool(env_commands::kSunEnabled, sun_enabled);
  }

  ImGui::Separator();
  ImGui::Text("Direction (toward sun):");

  float sun_azimuth_deg = environment_vm_->GetSunAzimuthDeg();
  if (ImGui::SliderFloat(
        "Azimuth (deg)", &sun_azimuth_deg, 0.0F, 360.0F, "%.1F")) {
    commands_->RunFloat(env_commands::kSunAzimuthDeg, sun_azimuth_deg);
  }

  float sun_elevation_deg = environment_vm_->GetSunElevationDeg();
  if (ImGui::DragFloat(
        "Elevation (deg)", &sun_elevation_deg, 0.1F, -90.0F, 90.0F, "%.1F")) {
    commands_->RunFloat(env_commands::kSunElevationDeg, sun_elevation_deg);
  }

  const auto sun_dir
    = DirectionFromAzimuthElevation(sun_azimuth_deg, sun_elevation_deg);
  ImGui::Text("Direction: (%.2F, %.2F, %.2F)", sun_dir.x, sun_dir.y, sun_dir.z);

  ImGui::Separator();
  ImGui::Text("Light:");

  float sun_illuminance_lx = environment_vm_->GetSunIlluminanceLx();
  if (ImGui::DragFloat("Illuminance (lux)", &sun_illuminance_lx, 100.0F, 0.0F,
        1000000.0F, "%.1F")) {
    commands_->RunFloat(env_commands::kSunIlluminanceLx, sun_illuminance_lx);
  }

  bool sun_use_temperature = environment_vm_->GetSunUseTemperature();
  if (ImGui::Checkbox("Use temperature", &sun_use_temperature)) {
    commands_->RunBool(env_commands::kSunUseTemperature, sun_use_temperature);
  }

  if (sun_use_temperature) {
    float sun_temperature_kelvin = environment_vm_->GetSunTemperatureKelvin();
    if (ImGui::DragFloat("Temperature (K)", &sun_temperature_kelvin, 50.0F,
          1000.0F, 40000.0F, "%.0F")) {
      commands_->RunFloat(
        env_commands::kSunTemperatureKelvin, sun_temperature_kelvin);
    }
    const auto preview = KelvinToLinearRgb(sun_temperature_kelvin);
    ImGui::ColorButton("Temperature Preview",
      ImVec4(preview.r, preview.g, preview.b, 1.0F), ImGuiColorEditFlags_Float);
  }

  ImGui::BeginDisabled(sun_use_temperature);
  auto sun_color_rgb = environment_vm_->GetSunColorRgb();
  std::array sun_color = {
    sun_color_rgb.x,
    sun_color_rgb.y,
    sun_color_rgb.z,
  };
  if (ImGui::ColorEdit3("Color", sun_color.data(),
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    commands_->RunRgb(env_commands::kSunColorRgb,
      { sun_color.at(0), sun_color.at(1), sun_color.at(2) });
  }
  ImGui::EndDisabled();

  ImGui::Separator();
  float sun_source_angle_deg = environment_vm_->GetSunSourceAngleDeg();
  if (ImGui::DragFloat("Source angle (deg)", &sun_source_angle_deg, 0.01F,
        0.01F, 5.0F, "%.3F")) {
    commands_->RunFloat(env_commands::kSunSourceAngleDeg, sun_source_angle_deg);
  }

  ImGui::SeparatorText("Atmosphere Light");
  constexpr std::array kAtmosphereLightSlotLabels = {
    "None",
    "Slot 0 (Primary)",
    "Slot 1 (Secondary)",
  };
  int atmosphere_light_slot = environment_vm_->GetSunAtmosphereLightSlot();
  const bool preview_owns_role = environment_vm_->IsPreviewSunActive();
  ImGui::BeginDisabled(preview_owns_role);
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo("Atmosphere Slot", &atmosphere_light_slot,
        kAtmosphereLightSlotLabels.data(),
        static_cast<int>(kAtmosphereLightSlotLabels.size()))) {
    commands_->RunText(env_commands::kSunAtmosphereLightSlot,
      env_commands::ValueAt(
        env_commands::kAtmosphereLightSlots, atmosphere_light_slot));
  }
  ImGui::EndDisabled();
  if (preview_owns_role) {
    ImGui::TextWrapped("Preview sun uses Primary to light the atmosphere.");
  }
  bool use_per_pixel_atmosphere_transmittance
    = environment_vm_->GetSunUsePerPixelAtmosphereTransmittance();
  if (ImGui::Checkbox("Per-Pixel Atmosphere Transmittance",
        &use_per_pixel_atmosphere_transmittance)) {
    commands_->RunBool(env_commands::kSunUsePerPixelAtmosphereTransmittance,
      use_per_pixel_atmosphere_transmittance);
  }
  auto disk_luminance_scale
    = environment_vm_->GetSunAtmosphereDiskLuminanceScale();
  constexpr auto kSunDiskColorScaleFlags = ImGuiColorEditFlags_DisplayRGB
    | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float
    | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoOptions;
  if (ImGui::ColorEdit3("Atmosphere Sun Disk Color Scale",
        &disk_luminance_scale.x, kSunDiskColorScaleFlags)) {
    commands_->RunRgb(
      env_commands::kSunAtmosphereDiskLuminanceScale, disk_luminance_scale);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary)) {
    ImGui::SetTooltip(
      "Linear RGB multiplier applied only to the visible atmosphere sun disk.\n"
      "Effective disk color also depends on the sun light color and "
      "intensity.");
  }

  ImGui::SeparatorText("Directional Shadows");

  float sun_shadow_bias = environment_vm_->GetSunShadowBias();
  if (ImGui::DragFloat(
        "Shadow bias", &sun_shadow_bias, 0.0001F, 0.0F, 1.0F, "%.4f")) {
    commands_->RunFloat(env_commands::kSunShadowBias, sun_shadow_bias);
  }

  float sun_shadow_normal_bias = environment_vm_->GetSunShadowNormalBias();
  if (ImGui::DragFloat("Shadow normal bias", &sun_shadow_normal_bias, 0.0001F,
        0.0F, 1.0F, "%.4f")) {
    commands_->RunFloat(
      env_commands::kSunShadowNormalBias, sun_shadow_normal_bias);
  }

  int sun_shadow_resolution_hint
    = environment_vm_->GetSunShadowResolutionHint();
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo("Resolution hint", &sun_shadow_resolution_hint,
        kShadowResolutionLabels.data(),
        static_cast<int>(kShadowResolutionLabels.size()))) {
    commands_->RunText(env_commands::kSunShadowResolutionHint,
      env_commands::ValueAt(
        env_commands::kShadowResolutions, sun_shadow_resolution_hint));
  }

  int sun_shadow_cascade_count = environment_vm_->GetSunShadowCascadeCount();
  if (ImGui::SliderInt("Cascade count", &sun_shadow_cascade_count, 1, 4)) {
    commands_->RunInt(
      env_commands::kSunShadowCascadeCount, sun_shadow_cascade_count);
  }

  int sun_shadow_split_mode = environment_vm_->GetSunShadowSplitMode();
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo("Split mode", &sun_shadow_split_mode,
        kShadowSplitModeLabels.data(),
        static_cast<int>(kShadowSplitModeLabels.size()))) {
    commands_->RunText(env_commands::kSunShadowSplitMode,
      env_commands::ValueAt(
        env_commands::kShadowSplitModes, sun_shadow_split_mode));
  }

  const bool generated_splits = sun_shadow_split_mode == 0;

  if (generated_splits) {
    float sun_shadow_max_distance = environment_vm_->GetSunShadowMaxDistance();
    if (ImGui::DragFloat("Max shadow distance", &sun_shadow_max_distance, 1.0F,
          1.0F, 100000.0F, "%.1f")) {
      commands_->RunFloat(
        env_commands::kSunShadowMaxDistance, sun_shadow_max_distance);
    }

    float sun_shadow_distribution_exponent
      = environment_vm_->GetSunShadowDistributionExponent();
    if (ImGui::DragFloat("Distribution exponent",
          &sun_shadow_distribution_exponent, 0.05F, 1.0F, 16.0F, "%.2f")) {
      commands_->RunFloat(env_commands::kSunShadowDistributionExponent,
        sun_shadow_distribution_exponent);
    }
  } else {
    ImGui::TextDisabled("Manual split distances drive cascade ends directly.");
  }

  float sun_shadow_transition_fraction
    = environment_vm_->GetSunShadowTransitionFraction();
  if (ImGui::SliderFloat("Transition fraction", &sun_shadow_transition_fraction,
        0.0F, 1.0F, "%.3f")) {
    commands_->RunFloat(env_commands::kSunShadowTransitionFraction,
      sun_shadow_transition_fraction);
  }

  float sun_shadow_distance_fadeout_fraction
    = environment_vm_->GetSunShadowDistanceFadeoutFraction();
  if (ImGui::SliderFloat("Distance fadeout fraction",
        &sun_shadow_distance_fadeout_fraction, 0.0F, 1.0F, "%.3f")) {
    commands_->RunFloat(env_commands::kSunShadowDistanceFadeoutFraction,
      sun_shadow_distance_fadeout_fraction);
  }

  if (!generated_splits) {
    ImGui::SeparatorText("Manual Cascade Distances");
    float minimum_distance = 0.0F;
    for (int cascade_index = 0; cascade_index < sun_shadow_cascade_count;
      ++cascade_index) {
      float distance
        = environment_vm_->GetSunShadowCascadeDistance(cascade_index);
      const std::string label
        = "Cascade " + std::to_string(cascade_index) + " End";
      const float drag_min = minimum_distance + 0.1F;
      if (ImGui::DragFloat(
            label.c_str(), &distance, 0.5F, drag_min, 100000.0F, "%.1f")) {
        commands_->Run(env_commands::kSunShadowCascadeDistance,
          { { "cascade", cascade_index }, { "distance", distance } });
      }
      minimum_distance = std::max(distance, drag_min);
    }
  }
}

void EnvironmentDebugPanel::DrawSkyAtmosphereSection()
{
  const bool sky_atmo_enabled = environment_vm_->GetSkyAtmosphereEnabled();
  const bool sky_sphere_enabled = environment_vm_->GetSkySphereEnabled();

  // Show warning if both sky systems are enabled
  if (sky_atmo_enabled && sky_sphere_enabled) {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
      "Warning: SkyAtmosphere takes priority over SkySphere");
  }

  bool enabled = sky_atmo_enabled;
  if (ImGui::Checkbox("Enabled##SkyAtmo", &enabled)) {
    commands_->RunBool(env_commands::kSkyAtmosphereEnabled, enabled);
    if (enabled) {
      commands_->RunBool(env_commands::kSkySphereEnabled, false);
    }
  }

  if (!enabled) {
    return;
  }

  ImGui::PushItemWidth(150);

  const std::array transform_modes = {
    "Planet Top @ World Origin",
    "Planet Top @ Component",
    "Planet Center @ Component",
  };
  int transform_mode = environment_vm_->GetSkyAtmosphereTransformMode();
  if (ImGui::Combo("Transform Mode", &transform_mode, transform_modes.data(),
        static_cast<int>(transform_modes.size()))) {
    commands_->RunText(env_commands::kSkyAtmosphereTransformMode,
      env_commands::ValueAt(env_commands::kTransformModes, transform_mode));
  }

  // Planet parameters
  ImGui::SeparatorText("Planet:");
  ImGui::TextDisabled(
    "Earth defaults: radius 6360 km, atmosphere height 100 km. "
    "UI clamps: radius 10-100000 km, height 1-1000 km.");
  float planet_radius_km = environment_vm_->GetPlanetRadiusKm();
  if (ImGui::DragFloat(
        "Radius (km)", &planet_radius_km, 10.0F, 10.0F, 100000.0F, "%.0F")) {
    commands_->RunFloat(env_commands::kPlanetRadiusKm, planet_radius_km);
  }
  float atmosphere_height_km = environment_vm_->GetAtmosphereHeightKm();
  if (ImGui::DragFloat("Atmosphere Height (km)", &atmosphere_height_km, 1.0F,
        1.0F, 1000.0F, "%.1F")) {
    commands_->RunFloat(
      env_commands::kAtmosphereHeightKm, atmosphere_height_km);
  }
  ImGui::SetNextItemWidth(240);
  auto ground_albedo = environment_vm_->GetGroundAlbedo();
  if (ImGui::ColorEdit3("Ground Albedo", &ground_albedo.x)) {
    commands_->RunRgb(env_commands::kGroundAlbedo, ground_albedo);
  }

  ImGui::Separator();

  // Scattering parameters
  ImGui::SeparatorText("Scattering:");
  ImGui::TextDisabled("Earth defaults: Rayleigh 8 km, Mie 1.2 km. "
                      "UI clamps: Rayleigh 0.1-50 km, Mie 0.05-20 km.");
  float rayleigh_scale_height_km = environment_vm_->GetRayleighScaleHeightKm();
  if (ImGui::DragFloat("Rayleigh Scale Height (km)", &rayleigh_scale_height_km,
        0.1F, 0.1F, 50.0F, "%.1F")) {
    commands_->RunFloat(
      env_commands::kRayleighScaleHeightKm, rayleigh_scale_height_km);
  }
  float mie_scale_height_km = environment_vm_->GetMieScaleHeightKm();
  if (ImGui::DragFloat("Mie Scale Height (km)", &mie_scale_height_km, 0.05F,
        0.05F, 20.0F, "%.2F")) {
    commands_->RunFloat(env_commands::kMieScaleHeightKm, mie_scale_height_km);
  }
  float mie_anisotropy = environment_vm_->GetMieAnisotropy();
  if (ImGui::SliderFloat(
        "Mie Anisotropy", &mie_anisotropy, 0.0F, 0.99F, "%.2F")) {
    commands_->RunFloat(env_commands::kMieAnisotropy, mie_anisotropy);
  }
  float mie_absorption_scale = environment_vm_->GetMieAbsorptionScale();
  if (ImGui::SliderFloat(
        "Mie Absorption", &mie_absorption_scale, 0.0F, 5.0F, "%.2F")) {
    commands_->RunFloat(
      env_commands::kMieAbsorptionScale, mie_absorption_scale);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(
      "Scales Mie absorption relative to Earth-like default.\n"
      "0 = pure scattering (bright halos), 1 = Earth (SSA ~0.9),\n"
      "higher = darker/hazier atmosphere.");
  }

  float multi_scattering = environment_vm_->GetMultiScattering();
  if (ImGui::SliderFloat(
        "Multi-Scattering", &multi_scattering, 0.0F, 5.0F, "%.2F")) {
    commands_->RunFloat(env_commands::kMultiScattering, multi_scattering);
  }

  ImGui::SeparatorText("Art Direction");
  auto sky_luminance = environment_vm_->GetSkyLuminanceFactor();
  if (ImGui::ColorEdit3("Sky Luminance Factor", &sky_luminance.x,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    commands_->RunRgb(env_commands::kSkyLuminanceFactor, sky_luminance);
  }
  auto sky_aerial_luminance
    = environment_vm_->GetSkyAndAerialPerspectiveLuminanceFactor();
  if (ImGui::ColorEdit3("Sky+Aerial Luminance", &sky_aerial_luminance.x,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    commands_->RunRgb(env_commands::kSkyAndAerialPerspectiveLuminanceFactor,
      sky_aerial_luminance);
  }

  // Ozone Profile (2-layer density profile)
  ImGui::SeparatorText("Ozone Density Profile (2-layer):");

  constexpr float kMetersToKm = engine::atmos::kMToSkyUnit;
  constexpr float kKmToMeters = engine::atmos::kSkyUnitToM;

  ImGui::TextDisabled("Peak altitude is in km. Slopes are shown in 1/km. "
                      "Offsets are unitless density terms.");

  const auto ozone_profile = environment_vm_->GetOzoneDensityProfile();
  const auto& lower = ozone_profile.layers.at(0);
  const auto& upper = ozone_profile.layers.at(1);

  const bool tent_like = (lower.exp_term == 0.0F && upper.exp_term == 0.0F)
    && (lower.linear_term > 0.0F)
    && (std::abs(upper.linear_term + lower.linear_term) < 1.0e-3F);
  if (tent_like) {
    const float center_km = lower.width_m * kMetersToKm;
    const float half_width_km = (1.0F / lower.linear_term) * kMetersToKm;
    ImGui::TextDisabled("Derived: center=%.1f km, width=%.1f km", center_km,
      2.0F * half_width_km);
  } else {
    ImGui::TextDisabled("Derived: (custom profile)");
  }

  bool ozone_profile_changed = false;

  float peak_alt_km = ozone_profile.layers.at(0).width_m * kMetersToKm;
  if (ImGui::DragFloat(
        "Peak Altitude (km)", &peak_alt_km, 0.1F, 0.0F, 120.0F, "%.2F")) {
    ozone_profile_changed = true;
  }

  float lower_slope_inv_km
    = ozone_profile.layers.at(0).linear_term * kKmToMeters;
  if (ImGui::DragFloat("Lower Slope (1/km)", &lower_slope_inv_km, 0.01F, -1.0F,
        1.0F, "%.4f")) {
    ozone_profile_changed = true;
  }
  float lower_offset = ozone_profile.layers.at(0).constant_term;
  if (ImGui::DragFloat(
        "Lower Offset", &lower_offset, 0.01F, -8.0F, 8.0F, "%.4f")) {
    ozone_profile_changed = true;
  }

  float upper_slope_inv_km
    = ozone_profile.layers.at(1).linear_term * kKmToMeters;
  if (ImGui::DragFloat("Upper Slope (1/km)", &upper_slope_inv_km, 0.01F, -1.0F,
        1.0F, "%.4f")) {
    ozone_profile_changed = true;
  }
  float upper_offset = ozone_profile.layers.at(1).constant_term;
  if (ImGui::DragFloat(
        "Upper Offset", &upper_offset, 0.01F, -8.0F, 8.0F, "%.4f")) {
    ozone_profile_changed = true;
  }

  if (ozone_profile_changed) {
    commands_->Run(env_commands::kOzoneDensityProfile,
      {
        { "peak_km", peak_alt_km },
        { "lower_slope", lower_slope_inv_km },
        { "lower_offset", lower_offset },
        { "upper_slope", upper_slope_inv_km },
        { "upper_offset", upper_offset },
      });
  }

  // Ozone absorption is about 1e-6 per meter; it is shown scaled by 1e6.
  constexpr float kOzoneScale = 1.0e6F;
  glm::vec3 absorption_rgb = environment_vm_->GetOzoneRgb() * kOzoneScale;
  std::array<float, 3> absorption_rgb_arr
    = { absorption_rgb.r, absorption_rgb.g, absorption_rgb.b };

  if (ImGui::DragFloat3("Ozone Absorption (x1e-6 1/m)",
        absorption_rgb_arr.data(), 0.01F, 0.0F, 10.0F, "%.3f")) {
    commands_->RunRgb(env_commands::kOzoneRgb,
      glm::vec3(absorption_rgb_arr.at(0), absorption_rgb_arr.at(1),
        absorption_rgb_arr.at(2))
        / kOzoneScale);
  }
  ImGui::SameLine();
  if (ImGui::ColorEdit3("##OzoneColorPreview", absorption_rgb_arr.data(),
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_HDR)) {
    commands_->RunRgb(env_commands::kOzoneRgb,
      glm::vec3(absorption_rgb_arr.at(0), absorption_rgb_arr.at(1),
        absorption_rgb_arr.at(2))
        / kOzoneScale);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(
      "Absorption coefficient shown as x1e-6 1/m.\n"
      "Default Earth ozone is approximately (0.65, 1.88, 0.085).");
  }

  // Sun disk
  ImGui::SeparatorText("Sun Disk");
  bool sun_disk_enabled = environment_vm_->GetSunDiskEnabled();
  if (ImGui::Checkbox("Show Sun Disk", &sun_disk_enabled)) {
    commands_->RunBool(env_commands::kSunDiskEnabled, sun_disk_enabled);
  }
  ImGui::TextDisabled("Radius is controlled in the Sun section.");

  ImGui::SeparatorText("Aerial Perspective");

  bool aerial_perspective_enabled
    = environment_vm_->GetAerialPerspectiveEnabled();
  if (ImGui::Checkbox(
        "Enabled##AerialPerspective", &aerial_perspective_enabled)) {
    commands_->RunBool(
      env_commands::kAerialPerspectiveEnabled, aerial_perspective_enabled);
  }
  ImGui::TextDisabled("Strength zero disables AP without disabling sky");

  ImGui::BeginDisabled(!aerial_perspective_enabled);
  float aerial_perspective_scale = environment_vm_->GetAerialPerspectiveScale();
  if (ImGui::DragFloat("Distance Scale", &aerial_perspective_scale, 1.0F, 0.0F,
        10000.0F, "%.1F")) {
    commands_->RunFloat(
      env_commands::kAerialPerspectiveScale, aerial_perspective_scale);
  }
  float aerial_start_depth
    = environment_vm_->GetAerialPerspectiveStartDepthMeters();
  if (ImGui::DragFloat("Start Depth (m)", &aerial_start_depth, 1.0F, 0.0F,
        1000000.0F, "%.1F")) {
    commands_->RunFloat(
      env_commands::kAerialPerspectiveStartDepthMeters, aerial_start_depth);
  }
  float aerial_scattering_strength
    = environment_vm_->GetAerialScatteringStrength();
  if (ImGui::DragFloat("Haze Strength", &aerial_scattering_strength, 0.01F,
        0.0F, 100.0F, "%.2F")) {
    commands_->RunFloat(
      env_commands::kAerialScatteringStrength, aerial_scattering_strength);
  }
  float height_fog_contribution = environment_vm_->GetHeightFogContribution();
  if (ImGui::SliderFloat("Height Fog Contribution", &height_fog_contribution,
        0.0F, 1.0F, "%.2F")) {
    commands_->RunFloat(
      env_commands::kHeightFogContribution, height_fog_contribution);
  }
  float trace_sample_count_scale = environment_vm_->GetTraceSampleCountScale();
  if (ImGui::SliderFloat(
        "Trace Sample Scale", &trace_sample_count_scale, 0.25F, 8.0F, "%.2F")) {
    commands_->RunFloat(
      env_commands::kTraceSampleCountScale, trace_sample_count_scale);
  }
  float transmittance_min_light_elevation
    = environment_vm_->GetTransmittanceMinLightElevationDeg();
  if (ImGui::SliderFloat("Min Light Elevation (deg)",
        &transmittance_min_light_elevation, -90.0F, 90.0F, "%.1F")) {
    commands_->RunFloat(env_commands::kTransmittanceMinLightElevationDeg,
      transmittance_min_light_elevation);
  }
  bool atmosphere_holdout = environment_vm_->GetAtmosphereHoldout();
  if (ImGui::Checkbox("Holdout##SkyAtmo", &atmosphere_holdout)) {
    commands_->RunBool(env_commands::kAtmosphereHoldout, atmosphere_holdout);
  }
  bool atmosphere_render_in_main_pass
    = environment_vm_->GetAtmosphereRenderInMainPass();
  if (ImGui::Checkbox(
        "Render In Main Pass##SkyAtmo", &atmosphere_render_in_main_pass)) {
    commands_->RunBool(env_commands::kAtmosphereRenderInMainPass,
      atmosphere_render_in_main_pass);
  }
  ImGui::EndDisabled();

  ImGui::PopItemWidth();
}

void EnvironmentDebugPanel::DrawSkySphereSection()
{
  const bool sky_atmo_enabled = environment_vm_->GetSkyAtmosphereEnabled();
  const bool sky_sphere_enabled = environment_vm_->GetSkySphereEnabled();

  if (sky_atmo_enabled && sky_sphere_enabled) {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
      "Warning: SkySphere is disabled when SkyAtmosphere is active");
  }

  bool enabled = sky_sphere_enabled;
  if (ImGui::Checkbox("Enabled##SkySphere", &enabled)) {
    if (enabled) {
      commands_->RunBool(env_commands::kSkyAtmosphereEnabled, false);
      commands_->RunBool(env_commands::kSkySphereEnabled, true);
    } else {
      commands_->RunBool(env_commands::kSkySphereEnabled, false);
    }
  }

  if (!enabled) {
    return;
  }

  ImGui::Indent();
  ImGui::PushItemWidth(150);

  const std::array sources = { "Cubemap", "Solid Color" };
  int sky_sphere_source = environment_vm_->GetSkySphereSource();
  if (ImGui::Combo("Source##SkySphere", &sky_sphere_source, sources.data(),
        static_cast<int>(sources.size()))) {
    commands_->RunText(env_commands::kSkySphereSource,
      env_commands::ValueAt(
        env_commands::kSkySphereSources, sky_sphere_source));
  }

  if (sky_sphere_source == 0) { // Cubemap
    if (environment_vm_->GetSkySphereCubemapResourceKey().IsPlaceholder()) {
      ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F), "No cubemap bound");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Skybox Loader");
    ImGui::TextDisabled(
      "Loads an image from disk, cooks it to a cubemap, and binds it.");

    ImGui::PushItemWidth(280);
    const auto skybox_path = environment_vm_->GetSkyboxPath();
    if (skybox_path_.front() == '\0' && !skybox_path.empty()) {
      CopyPathToBuffer(std::filesystem::path(skybox_path),
        std::span(skybox_path_.data(), skybox_path_.size()));
    }
    ImGui::InputText("Path##Skybox", skybox_path_.data(), skybox_path_.size());
    const bool path_active = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      commands_->RunText(
        env_commands::kSkyboxPath, std::string_view(skybox_path_.data()));
    } else if (!path_active) {
      const std::string current_path(skybox_path_.data());
      if (!skybox_path.empty() && skybox_path != current_path) {
        CopyPathToBuffer(std::filesystem::path(skybox_path),
          std::span(skybox_path_.data(), skybox_path_.size()));
      }
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Browse...##Skybox")) {
      environment_vm_->BeginSkyboxBrowse(std::string_view(skybox_path_.data()));
    }

    if (const auto selected_path
      = environment_vm_->ConsumeSkyboxBrowseResult()) {
      CopyPathToBuffer(
        *selected_path, std::span(skybox_path_.data(), skybox_path_.size()));
      commands_->RunText(
        env_commands::kSkyboxPath, std::string_view(skybox_path_.data()));
    }

    const std::array layouts = {
      "Equirectangular",
      "Horizontal Cross",
      "Vertical Cross",
      "Horizontal Strip",
      "Vertical Strip",
    };
    int skybox_layout_idx = environment_vm_->GetSkyboxLayoutIndex();
    if (ImGui::Combo("Layout##Skybox", &skybox_layout_idx, layouts.data(),
          static_cast<int>(layouts.size()))) {
      commands_->RunText(env_commands::kSkyboxLayout,
        env_commands::ValueAt(env_commands::kSkyboxLayouts, skybox_layout_idx));
    }

    const std::array formats = { "RGBA8", "RGBA16F", "RGBA32F", "BC7" };
    int skybox_output_format_idx
      = environment_vm_->GetSkyboxOutputFormatIndex();
    if (ImGui::Combo("Output##Skybox", &skybox_output_format_idx,
          formats.data(), static_cast<int>(formats.size()))) {
      commands_->RunText(env_commands::kSkyboxFormat,
        env_commands::ValueAt(
          env_commands::kSkyboxFormats, skybox_output_format_idx));
    }

    int skybox_face_size = environment_vm_->GetSkyboxFaceSize();
    if (ImGui::DragInt("Face Size##Skybox", &skybox_face_size, 16, 16, 4096)) {
      commands_->RunInt(env_commands::kSkyboxFaceSize, skybox_face_size);
    }
    bool skybox_flip_y = environment_vm_->GetSkyboxFlipY();
    if (ImGui::Checkbox("Flip Y##Skybox", &skybox_flip_y)) {
      commands_->RunBool(env_commands::kSkyboxFlipY, skybox_flip_y);
    }

    const bool output_is_ldr
      = skybox_output_format_idx == 0 || skybox_output_format_idx == 3;
    if (output_is_ldr) {
      bool skybox_tonemap_hdr_to_ldr
        = environment_vm_->GetSkyboxTonemapHdrToLdr();
      if (ImGui::Checkbox(
            "HDR->LDR Tonemap##Skybox", &skybox_tonemap_hdr_to_ldr)) {
        commands_->RunBool(
          env_commands::kSkyboxTonemapHdrToLdr, skybox_tonemap_hdr_to_ldr);
      }
      float skybox_hdr_exposure_ev = environment_vm_->GetSkyboxHdrExposureEv();
      if (ImGui::DragFloat("HDR Exposure (EV)##Skybox", &skybox_hdr_exposure_ev,
            0.1F, 0.0F, 16.0F, "%.2F")) {
        commands_->RunFloat(env_commands::kSkyboxHdrExposureEv,
          std::max(skybox_hdr_exposure_ev, 0.0F));
      }
    }

    if (ImGui::Button("Load Skybox##Skybox")) {
      commands_->Run(env_commands::kLoadSkybox,
        {
          { "path", std::string_view(skybox_path_.data()) },
          {
            "layout",
            env_commands::ValueAt(
              env_commands::kSkyboxLayouts, skybox_layout_idx),
          },
          {
            "format",
            env_commands::ValueAt(
              env_commands::kSkyboxFormats, skybox_output_format_idx),
          },
          { "face_size", skybox_face_size },
          { "flip_y", skybox_flip_y },
          { "tonemap", environment_vm_->GetSkyboxTonemapHdrToLdr() },
          { "exposure_ev", environment_vm_->GetSkyboxHdrExposureEv() },
        });
    }
    ImGui::SameLine();
    const auto status_message = environment_vm_->GetSkyboxStatusMessage();
    if (!status_message.empty()) {
      ImGui::TextUnformatted(std::string(status_message).c_str());
    }

  } else { // Solid color
    auto sky_sphere_solid_color = environment_vm_->GetSkySphereSolidColor();
    if (ImGui::ColorEdit3("Color##SkySphere", &sky_sphere_solid_color.x)) {
      commands_->RunRgb(
        env_commands::kSkySphereSolidColor, sky_sphere_solid_color);
    }
  }

  float illuminance = environment_vm_->GetSkySphereIlluminanceLux();
  if (ImGui::DragFloat("Sky Illuminance (lux)", &illuminance, 50.0F, 0.0F,
        200000.0F, illuminance > 0.0F ? "%.0f lux" : "Off (raw image)",
        ImGuiSliderFlags_Logarithmic)) {
    commands_->RunFloat(env_commands::kSkySphereIlluminanceLux, illuminance);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(
      "Calibrates the sky to a physical brightness.\n"
      "\n"
      "Sets the illuminance the sky delivers on an upward-facing surface.\n"
      "The image (or solid color) is scaled to deliver it, both for what\n"
      "the view shows and for the light a Captured Scene sky light reads,\n"
      "so the sky matches a physical sun.\n"
      "\n"
      "HDR images rarely carry real units: a typical one delivers a few\n"
      "lux, while a daylight sun delivers 25,000 to 100,000 lux.\n"
      "Uncalibrated, auto exposure meters the dim sky and burns sunlit\n"
      "surfaces to white.\n"
      "\n"
      "Typical values: clear day 10,000-25,000; overcast 1,000-10,000;\n"
      "sunset 100-1,000; night below 1. 0 uses the image's own values.\n"
      "\n"
      "Only a float (HDR) cubemap can be measured: an LDR one stays\n"
      "uncalibrated and the renderer warns (sky.uncalibrated).");
  }

  const float sky_intensity = environment_vm_->GetSkyIntensity();
  float sky_exposure_ev = SkySphereRadianceScaleToEv(sky_intensity);
  const char* exposure_label = sky_sphere_source == 0
    ? "Skybox Exposure (EV)"
    : "Sky Color Exposure (EV)";
  if (ImGui::DragFloat(exposure_label, &sky_exposure_ev, 0.05F,
        kMinSkySphereExposureEv, kMaxSkySphereExposureEv, "%+.2f")) {
    commands_->RunFloat(env_commands::kSkyIntensity,
      SkySphereExposureEvToRadianceScale(sky_exposure_ev));
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(
      "Display-only brightness adjustment, in exposure stops.\n"
      "Radiance scale %.6g (EV +8 = 256, EV +10 = 1024).\n"
      "It changes what the view shows, never the light the sky casts:\n"
      "calibrate with Sky Illuminance, and scale lighting with the Sky\n"
      "Light's intensity.",
      static_cast<double>(sky_intensity));
  }

  float sky_sphere_rotation_deg = environment_vm_->GetSkySphereRotationDeg();
  if (ImGui::SliderFloat(
        "Rotation (deg)", &sky_sphere_rotation_deg, 0.0F, 360.0F, "%.1F")) {
    commands_->RunFloat(
      env_commands::kSkySphereRotationDeg, sky_sphere_rotation_deg);
  }

  ImGui::PopItemWidth();
  ImGui::Unindent();
}

void EnvironmentDebugPanel::DrawSkyLightFeedback()
{
  const auto feedback = environment_vm_->GetSkyLightFeedback();
  auto color = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
  if (feedback.warning) {
    color = ImVec4(1.0F, 0.7F, 0.0F, 1.0F);
  } else if (feedback.active) {
    color = ImVec4(0.4F, 0.9F, 0.4F, 1.0F);
  }
  ImGui::TextColored(color, "%.*s", static_cast<int>(feedback.label.size()),
    feedback.label.data());
  if (!feedback.detail.empty() && ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "%.*s", static_cast<int>(feedback.detail.size()), feedback.detail.data());
  }
}

void EnvironmentDebugPanel::DrawSkyLightSection()
{
  bool enabled = environment_vm_->GetSkyLightEnabled();
  if (ImGui::Checkbox("Enabled##SkyLight", &enabled)) {
    commands_->RunBool(env_commands::kSkyLightEnabled, enabled);
  }

  if (!enabled) {
    return;
  }

  ImGui::PushItemWidth(150);

  float intensity = environment_vm_->GetSkyLightIntensityMul();
  if (ImGui::DragFloat("Intensity##SkyLight", &intensity, 0.01F, 0.0F,
        100000.0F, "%.2F", ImGuiSliderFlags_Logarithmic)) {
    commands_->RunFloat(env_commands::kSkyLightIntensityMul, intensity);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(
      "Multiplies the sky light's diffuse and specular lighting.\n"
      "1 lights with the sky's (calibrated) radiance as is. To match a\n"
      "physical sun, calibrate the source with Sky Illuminance instead of\n"
      "raising this.");
  }

  const std::array sources = { "Captured Scene", "Specified Cubemap" };
  int source = environment_vm_->GetSkyLightSource();
  ImGui::SetNextItemWidth(220.0F);
  if (ImGui::Combo("Source##SkyLight", &source, sources.data(),
        static_cast<int>(sources.size()))) {
    commands_->RunText(env_commands::kSkyLightSource,
      env_commands::ValueAt(env_commands::kSkyLightSources, source));
  }
  if (source == 1) {
    float cubemap_lux = environment_vm_->GetSkyLightIlluminanceLux();
    if (ImGui::DragFloat("Cubemap Illuminance (lux)", &cubemap_lux, 50.0F, 0.0F,
          200000.0F, cubemap_lux > 0.0F ? "%.0f lux" : "Off (raw image)",
          ImGuiSliderFlags_Logarithmic)) {
      commands_->RunFloat(env_commands::kSkyLightIlluminanceLux, cubemap_lux);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::SetTooltip(
        "Calibrates the specified cubemap to a physical brightness.\n"
        "\n"
        "Sets the illuminance the cubemap delivers on an upward-facing\n"
        "surface; its radiance is scaled to deliver it before the sky\n"
        "light's multipliers apply. A Captured Scene sky light uses the\n"
        "Sky Sphere's calibration instead.\n"
        "\n"
        "Typical values: clear day 10,000-25,000; overcast 1,000-10,000.\n"
        "0 uses the image's own values. Only a float (HDR) cubemap can be\n"
        "measured and light the scene.");
    }
  }
  DrawSkyLightFeedback();
  if (!ImGui::TreeNode("Advanced##SkyLight")) {
    ImGui::PopItemWidth();
    return;
  }

  auto sky_light_tint = environment_vm_->GetSkyLightTint();
  if (ImGui::ColorEdit3("Tint##SkyLight", &sky_light_tint.x)) {
    commands_->RunRgb(env_commands::kSkyLightTint, sky_light_tint);
  }

  float sky_light_diffuse = environment_vm_->GetSkyLightDiffuse();
  if (ImGui::DragFloat(
        "Diffuse Indirect", &sky_light_diffuse, 0.01F, 0.0F, 6.0F, "%.2F")) {
    commands_->RunFloat(env_commands::kSkyLightDiffuse, sky_light_diffuse);
  }

  float specular = environment_vm_->GetSkyLightSpecular();
  if (ImGui::DragFloat(
        "Specular Indirect", &specular, 0.01F, 0.0F, 6.0F, "%.2F")) {
    commands_->RunFloat(env_commands::kSkyLightSpecular, specular);
  }
  bool reflections = environment_vm_->GetSkyLightAffectReflections();
  if (ImGui::Checkbox("Affect Reflections", &reflections)) {
    commands_->RunBool(env_commands::kSkyLightAffectReflections, reflections);
  }

  bool override_hemisphere
    = environment_vm_->GetSkyLightLowerHemisphereOverride();
  if (ImGui::Checkbox("Override Lower Hemisphere", &override_hemisphere)) {
    commands_->RunBool(
      env_commands::kSkyLightLowerHemisphereOverride, override_hemisphere);
  }
  ImGui::BeginDisabled(!override_hemisphere);
  auto lower_hemisphere = environment_vm_->GetSkyLightLowerHemisphereColor();
  if (ImGui::ColorEdit3(
        "Lower Hemisphere", &lower_hemisphere.x, ImGuiColorEditFlags_Float)) {
    commands_->RunRgb(
      env_commands::kSkyLightLowerHemisphereColor, lower_hemisphere);
  }
  float blend = environment_vm_->GetSkyLightLowerHemisphereBlend();
  if (ImGui::SliderFloat("Hemisphere Blend", &blend, 0.0F, 1.0F, "%.2F")) {
    commands_->RunFloat(env_commands::kSkyLightLowerHemisphereBlend, blend);
  }
  ImGui::EndDisabled();

  float volumetric_scattering_intensity
    = environment_vm_->GetSkyLightVolumetricScatteringIntensity();
  if (ImGui::DragFloat("Volumetric Scattering",
        &volumetric_scattering_intensity, 0.01F, 0.0F, 4.0F, "%.2F")) {
    commands_->RunFloat(env_commands::kSkyLightVolumetricScatteringIntensity,
      volumetric_scattering_intensity);
  }

  ImGui::TreePop();
  ImGui::PopItemWidth();
}

// NOLINTEND(*-magic-numbers, bugprone-signed-bitwise)

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
