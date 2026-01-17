//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DebugUI.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include <imgui.h>

#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>

#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>

#if defined(OXYGEN_WINDOWS)
#  include <shobjidl_core.h>
#  include <windows.h>
#  include <wrl/client.h>
#endif

namespace {

#if defined(OXYGEN_WINDOWS)
auto TryBrowseForImageFile(std::string& out_utf8_path) -> bool
{
  out_utf8_path.clear();

  const HRESULT init_hr = CoInitializeEx(
    nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool co_initialized = SUCCEEDED(init_hr);

  auto cleanup = [&]() noexcept {
    if (co_initialized) {
      CoUninitialize();
    }
  };

  Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
  const HRESULT create_hr = CoCreateInstance(
    CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(create_hr) || !dlg) {
    cleanup();
    return false;
  }

  constexpr COMDLG_FILTERSPEC kFilters[] = {
    { L"Image files", L"*.hdr;*.exr;*.png;*.jpg;*.jpeg;*.tga;*.bmp" },
    { L"HDR images", L"*.hdr;*.exr" },
    { L"All files", L"*.*" },
  };
  (void)dlg->SetFileTypes(static_cast<UINT>(std::size(kFilters)), kFilters);
  (void)dlg->SetDefaultExtension(L"hdr");

  const HRESULT show_hr = dlg->Show(nullptr);
  if (FAILED(show_hr)) {
    cleanup();
    return false;
  }

  Microsoft::WRL::ComPtr<IShellItem> item;
  if (FAILED(dlg->GetResult(&item)) || !item) {
    cleanup();
    return false;
  }

  PWSTR wide_path = nullptr;
  const HRESULT name_hr = item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path);
  if (FAILED(name_hr) || !wide_path) {
    cleanup();
    return false;
  }

  std::string utf8;
  oxygen::string_utils::WideToUtf8(wide_path, utf8);
  CoTaskMemFree(wide_path);

  cleanup();

  if (utf8.empty()) {
    return false;
  }

  out_utf8_path = std::move(utf8);
  return true;
}

auto TryBrowseForDirectory(std::string& out_utf8_path) -> bool
{
  out_utf8_path.clear();

  const HRESULT init_hr = CoInitializeEx(
    nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool co_initialized = SUCCEEDED(init_hr);

  auto cleanup = [&]() noexcept {
    if (co_initialized) {
      CoUninitialize();
    }
  };

  Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
  const HRESULT create_hr = CoCreateInstance(
    CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(create_hr) || !dlg) {
    cleanup();
    return false;
  }

  DWORD flags = 0;
  if (FAILED(dlg->GetOptions(&flags))) {
    cleanup();
    return false;
  }
  flags |= FOS_PICKFOLDERS;
  (void)dlg->SetOptions(flags);

  const HRESULT show_hr = dlg->Show(nullptr);
  if (FAILED(show_hr)) {
    cleanup();
    return false;
  }

  Microsoft::WRL::ComPtr<IShellItem> item;
  if (FAILED(dlg->GetResult(&item)) || !item) {
    cleanup();
    return false;
  }

  PWSTR wide_path = nullptr;
  const HRESULT name_hr = item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path);
  if (FAILED(name_hr) || !wide_path) {
    cleanup();
    return false;
  }

  std::string utf8;
  oxygen::string_utils::WideToUtf8(wide_path, utf8);
  CoTaskMemFree(wide_path);

  cleanup();

  if (utf8.empty()) {
    return false;
  }

  out_utf8_path = std::move(utf8);
  return true;
}

#endif

auto FormatLabel(const oxygen::Format format) -> const char*
{
  switch (format) {
  case oxygen::Format::kRGBA8UNormSRGB:
    return "RGBA8 sRGB";
  case oxygen::Format::kBC7UNormSRGB:
    return "BC7 sRGB";
  case oxygen::Format::kRGBA16Float:
    return "RGBA16F";
  case oxygen::Format::kRGBA32Float:
    return "RGBA32F";
  default:
    return "Unknown";
  }
}

auto TextureTypeLabel(const oxygen::TextureType type) -> const char*
{
  switch (type) {
  case oxygen::TextureType::kTexture2D:
    return "2D";
  case oxygen::TextureType::kTextureCube:
    return "Cube";
  case oxygen::TextureType::kTexture3D:
    return "3D";
  default:
    return "Other";
  }
}

auto IsCubemapType(const oxygen::TextureType type) -> bool
{
  return type == oxygen::TextureType::kTextureCube;
}

auto ApplyUvOriginFix(const glm::vec2 scale, const glm::vec2 offset,
  const bool flip_u, const bool flip_v) -> std::pair<glm::vec2, glm::vec2>
{
  glm::vec2 out_scale = scale;
  glm::vec2 out_offset = offset;

  if (flip_u) {
    out_offset.x = out_scale.x + out_offset.x;
    out_scale.x = -out_scale.x;
  }
  if (flip_v) {
    out_offset.y = out_scale.y + out_offset.y;
    out_scale.y = -out_scale.y;
  }

  return { out_scale, out_offset };
}

} // namespace

