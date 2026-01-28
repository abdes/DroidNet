//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

#include <imgui.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/UI/EnvironmentDebugPanel.h"

namespace oxygen::examples::ui {

namespace {

  constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0F;
  constexpr float kRadToDeg = 180.0F / std::numbers::pi_v<float>;
  constexpr float kMetersToKm = 0.001F;
  constexpr float kKmToMeters = 1000.0F;

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
    kelvin = glm::clamp(kelvin, 1000.0F, 40000.0F);
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

  auto RotationFromDirection(const glm::vec3& direction_ws) -> glm::quat
  {
    const glm::vec3 from_dir(0.0F, -1.0F, 0.0F);
    const glm::vec3 to_dir = glm::normalize(direction_ws);

    const float cos_theta = std::clamp(glm::dot(from_dir, to_dir), -1.0F, 1.0F);
    glm::quat rotation(1.0F, 0.0F, 0.0F, 0.0F);
    if (cos_theta < 0.9999F) {
      if (cos_theta > -0.9999F) {
        const glm::vec3 axis = glm::normalize(glm::cross(from_dir, to_dir));
        const float angle = std::acos(cos_theta);
        rotation = glm::angleAxis(angle, axis);
      } else {
        const glm::vec3 axis = glm::vec3(0.0F, 0.0F, 1.0F);
        rotation = glm::angleAxis(glm::pi<float>(), axis);
      }
    }

    return rotation;
  }

  auto CopyPathToBuffer(
    const std::filesystem::path& path, std::span<char> buffer) -> void
  {
    if (buffer.empty()) {
      return;
    }
    const std::string s = path.string();
    const std::size_t to_copy
      = std::min(buffer.size() - 1, static_cast<std::size_t>(s.size()));
    std::memcpy(buffer.data(), s.data(), to_copy);
    buffer[to_copy] = '\0';
  }

} // namespace

void EnvironmentDebugPanel::Initialize(const EnvironmentDebugConfig& config)
{
  config_ = config;
  CHECK_NOTNULL_F(config_.file_browser_service,
    "EnvironmentDebugPanel requires a FileBrowserService");
  file_browser_ = config_.file_browser_service;
  initialized_ = true;
  needs_sync_ = true;

  // Sync debug flags from renderer (these persist across scene loads)
  SyncDebugFlagsFromRenderer();
}

void EnvironmentDebugPanel::UpdateConfig(const EnvironmentDebugConfig& config)
{
  // Only trigger sync if the scene actually changed
  const bool scene_changed = config_.scene.get() != config.scene.get();
  config_ = config;
  file_browser_ = config_.file_browser_service;
  if (scene_changed) {
    needs_sync_ = true;
  }
}

void EnvironmentDebugPanel::Draw()
{
  if (!initialized_) {
    return;
  }

  // Sync from scene on first draw or when scene changes
  if (needs_sync_) {
    SyncFromScene();
    needs_sync_ = false;
  }

  ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.45F);

  if (!ImGui::Begin("Environment Systems")) {
    ImGui::End();
    return;
  }

  DrawContents();

  ImGui::End();
}

void EnvironmentDebugPanel::DrawContents()
{
  const bool has_scene = config_.scene != nullptr;

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

  if (ImGui::CollapsingHeader("Post Process")) {
    DrawPostProcessSection();
  }
}

void EnvironmentDebugPanel::DrawRendererDebugSection()
{
  ImGui::Text("Renderer State");
  ImGui::Indent();

  // LUT status
  bool luts_valid = false;
  bool luts_dirty = true;

  if (config_.renderer) {
    if (auto lut_mgr = config_.renderer->GetSkyAtmosphereLutManager()) {
      luts_valid = lut_mgr->HasBeenGenerated();
      luts_dirty = lut_mgr->IsDirty();
    }
  }

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

  if (ImGui::RadioButton("Enabled", use_lut_ && !force_analytic_)) {
    use_lut_ = true;
    force_analytic_ = false;
    MarkDirty();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Disabled", !use_lut_)) {
    use_lut_ = false;
    force_analytic_ = false;
    MarkDirty();
  }

  ImGui::Unindent();
}

