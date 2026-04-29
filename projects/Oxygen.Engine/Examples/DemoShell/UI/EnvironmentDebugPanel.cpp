//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <span>
#include <string>

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/EnvironmentDebugPanel.h"
#include "DemoShell/UI/EnvironmentVm.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)

namespace oxygen::examples::ui {

namespace {

  constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0F;
  constexpr float kMaxSkySphereIntensity = 100000.0F;
  constexpr float kMinSkySphereExposureEv = -16.0F;
  constexpr float kMaxSkySphereExposureEv = 16.6F;
  constexpr const char* kShadowResolutionLabels[] = {
    "Low",
    "Medium",
    "High",
    "Ultra",
  };
  constexpr const char* kShadowSplitModeLabels[] = {
    "Generated",
    "Manual Distances",
  };

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
    return std::clamp(
      std::exp2(exposure_ev), 0.0F, kMaxSkySphereIntensity);
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
        0.39008157877F * std::log(temp) - 0.63184144378F, 0.0F, 1.0F);
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
    buffer[to_copy] = '\0';
  }

} // namespace

void EnvironmentDebugPanel::Initialize(const EnvironmentDebugConfig& config)
{
  config_ = config;
  environment_vm_ = config_.environment_vm;
  CHECK_NOTNULL_F(
    environment_vm_, "EnvironmentDebugPanel requires an EnvironmentVm");
  initialized_ = true;
  environment_vm_->SyncFromSceneIfNeeded();
}

void EnvironmentDebugPanel::UpdateConfig(const EnvironmentDebugConfig& config)
{
  config_ = config;
}

