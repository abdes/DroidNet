//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DebugUI.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    { L"Image files", L"*.hdr;*.exr;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.dds" },
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

#endif

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
  const CameraController& camera, SceneSetup::TextureIndexMode& texture_mode,
  std::uint32_t& custom_texture_resource_index,
  oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
  oxygen::engine::ShaderPassConfig* shader_pass_config,
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
      DrawMaterialsTab(texture_mode, custom_texture_resource_index, renderer,
        cube_material, cube_needs_rebuild);
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
}

auto DebugUI::DrawMaterialsTab(SceneSetup::TextureIndexMode& texture_mode,
  std::uint32_t& /*custom_texture_resource_index*/,
  oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
  const std::shared_ptr<const oxygen::data::MaterialAsset>& cube_material,
  bool& cube_needs_rebuild) -> void
{
  ImGui::Separator();
  ImGui::TextUnformatted("Texture:");

  bool rebuild_requested = false;
  bool uv_transform_changed = false;

  {
    int mode = static_cast<int>(texture_mode);
    const bool mode_changed = ImGui::RadioButton("Forced error", &mode,
      static_cast<int>(SceneSetup::TextureIndexMode::kForcedError));
    ImGui::SameLine();
    const bool mode_changed_2 = ImGui::RadioButton("Fallback (0)", &mode,
      static_cast<int>(SceneSetup::TextureIndexMode::kFallback));
    ImGui::SameLine();
    const bool mode_changed_3 = ImGui::RadioButton(
      "Custom", &mode, static_cast<int>(SceneSetup::TextureIndexMode::kCustom));

    rebuild_requested |= (mode_changed || mode_changed_2 || mode_changed_3);
    texture_mode = static_cast<SceneSetup::TextureIndexMode>(mode);
  }

  if (texture_mode == SceneSetup::TextureIndexMode::kCustom) {
    ImGui::InputText(
      "Image path", texture_state_.path.data(), texture_state_.path.size());
    if (ImGui::Button("Browse...")) {
#if defined(OXYGEN_WINDOWS)
      std::string chosen;
      if (TryBrowseForImageFile(chosen)) {
        std::snprintf(texture_state_.path.data(), texture_state_.path.size(),
          "%s", chosen.c_str());
      }
#endif
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Image")) {
      texture_state_.load_requested = true;
      texture_state_.status_message.clear();
    }

    constexpr const char* kFormatNames[] = {
      "RGBA8 sRGB",
      "BC7 sRGB (upload fails)",
      "RGBA16F (upload fails)",
      "RGBA32F (upload fails)",
    };
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Output format", &texture_state_.output_format_idx,
      kFormatNames, IM_ARRAYSIZE(kFormatNames));
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "RGBA8: Works end-to-end (LDR only)\n"
        "BC7: Importer works, TextureBinder upload not implemented\n"
        "RGBA16F/32F: For HDR content, upload not implemented");
    }

    ImGui::Checkbox("Generate mips", &texture_state_.generate_mips);

    ImGui::Checkbox("Tonemap HDR to LDR", &texture_state_.tonemap_hdr_to_ldr);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Converts HDR (float) content to LDR (8-bit).\n"
                        "Required when: HDR input + RGBA8/BC7 output.\n"
                        "Disable for: HDR input + RGBA16F/32F output.");
    }
    if (texture_state_.tonemap_hdr_to_ldr) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(100.0f);
      ImGui::DragFloat("Exposure (EV)", &texture_state_.hdr_exposure_ev, 0.1f,
        -10.0f, 10.0f, "%.1f");
    }

    if (!texture_state_.status_message.empty()) {
      ImGui::Text("Status: %s", texture_state_.status_message.c_str());
    }
    if (texture_state_.last_width > 0 && texture_state_.last_height > 0) {
      ImGui::Text("Last image: %dx%d", texture_state_.last_width,
        texture_state_.last_height);
    }
  }

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

  if (uv_transform_changed && cube_material && renderer) {
    const auto [effective_uv_scale, effective_uv_offset]
      = GetEffectiveUvTransform();
    (void)renderer->OverrideMaterialUvTransform(
      *cube_material, effective_uv_scale, effective_uv_offset);
  }

  if (rebuild_requested) {
    cube_needs_rebuild = true;
  }
}

auto DebugUI::DrawLightingTab(oxygen::observer_ptr<scene::Scene> scene,
  oxygen::observer_ptr<oxygen::engine::Renderer> renderer,
  oxygen::engine::ShaderPassConfig* shader_pass_config) -> void
{
  ImGui::Separator();
  ImGui::TextUnformatted("Skybox:");

  ImGui::InputText(
    "Skybox path", skybox_state_.path.data(), skybox_state_.path.size());
  if (ImGui::Button("Browse skybox...")) {
#if defined(OXYGEN_WINDOWS)
    std::string chosen;
    if (TryBrowseForImageFile(chosen)) {
      std::snprintf(skybox_state_.path.data(), skybox_state_.path.size(), "%s",
        chosen.c_str());
    }
#endif
  }
  ImGui::SameLine();
  if (ImGui::Button("Load skybox")) {
    skybox_state_.load_requested = true;
    skybox_state_.status_message.clear();
  }

  // Layout selection
  {
    constexpr const char* kLayoutNames[] = { "Equirectangular (2:1)",
      "Horizontal Cross (4x3)", "Vertical Cross (3x4)",
      "Horizontal Strip (6x1)", "Vertical Strip (1x6)" };
    ImGui::Combo("Layout", &skybox_state_.layout_idx, kLayoutNames,
      IM_ARRAYSIZE(kLayoutNames));
  }

  // Output format selection
  {
    constexpr const char* kFormatNames[]
      = { "RGBA8 (LDR)", "RGBA16F (HDR)", "RGBA32F (HDR)", "BC7 (LDR)" };
    ImGui::Combo("Output format", &skybox_state_.output_format_idx,
      kFormatNames, IM_ARRAYSIZE(kFormatNames));
  }

  // Face size (for equirectangular conversion)
  if (skybox_state_.layout_idx == 0) {
    constexpr int kFaceSizes[] = { 128, 256, 512, 1024, 2048 };
    constexpr const char* kFaceSizeNames[]
      = { "128", "256", "512", "1024", "2048" };

    int current_idx = 2;
    for (int i = 0; i < IM_ARRAYSIZE(kFaceSizes); ++i) {
      if (kFaceSizes[i] == skybox_state_.cube_face_size) {
        current_idx = i;
        break;
      }
    }

    if (ImGui::Combo("Cube face size", &current_idx, kFaceSizeNames,
          IM_ARRAYSIZE(kFaceSizeNames))) {
      skybox_state_.cube_face_size = kFaceSizes[current_idx];
      skybox_state_.load_requested = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
        "Resolution of each cube map face (must be power-of-two)");
    }
  }

  if (ImGui::Checkbox("Flip Y", &skybox_state_.flip_y)) {
    skybox_state_.load_requested = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Flip image vertically during decode. Enable for standard "
      "equirectangular HDRIs where Y=0 is at the top.");
  }

  if (!skybox_state_.status_message.empty()) {
    ImGui::Text("Skybox: %s", skybox_state_.status_message.c_str());
  }
  if (skybox_state_.last_face_size > 0) {
    ImGui::Text("Last skybox face: %dx%d", skybox_state_.last_face_size,
      skybox_state_.last_face_size);
  }

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