void EnvironmentDebugPanel::DrawSunSection()
{
  ImGui::Text("Sun");
  ImGui::Indent();

  if (!sun_present_) {
    ImGui::TextColored(ImVec4(0.7F, 0.7F, 0.7F, 1.0F),
      "No Sun component found in the scene environment.");
    ImGui::TextDisabled(
      "From Scene is selected; no sun settings are available.");
    if (ImGui::Button("Add Synthetic Sun")) {
      sun_present_ = true;
      sun_source_ = 1;
      LoadSunSettingsFromProfile(sun_source_);
      MarkDirty();
    }
    ImGui::Unindent();
    return;
  }

  if (ImGui::Checkbox("Enabled##Sun", &sun_enabled_)) {
    MarkDirty();
  }

  constexpr const char* kSourceLabels[] = { "From Scene", "Synthetic" };
  const int previous_source = sun_source_;
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo(
        "Source", &sun_source_, kSourceLabels, IM_ARRAYSIZE(kSourceLabels))) {
    SaveSunSettingsToProfile(previous_source);
    LoadSunSettingsFromProfile(sun_source_);
    MarkDirty();
  }

  const bool sun_from_scene = (sun_source_ == 0);
  if (sun_from_scene) {
    UpdateSunLightCandidate();
    if (!sun_light_available_) {
      ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F),
        "No DirectionalLight found to use as the sun.");
    }
  }

  if (sun_source_ == 0) {
    ImGui::TextDisabled("Uses the first DirectionalLight flagged as sun "
                        "(or first available).");
  }

  const bool disable_sun_controls = sun_from_scene && !sun_light_available_;
  ImGui::BeginDisabled(disable_sun_controls);

  ImGui::Separator();
  ImGui::Text("Direction (toward sun):");

  if (ImGui::SliderFloat(
        "Azimuth (deg)", &sun_azimuth_deg_, 0.0F, 360.0F, "%.1f")) {
    MarkDirty();
  }

  if (ImGui::DragFloat(
        "Elevation (deg)", &sun_elevation_deg_, 0.1F, -90.0F, 90.0F, "%.1f")) {
    MarkDirty();
  }

  const auto sun_dir
    = DirectionFromAzimuthElevation(sun_azimuth_deg_, sun_elevation_deg_);
  ImGui::Text("Direction: (%.2f, %.2f, %.2f)", sun_dir.x, sun_dir.y, sun_dir.z);

  ImGui::Separator();
  ImGui::Text("Light:");

  if (ImGui::DragFloat("Intensity (lux)", &sun_intensity_lux_, 10.0F, 0.0F,
        500000.0F, "%.1f")) {
    MarkDirty();
  }

  if (ImGui::Checkbox("Use temperature", &sun_use_temperature_)) {
    MarkDirty();
  }

  if (sun_use_temperature_) {
    if (ImGui::DragFloat("Temperature (K)", &sun_temperature_kelvin_, 50.0F,
          1000.0F, 40000.0F, "%.0f")) {
      MarkDirty();
    }
    const auto preview = KelvinToLinearRgb(sun_temperature_kelvin_);
    ImGui::ColorButton("Temperature Preview",
      ImVec4(preview.r, preview.g, preview.b, 1.0F), ImGuiColorEditFlags_Float);
  }

  ImGui::BeginDisabled(sun_use_temperature_);
  float sun_color[3] = {
    sun_color_rgb_.x,
    sun_color_rgb_.y,
    sun_color_rgb_.z,
  };
  if (ImGui::ColorEdit3("Color", sun_color,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    sun_color_rgb_ = { sun_color[0], sun_color[1], sun_color[2] };
    MarkDirty();
  }
  ImGui::EndDisabled();

  ImGui::Separator();
  if (ImGui::DragFloat("Disk radius (deg)", &sun_component_disk_radius_deg_,
        0.01F, 0.01F, 5.0F, "%.3f")) {
    MarkDirty();
  }

  ImGui::EndDisabled();

  SaveSunSettingsToProfile(sun_source_);

  ImGui::Unindent();
}

void EnvironmentDebugPanel::DrawSkyAtmosphereSection()
{
  auto* env = config_.scene ? config_.scene->GetEnvironment().get() : nullptr;
  auto* atmo = env
    ? env->TryGetSystem<scene::environment::SkyAtmosphere>().get()
    : nullptr;

  // Show warning if both sky systems are enabled
  if (sky_atmo_enabled_ && sky_sphere_enabled_) {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
      "Warning: SkyAtmosphere takes priority over SkySphere");
  }

  if (!atmo) {
    if (ImGui::Button("Add SkyAtmosphere")) {
      pending_changes_ = true;
      sky_atmo_enabled_ = true;
      // SkyAtmosphere and SkySphere are mutually exclusive; disable SkySphere
      sky_sphere_enabled_ = false;
    }
    return;
  }

  // Only sync from scene when no pending changes to avoid overwriting
  // mutual exclusion state set by user interaction
  if (!pending_changes_) {
    sky_atmo_enabled_ = atmo->IsEnabled();
  }
  if (ImGui::Checkbox("Enabled##SkyAtmo", &sky_atmo_enabled_)) {
    // SkyAtmosphere and SkySphere are mutually exclusive
    if (sky_atmo_enabled_) {
      sky_sphere_enabled_ = false;
    }
    MarkDirty();
  }

  if (!sky_atmo_enabled_) {
    return;
  }

  ImGui::Indent();
  ImGui::PushItemWidth(150);

  // Planet parameters
  ImGui::Text("Planet:");
  // Note: Max radius limited to 15000 km due to float precision issues in
  // ray-sphere intersection at larger values (causes sky/ground flip).
  // Min radius 10 km allows testing small asteroid-like bodies.
  if (ImGui::DragFloat(
        "Radius (km)", &planet_radius_km_, 10.0F, 10.0F, 15000.0F, "%.0f")) {
    MarkDirty();
  }
  if (ImGui::DragFloat("Atmo Height (km)", &atmosphere_height_km_, 1.0F, 1.0F,
        1000.0F, "%.1f")) {
    MarkDirty();
  }
  ImGui::SetNextItemWidth(240);
  if (ImGui::ColorEdit3("Ground Albedo", &ground_albedo_.x)) {
    MarkDirty();
  }

  ImGui::Separator();

  // Scattering parameters
  ImGui::Text("Scattering:");
  if (ImGui::DragFloat("Rayleigh Scale H (km)", &rayleigh_scale_height_km_,
        0.1F, 0.1F, 100.0F, "%.1f")) {
    MarkDirty();
  }
  if (ImGui::DragFloat("Mie Scale H (km)", &mie_scale_height_km_, 0.1F, 0.1F,
        100.0F, "%.2f")) {
    MarkDirty();
  }
  if (ImGui::SliderFloat(
        "Mie Anisotropy", &mie_anisotropy_, 0.0F, 0.99F, "%.2f")) {
    MarkDirty();
  }
  if (ImGui::SliderFloat(
        "Multi-Scattering", &multi_scattering_, 0.0F, 1.0F, "%.2f")) {
    MarkDirty();
  }

  ImGui::Separator();

  // Sun disk
  ImGui::Text("Sun Disk:");
  if (ImGui::Checkbox("Show Sun Disk", &sun_disk_enabled_)) {
    MarkDirty();
  }
  if (sun_disk_enabled_) {
    if (ImGui::SliderFloat(
          "Angular Radius (deg)", &sun_disk_radius_deg_, 0.01F, 5.0F, "%.3f")) {
      MarkDirty();
    }
  }

  ImGui::Separator();

  // Aerial perspective
  ImGui::Text("Aerial Perspective:");
  ImGui::TextDisabled("0 disables; higher increases effect");
  if (ImGui::DragFloat("Distance Scale", &aerial_perspective_scale_, 0.01F,
        0.0F, 50.0F, "%.2f")) {
    MarkDirty();
  }
  if (ImGui::DragFloat("Scattering Strength", &aerial_scattering_strength_,
        0.01F, 0.0F, 50.0F, "%.2f")) {
    MarkDirty();
  }

  ImGui::PopItemWidth();
  ImGui::Unindent();
}

