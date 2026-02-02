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

#include "DemoShell/UI/EnvironmentDebugPanel.h"

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
    return glm::vec3(
      cos_el * std::cos(az_rad), cos_el * std::sin(az_rad), std::sin(el_rad));
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
          0.54320678911F * std::log(temp - 10.0F) - 1.19625408914F, 0.0F, 1.0F);
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

  // Renderer debug section always at top for visibility
  DrawRendererDebugSection();

  ImGui::Separator();

  // Sun controls
  if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSunSection();
  }

  ImGui::Separator();

  // Environment system sections
  if (ImGui::CollapsingHeader(
        "Sky Atmosphere", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSkyAtmosphereSection();
  }

  if (ImGui::CollapsingHeader("Sky Sphere")) {
    DrawSkySphereSection();
  }

  if (ImGui::CollapsingHeader("Sky Light (IBL)")) {
    DrawSkyLightSection();
  }

  // NOTE: Fog section removed - use Aerial Perspective from SkyAtmosphere
  // instead. Real volumetric fog system to be implemented in the future.
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
  ImGui::Text("Renderer State");
  ImGui::Indent();

  // LUT status
  const auto [luts_valid, luts_dirty]
    = environment_vm_->GetAtmosphereLutStatus();

  ImGui::Text("Atmosphere LUTs:");
  ImGui::SameLine();
  if (luts_valid) {
    ImGui::TextColored(ImVec4(0.0F, 1.0F, 0.0F, 1.0F), "Valid");
  } else {
    ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F), "Not Generated");
  }

  if (luts_valid && luts_dirty) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0F, 1.0F, 0.0F, 1.0F), "(pending update)");
  }

  // Atmosphere debug flags
  ImGui::Separator();
  ImGui::Text("Aerial Perspective Mode:");
  ImGui::TextDisabled("(affects geometry only, not sky)");

  const bool use_lut = environment_vm_->GetUseLut();
  const bool force_analytic = environment_vm_->GetForceAnalytic();
  if (ImGui::RadioButton("Enabled", use_lut && !force_analytic)) {
    environment_vm_->SetUseLut(true);
    environment_vm_->SetForceAnalytic(false);
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Disabled", !use_lut)) {
    environment_vm_->SetUseLut(false);
    environment_vm_->SetForceAnalytic(false);
  }

  ImGui::Unindent();
}

