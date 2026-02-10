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

  // Renderer debug section always at top for visibility
  DrawRendererDebugSection();

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
  if (!ImGui::CollapsingHeader("Fog")) {
    return;
  }

  bool fog_enabled = environment_vm_->GetFogEnabled();
  if (ImGui::Checkbox("Enabled##Fog", &fog_enabled)) {
    environment_vm_->SetFogEnabled(fog_enabled);
  }

  int model = environment_vm_->GetFogModel();
  if (ImGui::RadioButton("Model: Exponential Height", model == 0)) {
    environment_vm_->SetFogModel(0);
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Model: Volumetric", model == 1)) {
    environment_vm_->SetFogModel(1);
  }

  float extinction_sigma_t_per_m
    = environment_vm_->GetFogExtinctionSigmaTPerMeter();
  if (ImGui::SliderFloat("Extinction σt (1/m)", &extinction_sigma_t_per_m, 0.0F,
        1.0F, "%.6f", ImGuiSliderFlags_Logarithmic)) {
    environment_vm_->SetFogExtinctionSigmaTPerMeter(extinction_sigma_t_per_m);
  }

  float start_distance_m = environment_vm_->GetFogStartDistanceMeters();
  if (ImGui::DragFloat(
        "Start Distance (m)", &start_distance_m, 1.0F, 0.0F, 0.0F, "%.1f")) {
    environment_vm_->SetFogStartDistanceMeters(
      std::max(start_distance_m, 0.0F));
  }

  float height_falloff_per_m = environment_vm_->GetFogHeightFalloffPerMeter();
  if (ImGui::DragFloat("Height Falloff (1/m)", &height_falloff_per_m, 0.0001F,
        0.0F, 2.0F, "%.4f")) {
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
  if (ImGui::DragFloat(
        "Height Offset (m)", &height_offset_m, 0.25F, 0.0F, 0.0F, "%.1f")) {
    environment_vm_->SetFogHeightOffsetMeters(height_offset_m);
  }

  float max_opacity = environment_vm_->GetFogMaxOpacity();
  if (ImGui::SliderFloat("Max Opacity", &max_opacity, 0.0F, 1.0F, "%.3f")) {
    environment_vm_->SetFogMaxOpacity(max_opacity);
  }

  glm::vec3 single_scattering_albedo_rgb
    = environment_vm_->GetFogSingleScatteringAlbedoRgb();
  float albedo_rgb[3] = { single_scattering_albedo_rgb.r,
    single_scattering_albedo_rgb.g, single_scattering_albedo_rgb.b };
  if (ImGui::ColorEdit3(
        "Single-Scattering Albedo", albedo_rgb, ImGuiColorEditFlags_Float)) {
    environment_vm_->SetFogSingleScatteringAlbedoRgb(
      glm::vec3(albedo_rgb[0], albedo_rgb[1], albedo_rgb[2]));
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

void EnvironmentDebugPanel::DrawRendererDebugSection()
{
  ImGui::SeparatorText("Renderer State");

  // LUT status
  const auto [luts_valid, luts_dirty]
    = environment_vm_->GetAtmosphereLutStatus();

  ImGui::Text("Atmosphere LUTs:");
  ImGui::SameLine();
  if (luts_valid) {
    ImGui::TextColored(ImVec4(0.0F, 1.0F, 0.0F, 1.0F), "Generated");
  } else {
    ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F), "Not Generated");
  }

  if (luts_valid && luts_dirty) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0F, 1.0F, 0.0F, 1.0F), "(updating)");
  }

  ImGui::SameLine();
  ImGui::Spacing();
  ImGui::SameLine();
  if (ImGui::Button("Regenerate LUT")) {
    environment_vm_->RequestRegenerateLut();
  }
}