void EnvironmentDebugPanel::DrawSkySphereSection()
{
  auto* env = config_.scene ? config_.scene->GetEnvironment().get() : nullptr;
  auto* sky
    = env ? env->TryGetSystem<scene::environment::SkySphere>().get() : nullptr;

  // Show warning if both sky systems are enabled
  if (sky_atmo_enabled_ && sky_sphere_enabled_) {
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
      "Warning: SkySphere is disabled when SkyAtmosphere is active");
  }

  if (!sky) {
    if (ImGui::Button("Add SkySphere")) {
      pending_changes_ = true;
      sky_sphere_enabled_ = true;
      // SkyAtmosphere and SkySphere are mutually exclusive; disable
      // SkyAtmosphere
      sky_atmo_enabled_ = false;
    }
    return;
  }

  // Only sync from scene when no pending changes to avoid overwriting
  // mutual exclusion state set by user interaction
  if (!pending_changes_) {
    sky_sphere_enabled_ = sky->IsEnabled();
  }
  if (ImGui::Checkbox("Enabled##SkySphere", &sky_sphere_enabled_)) {
    // SkyAtmosphere and SkySphere are mutually exclusive
    if (sky_sphere_enabled_) {
      sky_atmo_enabled_ = false;
    }
    MarkDirty();
  }

  if (!sky_sphere_enabled_) {
    return;
  }

  ImGui::Indent();
  ImGui::PushItemWidth(150);

  const char* sources[] = { "Cubemap", "Solid Color" };
  if (ImGui::Combo("Source##SkySphere", &sky_sphere_source_, sources, 2)) {
    MarkDirty();
  }

  if (sky_sphere_source_ == 0) { // Cubemap
    const auto key = sky->GetCubemapResource();
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
    ImGui::InputText("Path##Skybox", skybox_path_.data(), skybox_path_.size());
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Browse...##Skybox")) {
      const auto roots = file_browser_->GetContentRoots();
      auto picker_config = MakeSkyboxFileBrowserConfig(roots);

      // If the user already entered a path, use its parent as the starting dir.
      const std::filesystem::path current_path(
        std::string(skybox_path_.data()));
      if (!current_path.empty() && current_path.has_parent_path()) {
        picker_config.initial_directory = current_path.parent_path();
      }

      file_browser_->Open(picker_config);
    }

    file_browser_->UpdateAndDraw();
    if (const auto selected_path = file_browser_->ConsumeSelection()) {
      CopyPathToBuffer(*selected_path,
        std::span<char>(skybox_path_.data(), skybox_path_.size()));
    }

    const char* layouts[] = { "Equirectangular", "Horizontal Cross",
      "Vertical Cross", "Horizontal Strip", "Vertical Strip" };
    ImGui::Combo("Layout##Skybox", &skybox_layout_idx_, layouts, 5);

    const char* formats[] = { "RGBA8", "RGBA16F", "RGBA32F", "BC7" };
    ImGui::Combo("Output##Skybox", &skybox_output_format_idx_, formats, 4);

    ImGui::DragInt("Face Size##Skybox", &skybox_face_size_, 16, 16, 4096);
    ImGui::Checkbox("Flip Y##Skybox", &skybox_flip_y_);

    const bool output_is_ldr
      = (skybox_output_format_idx_ == 0) || (skybox_output_format_idx_ == 3);
    if (output_is_ldr) {
      ImGui::Checkbox("HDR->LDR Tonemap##Skybox", &skybox_tonemap_hdr_to_ldr_);
      ImGui::DragFloat("HDR Exposure (EV)##Skybox", &skybox_hdr_exposure_ev_,
        0.1F, -16.0F, 16.0F, "%.2f");
    }

    if (ImGui::Button("Load Skybox##Skybox")) {
      skybox_status_message_ = "Loading skybox...";
      skybox_last_face_size_ = 0;
      skybox_last_resource_key_ = oxygen::content::ResourceKey { 0U };

      if (!config_.skybox_service) {
        skybox_status_message_ = "Skybox service unavailable";
      } else {
        SkyboxService::LoadOptions options;
        options.layout = static_cast<SkyboxService::Layout>(
          std::clamp(skybox_layout_idx_, 0, 4));
        options.output_format = static_cast<SkyboxService::OutputFormat>(
          std::clamp(skybox_output_format_idx_, 0, 3));
        options.cube_face_size = std::clamp(skybox_face_size_, 16, 4096);
        options.flip_y = skybox_flip_y_;
        options.tonemap_hdr_to_ldr = skybox_tonemap_hdr_to_ldr_;
        options.hdr_exposure_ev = skybox_hdr_exposure_ev_;

        config_.skybox_service->LoadAndEquip(std::string(skybox_path_.data()),
          options, GetSkyLightParams(),
          [this](SkyboxService::LoadResult result) {
            SetSkyboxLoadStatus(
              result.status_message, result.face_size, result.resource_key);
            if (result.success) {
              RequestResync();
            }
          });
      }
    }
    ImGui::SameLine();
    if (!skybox_status_message_.empty()) {
      ImGui::Text("%s", skybox_status_message_.c_str());
    }

    if (skybox_last_face_size_ > 0) {
      ImGui::Text("Last face size: %d", skybox_last_face_size_);
      ImGui::Text("Last ResourceKey: %llu",
        static_cast<unsigned long long>(skybox_last_resource_key_.get()));
    }
  } else { // Solid color
    if (ImGui::ColorEdit3("Color##SkySphere", &sky_sphere_solid_color_.x)) {
      MarkDirty();
    }
  }

  if (ImGui::DragFloat("Intensity##SkySphere", &sky_sphere_intensity_, 0.01F,
        0.0F, 10.0F, "%.2f")) {
    MarkDirty();
  }

  if (ImGui::SliderFloat(
        "Rotation (deg)", &sky_sphere_rotation_deg_, 0.0F, 360.0F, "%.1f")) {
    MarkDirty();
  }

  ImGui::PopItemWidth();
  ImGui::Unindent();
}