namespace oxygen::examples::textured_cube {

auto DebugUI::GetEffectiveUvTransform() const -> std::pair<glm::vec2, glm::vec2>
{
  bool fix_u = uv_state_.extra_flip_u;
  bool fix_v = uv_state_.extra_flip_v;

  if (uv_state_.fix_mode == OrientationFixMode::kNormalizeUvInTransform) {
    if (uv_state_.uv_origin != UvOrigin::kTopLeft
      && uv_state_.image_origin == ImageOrigin::kTopLeft) {
      fix_v = !fix_v;
    }
    if (uv_state_.uv_origin == UvOrigin::kTopLeft
      && uv_state_.image_origin != ImageOrigin::kTopLeft) {
      fix_v = !fix_v;
    }
  }

  return ApplyUvOriginFix(uv_state_.scale, uv_state_.offset, fix_u, fix_v);
}

auto DebugUI::Draw(engine::FrameContext& context,
  const CameraController& camera,
  oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
  oxygen::engine::ShaderPassConfig* shader_pass_config,
  const std::shared_ptr<const oxygen::data::MaterialAsset>& sphere_material,
  const std::shared_ptr<const oxygen::data::MaterialAsset>& cube_material,
  bool& cube_needs_rebuild) -> void
{
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 200), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(
        "Textured Cube Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Controls:");
  ImGui::BulletText("Mouse wheel: zoom");
  ImGui::BulletText("RMB + mouse drag: orbit");