void EnvironmentDebugPanel::DrawSunSection()
{
  if (!environment_vm_->GetSunPresent()) {
    ImGui::TextColored(ImVec4(0.7F, 0.7F, 0.7F, 1.0F),
      "No Sun component found in the scene environment.");
    ImGui::TextDisabled(
      "From Scene is selected; no sun settings are available.");
    if (ImGui::Button("Add Synthetic Sun")) {
      environment_vm_->EnableSyntheticSun();
    }
    ImGui::Unindent();
    return;
  }

  bool sun_enabled = environment_vm_->GetSunEnabled();
  if (ImGui::Checkbox("Enabled##Sun", &sun_enabled)) {
    environment_vm_->SetSunEnabled(sun_enabled);
  }

  constexpr const char* kSourceLabels[] = { "From Scene", "Synthetic" };
  int sun_source = environment_vm_->GetSunSource();
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo(
        "Source", &sun_source, kSourceLabels, IM_ARRAYSIZE(kSourceLabels))) {
    environment_vm_->SetSunSource(sun_source);
  }

  const bool sun_from_scene = sun_source == 0;
  if (sun_from_scene) {
    environment_vm_->UpdateSunLightCandidate();
    if (!environment_vm_->GetSunLightAvailable()) {
      ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F),
        "No DirectionalLight found to use as the sun.");
    }
  }

  if (sun_source == 0) {
    ImGui::TextDisabled("Uses the first DirectionalLight flagged as sun "
                        "(or first available).");
  }

  const bool disable_sun_controls
    = sun_from_scene && !environment_vm_->GetSunLightAvailable();
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
  float sun_disk_radius_deg = environment_vm_->GetSunDiskRadiusDeg();
  if (ImGui::DragFloat("Disk radius (deg)", &sun_disk_radius_deg, 0.01F, 0.01F,
        5.0F, "%.3F")) {
    environment_vm_->SetSunDiskRadiusDeg(sun_disk_radius_deg);
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

  // Planet parameters
  ImGui::SeparatorText("Planet:");
  // Note: Max radius limited to 15000 km due to float precision issues in
  // ray-sphere intersection at larger values (causes sky/ground flip).
  // Min radius 10 km allows testing small asteroid-like bodies.
  float planet_radius_km = environment_vm_->GetPlanetRadiusKm();
  if (ImGui::DragFloat(
        "Radius (km)", &planet_radius_km, 10.0F, 10.0F, 15000.0F, "%.0F")) {
    environment_vm_->SetPlanetRadiusKm(planet_radius_km);
  }
  float atmosphere_height_km = environment_vm_->GetAtmosphereHeightKm();
  if (ImGui::DragFloat("Atmo Height (km)", &atmosphere_height_km, 1.0F, 1.0F,
        1000.0F, "%.1F")) {
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
  float rayleigh_scale_height_km = environment_vm_->GetRayleighScaleHeightKm();
  if (ImGui::DragFloat("Rayleigh Scale H (km)", &rayleigh_scale_height_km, 0.1F,
        0.1F, 100.0F, "%.1F")) {
    environment_vm_->SetRayleighScaleHeightKm(rayleigh_scale_height_km);
  }
  float mie_scale_height_km = environment_vm_->GetMieScaleHeightKm();
  if (ImGui::DragFloat(
        "Mie Scale H (km)", &mie_scale_height_km, 0.1F, 0.1F, 100.0F, "%.2F")) {
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

  // Ozone Profile (2-layer density profile)
  ImGui::SeparatorText("Ozone Density Profile (2-layer):");

  constexpr float kMetersToKm = 0.001F;
  constexpr float kKmToMeters = 1000.0F;

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
  if (ImGui::DragFloat("Lower Slope (1/km)", &lower_slope_inv_km, 0.01F,
        -1.0e3F, 1.0e3F, "%.4f")) {
    ozone_profile_changed = true;
  }
  float lower_offset = ozone_profile.layers[0].constant_term;
  if (ImGui::DragFloat(
        "Lower Offset", &lower_offset, 0.01F, -1000.0F, 1000.0F, "%.4f")) {
    ozone_profile_changed = true;
  }

  float upper_slope_inv_km = ozone_profile.layers[1].linear_term * kKmToMeters;
  if (ImGui::DragFloat("Upper Slope (1/km)", &upper_slope_inv_km, 0.01F,
        -1.0e3F, 1.0e3F, "%.4f")) {
    ozone_profile_changed = true;
  }
  float upper_offset = ozone_profile.layers[1].constant_term;
  if (ImGui::DragFloat(
        "Upper Offset", &upper_offset, 0.01F, -1000.0F, 1000.0F, "%.4f")) {
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

  if (ImGui::DragFloat3("Ozone Coeffs (x1e-6)", absorption_rgb_arr, 0.01F, 0.0F,
        10.0F, "%.3f")) {
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
      "Absorption coefficient in inverse micrometers (1e-6 m^-1).\n"
      "Default Earth Ozone ~ (0.65, 1.88, 0.085).");
  }

  // Sun disk
  ImGui::SeparatorText("Sun Disk");
  bool sun_disk_enabled = environment_vm_->GetSunDiskEnabled();
  if (ImGui::Checkbox("Show Sun Disk", &sun_disk_enabled)) {
    environment_vm_->SetSunDiskEnabled(sun_disk_enabled);
  }
  ImGui::TextDisabled("Radius is controlled in the Sun section.");

  ImGui::SeparatorText("Aerial Perspective");

  bool aerial_perspective_enabled = environment_vm_->GetUseLut();
  if (ImGui::Checkbox("Enabled (LUT)", &aerial_perspective_enabled)) {
    environment_vm_->SetUseLut(aerial_perspective_enabled);
  }
  ImGui::TextDisabled("Affects geometry only, not sky");

  ImGui::BeginDisabled(!aerial_perspective_enabled);
  float aerial_perspective_scale = environment_vm_->GetAerialPerspectiveScale();
  if (ImGui::DragFloat("Distance Scale", &aerial_perspective_scale, 0.01F, 0.0F,
        50.0F, "%.2F")) {
    environment_vm_->SetAerialPerspectiveScale(aerial_perspective_scale);
  }
  float aerial_scattering_strength
    = environment_vm_->GetAerialScatteringStrength();
  if (ImGui::DragFloat(
        "Haze", &aerial_scattering_strength, 0.01F, 0.0F, 50.0F, "%.2F")) {
    environment_vm_->SetAerialScatteringStrength(aerial_scattering_strength);
  }
  ImGui::EndDisabled();

  // Sky-View LUT Slicing
  ImGui::SeparatorText("Sky-View LUT");

  ImGui::TextDisabled("Altitude slices for multi-view sampling");

  int lut_slices = environment_vm_->GetSkyViewLutSlices();
  if (ImGui::DragInt("Slices", &lut_slices, 1, 4, 32)) {
    environment_vm_->SetSkyViewLutSlices(lut_slices);
  }

  const char* mapping_modes[] = { "Linear", "Log" };
  int mapping_mode = environment_vm_->GetSkyViewAltMappingMode();
  if (ImGui::Combo("Alt Mapping", &mapping_mode, mapping_modes, 2)) {
    environment_vm_->SetSkyViewAltMappingMode(mapping_mode);
  }

  ImGui::PopItemWidth();
}

void EnvironmentDebugPanel::DrawSkySphereSection()
{
  const bool sky_atmo_enabled = environment_vm_->GetSkyAtmosphereEnabled();
  const bool sky_sphere_enabled = environment_vm_->GetSkySphereEnabled();

  // Show warning if both sky systems are enabled
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
    const auto key = environment_vm_->GetSkyboxLastResourceKey();
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

  float sky_intensity = environment_vm_->GetSkyIntensity();
  if (ImGui::DragFloat(
        "SkySphere Intensity", &sky_intensity, 0.01F, 0.0F, 20.0F, "%.2F")) {
    environment_vm_->SetSkyIntensity(sky_intensity);
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
  ImGui::TextDisabled(
    "IBL is active when SkyLight is enabled and a cubemap is available\n"
    "(SkyLight specified cubemap, or SkySphere cubemap).");
  ImGui::Spacing();

  bool enabled = environment_vm_->GetSkyLightEnabled();
  if (ImGui::Checkbox("Enabled##SkyLight", &enabled)) {
    environment_vm_->SetSkyLightEnabled(enabled);
  }

  if (!enabled) {
    return;
  }

  ImGui::PushItemWidth(150);

  const char* sources[] = { "Captured Scene", "Specified Cubemap" };
  int sky_light_source = environment_vm_->GetSkyLightSource();
  if (ImGui::Combo("Source##SkyLight", &sky_light_source, sources, 2)) {
    environment_vm_->SetSkyLightSource(sky_light_source);
  }

  if (sky_light_source == 1) {
    const auto key = environment_vm_->GetSkyboxLastResourceKey();
    ImGui::Text(
      "Cubemap ResourceKey: %llu", static_cast<unsigned long long>(key.get()));
    if (key.IsPlaceholder()) {
      ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
        "No SkyLight cubemap bound; SkySphere cubemap may still drive IBL");
    }
  } else {
    ImGui::TextDisabled(
      "Captured-scene mode may not provide a cubemap yet; SkySphere cubemap\n"
      "can still drive IBL if present.");
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
  if (ImGui::DragFloat(
        "Diffuse", &sky_light_diffuse, 0.01F, 0.0F, 2.0F, "%.2F")) {
    environment_vm_->SetSkyLightDiffuse(sky_light_diffuse);
  }

  float sky_light_specular = environment_vm_->GetSkyLightSpecular();
  if (ImGui::DragFloat(
        "Specular", &sky_light_specular, 0.01F, 0.0F, 2.0F, "%.2F")) {
    environment_vm_->SetSkyLightSpecular(sky_light_specular);
  }

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