void EnvironmentDebugPanel::DrawSkyLightSection()
{
  auto* env = config_.scene ? config_.scene->GetEnvironment().get() : nullptr;
  auto* light
    = env ? env->TryGetSystem<scene::environment::SkyLight>().get() : nullptr;

  ImGui::TextDisabled(
    "IBL is active when SkyLight is enabled and a cubemap is available\n"
    "(SkyLight specified cubemap, or SkySphere cubemap).");
  ImGui::Spacing();

  if (!light) {
    if (ImGui::Button("Add SkyLight")) {
      pending_changes_ = true;
      sky_light_enabled_ = true;
    }
    return;
  }

  sky_light_enabled_ = light->IsEnabled();
  if (ImGui::Checkbox("Enabled##SkyLight", &sky_light_enabled_)) {
    MarkDirty();
  }

  if (!sky_light_enabled_) {
    return;
  }

  ImGui::Indent();
  ImGui::PushItemWidth(150);

  const char* sources[] = { "Captured Scene", "Specified Cubemap" };
  if (ImGui::Combo("Source##SkyLight", &sky_light_source_, sources, 2)) {
    MarkDirty();
  }

  if (sky_light_source_ == 1) {
    const auto key = light->GetCubemapResource();
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

  if (ImGui::ColorEdit3("Tint##SkyLight", &sky_light_tint_.x)) {
    MarkDirty();
  }

  if (ImGui::DragFloat("Intensity##SkyLight", &sky_light_intensity_, 0.01F,
        0.0F, 10.0F, "%.2f")) {
    MarkDirty();
  }

  if (ImGui::DragFloat(
        "Diffuse", &sky_light_diffuse_, 0.01F, 0.0F, 2.0F, "%.2f")) {
    MarkDirty();
  }

  if (ImGui::DragFloat(
        "Specular", &sky_light_specular_, 0.01F, 0.0F, 2.0F, "%.2f")) {
    MarkDirty();
  }

  ImGui::PopItemWidth();
  ImGui::Unindent();
}

void EnvironmentDebugPanel::SetSkyboxLoadStatus(std::string_view status,
  int face_size, oxygen::content::ResourceKey resource_key)
{
  skybox_status_message_ = std::string(status);
  skybox_last_face_size_ = face_size;
  skybox_last_resource_key_ = resource_key;
  needs_sync_ = true;
}

auto EnvironmentDebugPanel::GetSkyLightParams() const
  -> SkyboxService::SkyLightParams
{
  return {
    .intensity = sky_light_intensity_,
    .diffuse_intensity = sky_light_diffuse_,
    .specular_intensity = sky_light_specular_,
    .tint_rgb = sky_light_tint_,
  };
}

// NOTE: DrawFogSection removed - use Aerial Perspective from SkyAtmosphere.
// Real volumetric fog system to be implemented in the future.

void EnvironmentDebugPanel::DrawPostProcessSection()
{
  auto* env = config_.scene ? config_.scene->GetEnvironment().get() : nullptr;
  auto* pp = env
    ? env->TryGetSystem<scene::environment::PostProcessVolume>().get()
    : nullptr;

  if (!pp) {
    if (ImGui::Button("Add PostProcess")) {
      pending_changes_ = true;
      post_process_enabled_ = true;
    }
    return;
  }

  post_process_enabled_ = pp->IsEnabled();
  if (ImGui::Checkbox("Enabled##PostProcess", &post_process_enabled_)) {
    MarkDirty();
  }

  if (!post_process_enabled_) {
    return;
  }

  ImGui::Indent();
  ImGui::PushItemWidth(150);

  // Tonemapping
  ImGui::Text("Tonemapping:");
  const char* tonemappers[] = { "ACES Fitted", "Reinhard", "None" };
  if (ImGui::Combo("Tonemapper", &tone_mapper_, tonemappers, 3)) {
    MarkDirty();
  }

  ImGui::Separator();

  // Exposure
  ImGui::Text("Exposure:");
  const char* exposure_modes[] = { "Manual", "Auto" };
  if (ImGui::Combo("Mode", &exposure_mode_, exposure_modes, 2)) {
    MarkDirty();
  }

  if (ImGui::DragFloat("Compensation (EV)", &exposure_compensation_ev_, 0.1F,
        -10.0F, 10.0F, "%.1f")) {
    MarkDirty();
  }

  if (exposure_mode_ == 1) { // Auto
    if (ImGui::DragFloat(
          "Min EV", &auto_exposure_min_ev_, 0.1F, -10.0F, 20.0F, "%.1f")) {
      MarkDirty();
    }
    if (ImGui::DragFloat(
          "Max EV", &auto_exposure_max_ev_, 0.1F, -10.0F, 20.0F, "%.1f")) {
      MarkDirty();
    }
    if (ImGui::DragFloat(
          "Speed Up", &auto_exposure_speed_up_, 0.1F, 0.1F, 10.0F, "%.1f")) {
      MarkDirty();
    }
    if (ImGui::DragFloat("Speed Down", &auto_exposure_speed_down_, 0.1F, 0.1F,
          10.0F, "%.1f")) {
      MarkDirty();
    }
  }

  ImGui::Separator();

  // Bloom
  ImGui::Text("Bloom:");
  if (ImGui::DragFloat(
        "Intensity##Bloom", &bloom_intensity_, 0.01F, 0.0F, 2.0F, "%.2f")) {
    MarkDirty();
  }
  if (bloom_intensity_ > 0.0F) {
    if (ImGui::DragFloat(
          "Threshold", &bloom_threshold_, 0.1F, 0.0F, 10.0F, "%.1f")) {
      MarkDirty();
    }
  }

  ImGui::Separator();

  // Color grading
  ImGui::Text("Color Grading:");
  if (ImGui::SliderFloat("Saturation", &saturation_, 0.0F, 2.0F, "%.2f")) {
    MarkDirty();
  }
  if (ImGui::SliderFloat("Contrast", &contrast_, 0.0F, 2.0F, "%.2f")) {
    MarkDirty();
  }
  if (ImGui::SliderFloat("Vignette", &vignette_, 0.0F, 1.0F, "%.2f")) {
    MarkDirty();
  }

  ImGui::PopItemWidth();
  ImGui::Unindent();
}

void EnvironmentDebugPanel::SyncFromScene()
{
  if (!config_.scene) {
    // Reset to defaults when no scene
    sky_atmo_enabled_ = false;
    sky_sphere_enabled_ = false;
    sky_light_enabled_ = false;
    post_process_enabled_ = false;
    sun_present_ = false;
    return;
  }

  auto* env = config_.scene->GetEnvironment().get();
  if (!env) {
    // Scene has no environment - reset all to defaults
    sky_atmo_enabled_ = false;
    sky_sphere_enabled_ = false;
    sky_light_enabled_ = false;
    post_process_enabled_ = false;
    sun_present_ = false;
    return;
  }

  // SkyAtmosphere - if system doesn't exist, just mark as disabled
  if (auto* atmo
    = env->TryGetSystem<scene::environment::SkyAtmosphere>().get()) {
    sky_atmo_enabled_ = atmo->IsEnabled();
    planet_radius_km_ = atmo->GetPlanetRadiusMeters() * kMetersToKm;
    atmosphere_height_km_ = atmo->GetAtmosphereHeightMeters() * kMetersToKm;
    ground_albedo_ = atmo->GetGroundAlbedoRgb();
    rayleigh_scale_height_km_
      = atmo->GetRayleighScaleHeightMeters() * kMetersToKm;
    mie_scale_height_km_ = atmo->GetMieScaleHeightMeters() * kMetersToKm;
    mie_anisotropy_ = atmo->GetMieAnisotropy();
    multi_scattering_ = atmo->GetMultiScatteringFactor();
    sun_disk_enabled_ = atmo->GetSunDiskEnabled();
    sun_disk_radius_deg_ = atmo->GetSunDiskAngularRadiusRadians() * kRadToDeg;
    aerial_perspective_scale_ = atmo->GetAerialPerspectiveDistanceScale();
    aerial_scattering_strength_ = atmo->GetAerialScatteringStrength();
  } else {
    sky_atmo_enabled_ = false;
  }

  // SkySphere
  if (auto* sky = env->TryGetSystem<scene::environment::SkySphere>().get()) {
    sky_sphere_enabled_ = sky->IsEnabled();
    sky_sphere_source_ = static_cast<int>(sky->GetSource());
    sky_sphere_solid_color_ = sky->GetSolidColorRgb();
    sky_sphere_intensity_ = sky->GetIntensity();
    sky_sphere_rotation_deg_ = sky->GetRotationRadians() * kRadToDeg;
  } else {
    sky_sphere_enabled_ = false;
  }

  // SkyLight
  if (auto* light = env->TryGetSystem<scene::environment::SkyLight>().get()) {
    sky_light_enabled_ = light->IsEnabled();
    sky_light_source_ = static_cast<int>(light->GetSource());
    sky_light_tint_ = light->GetTintRgb();
    sky_light_intensity_ = light->GetIntensity();
    sky_light_diffuse_ = light->GetDiffuseIntensity();
    sky_light_specular_ = light->GetSpecularIntensity();
  } else {
    sky_light_enabled_ = false;
  }

  // Sun
  if (auto* sun = env->TryGetSystem<scene::environment::Sun>().get()) {
    sun_present_ = true;
    sun_enabled_ = sun->IsEnabled();
    const bool from_scene
      = (sun->GetSunSource() == scene::environment::SunSource::kFromScene);
    sun_source_ = from_scene ? 0 : 1;
    sun_azimuth_deg_ = sun->GetAzimuthDegrees();
    sun_elevation_deg_ = sun->GetElevationDegrees();
    sun_color_rgb_ = sun->GetColorRgb();
    sun_intensity_lux_ = sun->GetIntensityLux();
    sun_use_temperature_ = sun->HasLightTemperature();
    if (sun_use_temperature_) {
      sun_temperature_kelvin_ = sun->GetLightTemperatureKelvin();
    }
    sun_component_disk_radius_deg_
      = sun->GetDiskAngularRadiusRadians() * kRadToDeg;

    if (from_scene) {
      UpdateSunLightCandidate();
      if (sun_light_available_) {
        if (auto light
          = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
          sun_enabled_ = light->get().Common().affects_world;
        }
      }
    }

    SaveSunSettingsToProfile(sun_source_);
  } else {
    sun_present_ = false;
    sun_source_ = 0;
    sun_enabled_ = false;
  }

  // NOTE: Fog sync removed - use Aerial Perspective from SkyAtmosphere.

  // PostProcess
  if (auto* pp
    = env->TryGetSystem<scene::environment::PostProcessVolume>().get()) {
    post_process_enabled_ = pp->IsEnabled();
    tone_mapper_ = static_cast<int>(pp->GetToneMapper());
    exposure_mode_ = static_cast<int>(pp->GetExposureMode());
    exposure_compensation_ev_ = pp->GetExposureCompensationEv();
    auto_exposure_min_ev_ = pp->GetAutoExposureMinEv();
    auto_exposure_max_ev_ = pp->GetAutoExposureMaxEv();
    auto_exposure_speed_up_ = pp->GetAutoExposureSpeedUp();
    auto_exposure_speed_down_ = pp->GetAutoExposureSpeedDown();
    bloom_intensity_ = pp->GetBloomIntensity();
    bloom_threshold_ = pp->GetBloomThreshold();
    saturation_ = pp->GetSaturation();
    contrast_ = pp->GetContrast();
    vignette_ = pp->GetVignetteIntensity();
  } else {
    post_process_enabled_ = false;
  }

  // NOTE: Debug flags (use_lut_, force_analytic_, etc.)
  // are NOT synced from scene - they are renderer debug controls that
  // persist across scene loads. See SyncDebugFlagsFromRenderer().
}

/*!
 Resets cached sun UI values to match the Sun component defaults.

 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Stack-only defaults object.
*/
void EnvironmentDebugPanel::ResetSunUiToDefaults()
{
  const scene::environment::Sun defaults;

  sun_present_ = true;
  sun_enabled_ = defaults.IsEnabled();
  sun_source_
    = (defaults.GetSunSource() == scene::environment::SunSource::kFromScene)
    ? 0
    : 1;
  sun_azimuth_deg_ = defaults.GetAzimuthDegrees();
  sun_elevation_deg_ = defaults.GetElevationDegrees();
  sun_color_rgb_ = defaults.GetColorRgb();
  sun_intensity_lux_ = defaults.GetIntensityLux();
  sun_use_temperature_ = defaults.HasLightTemperature();
  sun_temperature_kelvin_
    = sun_use_temperature_ ? defaults.GetLightTemperatureKelvin() : 6500.0F;
  sun_component_disk_radius_deg_
    = defaults.GetDiskAngularRadiusRadians() * kRadToDeg;

  SaveSunSettingsToProfile(0);
  SaveSunSettingsToProfile(1);
}

void EnvironmentDebugPanel::SyncDebugFlagsFromRenderer()
{
  if (!config_.renderer) {
    return;
  }

  // Sync current renderer debug state into UI
  const uint32_t flags = config_.renderer->GetAtmosphereDebugFlags();
  force_analytic_
    = (flags & static_cast<uint32_t>(AtmosphereDebugFlags::kForceAnalytic))
    != 0;
  visualize_lut_
    = (flags & static_cast<uint32_t>(AtmosphereDebugFlags::kVisualizeLut)) != 0;

  // Derive use_lut_ state: if neither force_analytic nor disabled
  use_lut_ = !force_analytic_;
}

void EnvironmentDebugPanel::MarkDirty() { pending_changes_ = true; }

auto EnvironmentDebugPanel::GetSunSettingsForSource(const int source)
  -> SunUiSettings&
{
  return (source == 0) ? sun_scene_settings_ : sun_synthetic_settings_;
}

void EnvironmentDebugPanel::LoadSunSettingsFromProfile(const int source)
{
  const auto& settings = GetSunSettingsForSource(source);
  sun_enabled_ = settings.enabled;
  sun_azimuth_deg_ = settings.azimuth_deg;
  sun_elevation_deg_ = settings.elevation_deg;
  sun_color_rgb_ = settings.color_rgb;
  sun_intensity_lux_ = settings.intensity_lux;
  sun_use_temperature_ = settings.use_temperature;
  sun_temperature_kelvin_ = settings.temperature_kelvin;
  sun_component_disk_radius_deg_ = settings.disk_radius_deg;
}

void EnvironmentDebugPanel::SaveSunSettingsToProfile(const int source)
{
  auto& settings = GetSunSettingsForSource(source);
  settings.enabled = sun_enabled_;
  settings.azimuth_deg = sun_azimuth_deg_;
  settings.elevation_deg = sun_elevation_deg_;
  settings.color_rgb = sun_color_rgb_;
  settings.intensity_lux = sun_intensity_lux_;
  settings.use_temperature = sun_use_temperature_;
  settings.temperature_kelvin = sun_temperature_kelvin_;
  settings.disk_radius_deg = sun_component_disk_radius_deg_;
}

auto EnvironmentDebugPanel::HasPendingChanges() const -> bool
{
  return pending_changes_;
}

void EnvironmentDebugPanel::ApplyPendingChanges()
{
  if (!pending_changes_ || !config_.scene) {
    return;
  }

  auto* env = config_.scene->GetEnvironment().get();

  // Create environment if needed
  if (!env) {
    config_.scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    env = config_.scene->GetEnvironment().get();
  }

  // Sun
  auto* sun = env->TryGetSystem<scene::environment::Sun>().get();
  if (sun_present_ && !sun) {
    sun = &env->AddSystem<scene::environment::Sun>();
  }
  if (sun) {
    sun->SetEnabled(sun_enabled_);
    const auto sun_source = (sun_source_ == 0)
      ? scene::environment::SunSource::kFromScene
      : scene::environment::SunSource::kSynthetic;
    sun->SetSunSource(sun_source);
    sun->SetAzimuthElevationDegrees(sun_azimuth_deg_, sun_elevation_deg_);
    sun->SetIntensityLux(sun_intensity_lux_);
    sun->SetDiskAngularRadiusRadians(
      sun_component_disk_radius_deg_ * kDegToRad);
    if (sun_use_temperature_) {
      sun->SetLightTemperatureKelvin(sun_temperature_kelvin_);
    } else {
      sun->SetColorRgb(sun_color_rgb_);
    }

    if (sun_source == scene::environment::SunSource::kFromScene) {
      DestroySyntheticSunLight();
      UpdateSunLightCandidate();
      if (sun_light_available_) {
        if (auto light
          = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
          light->get().SetIsSunLight(true);

          auto& common = light->get().Common();
          common.affects_world = sun_enabled_;
          common.intensity = sun_intensity_lux_;
          common.color_rgb = sun_use_temperature_
            ? KelvinToLinearRgb(sun_temperature_kelvin_)
            : sun_color_rgb_;

          const auto sun_dir = DirectionFromAzimuthElevation(
            sun_azimuth_deg_, sun_elevation_deg_);
          const glm::vec3 light_dir = -sun_dir;
          auto transform = sun_light_node_.GetTransform();
          transform.SetLocalRotation(RotationFromDirection(light_dir));
        }

        sun->SetLightReference(sun_light_node_);
      } else {
        sun->ClearLightReference();
      }
    } else {
      UpdateSunLightCandidate();
      if (sun_light_available_) {
        if (auto light
          = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
          light->get().SetIsSunLight(false);
          light->get().Common().affects_world = false;
        }
      }

      EnsureSyntheticSunLight();
      if (synthetic_sun_light_node_.IsAlive()) {
        if (auto light
          = synthetic_sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
          light->get().SetIsSunLight(sun_enabled_);
          light->get().SetEnvironmentContribution(true);

          auto& common = light->get().Common();
          common.affects_world = sun_enabled_;
          common.intensity = sun_intensity_lux_;
          common.color_rgb = sun_use_temperature_
            ? KelvinToLinearRgb(sun_temperature_kelvin_)
            : sun_color_rgb_;

          const auto sun_dir = DirectionFromAzimuthElevation(
            sun_azimuth_deg_, sun_elevation_deg_);
          const glm::vec3 light_dir = -sun_dir;
          auto transform = synthetic_sun_light_node_.GetTransform();
          transform.SetLocalRotation(RotationFromDirection(light_dir));
        }

        sun->SetLightReference(synthetic_sun_light_node_);
      } else {
        sun->ClearLightReference();
      }
    }
  }

  // SkyAtmosphere
  auto* atmo = env->TryGetSystem<scene::environment::SkyAtmosphere>().get();
  if (sky_atmo_enabled_ && !atmo) {
    atmo = &env->AddSystem<scene::environment::SkyAtmosphere>();
  }
  if (atmo) {
    atmo->SetEnabled(sky_atmo_enabled_);
    atmo->SetPlanetRadiusMeters(planet_radius_km_ * kKmToMeters);
    atmo->SetAtmosphereHeightMeters(atmosphere_height_km_ * kKmToMeters);
    atmo->SetGroundAlbedoRgb(ground_albedo_);
    atmo->SetRayleighScaleHeightMeters(rayleigh_scale_height_km_ * kKmToMeters);
    atmo->SetMieScaleHeightMeters(mie_scale_height_km_ * kKmToMeters);
    atmo->SetMieAnisotropy(mie_anisotropy_);
    atmo->SetMultiScatteringFactor(multi_scattering_);
    atmo->SetSunDiskEnabled(sun_disk_enabled_);
    atmo->SetSunDiskAngularRadiusRadians(sun_disk_radius_deg_ * kDegToRad);
    atmo->SetAerialPerspectiveDistanceScale(aerial_perspective_scale_);
    atmo->SetAerialScatteringStrength(aerial_scattering_strength_);

    if (config_.on_atmosphere_params_changed) {
      config_.on_atmosphere_params_changed();
    }
  }

  // SkySphere
  auto* sky = env->TryGetSystem<scene::environment::SkySphere>().get();
  if (sky_sphere_enabled_ && !sky) {
    sky = &env->AddSystem<scene::environment::SkySphere>();
  }
  if (sky) {
    sky->SetEnabled(sky_sphere_enabled_);
    sky->SetSource(
      static_cast<scene::environment::SkySphereSource>(sky_sphere_source_));
    sky->SetSolidColorRgb(sky_sphere_solid_color_);
    sky->SetIntensity(sky_sphere_intensity_);
    sky->SetRotationRadians(sky_sphere_rotation_deg_ * kDegToRad);
  }

  // SkyLight
  auto* light = env->TryGetSystem<scene::environment::SkyLight>().get();
  if (sky_light_enabled_ && !light) {
    light = &env->AddSystem<scene::environment::SkyLight>();
  }
  if (light) {
    light->SetEnabled(sky_light_enabled_);
    light->SetSource(
      static_cast<scene::environment::SkyLightSource>(sky_light_source_));
    light->SetTintRgb(sky_light_tint_);
    light->SetIntensity(sky_light_intensity_);
    light->SetDiffuseIntensity(sky_light_diffuse_);
    light->SetSpecularIntensity(sky_light_specular_);
  }

  // NOTE: Fog handling removed - use Aerial Perspective from SkyAtmosphere.
  // Real volumetric fog system to be implemented in the future.

  // PostProcess
  auto* pp = env->TryGetSystem<scene::environment::PostProcessVolume>().get();
  if (post_process_enabled_ && !pp) {
    pp = &env->AddSystem<scene::environment::PostProcessVolume>();
  }
  if (pp) {
    pp->SetEnabled(post_process_enabled_);
    pp->SetToneMapper(
      static_cast<scene::environment::ToneMapper>(tone_mapper_));
    pp->SetExposureMode(
      static_cast<scene::environment::ExposureMode>(exposure_mode_));
    pp->SetExposureCompensationEv(exposure_compensation_ev_);
    pp->SetAutoExposureRangeEv(auto_exposure_min_ev_, auto_exposure_max_ev_);
    pp->SetAutoExposureAdaptationSpeeds(
      auto_exposure_speed_up_, auto_exposure_speed_down_);
    pp->SetBloomIntensity(bloom_intensity_);
    pp->SetBloomThreshold(bloom_threshold_);
    pp->SetSaturation(saturation_);
    pp->SetContrast(contrast_);
    pp->SetVignetteIntensity(vignette_);

    if (config_.on_exposure_changed) {
      config_.on_exposure_changed();
    }
  }

  // Update renderer debug flags
  if (config_.renderer) {
    // Set atmosphere debug flags (use the centralized GetAtmosphereFlags
    // method)
    const uint32_t debug_flags = GetAtmosphereFlags();
    LOG_F(INFO, "ApplyPendingChanges: Setting atmosphere debug flags=0x{:x}",
      debug_flags);
    config_.renderer->SetAtmosphereDebugFlags(debug_flags);
  }

  pending_changes_ = false;
}

auto EnvironmentDebugPanel::FindSunLightCandidate() const
  -> std::optional<scene::SceneNode>
{
  if (!config_.scene) {
    return std::nullopt;
  }

  auto roots = config_.scene->GetRootNodes();
  std::vector<scene::SceneNode> stack;
  stack.reserve(roots.size());
  for (auto& root : roots) {
    stack.push_back(root);
  }

  std::optional<scene::SceneNode> first_directional;
  while (!stack.empty()) {
    auto node = stack.back();
    stack.pop_back();

    if (!node.IsAlive()) {
      continue;
    }

    if (auto light = node.GetLightAs<scene::DirectionalLight>()) {
      if (light->get().IsSunLight()) {
        return node;
      }
      if (!first_directional.has_value()) {
        first_directional = node;
      }
    }

    auto child_opt = node.GetFirstChild();
    while (child_opt.has_value()) {
      stack.push_back(*child_opt);
      child_opt = child_opt->GetNextSibling();
    }
  }

  return first_directional;
}

void EnvironmentDebugPanel::UpdateSunLightCandidate()
{
  sun_light_available_ = false;

  if (!config_.scene) {
    sun_light_node_ = scene::SceneNode {};
    return;
  }

  if (sun_light_node_.IsAlive()) {
    if (auto light = sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
      sun_light_available_ = true;
      return;
    }
  }

  auto candidate = FindSunLightCandidate();
  if (candidate.has_value() && candidate->IsAlive()) {
    sun_light_node_ = *candidate;
    sun_light_available_ = true;
    return;
  }

  sun_light_node_ = scene::SceneNode {};
}

void EnvironmentDebugPanel::EnsureSyntheticSunLight()
{
  if (!config_.scene) {
    synthetic_sun_light_node_ = scene::SceneNode {};
    synthetic_sun_light_created_ = false;
    return;
  }

  if (synthetic_sun_light_node_.IsAlive()) {
    if (auto light
      = synthetic_sun_light_node_.GetLightAs<scene::DirectionalLight>()) {
      return;
    }
  }

  synthetic_sun_light_node_ = config_.scene->CreateNode("SyntheticSunLight");
  synthetic_sun_light_created_ = synthetic_sun_light_node_.IsAlive();
  if (!synthetic_sun_light_created_) {
    return;
  }

  if (!synthetic_sun_light_node_.HasLight()) {
    auto light = std::make_unique<scene::DirectionalLight>();
    (void)synthetic_sun_light_node_.AttachLight(std::move(light));
  }
}

void EnvironmentDebugPanel::DestroySyntheticSunLight()
{
  if (!synthetic_sun_light_created_) {
    return;
  }

  if (!config_.scene) {
    synthetic_sun_light_node_ = scene::SceneNode {};
    synthetic_sun_light_created_ = false;
    return;
  }

  if (synthetic_sun_light_node_.IsAlive()) {
    (void)config_.scene->DestroyNode(synthetic_sun_light_node_);
  }

  synthetic_sun_light_node_ = scene::SceneNode {};
  synthetic_sun_light_created_ = false;
}

auto EnvironmentDebugPanel::GetAtmosphereFlags() const -> uint32_t
{
  uint32_t flags = 0;
  if (use_lut_) {
    flags |= static_cast<uint32_t>(AtmosphereDebugFlags::kUseLut);
  }
  if (visualize_lut_) {
    flags |= static_cast<uint32_t>(AtmosphereDebugFlags::kVisualizeLut);
  }
  if (force_analytic_) {
    flags |= static_cast<uint32_t>(AtmosphereDebugFlags::kForceAnalytic);
  }
  return flags;
}

} // namespace oxygen::examples::ui