void EnvironmentDebugPanel::DrawSunSection()
{
  ImGui::Text("Sun");
  ImGui::Indent();

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

  float sun_intensity_lux = environment_vm_->GetSunIntensityLux();
  if (ImGui::DragFloat("Intensity (lux)", &sun_intensity_lux, 10.0F, 0.0F,
        500000.0F, "%.1F")) {
    environment_vm_->SetSunIntensityLux(sun_intensity_lux);
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

  ImGui::Unindent();
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

  ImGui::Indent();
  ImGui::PushItemWidth(150);

  // Planet parameters
  ImGui::Text("Planet:");
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
  ImGui::Text("Scattering:");
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
  float multi_scattering = environment_vm_->GetMultiScattering();
  if (ImGui::SliderFloat(
        "Multi-Scattering", &multi_scattering, 0.0F, 1.0F, "%.2F")) {
    environment_vm_->SetMultiScattering(multi_scattering);
  }

  ImGui::Separator();

  // Sun disk
  ImGui::Text("Sun Disk:");
  bool sun_disk_enabled = environment_vm_->GetSunDiskEnabled();
  if (ImGui::Checkbox("Show Sun Disk", &sun_disk_enabled)) {
    environment_vm_->SetSunDiskEnabled(sun_disk_enabled);
  }
  if (sun_disk_enabled) {
    float sun_disk_radius_deg
      = environment_vm_->GetAtmosphereSunDiskRadiusDeg();
    if (ImGui::SliderFloat(
          "Angular Radius (deg)", &sun_disk_radius_deg, 0.01F, 5.0F, "%.3F")) {
      environment_vm_->SetAtmosphereSunDiskRadiusDeg(sun_disk_radius_deg);
    }
  }

  ImGui::Separator();

  // Aerial perspective
  ImGui::Text("Aerial Perspective:");
  ImGui::TextDisabled("0 disables; higher increases effect");
  float aerial_perspective_scale = environment_vm_->GetAerialPerspectiveScale();
  if (ImGui::DragFloat("Distance Scale", &aerial_perspective_scale, 0.01F, 0.0F,
        50.0F, "%.2F")) {
    environment_vm_->SetAerialPerspectiveScale(aerial_perspective_scale);
  }
  float aerial_scattering_strength
    = environment_vm_->GetAerialScatteringStrength();
  if (ImGui::DragFloat("Scattering Strength", &aerial_scattering_strength,
        0.01F, 0.0F, 50.0F, "%.2F")) {
    environment_vm_->SetAerialScatteringStrength(aerial_scattering_strength);
  }

  ImGui::PopItemWidth();
  ImGui::Unindent();
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
    environment_vm_->SetSkySphereEnabled(enabled);
    if (enabled) {
      environment_vm_->SetSkyAtmosphereEnabled(false);
    }
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
    }

    const char* layouts[] = { "Equirectangular", "Horizontal Cross",
      "Vertical Cross", "Horizontal Strip", "Vertical Strip" };
    int skybox_layout_idx = environment_vm_->GetSkyboxLayoutIndex();
    if (ImGui::Combo("Layout##Skybox", &skybox_layout_idx, layouts, 5)) {
      environment_vm_->SetSkyboxLayoutIndex(skybox_layout_idx);
    }

    const char* formats[] = { "RGBA8", "RGBA16F", "RGBA32F", "BC7" };
    int skybox_output_format_idx
      = environment_vm_->GetSkyboxOutputFormatIndex();
    if (ImGui::Combo("Output##Skybox", &skybox_output_format_idx, formats, 4)) {
      environment_vm_->SetSkyboxOutputFormatIndex(skybox_output_format_idx);
    }

    int skybox_face_size = environment_vm_->GetSkyboxFaceSize();
    if (ImGui::DragInt("Face Size##Skybox", &skybox_face_size, 16, 16, 4096)) {
      environment_vm_->SetSkyboxFaceSize(skybox_face_size);
    }
    bool skybox_flip_y = environment_vm_->GetSkyboxFlipY();
    if (ImGui::Checkbox("Flip Y##Skybox", &skybox_flip_y)) {
      environment_vm_->SetSkyboxFlipY(skybox_flip_y);
    }

    const bool output_is_ldr
      = skybox_output_format_idx == 0 || skybox_output_format_idx == 3;
    if (output_is_ldr) {
      bool skybox_tonemap_hdr_to_ldr
        = environment_vm_->GetSkyboxTonemapHdrToLdr();
      if (ImGui::Checkbox(
            "HDR->LDR Tonemap##Skybox", &skybox_tonemap_hdr_to_ldr)) {
        environment_vm_->SetSkyboxTonemapHdrToLdr(skybox_tonemap_hdr_to_ldr);
      }
      float skybox_hdr_exposure_ev = environment_vm_->GetSkyboxHdrExposureEv();
      if (ImGui::DragFloat("HDR Exposure (EV)##Skybox", &skybox_hdr_exposure_ev,
            0.1F, -16.0F, 16.0F, "%.2F")) {
        environment_vm_->SetSkyboxHdrExposureEv(skybox_hdr_exposure_ev);
      }
    }

    if (ImGui::Button("Load Skybox##Skybox")) {
      environment_vm_->LoadSkybox(std::string_view(skybox_path_.data()),
        skybox_layout_idx, skybox_output_format_idx, skybox_face_size,
        skybox_flip_y, environment_vm_->GetSkyboxTonemapHdrToLdr(),
        environment_vm_->GetSkyboxHdrExposureEv());
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

  float sky_sphere_intensity = environment_vm_->GetSkySphereIntensity();
  if (ImGui::DragFloat("Intensity##SkySphere", &sky_sphere_intensity, 0.01F,
        0.0F, 10.0F, "%.2F")) {
    environment_vm_->SetSkySphereIntensity(sky_sphere_intensity);
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

  ImGui::Indent();
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

  float sky_light_intensity = environment_vm_->GetSkyLightIntensity();
  if (ImGui::DragFloat("Intensity##SkyLight", &sky_light_intensity, 0.01F, 0.0F,
        10.0F, "%.2F")) {
    environment_vm_->SetSkyLightIntensity(sky_light_intensity);
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
  ImGui::Unindent();
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