auto EnvironmentDebugPanel::DrawContents() -> void
{
  if (!initialized_) {
    return;
  }

  if (!environment_vm_) {
    return;
  }
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

  DrawRuntimeStateSection();

  HandleSkyboxAutoLoad();

  ImGui::Spacing();
  ImGui::SeparatorText("Presets");
  const auto preset_label = environment_vm_->GetPresetLabel();
  ImGui::SetNextItemWidth(220.0F);
  if (ImGui::BeginCombo("Environment Preset", preset_label.data())) {
    const int current_index = environment_vm_->GetPresetIndex();
    const int preset_count = environment_vm_->GetPresetCount();
    for (int i = 0; i < preset_count; ++i) {
      const auto name = environment_vm_->GetPresetName(i);
      if (ImGui::Selectable(name.data(), i == current_index)) {
        environment_vm_->ApplyPreset(i);
      }
    }
    ImGui::EndCombo();
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

  if (environment_vm_->GetFogModel() != 0) {
    environment_vm_->SetFogModel(0);
  }

  bool fog_enabled = environment_vm_->GetFogEnabled();
  if (ImGui::Checkbox("Enable Height Fog", &fog_enabled)) {
    environment_vm_->SetFogModel(0);
    environment_vm_->SetFogEnabled(fog_enabled);
  }

  bool fog_main_pass = environment_vm_->GetFogRenderInMainPass();
  if (ImGui::Checkbox("Render In Main Pass##Fog", &fog_main_pass)) {
    environment_vm_->SetFogRenderInMainPass(fog_main_pass);
  }

  ImGui::BeginDisabled(!fog_enabled || !fog_main_pass);

  ImGui::SeparatorText("Primary Layer");
  float extinction_sigma_t_per_m
    = environment_vm_->GetFogExtinctionSigmaTPerMeter();
  if (ImGui::SliderFloat("Density / Extinction (1/m)",
        &extinction_sigma_t_per_m, 0.0F, 10.0F, "%.6f",
        ImGuiSliderFlags_Logarithmic)) {
    environment_vm_->SetFogExtinctionSigmaTPerMeter(extinction_sigma_t_per_m);
  }

  float height_falloff_per_m = environment_vm_->GetFogHeightFalloffPerMeter();
  if (ImGui::DragFloat("Height Falloff (1/m)", &height_falloff_per_m, 0.0001F,
        0.0F, 10.0F, "%.4f")) {
    environment_vm_->SetFogHeightFalloffPerMeter(height_falloff_per_m);
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
    environment_vm_->SetFogHeightOffsetMeters(height_offset_m);
  }

  ImGui::SeparatorText("Distance and Opacity");
  float start_distance_m = environment_vm_->GetFogStartDistanceMeters();
  if (ImGui::DragFloat("Start Distance (m)", &start_distance_m, 1.0F, 0.0F,
        1000000.0F, "%.1f")) {
    environment_vm_->SetFogStartDistanceMeters(
      std::max(start_distance_m, 0.0F));
  }
  float end_distance = environment_vm_->GetFogEndDistanceMeters();
  if (ImGui::DragFloat(
        "End Distance (m)", &end_distance, 1.0F, 0.0F, 1000000.0F, "%.1f")) {
    environment_vm_->SetFogEndDistanceMeters(end_distance);
  }
  float cutoff_distance = environment_vm_->GetFogCutoffDistanceMeters();
  if (ImGui::DragFloat("Cutoff Distance (m)", &cutoff_distance, 10.0F, 0.0F,
        1000000.0F, "%.1f")) {
    environment_vm_->SetFogCutoffDistanceMeters(cutoff_distance);
  }
  float max_opacity = environment_vm_->GetFogMaxOpacity();
  if (ImGui::SliderFloat("Max Opacity", &max_opacity, 0.0F, 1.0F, "%.3f")) {
    environment_vm_->SetFogMaxOpacity(max_opacity);
  }

  ImGui::SeparatorText("Secondary Layer");
  float second_fog_density = environment_vm_->GetSecondFogDensity();
  if (ImGui::DragFloat("Second Density (1/m)", &second_fog_density, 0.001F,
        0.0F, 10.0F, "%.4f")) {
    environment_vm_->SetSecondFogDensity(second_fog_density);
  }
  float second_fog_height_falloff
    = environment_vm_->GetSecondFogHeightFalloff();
  if (ImGui::DragFloat("Second Height Falloff (1/m)",
        &second_fog_height_falloff, 0.0001F, 0.0F, 10.0F, "%.4f")) {
    environment_vm_->SetSecondFogHeightFalloff(second_fog_height_falloff);
  }
  float second_fog_height_offset = environment_vm_->GetSecondFogHeightOffset();
  if (ImGui::DragFloat("Second Height Offset (m)", &second_fog_height_offset,
        0.25F, -100000.0F, 100000.0F, "%.1f")) {
    environment_vm_->SetSecondFogHeightOffset(second_fog_height_offset);
  }

  ImGui::SeparatorText("Inscattering");
  auto fog_inscattering = environment_vm_->GetFogInscatteringLuminance();
  if (ImGui::DragFloat3("Fog Inscattering Luminance", &fog_inscattering.x, 1.0F,
        0.0F, 100000.0F, "%.3f")) {
    environment_vm_->SetFogInscatteringLuminance(fog_inscattering);
  }
  auto ambient_scale
    = environment_vm_->GetSkyAtmosphereAmbientContributionColorScale();
  if (ImGui::DragFloat3("Sky Atmosphere Ambient Scale", &ambient_scale.x, 0.01F,
        0.0F, 16.0F, "%.3f")) {
    environment_vm_->SetSkyAtmosphereAmbientContributionColorScale(
      ambient_scale);
  }
  auto directional_luminance
    = environment_vm_->GetDirectionalInscatteringLuminance();
  if (ImGui::DragFloat3("Directional Inscattering Luminance",
        &directional_luminance.x, 1.0F, 0.0F, 100000.0F, "%.3f")) {
    environment_vm_->SetDirectionalInscatteringLuminance(directional_luminance);
  }
  float directional_exponent
    = environment_vm_->GetDirectionalInscatteringExponent();
  if (ImGui::DragFloat("Directional Exponent", &directional_exponent, 0.1F,
        0.0F, 128.0F, "%.2f")) {
    environment_vm_->SetDirectionalInscatteringExponent(directional_exponent);
  }
  float directional_start_distance
    = environment_vm_->GetDirectionalInscatteringStartDistance();
  if (ImGui::DragFloat("Directional Start Distance (m)",
        &directional_start_distance, 1.0F, 0.0F, 1000000.0F, "%.1f")) {
    environment_vm_->SetDirectionalInscatteringStartDistance(
      directional_start_distance);
  }

  ImGui::SeparatorText("Visibility");
  bool fog_holdout = environment_vm_->GetFogHoldout();
  if (ImGui::Checkbox("Holdout##Fog", &fog_holdout)) {
    environment_vm_->SetFogHoldout(fog_holdout);
  }
  bool fog_reflection_captures
    = environment_vm_->GetFogVisibleInReflectionCaptures();
  if (ImGui::Checkbox(
        "Visible In Reflection Captures", &fog_reflection_captures)) {
    environment_vm_->SetFogVisibleInReflectionCaptures(fog_reflection_captures);
  }
  bool fog_realtime_sky = environment_vm_->GetFogVisibleInRealTimeSkyCaptures();
  if (ImGui::Checkbox("Visible In Real-Time Sky Captures", &fog_realtime_sky)) {
    environment_vm_->SetFogVisibleInRealTimeSkyCaptures(fog_realtime_sky);
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
    environment_vm_->AddLocalFogVolume();
  }
  ImGui::SameLine();
  const bool has_selection = count > 0;
  ImGui::BeginDisabled(!has_selection);
  if (ImGui::Button("Remove Selected")) {
    environment_vm_->RemoveSelectedLocalFogVolume();
  }
  ImGui::EndDisabled();

  if (!has_selection) {
    return;
  }

  int selected_index = environment_vm_->GetSelectedLocalFogVolumeIndex();
  if (ImGui::SliderInt(
        "Selected", &selected_index, 0, std::max(count - 1, 0))) {
    environment_vm_->SetSelectedLocalFogVolumeIndex(selected_index);
  }

  bool enabled = environment_vm_->GetSelectedLocalFogVolumeEnabled();
  if (ImGui::Checkbox("Enabled##LocalFog", &enabled)) {
    environment_vm_->SetSelectedLocalFogVolumeEnabled(enabled);
  }

  float radial_extinction
    = environment_vm_->GetSelectedLocalFogVolumeRadialFogExtinction();
  if (ImGui::DragFloat("Radial Extinction (1/m)##LocalFog", &radial_extinction,
        0.001F, 0.0F, 10.0F, "%.4f")) {
    environment_vm_->SetSelectedLocalFogVolumeRadialFogExtinction(
      radial_extinction);
  }

  float height_extinction
    = environment_vm_->GetSelectedLocalFogVolumeHeightFogExtinction();
  if (ImGui::DragFloat("Height Extinction (1/m)##LocalFog", &height_extinction,
        0.001F, 0.0F, 10.0F, "%.4f")) {
    environment_vm_->SetSelectedLocalFogVolumeHeightFogExtinction(
      height_extinction);
  }

  float height_falloff
    = environment_vm_->GetSelectedLocalFogVolumeHeightFogFalloff();
  if (ImGui::DragFloat("Height Falloff (1/m)##LocalFog", &height_falloff,
        0.001F, 0.0F, 10.0F, "%.4f")) {
    environment_vm_->SetSelectedLocalFogVolumeHeightFogFalloff(height_falloff);
  }

  float height_offset
    = environment_vm_->GetSelectedLocalFogVolumeHeightFogOffset();
  if (ImGui::DragFloat("Height Offset (m)##LocalFog", &height_offset, 0.05F,
        -1000.0F, 1000.0F, "%.2f")) {
    environment_vm_->SetSelectedLocalFogVolumeHeightFogOffset(height_offset);
  }

  float fog_phase_g = environment_vm_->GetSelectedLocalFogVolumeFogPhaseG();
  if (ImGui::SliderFloat(
        "Phase g##LocalFog", &fog_phase_g, -0.99F, 0.99F, "%.2f")) {
    environment_vm_->SetSelectedLocalFogVolumeFogPhaseG(fog_phase_g);
  }

  auto fog_albedo = environment_vm_->GetSelectedLocalFogVolumeFogAlbedo();
  if (ImGui::ColorEdit3(
        "Albedo##LocalFog", &fog_albedo.x, ImGuiColorEditFlags_Float)) {
    environment_vm_->SetSelectedLocalFogVolumeFogAlbedo(fog_albedo);
  }

  auto fog_emissive = environment_vm_->GetSelectedLocalFogVolumeFogEmissive();
  if (ImGui::ColorEdit3(
        "Emissive##LocalFog", &fog_emissive.x, ImGuiColorEditFlags_Float)) {
    environment_vm_->SetSelectedLocalFogVolumeFogEmissive(fog_emissive);
  }

  int sort_priority = environment_vm_->GetSelectedLocalFogVolumeSortPriority();
  if (ImGui::DragInt("Sort Priority##LocalFog", &sort_priority, 1, -128, 128)) {
    environment_vm_->SetSelectedLocalFogVolumeSortPriority(sort_priority);
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

auto EnvironmentDebugPanel::OnRegistered() -> void { (void)initialized_; }

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
  const bool height_fog_requested = environment_vm_->GetHeightFogPassRequested();
  ImGui::Text("Height fog:");
  ImGui::SameLine();
  if (height_fog_enabled && height_fog_requested) {
    ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1.0F), "Active");
  } else if (height_fog_enabled) {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F), "Enabled, not routed");
  } else {
    ImGui::TextDisabled("Off");
  }

  const bool sky_light_enabled = environment_vm_->GetSkyLightEnabled();
  const int sky_light_source = environment_vm_->GetSkyLightSource();
  const bool sky_light_has_cubemap
    = !environment_vm_->GetSkyLightCubemapResourceKey().IsPlaceholder();
  const bool sky_light_can_light
    = sky_light_enabled && sky_light_source == 1 && sky_light_has_cubemap
    && environment_vm_->GetSkyLightDiffuse() > 0.0F
    && environment_vm_->GetSkyLightIntensityMul() > 0.0F;

  ImGui::Text("Static SkyLight diffuse:");
  ImGui::SameLine();
  if (sky_light_can_light) {
    ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.4F, 1.0F), "Active");
  } else if (sky_light_enabled) {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F), "Unavailable");
  } else {
    ImGui::TextDisabled("Off");
  }
}