  if (ImGui::BeginTabBar("DemoTabs")) {
    if (ImGui::BeginTabItem("Materials/UV")) {
      DrawMaterialsTab(
        renderer, sphere_material, cube_material, cube_needs_rebuild);
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Lighting")) {
      DrawLightingTab(context.GetScene(), renderer, shader_pass_config);
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::Separator();
  ImGui::Text("Orbit yaw:   %.3f rad", camera.GetOrbitYaw());
  ImGui::Text("Orbit pitch: %.3f rad", camera.GetOrbitPitch());
  ImGui::Text("Distance:    %.3f", camera.GetDistance());

  ImGui::End();

  DrawImportWindow();
  DrawCookedBrowserWindow();
}

auto DebugUI::DrawMaterialsTab(
  oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
  const std::shared_ptr<const oxygen::data::MaterialAsset>& sphere_material,
  const std::shared_ptr<const oxygen::data::MaterialAsset>& cube_material,
  bool& cube_needs_rebuild) -> void
{
  ImGui::Separator();
  ImGui::TextUnformatted("Textures:");

  bool rebuild_requested = false;
  bool uv_transform_changed = false;

  auto DrawSlotControls = [&](const char* label, TextureSlotState& slot) {
    ImGui::Separator();
    ImGui::TextUnformatted(label);
    ImGui::PushID(label);

    int mode = static_cast<int>(slot.mode);
    const bool mode_changed_1 = ImGui::RadioButton("Forced error", &mode,
      static_cast<int>(SceneSetup::TextureIndexMode::kForcedError));
    ImGui::SameLine();
    const bool mode_changed_2 = ImGui::RadioButton("Fallback (0)", &mode,
      static_cast<int>(SceneSetup::TextureIndexMode::kFallback));
    ImGui::SameLine();
    const bool mode_changed_3 = ImGui::RadioButton(
      "Custom", &mode, static_cast<int>(SceneSetup::TextureIndexMode::kCustom));

    if (mode_changed_1 || mode_changed_2 || mode_changed_3) {
      slot.mode = static_cast<SceneSetup::TextureIndexMode>(mode);
      rebuild_requested = true;
    }

    if (slot.mode == SceneSetup::TextureIndexMode::kCustom) {
      ImGui::Text("Cooked texture index: %u", slot.resource_index);
    }

    ImGui::PopID();
  };

  DrawSlotControls("Sphere base color", sphere_texture_);
  DrawSlotControls("Cube base color", cube_texture_);

  ImGui::Separator();
  ImGui::TextUnformatted("UV:");

  ImGui::Separator();
  ImGui::TextUnformatted("Surface:");

  if (ImGui::Button("Preset: reflective metal")) {
    surface_state_.metalness = 0.85f;
    surface_state_.roughness = 0.12f;
    rebuild_requested = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Preset: mirror")) {
    surface_state_.metalness = 1.0f;
    surface_state_.roughness = 0.02f;
    rebuild_requested = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Mirror preset still uses the PBR pipeline; it just sets parameters.\n"
      "For strong reflections, load a skybox so IBL is available.");
  }

  if (ImGui::SliderFloat("Metalness", &surface_state_.metalness, 0.0f, 1.0f,
        "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
    rebuild_requested = true;
  }

  if (ImGui::SliderFloat("Roughness", &surface_state_.roughness, 0.0f, 1.0f,
        "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
    rebuild_requested = true;
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Base color:");

  if (ImGui::Checkbox("Use constant base color (disable texture sampling)",
        &surface_state_.use_constant_base_color)) {
    rebuild_requested = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Disables *all* material texture sampling and uses a constant base "
      "color.\n"
      "This is the fastest way to confirm PBR+IBL reflections are working.\n\n"
      "Why this matters: in metallic workflow, if the base-color texture is "
      "black, F0 becomes black and specular IBL will be black too.");
  }

  ImGui::BeginDisabled(!surface_state_.use_constant_base_color);
  float rgb[3] = { surface_state_.constant_base_color_rgb.x,
    surface_state_.constant_base_color_rgb.y,
    surface_state_.constant_base_color_rgb.z };
  if (ImGui::ColorEdit3("Constant color", rgb,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    surface_state_.constant_base_color_rgb = { rgb[0], rgb[1], rgb[2] };
    rebuild_requested = true;
  }
  ImGui::EndDisabled();

  constexpr float kUvScaleMin = 0.01f;
  constexpr float kUvScaleMax = 64.0f;
  constexpr float kUvOffsetMin = -64.0f;
  constexpr float kUvOffsetMax = 64.0f;

  auto SanitizeFinite = [](float v, float fallback) -> float {
    return std::isfinite(v) ? v : fallback;
  };

  float uv_scale[2] = { uv_state_.scale.x, uv_state_.scale.y };
  if (ImGui::DragFloat2("UV scale", uv_scale, 0.01f, kUvScaleMin, kUvScaleMax,
        "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
    const glm::vec2 new_scale {
      std::clamp(SanitizeFinite(uv_scale[0], 1.0f), kUvScaleMin, kUvScaleMax),
      std::clamp(SanitizeFinite(uv_scale[1], 1.0f), kUvScaleMin, kUvScaleMax),
    };
    if (new_scale != uv_state_.scale) {
      uv_state_.scale = new_scale;
      uv_transform_changed = true;
    }
  }

  float uv_offset[2] = { uv_state_.offset.x, uv_state_.offset.y };
  if (ImGui::DragFloat2("UV offset", uv_offset, 0.01f, kUvOffsetMin,
        kUvOffsetMax, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
    const glm::vec2 new_offset {
      std::clamp(
        SanitizeFinite(uv_offset[0], 0.0f), kUvOffsetMin, kUvOffsetMax),
      std::clamp(
        SanitizeFinite(uv_offset[1], 0.0f), kUvOffsetMin, kUvOffsetMax),
    };
    if (new_offset != uv_state_.offset) {
      uv_state_.offset = new_offset;
      uv_transform_changed = true;
    }
  }

  if (ImGui::Button("Reset UV")) {
    uv_state_.scale = { 1.0f, 1.0f };
    uv_state_.offset = { 0.0f, 0.0f };
    uv_transform_changed = true;
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Orientation:");

  if (ImGui::Button("Apply recommended settings")) {
    uv_state_.fix_mode = OrientationFixMode::kNormalizeTextureOnUpload;
    uv_state_.uv_origin = UvOrigin::kBottomLeft;
    uv_state_.image_origin = ImageOrigin::kTopLeft;
    uv_state_.extra_flip_u = false;
    uv_state_.extra_flip_v = false;
    uv_transform_changed = true;
  }

  if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_None)) {
    ImGui::TextUnformatted("These controls affect UV transform visualization.");

    {
      int mode = static_cast<int>(uv_state_.fix_mode);
      const bool m0 = ImGui::RadioButton("Fix: normalize texture on upload",
        &mode, static_cast<int>(OrientationFixMode::kNormalizeTextureOnUpload));
      const bool m1 = ImGui::RadioButton("Fix: normalize UV in transform",
        &mode, static_cast<int>(OrientationFixMode::kNormalizeUvInTransform));
      const bool m2 = ImGui::RadioButton(
        "Fix: none", &mode, static_cast<int>(OrientationFixMode::kNone));

      if (m0 || m1 || m2) {
        uv_state_.fix_mode = static_cast<OrientationFixMode>(mode);
        uv_transform_changed = true;
      }
    }

    {
      int uv_origin = static_cast<int>(uv_state_.uv_origin);
      if (ImGui::RadioButton("UV origin: bottom-left (authoring)", &uv_origin,
            static_cast<int>(UvOrigin::kBottomLeft))) {
        uv_state_.uv_origin = static_cast<UvOrigin>(uv_origin);
        uv_transform_changed = true;
      }
      if (ImGui::RadioButton("UV origin: top-left", &uv_origin,
            static_cast<int>(UvOrigin::kTopLeft))) {
        uv_state_.uv_origin = static_cast<UvOrigin>(uv_origin);
        uv_transform_changed = true;
      }
    }

    {
      int img_origin = static_cast<int>(uv_state_.image_origin);
      if (ImGui::RadioButton("Image origin: top-left (PNG)", &img_origin,
            static_cast<int>(ImageOrigin::kTopLeft))) {
        uv_state_.image_origin = static_cast<ImageOrigin>(img_origin);
        uv_transform_changed = true;
      }
      if (ImGui::RadioButton("Image origin: bottom-left", &img_origin,
            static_cast<int>(ImageOrigin::kBottomLeft))) {
        uv_state_.image_origin = static_cast<ImageOrigin>(img_origin);
        uv_transform_changed = true;
      }
    }

    {
      bool flip_u = uv_state_.extra_flip_u;
      bool flip_v = uv_state_.extra_flip_v;
      if (ImGui::Checkbox("Extra flip U", &flip_u)) {
        uv_state_.extra_flip_u = flip_u;
        uv_transform_changed = true;
      }
      ImGui::SameLine();
      if (ImGui::Checkbox("Extra flip V", &flip_v)) {
        uv_state_.extra_flip_v = flip_v;
        uv_transform_changed = true;
      }
    }
  }

  if (uv_transform_changed && renderer) {
    const auto [effective_uv_scale, effective_uv_offset]
      = GetEffectiveUvTransform();
    if (sphere_material) {
      // TODO: Apply baseline UV transform from MaterialAsset defaults and
      // move this override to per-instance material overrides when the
      // MaterialInstance system is available.
      (void)renderer->OverrideMaterialUvTransform(
        *sphere_material, effective_uv_scale, effective_uv_offset);
    }
    if (cube_material) {
      // TODO: Apply baseline UV transform from MaterialAsset defaults and
      // move this override to per-instance material overrides when the
      // MaterialInstance system is available.
      (void)renderer->OverrideMaterialUvTransform(
        *cube_material, effective_uv_scale, effective_uv_offset);
    }
  }

  if (rebuild_requested) {
    cube_needs_rebuild = true;
  }
}

auto DebugUI::DrawImportWindow() -> void
{
  ImGui::SetNextWindowPos(ImVec2(460, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(440, 260), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(
        "Cooked Texture Import", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  ImGui::InputText("Cooked root", import_state_.cooked_root.data(),
    import_state_.cooked_root.size());
  ImGui::SameLine();
  if (ImGui::Button("Browse##CookedRoot")) {
#if defined(OXYGEN_WINDOWS)
    std::string chosen;
    if (TryBrowseForDirectory(chosen)) {
      std::snprintf(import_state_.cooked_root.data(),
        import_state_.cooked_root.size(), "%s", chosen.c_str());
    }
#endif
  }

  ImGui::InputText("Source image", import_state_.source_path.data(),
    import_state_.source_path.size());
  ImGui::SameLine();
  if (ImGui::Button("Browse##SourceImage")) {
#if defined(OXYGEN_WINDOWS)
    std::string chosen;
    if (TryBrowseForImageFile(chosen)) {
      std::snprintf(import_state_.source_path.data(),
        import_state_.source_path.size(), "%s", chosen.c_str());
    }
#endif
  }

  constexpr const char* kImportKinds[] = {
    "Texture (2D)",
    "Skybox: HDR equirect",
    "Skybox: layout image",
  };
  ImGui::SetNextItemWidth(220.0f);
  ImGui::Combo("Import kind", &import_state_.import_kind, kImportKinds,
    IM_ARRAYSIZE(kImportKinds));

  constexpr const char* kFormats[] = {
    "RGBA8 sRGB",
    "BC7 sRGB",
    "RGBA16F",
    "RGBA32F",
  };
  ImGui::SetNextItemWidth(200.0f);
  ImGui::Combo("Output format", &import_state_.output_format_idx, kFormats,
    IM_ARRAYSIZE(kFormats));

  ImGui::Checkbox("Generate mips", &import_state_.generate_mips);

  ImGui::BeginDisabled(!import_state_.generate_mips);
  ImGui::SetNextItemWidth(160.0f);
  ImGui::SliderInt("Max mip levels", &import_state_.max_mip_levels, 0, 12,
    import_state_.max_mip_levels == 0 ? "Full chain" : "%d");

  constexpr const char* kMipFilters[] = {
    "Box (fast)",
    "Kaiser",
    "Lanczos",
  };
  ImGui::SetNextItemWidth(160.0f);
  ImGui::Combo("Mip filter", &import_state_.mip_filter_idx, kMipFilters,
    IM_ARRAYSIZE(kMipFilters));
  ImGui::EndDisabled();
  ImGui::Checkbox("Flip Y on decode", &import_state_.flip_y);
  ImGui::SameLine();
  ImGui::Checkbox("Force RGBA", &import_state_.force_rgba);

  if (import_state_.import_kind == 1) {
    constexpr int kFaceSizes[] = { 128, 256, 512, 1024, 2048 };
    constexpr const char* kFaceSizeNames[]
      = { "128", "256", "512", "1024", "2048" };
    int current_idx = 2;
    for (int i = 0; i < IM_ARRAYSIZE(kFaceSizes); ++i) {
      if (kFaceSizes[i] == import_state_.cube_face_size) {
        current_idx = i;
        break;
      }
    }
    if (ImGui::Combo("Cube face size", &current_idx, kFaceSizeNames,
          IM_ARRAYSIZE(kFaceSizeNames))) {
      import_state_.cube_face_size = kFaceSizes[current_idx];
    }
  }

  if (import_state_.import_kind == 2) {
    constexpr const char* kLayoutNames[] = {
      "Auto",
      "Horizontal Cross",
      "Vertical Cross",
      "Horizontal Strip",
      "Vertical Strip",
    };
    ImGui::Combo("Cube layout", &import_state_.layout_idx, kLayoutNames,
      IM_ARRAYSIZE(kLayoutNames));
  }

  if (ImGui::Button("Submit Import")) {
    import_state_.import_requested = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh Cooked Root")) {
    import_state_.refresh_requested = true;
  }

  if (!import_state_.status_message.empty()) {
    ImGui::Text("Status: %s", import_state_.status_message.c_str());
  }
  if (import_state_.import_in_flight) {
    ImGui::ProgressBar(import_state_.import_progress, ImVec2(-1.0f, 0.0f));
  }

  ImGui::End();
}

auto DebugUI::DrawCookedBrowserWindow() -> void
{
  ImGui::SetNextWindowPos(ImVec2(460, 300), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(
        "Cooked Texture Browser", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  ImGui::Text("Cooked entries: %zu", cooked_entries_.size());
  ImGui::Text("Sphere texture index: %u", sphere_texture_.resource_index);
  ImGui::Text("Cube texture index:   %u", cube_texture_.resource_index);
  ImGui::Separator();

  if (cooked_entries_.empty()) {
    ImGui::TextDisabled("No cooked textures loaded.");
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("CookedTextures", 7,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
          | ImGuiTableFlags_ScrollY,
        ImVec2(0.0f, 320.0f))) {
    ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Dims", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Mips", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (const auto& entry : cooked_entries_) {
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%u", entry.index);

      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(TextureTypeLabel(entry.texture_type));

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%ux%u", entry.width, entry.height);

      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%u", entry.mip_levels);

      ImGui::TableSetColumnIndex(4);
      ImGui::TextUnformatted(FormatLabel(entry.format));

      ImGui::TableSetColumnIndex(5);
      ImGui::Text("%u", static_cast<unsigned int>(entry.size_bytes / 1024));

      ImGui::TableSetColumnIndex(6);
      ImGui::PushID(static_cast<int>(entry.index));
      if (ImGui::Button("Sphere")) {
        browser_action_.type = BrowserAction::Type::kSetSphere;
        browser_action_.entry_index = entry.index;
      }
      ImGui::SameLine();
      if (ImGui::Button("Cube")) {
        browser_action_.type = BrowserAction::Type::kSetCube;
        browser_action_.entry_index = entry.index;
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!IsCubemapType(entry.texture_type));
      if (ImGui::Button("Skybox")) {
        browser_action_.type = BrowserAction::Type::kSetSkybox;
        browser_action_.entry_index = entry.index;
      }
      ImGui::EndDisabled();
      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  ImGui::End();
}

auto DebugUI::DrawLightingTab(oxygen::observer_ptr<scene::Scene> scene,
  oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
  oxygen::engine::ShaderPassConfig* shader_pass_config) -> void
{
  ImGui::Separator();
  ImGui::TextUnformatted("Environment:");
  ImGui::TextDisabled("Use the Cooked Texture Browser to set a skybox.");

  if (renderer) {
    if (const auto env_static = renderer->GetEnvironmentStaticDataManager();
      env_static) {
      const auto brdf_slot = env_static->GetBrdfLutSlot();
      const bool brdf_ready = brdf_slot != kInvalidShaderVisibleIndex;

      ImGui::Text("BRDF LUT: %s (slot=%u)", brdf_ready ? "ready" : "pending",
        brdf_ready ? brdf_slot.get() : 0U);
      if (!brdf_ready && ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
          "BRDF LUT is generated/uploaded asynchronously.\n"
          "While pending, Real(PBR) may temporarily fall back to an analytic "
          "approximation.");
      }
    }
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Sky light:");

  if (shader_pass_config) {
    ImGui::SeparatorText("Shader debug");

    constexpr const char* kShaderModeNames[] = {
      "Real (PBR)",
      "Debug: light culling heat map",
      "Debug: depth slice",
      "Debug: cluster index",
      "Debug: IBL specular (prefilter)",
      "Debug: raw sky cubemap (reflect)",
      "Debug: raw sky cubemap (camera ray)",
    };

    int mode_idx = 0;
    switch (shader_pass_config->debug_mode) {
    case oxygen::engine::ShaderDebugMode::kLightCullingHeatMap:
      mode_idx = 1;
      break;
    case oxygen::engine::ShaderDebugMode::kDepthSlice:
      mode_idx = 2;
      break;
    case oxygen::engine::ShaderDebugMode::kClusterIndex:
      mode_idx = 3;
      break;
    case oxygen::engine::ShaderDebugMode::kIblSpecular:
      mode_idx = 4;
      break;
    case oxygen::engine::ShaderDebugMode::kIblRawSky:
      mode_idx = 5;
      break;
    case oxygen::engine::ShaderDebugMode::kIblRawSkyViewDir:
      mode_idx = 6;
      break;
    case oxygen::engine::ShaderDebugMode::kDisabled:
    default:
      mode_idx = 0;
      break;
    }

    ImGui::SetNextItemWidth(260.0F);
    if (ImGui::Combo("Shader mode", &mode_idx, kShaderModeNames,
          IM_ARRAYSIZE(kShaderModeNames))) {
      switch (mode_idx) {
      case 1:
        shader_pass_config->debug_mode
          = oxygen::engine::ShaderDebugMode::kLightCullingHeatMap;
        break;
      case 2:
        shader_pass_config->debug_mode
          = oxygen::engine::ShaderDebugMode::kDepthSlice;
        break;
      case 3:
        shader_pass_config->debug_mode
          = oxygen::engine::ShaderDebugMode::kClusterIndex;
        break;
      case 4:
        shader_pass_config->debug_mode
          = oxygen::engine::ShaderDebugMode::kIblSpecular;
        break;
      case 5:
        shader_pass_config->debug_mode
          = oxygen::engine::ShaderDebugMode::kIblRawSky;
        break;
      case 6:
        shader_pass_config->debug_mode
          = oxygen::engine::ShaderDebugMode::kIblRawSkyViewDir;
        break;
      default:
        shader_pass_config->debug_mode
          = oxygen::engine::ShaderDebugMode::kDisabled;
        break;
      }
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Real (PBR) renders the normal forward shading path.\n"
        "Debug modes swap in a specialized pixel shader variant.\n"
        "Note: changing this recompiles the ShaderPass PSO.");
    }
  }

  bool skylight_changed = false;

  bool has_valid_ibl_source = false;
  const char* ibl_source_label = "None";
  if (scene) {
    if (auto env = scene->GetEnvironment(); env) {
      const auto sky_light = env->TryGetSystem<scene::environment::SkyLight>();
      const auto sky_sphere
        = env->TryGetSystem<scene::environment::SkySphere>();

      const bool skylight_has_cubemap = sky_light && sky_light->IsEnabled()
        && sky_light->GetSource()
          == scene::environment::SkyLightSource::kSpecifiedCubemap
        && !sky_light->GetCubemapResource().IsPlaceholder();
      const bool skysphere_has_cubemap = sky_sphere && sky_sphere->IsEnabled()
        && sky_sphere->GetSource()
          == scene::environment::SkySphereSource::kCubemap
        && !sky_sphere->GetCubemapResource().IsPlaceholder();

      if (skylight_has_cubemap) {
        has_valid_ibl_source = true;
        ibl_source_label = "SkyLight cubemap";
      } else if (skysphere_has_cubemap) {
        has_valid_ibl_source = true;
        ibl_source_label = "SkySphere cubemap";
      } else if (sky_light && sky_light->IsEnabled()
        && sky_light->GetSource()
          == scene::environment::SkyLightSource::kCapturedScene) {
        ibl_source_label = "Captured scene (not available)";
      } else {
        ibl_source_label = "None";
      }
    } else {
      ibl_source_label = "No SceneEnvironment";
    }
  } else {
    ibl_source_label = "No Scene";
  }

  ImGui::Text("IBL source: %s", ibl_source_label);
  if (has_valid_ibl_source) {
    ImGui::TextDisabled(
      "Tip: temporarily reduce direct light and diffuse IBL.");
  }

  if (renderer) {
    ImGui::SeparatorText("IBL status");

    const bool can_regenerate = has_valid_ibl_source;
    ImGui::BeginDisabled(!can_regenerate);
    if (ImGui::Button("Regenerate IBL now")) {
      renderer->RequestIblRegeneration();
    }
    ImGui::EndDisabled();
    if (!can_regenerate && ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Load a skybox first so SkyLight/SkySphere has a cubemap source.");
    }
  }

  if (has_valid_ibl_source) {
    if (ImGui::Button("Focus: IBL specular")) {
      // Make the specular contribution stand out by minimizing competing terms.
      lighting_state_.sky_light_intensity = 8.0f;
      lighting_state_.sky_light_diffuse = 0.0f;
      lighting_state_.sky_light_specular = 4.0f;
      lighting_state_.sun_intensity = 0.0f;
      skylight_changed = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Sets:\n"
        "- Sun intensity = 0\n"
        "- SkyLight intensity = 8\n"
        "- SkyLight diffuse = 0\n"
        "- SkyLight specular = 4\n\n"
        "Expected: the cube brightens with environment-colored reflections.");
    }
  }
  if (ImGui::SliderFloat("SkyLight intensity",
        &lighting_state_.sky_light_intensity, 0.0f, 8.0f, "%.2f",
        ImGuiSliderFlags_AlwaysClamp)) {
    skylight_changed = true;
  }
  if (ImGui::SliderFloat("SkyLight diffuse", &lighting_state_.sky_light_diffuse,
        0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
    skylight_changed = true;
  }

  const bool disable_specular = !has_valid_ibl_source;
  if (disable_specular) {
    ImGui::TextDisabled("Specular needs an IBL cubemap");
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "SkyLight specular is only visible when the renderer has a valid "
        "environment cubemap to sample.\n\n"
        "Valid sources:\n"
        "- SkyLight source = Specified Cubemap (with a loaded skybox)\n"
        "- SkySphere source = Cubemap (with a loaded skybox)");
    }
  }

  ImGui::BeginDisabled(disable_specular);
  if (ImGui::SliderFloat("SkyLight specular",
        &lighting_state_.sky_light_specular, 0.0f, 4.0f, "%.2f",
        ImGuiSliderFlags_AlwaysClamp)) {
    skylight_changed = true;
  }
  ImGui::EndDisabled();

  if (skylight_changed && scene) {
    if (auto env = scene->GetEnvironment(); env) {
      if (auto sky_light = env->TryGetSystem<scene::environment::SkyLight>()) {
        sky_light->SetIntensity(lighting_state_.sky_light_intensity);
        sky_light->SetDiffuseIntensity(lighting_state_.sky_light_diffuse);
        sky_light->SetSpecularIntensity(lighting_state_.sky_light_specular);
      }
    }
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Sun (directional):");

  if (ImGui::SliderFloat("Sun intensity", &lighting_state_.sun_intensity, 0.0f,
        30.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
    // Sun update handled by caller
  }

  float sun_color[3] = { lighting_state_.sun_color_rgb.x,
    lighting_state_.sun_color_rgb.y, lighting_state_.sun_color_rgb.z };
  if (ImGui::ColorEdit3("Sun color", sun_color,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    lighting_state_.sun_color_rgb
      = { sun_color[0], sun_color[1], sun_color[2] };
  }
}

} // namespace oxygen::examples::textured_cube