void EnvironmentDebugPanel::DrawSunSection()
{
  bool sun_enabled = environment_vm_->GetSunEnabled();
  if (ImGui::Checkbox("Enabled##Sun", &sun_enabled)) {
    environment_vm_->SetSunEnabled(sun_enabled);
  }

  environment_vm_->UpdateSunLightCandidate();
  if (!environment_vm_->GetSunLightAvailable()) {
    ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F),
      "No resolved scene sun is currently available.");
  }
  ImGui::TextDisabled(
    "Uses the resolved primary scene sun from authored directional lights.");

  const bool disable_sun_controls = !environment_vm_->GetSunLightAvailable();
  ImGui::BeginDisabled(disable_sun_controls);

  ImGui::Separator();
  ImGui::Text("Direction (toward sun):");

  float sun_azimuth_deg = environment_vm_->GetSunAzimuthDeg();
  if (ImGui::SliderFloat(
        "Azimuth (deg)", &sun_azimuth_deg, 0.0F, 360.0F, "%.1F")) {
    environment_vm_->SetSunAzimuthDeg(sun_azimuth_deg);
  }

  float sun_elevation_deg = environment_vm_->GetSunElevationDeg();
  if (ImGui::DragFloat(
        "Elevation (deg)", &sun_elevation_deg, 0.1F, -90.0F, 90.0F, "%.1F")) {
    environment_vm_->SetSunElevationDeg(sun_elevation_deg);
  }

  const auto sun_dir
    = DirectionFromAzimuthElevation(sun_azimuth_deg, sun_elevation_deg);
  ImGui::Text("Direction: (%.2F, %.2F, %.2F)", sun_dir.x, sun_dir.y, sun_dir.z);

  ImGui::Separator();
  ImGui::Text("Light:");

  float sun_illuminance_lx = environment_vm_->GetSunIlluminanceLx();
  if (ImGui::DragFloat("Illuminance (lux)", &sun_illuminance_lx, 100.0F, 0.0F,
        1000000.0F, "%.1F")) {
    environment_vm_->SetSunIlluminanceLx(sun_illuminance_lx);
  }

  bool sun_use_temperature = environment_vm_->GetSunUseTemperature();
  if (ImGui::Checkbox("Use temperature", &sun_use_temperature)) {
    environment_vm_->SetSunUseTemperature(sun_use_temperature);
  }

  if (sun_use_temperature) {
    float sun_temperature_kelvin = environment_vm_->GetSunTemperatureKelvin();
    if (ImGui::DragFloat("Temperature (K)", &sun_temperature_kelvin, 50.0F,
          1000.0F, 40000.0F, "%.0F")) {
      environment_vm_->SetSunTemperatureKelvin(sun_temperature_kelvin);
    }
    const auto preview = KelvinToLinearRgb(sun_temperature_kelvin);
    ImGui::ColorButton("Temperature Preview",
      ImVec4(preview.r, preview.g, preview.b, 1.0F), ImGuiColorEditFlags_Float);
  }

  ImGui::BeginDisabled(sun_use_temperature);
  auto sun_color_rgb = environment_vm_->GetSunColorRgb();
  float sun_color[3] = {
    sun_color_rgb.x,
    sun_color_rgb.y,
    sun_color_rgb.z,
  };
  if (ImGui::ColorEdit3("Color", sun_color,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    environment_vm_->SetSunColorRgb(
      { sun_color[0], sun_color[1], sun_color[2] });
  }
  ImGui::EndDisabled();

  ImGui::Separator();
  float sun_source_angle_deg = environment_vm_->GetSunSourceAngleDeg();
  if (ImGui::DragFloat("Source angle (deg)", &sun_source_angle_deg, 0.01F,
        0.01F, 5.0F, "%.3F")) {
    environment_vm_->SetSunSourceAngleDeg(sun_source_angle_deg);
  }

  ImGui::SeparatorText("Atmosphere Light");
  constexpr const char* kAtmosphereLightSlotLabels[] = {
    "None",
    "Slot 0 (Primary)",
    "Slot 1 (Secondary)",
  };
  int atmosphere_light_slot = environment_vm_->GetSunAtmosphereLightSlot();
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo("Atmosphere Slot", &atmosphere_light_slot,
        kAtmosphereLightSlotLabels, IM_ARRAYSIZE(kAtmosphereLightSlotLabels))) {
    environment_vm_->SetSunAtmosphereLightSlot(atmosphere_light_slot);
  }
  bool use_per_pixel_atmosphere_transmittance
    = environment_vm_->GetSunUsePerPixelAtmosphereTransmittance();
  if (ImGui::Checkbox("Per-Pixel Atmosphere Transmittance",
        &use_per_pixel_atmosphere_transmittance)) {
    environment_vm_->SetSunUsePerPixelAtmosphereTransmittance(
      use_per_pixel_atmosphere_transmittance);
  }
  auto disk_luminance_scale
    = environment_vm_->GetSunAtmosphereDiskLuminanceScale();
  constexpr auto kSunDiskColorScaleFlags = ImGuiColorEditFlags_DisplayRGB
    | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float
    | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoOptions;
  if (ImGui::ColorEdit4("Atmosphere Sun Disk Color Scale",
        &disk_luminance_scale.x, kSunDiskColorScaleFlags)) {
    environment_vm_->SetSunAtmosphereDiskLuminanceScale(disk_luminance_scale);
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
    environment_vm_->SetSunShadowBias(sun_shadow_bias);
  }

  float sun_shadow_normal_bias = environment_vm_->GetSunShadowNormalBias();
  if (ImGui::DragFloat("Shadow normal bias", &sun_shadow_normal_bias, 0.0001F,
        0.0F, 1.0F, "%.4f")) {
    environment_vm_->SetSunShadowNormalBias(sun_shadow_normal_bias);
  }

  int sun_shadow_resolution_hint
    = environment_vm_->GetSunShadowResolutionHint();
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo("Resolution hint", &sun_shadow_resolution_hint,
        kShadowResolutionLabels, IM_ARRAYSIZE(kShadowResolutionLabels))) {
    environment_vm_->SetSunShadowResolutionHint(sun_shadow_resolution_hint);
  }

  int sun_shadow_cascade_count = environment_vm_->GetSunShadowCascadeCount();
  if (ImGui::SliderInt("Cascade count", &sun_shadow_cascade_count, 1, 4)) {
    environment_vm_->SetSunShadowCascadeCount(sun_shadow_cascade_count);
  }

  int sun_shadow_split_mode = environment_vm_->GetSunShadowSplitMode();
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo("Split mode", &sun_shadow_split_mode, kShadowSplitModeLabels,
        IM_ARRAYSIZE(kShadowSplitModeLabels))) {
    environment_vm_->SetSunShadowSplitMode(sun_shadow_split_mode);
  }

  const bool generated_splits = sun_shadow_split_mode == 0;

  if (generated_splits) {
    float sun_shadow_max_distance = environment_vm_->GetSunShadowMaxDistance();
    if (ImGui::DragFloat("Max shadow distance", &sun_shadow_max_distance, 1.0F,
          1.0F, 100000.0F, "%.1f")) {
      environment_vm_->SetSunShadowMaxDistance(sun_shadow_max_distance);
    }

    float sun_shadow_distribution_exponent
      = environment_vm_->GetSunShadowDistributionExponent();
    if (ImGui::DragFloat("Distribution exponent",
          &sun_shadow_distribution_exponent, 0.05F, 1.0F, 16.0F, "%.2f")) {
      environment_vm_->SetSunShadowDistributionExponent(
        sun_shadow_distribution_exponent);
    }
  } else {
    ImGui::TextDisabled("Manual split distances drive cascade ends directly.");
  }

  float sun_shadow_transition_fraction
    = environment_vm_->GetSunShadowTransitionFraction();
  if (ImGui::SliderFloat("Transition fraction", &sun_shadow_transition_fraction,
        0.0F, 1.0F, "%.3f")) {
    environment_vm_->SetSunShadowTransitionFraction(
      sun_shadow_transition_fraction);
  }

  float sun_shadow_distance_fadeout_fraction
    = environment_vm_->GetSunShadowDistanceFadeoutFraction();
  if (ImGui::SliderFloat("Distance fadeout fraction",
        &sun_shadow_distance_fadeout_fraction, 0.0F, 1.0F, "%.3f")) {
    environment_vm_->SetSunShadowDistanceFadeoutFraction(
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
        environment_vm_->SetSunShadowCascadeDistance(cascade_index, distance);
      }
      minimum_distance = std::max(distance, drag_min);
    }
  }

  ImGui::EndDisabled();
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
    environment_vm_->SetSkyAtmosphereEnabled(enabled);
    if (enabled) {
      environment_vm_->SetSkySphereEnabled(false);
    }
  }

  if (!enabled) {
    return;
  }

  ImGui::PushItemWidth(150);

  const char* transform_modes[] = {
    "Planet Top @ World Origin",
    "Planet Top @ Component",
    "Planet Center @ Component",
  };
  int transform_mode = environment_vm_->GetSkyAtmosphereTransformMode();
  if (ImGui::Combo("Transform Mode", &transform_mode, transform_modes, 3)) {
    environment_vm_->SetSkyAtmosphereTransformMode(transform_mode);
  }

  // Planet parameters
  ImGui::SeparatorText("Planet:");
  ImGui::TextDisabled(
    "Earth defaults: radius 6360 km, atmosphere height 100 km. "
    "UI clamps: radius 10-100000 km, height 1-1000 km.");
  float planet_radius_km = environment_vm_->GetPlanetRadiusKm();
  if (ImGui::DragFloat(
        "Radius (km)", &planet_radius_km, 10.0F, 10.0F, 100000.0F, "%.0F")) {
    environment_vm_->SetPlanetRadiusKm(planet_radius_km);
  }
  float atmosphere_height_km = environment_vm_->GetAtmosphereHeightKm();
  if (ImGui::DragFloat("Atmosphere Height (km)", &atmosphere_height_km, 1.0F,
        1.0F, 1000.0F, "%.1F")) {
    environment_vm_->SetAtmosphereHeightKm(atmosphere_height_km);
  }
  ImGui::SetNextItemWidth(240);
  auto ground_albedo = environment_vm_->GetGroundAlbedo();
  if (ImGui::ColorEdit3("Ground Albedo", &ground_albedo.x)) {
    environment_vm_->SetGroundAlbedo(ground_albedo);
  }

  ImGui::Separator();

  // Scattering parameters
  ImGui::SeparatorText("Scattering:");
  ImGui::TextDisabled("Earth defaults: Rayleigh 8 km, Mie 1.2 km. "
                      "UI clamps: Rayleigh 0.1-50 km, Mie 0.05-20 km.");
  float rayleigh_scale_height_km = environment_vm_->GetRayleighScaleHeightKm();
  if (ImGui::DragFloat("Rayleigh Scale Height (km)", &rayleigh_scale_height_km,
        0.1F, 0.1F, 50.0F, "%.1F")) {
    environment_vm_->SetRayleighScaleHeightKm(rayleigh_scale_height_km);
  }
  float mie_scale_height_km = environment_vm_->GetMieScaleHeightKm();
  if (ImGui::DragFloat("Mie Scale Height (km)", &mie_scale_height_km, 0.05F,
        0.05F, 20.0F, "%.2F")) {
    environment_vm_->SetMieScaleHeightKm(mie_scale_height_km);
  }
  float mie_anisotropy = environment_vm_->GetMieAnisotropy();
  if (ImGui::SliderFloat(
        "Mie Anisotropy", &mie_anisotropy, 0.0F, 0.99F, "%.2F")) {
    environment_vm_->SetMieAnisotropy(mie_anisotropy);
  }
  float mie_absorption_scale = environment_vm_->GetMieAbsorptionScale();
  if (ImGui::SliderFloat(
        "Mie Absorption", &mie_absorption_scale, 0.0F, 5.0F, "%.2F")) {
    environment_vm_->SetMieAbsorptionScale(mie_absorption_scale);
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
    environment_vm_->SetMultiScattering(multi_scattering);
  }

  ImGui::SeparatorText("Art Direction");
  auto sky_luminance = environment_vm_->GetSkyLuminanceFactor();
  if (ImGui::ColorEdit3("Sky Luminance Factor", &sky_luminance.x,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    environment_vm_->SetSkyLuminanceFactor(sky_luminance);
  }
  auto sky_aerial_luminance
    = environment_vm_->GetSkyAndAerialPerspectiveLuminanceFactor();
  if (ImGui::ColorEdit3("Sky+Aerial Luminance", &sky_aerial_luminance.x,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    environment_vm_->SetSkyAndAerialPerspectiveLuminanceFactor(
      sky_aerial_luminance);
  }

  // Ozone Profile (2-layer density profile)
  ImGui::SeparatorText("Ozone Density Profile (2-layer):");

  constexpr float kMetersToKm = engine::atmos::kMToSkyUnit;
  constexpr float kKmToMeters = engine::atmos::kSkyUnitToM;

  ImGui::TextDisabled("Peak altitude is in km. Slopes are shown in 1/km. "
                      "Offsets are unitless density terms.");

  auto ozone_profile = environment_vm_->GetOzoneDensityProfile();
  const auto& lower = ozone_profile.layers[0];
  const auto& upper = ozone_profile.layers[1];

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

  float peak_alt_km = ozone_profile.layers[0].width_m * kMetersToKm;
  if (ImGui::DragFloat(
        "Peak Altitude (km)", &peak_alt_km, 0.1F, 0.0F, 120.0F, "%.2F")) {
    ozone_profile_changed = true;
  }

  float lower_slope_inv_km = ozone_profile.layers[0].linear_term * kKmToMeters;
  if (ImGui::DragFloat("Lower Slope (1/km)", &lower_slope_inv_km, 0.01F, -1.0F,
        1.0F, "%.4f")) {
    ozone_profile_changed = true;
  }
  float lower_offset = ozone_profile.layers[0].constant_term;
  if (ImGui::DragFloat(
        "Lower Offset", &lower_offset, 0.01F, -8.0F, 8.0F, "%.4f")) {
    ozone_profile_changed = true;
  }

  float upper_slope_inv_km = ozone_profile.layers[1].linear_term * kKmToMeters;
  if (ImGui::DragFloat("Upper Slope (1/km)", &upper_slope_inv_km, 0.01F, -1.0F,
        1.0F, "%.4f")) {
    ozone_profile_changed = true;
  }
  float upper_offset = ozone_profile.layers[1].constant_term;
  if (ImGui::DragFloat(
        "Upper Offset", &upper_offset, 0.01F, -8.0F, 8.0F, "%.4f")) {
    ozone_profile_changed = true;
  }

  if (ozone_profile_changed) {
    ozone_profile.layers[0].width_m = peak_alt_km * kKmToMeters;
    ozone_profile.layers[0].exp_term = 0.0F;
    ozone_profile.layers[0].linear_term = lower_slope_inv_km / kKmToMeters;
    ozone_profile.layers[0].constant_term = lower_offset;

    ozone_profile.layers[1].width_m = 0.0F;
    ozone_profile.layers[1].exp_term = 0.0F;
    ozone_profile.layers[1].linear_term = upper_slope_inv_km / kKmToMeters;
    ozone_profile.layers[1].constant_term = upper_offset;

    environment_vm_->SetOzoneDensityProfile(ozone_profile);
  }

  // Ozone Absorption Color (scaled for usability)
  // Ozone absorption is typically ~1e-6. We scale by 1e6 so user sees "0.65"
  // instead of "0.00000065".
  constexpr float kOzoneScale = 1.0e6F;
  glm::vec3 absorption_rgb = environment_vm_->GetOzoneRgb() * kOzoneScale;
  float absorption_rgb_arr[3]
    = { absorption_rgb.r, absorption_rgb.g, absorption_rgb.b };

  // Use DragFloat3 or ColorEdit3. ColorEdit3 is good for picking color ratios,
  // but if we want to visualize "intensity" maybe DragFloat3 is better?
  // Let's stick to ColorEdit3 but with the scaled validation.
  // Actually, ColorEdit3 clamps 0-1 for the picker visual, which is fine if we
  // map 0..1 to 0..1e-6 range? No, if the values are 0.65 (after scaling), they
  // fit nicely in 0..1 ColorEdit. So scaling by 1e6 makes it user-friendly
  // color picker compatible!

  if (ImGui::DragFloat3("Ozone Absorption (x1e-6 1/m)", absorption_rgb_arr,
        0.01F, 0.0F, 10.0F, "%.3f")) {
    environment_vm_->SetOzoneRgb(glm::vec3(absorption_rgb_arr[0],
                                   absorption_rgb_arr[1], absorption_rgb_arr[2])
      / kOzoneScale);
  }
  ImGui::SameLine();
  if (ImGui::ColorEdit3("##OzoneColorPreview", absorption_rgb_arr,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_HDR)) {
    environment_vm_->SetOzoneRgb(glm::vec3(absorption_rgb_arr[0],
                                   absorption_rgb_arr[1], absorption_rgb_arr[2])
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
    environment_vm_->SetSunDiskEnabled(sun_disk_enabled);
  }
  ImGui::TextDisabled("Radius is controlled in the Sun section.");

  ImGui::SeparatorText("Aerial Perspective");

  bool aerial_perspective_enabled
    = environment_vm_->GetAerialPerspectiveEnabled();
  if (ImGui::Checkbox(
        "Enabled##AerialPerspective", &aerial_perspective_enabled)) {
    environment_vm_->SetAerialPerspectiveEnabled(aerial_perspective_enabled);
  }
  ImGui::TextDisabled("Strength zero disables AP without disabling sky");

  ImGui::BeginDisabled(!aerial_perspective_enabled);
  float aerial_perspective_scale = environment_vm_->GetAerialPerspectiveScale();
  if (ImGui::DragFloat("Distance Scale", &aerial_perspective_scale, 1.0F, 0.0F,
        10000.0F, "%.1F")) {
    environment_vm_->SetAerialPerspectiveScale(aerial_perspective_scale);
  }
  float aerial_start_depth
    = environment_vm_->GetAerialPerspectiveStartDepthMeters();
  if (ImGui::DragFloat("Start Depth (m)", &aerial_start_depth, 1.0F, 0.0F,
        1000000.0F, "%.1F")) {
    environment_vm_->SetAerialPerspectiveStartDepthMeters(aerial_start_depth);
  }
  float aerial_scattering_strength
    = environment_vm_->GetAerialScatteringStrength();
  if (ImGui::DragFloat("Haze Strength", &aerial_scattering_strength, 0.01F,
        0.0F, 100.0F, "%.2F")) {
    environment_vm_->SetAerialScatteringStrength(aerial_scattering_strength);
  }
  float height_fog_contribution = environment_vm_->GetHeightFogContribution();
  if (ImGui::SliderFloat("Height Fog Contribution", &height_fog_contribution,
        0.0F, 1.0F, "%.2F")) {
    environment_vm_->SetHeightFogContribution(height_fog_contribution);
  }
  float trace_sample_count_scale = environment_vm_->GetTraceSampleCountScale();
  if (ImGui::SliderFloat(
        "Trace Sample Scale", &trace_sample_count_scale, 0.25F, 8.0F, "%.2F")) {
    environment_vm_->SetTraceSampleCountScale(trace_sample_count_scale);
  }
  float transmittance_min_light_elevation
    = environment_vm_->GetTransmittanceMinLightElevationDeg();
  if (ImGui::SliderFloat("Min Light Elevation (deg)",
        &transmittance_min_light_elevation, -90.0F, 90.0F, "%.1F")) {
    environment_vm_->SetTransmittanceMinLightElevationDeg(
      transmittance_min_light_elevation);
  }
  bool atmosphere_holdout = environment_vm_->GetAtmosphereHoldout();
  if (ImGui::Checkbox("Holdout##SkyAtmo", &atmosphere_holdout)) {
    environment_vm_->SetAtmosphereHoldout(atmosphere_holdout);
  }
  bool atmosphere_render_in_main_pass
    = environment_vm_->GetAtmosphereRenderInMainPass();
  if (ImGui::Checkbox(
        "Render In Main Pass##SkyAtmo", &atmosphere_render_in_main_pass)) {
    environment_vm_->SetAtmosphereRenderInMainPass(
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
      environment_vm_->SetSkyAtmosphereEnabled(false);
      environment_vm_->SetSkySphereEnabled(true);
    } else {
      environment_vm_->SetSkySphereEnabled(false);
    }
    skybox_auto_load_pending_ = true;
  }

  if (!enabled) {
    return;
  }

  ImGui::Indent();
  ImGui::PushItemWidth(150);

  const char* sources[] = { "Cubemap", "Solid Color" };
  int sky_sphere_source = environment_vm_->GetSkySphereSource();
  if (ImGui::Combo("Source##SkySphere", &sky_sphere_source, sources, 2)) {
    environment_vm_->SetSkySphereSource(sky_sphere_source);
    skybox_auto_load_pending_ = true;
  }

  if (sky_sphere_source == 0) { // Cubemap
    const auto key = environment_vm_->GetSkySphereCubemapResourceKey();
    ImGui::Text(
      "Cubemap ResourceKey: %llu", static_cast<unsigned long long>(key.get()));
    if (key.IsPlaceholder()) {
      ImGui::TextColored(
        ImVec4(1.0F, 0.7F, 0.0F, 1.0F), "No cubemap bound (placeholder)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Skybox Loader");
    ImGui::TextDisabled(
      "Loads an image from disk, cooks it to a cubemap, and binds it.");

    ImGui::PushItemWidth(280);
    const auto skybox_path = environment_vm_->GetSkyboxPath();
    if (skybox_path_[0] == '\0' && !skybox_path.empty()) {
      CopyPathToBuffer(std::filesystem::path(skybox_path),
        std::span(skybox_path_.data(), skybox_path_.size()));
    }
    const bool path_changed = ImGui::InputText(
      "Path##Skybox", skybox_path_.data(), skybox_path_.size());
    const bool path_active = ImGui::IsItemActive();
    if (path_changed) {
      environment_vm_->SetSkyboxPath(std::string_view(skybox_path_.data()));
      skybox_auto_load_pending_ = true;
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
      skybox_auto_load_pending_ = true;
    }

    const char* layouts[] = { "Equirectangular", "Horizontal Cross",
      "Vertical Cross", "Horizontal Strip", "Vertical Strip" };
    int skybox_layout_idx = environment_vm_->GetSkyboxLayoutIndex();
    if (ImGui::Combo("Layout##Skybox", &skybox_layout_idx, layouts, 5)) {
      environment_vm_->SetSkyboxLayoutIndex(skybox_layout_idx);
      skybox_auto_load_pending_ = true;
    }

    const char* formats[] = { "RGBA8", "RGBA16F", "RGBA32F", "BC7" };
    int skybox_output_format_idx
      = environment_vm_->GetSkyboxOutputFormatIndex();
    if (ImGui::Combo("Output##Skybox", &skybox_output_format_idx, formats, 4)) {
      environment_vm_->SetSkyboxOutputFormatIndex(skybox_output_format_idx);
      skybox_auto_load_pending_ = true;
    }

    int skybox_face_size = environment_vm_->GetSkyboxFaceSize();
    if (ImGui::DragInt("Face Size##Skybox", &skybox_face_size, 16, 16, 4096)) {
      environment_vm_->SetSkyboxFaceSize(skybox_face_size);
      skybox_auto_load_pending_ = true;
    }
    bool skybox_flip_y = environment_vm_->GetSkyboxFlipY();
    if (ImGui::Checkbox("Flip Y##Skybox", &skybox_flip_y)) {
      environment_vm_->SetSkyboxFlipY(skybox_flip_y);
      skybox_auto_load_pending_ = true;
    }

    const bool output_is_ldr
      = skybox_output_format_idx == 0 || skybox_output_format_idx == 3;
    if (output_is_ldr) {
      bool skybox_tonemap_hdr_to_ldr
        = environment_vm_->GetSkyboxTonemapHdrToLdr();
      if (ImGui::Checkbox(
            "HDR->LDR Tonemap##Skybox", &skybox_tonemap_hdr_to_ldr)) {
        environment_vm_->SetSkyboxTonemapHdrToLdr(skybox_tonemap_hdr_to_ldr);
        skybox_auto_load_pending_ = true;
      }
      float skybox_hdr_exposure_ev = environment_vm_->GetSkyboxHdrExposureEv();
      if (ImGui::DragFloat("HDR Exposure (EV)##Skybox", &skybox_hdr_exposure_ev,
            0.1F, 0.0F, 16.0F, "%.2F")) {
        environment_vm_->SetSkyboxHdrExposureEv(
          std::max(skybox_hdr_exposure_ev, 0.0F));
        skybox_auto_load_pending_ = true;
      }
    }

    if (ImGui::Button("Load Skybox##Skybox")) {
      environment_vm_->LoadSkybox(std::string_view(skybox_path_.data()),
        skybox_layout_idx, skybox_output_format_idx, skybox_face_size,
        skybox_flip_y, environment_vm_->GetSkyboxTonemapHdrToLdr(),
        environment_vm_->GetSkyboxHdrExposureEv());
      skybox_auto_load_pending_ = false;
      last_auto_load_path_ = std::string_view(skybox_path_.data());
      last_auto_load_layout_idx_ = skybox_layout_idx;
      last_auto_load_output_format_idx_ = skybox_output_format_idx;
      last_auto_load_face_size_ = skybox_face_size;
      last_auto_load_flip_y_ = skybox_flip_y;
      last_auto_load_tonemap_hdr_to_ldr_
        = environment_vm_->GetSkyboxTonemapHdrToLdr();
      last_auto_load_hdr_exposure_ev_
        = environment_vm_->GetSkyboxHdrExposureEv();
    }
    ImGui::SameLine();
    const auto status_message = environment_vm_->GetSkyboxStatusMessage();
    if (!status_message.empty()) {
      ImGui::TextUnformatted(
        status_message.data(), status_message.data() + status_message.size());
    }

    const int last_face_size = environment_vm_->GetSkyboxLastFaceSize();
    if (last_face_size > 0) {
      ImGui::Text("Last face size: %d", last_face_size);
      ImGui::Text("Last ResourceKey: %llu",
        static_cast<unsigned long long>(
          environment_vm_->GetSkyboxLastResourceKey().get()));
    }

  } else { // Solid color
    auto sky_sphere_solid_color = environment_vm_->GetSkySphereSolidColor();
    if (ImGui::ColorEdit3("Color##SkySphere", &sky_sphere_solid_color.x)) {
      environment_vm_->SetSkySphereSolidColor(sky_sphere_solid_color);
    }
  }

  const float sky_intensity = environment_vm_->GetSkyIntensity();
  float sky_exposure_ev = SkySphereRadianceScaleToEv(sky_intensity);
  const char* exposure_label = sky_sphere_source == 0
    ? "Skybox Exposure (EV)"
    : "Sky Color Exposure (EV)";
  if (ImGui::DragFloat(exposure_label, &sky_exposure_ev, 0.05F,
        kMinSkySphereExposureEv, kMaxSkySphereExposureEv, "%+.2f")) {
    environment_vm_->SetSkyIntensity(
      SkySphereExposureEvToRadianceScale(sky_exposure_ev));
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    ImGui::SetTooltip(
      "Radiance scale %.6g. EV +8 = 256, EV +10 = 1024.",
      static_cast<double>(sky_intensity));
  }

  float sky_sphere_rotation_deg = environment_vm_->GetSkySphereRotationDeg();
  if (ImGui::SliderFloat(
        "Rotation (deg)", &sky_sphere_rotation_deg, 0.0F, 360.0F, "%.1F")) {
    environment_vm_->SetSkySphereRotationDeg(sky_sphere_rotation_deg);
  }

  ImGui::PopItemWidth();
  ImGui::Unindent();
}

void EnvironmentDebugPanel::DrawSkyLightSection()
{
  bool enabled = environment_vm_->GetSkyLightEnabled();
  if (ImGui::Checkbox("Enabled##SkyLight", &enabled)) {
    environment_vm_->SetSkyLightEnabled(enabled);
  }

  if (!enabled) {
    return;
  }

  ImGui::PushItemWidth(150);

  const char* sources[] = { "Captured Scene (Unavailable)",
    "Specified Cubemap" };
  int sky_light_source = environment_vm_->GetSkyLightSource();
  if (ImGui::Combo("Source##SkyLight", &sky_light_source, sources, 2)) {
    environment_vm_->SetSkyLightSource(sky_light_source);
  }

  if (sky_light_source == 1) {
    const auto key = environment_vm_->GetSkyLightCubemapResourceKey();
    ImGui::Text(
      "Cubemap ResourceKey: %llu", static_cast<unsigned long long>(key.get()));
    if (key.IsPlaceholder()) {
      ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
        "No specified cubemap is bound to SkyLight.");
    }
  } else {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
      "Captured-scene SkyLight is not implemented in Vortex yet.");
  }

  auto sky_light_tint = environment_vm_->GetSkyLightTint();
  if (ImGui::ColorEdit3("Tint##SkyLight", &sky_light_tint.x)) {
    environment_vm_->SetSkyLightTint(sky_light_tint);
  }

  float sky_light_intensity_mul = environment_vm_->GetSkyLightIntensityMul();
  if (ImGui::DragFloat("SkyLight Multiplier", &sky_light_intensity_mul, 0.01F,
        0.0F, 20.0F, "%.2F")) {
    environment_vm_->SetSkyLightIntensityMul(sky_light_intensity_mul);
  }

  float sky_light_diffuse = environment_vm_->GetSkyLightDiffuse();
  if (ImGui::DragFloat("Diffuse Indirect", &sky_light_diffuse, 0.01F, 0.0F,
        6.0F, "%.2F")) {
    environment_vm_->SetSkyLightDiffuse(sky_light_diffuse);
  }

  auto lower_hemisphere = environment_vm_->GetSkyLightLowerHemisphereColor();
  if (ImGui::ColorEdit3(
        "Lower Hemisphere", &lower_hemisphere.x, ImGuiColorEditFlags_Float)) {
    environment_vm_->SetSkyLightLowerHemisphereColor(lower_hemisphere);
  }

  float volumetric_scattering_intensity
    = environment_vm_->GetSkyLightVolumetricScatteringIntensity();
  if (ImGui::DragFloat("Volumetric Scattering",
        &volumetric_scattering_intensity, 0.01F, 0.25F, 4.0F, "%.2F")) {
    environment_vm_->SetSkyLightVolumetricScatteringIntensity(
      volumetric_scattering_intensity);
  }

  ImGui::TextDisabled(
    "Specular/reflection IBL, captured-scene SkyLight, and real-time "
    "capture are not active runtime paths.");

  ImGui::PopItemWidth();
}

void EnvironmentDebugPanel::HandleSkyboxAutoLoad()
{
  if (!environment_vm_ || !environment_vm_->HasScene()) {
    return;
  }

  const bool sky_sphere_enabled = environment_vm_->GetSkySphereEnabled();
  const int sky_sphere_source = environment_vm_->GetSkySphereSource();
  const auto auto_load_path = environment_vm_->GetSkyboxPath();
  const bool auto_load_eligible
    = sky_sphere_enabled && sky_sphere_source == 0 && !auto_load_path.empty();
  const auto skybox_key = environment_vm_->GetSkyboxLastResourceKey();
  if (!auto_load_eligible || !skybox_key.IsPlaceholder()) {
    return;
  }

  const int skybox_layout_idx = environment_vm_->GetSkyboxLayoutIndex();
  const int skybox_output_format_idx
    = environment_vm_->GetSkyboxOutputFormatIndex();
  const int skybox_face_size = environment_vm_->GetSkyboxFaceSize();
  const bool skybox_flip_y = environment_vm_->GetSkyboxFlipY();
  const bool auto_load_tonemap_hdr_to_ldr
    = environment_vm_->GetSkyboxTonemapHdrToLdr();
  const float auto_load_hdr_exposure_ev
    = environment_vm_->GetSkyboxHdrExposureEv();
  const bool settings_changed = last_auto_load_path_ != auto_load_path
    || last_auto_load_layout_idx_ != skybox_layout_idx
    || last_auto_load_output_format_idx_ != skybox_output_format_idx
    || last_auto_load_face_size_ != skybox_face_size
    || last_auto_load_flip_y_ != skybox_flip_y
    || last_auto_load_tonemap_hdr_to_ldr_ != auto_load_tonemap_hdr_to_ldr
    || last_auto_load_hdr_exposure_ev_ != auto_load_hdr_exposure_ev;
  if (settings_changed) {
    skybox_auto_load_pending_ = true;
  }

  if (!skybox_auto_load_pending_) {
    return;
  }

  environment_vm_->LoadSkybox(std::string_view(auto_load_path),
    skybox_layout_idx, skybox_output_format_idx, skybox_face_size,
    skybox_flip_y, auto_load_tonemap_hdr_to_ldr, auto_load_hdr_exposure_ev);
  skybox_auto_load_pending_ = false;
  last_auto_load_path_ = auto_load_path;
  last_auto_load_layout_idx_ = skybox_layout_idx;
  last_auto_load_output_format_idx_ = skybox_output_format_idx;
  last_auto_load_face_size_ = skybox_face_size;
  last_auto_load_flip_y_ = skybox_flip_y;
  last_auto_load_tonemap_hdr_to_ldr_ = auto_load_tonemap_hdr_to_ldr;
  last_auto_load_hdr_exposure_ev_ = auto_load_hdr_exposure_ev;
}

auto EnvironmentDebugPanel::HasPendingChanges() const -> bool
{
  return environment_vm_ ? environment_vm_->HasPendingChanges() : false;
}

void EnvironmentDebugPanel::ApplyPendingChanges()
{
  if (environment_vm_) {
    environment_vm_->ApplyPendingChanges();
  }
}

void EnvironmentDebugPanel::RequestResync()
{
  if (environment_vm_) {
    environment_vm_->RequestResync();
  }
}

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
