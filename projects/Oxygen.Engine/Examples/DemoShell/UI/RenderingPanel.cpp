//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/UI/RenderingPanel.h"
#include "DemoShell/UI/RenderingVm.h"

namespace oxygen::examples::ui {

RenderingPanel::RenderingPanel(observer_ptr<RenderingVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "RenderingPanel requires RenderingVm");
}

auto RenderingPanel::DrawContents() -> void
{
  if (ImGui::CollapsingHeader(
        "Directional Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawShadowSettings();
  }

  if (vm_->SupportsRenderModeControls()
    && ImGui::CollapsingHeader("Render Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawViewModeControls();
    if (vm_->SupportsWireframeColorControl()) {
      DrawWireframeColor();
    }
  }

  if (ImGui::CollapsingHeader("Debug Modes", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (vm_->IsVortexRuntimeBound()) {
      ImGui::TextDisabled(
        "Vortex currently exposes only the live debug visualizations.");
    }
    DrawDebugModes();
  }
}

auto RenderingPanel::GetName() const noexcept -> std::string_view
{
  return "Rendering";
}

auto RenderingPanel::GetPreferredWidth() const noexcept -> float
{
  return 320.0F;
}

auto RenderingPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconRendering;
}

auto RenderingPanel::OnRegistered() -> void { }

auto RenderingPanel::OnLoaded() -> void { }

auto RenderingPanel::OnUnloaded() -> void
{
  // Persistence is handled by RenderingSettingsService via the ViewModel.
}

auto RenderingPanel::GetRenderMode() const -> renderer::RenderMode
{
  return vm_->GetRenderMode();
}

void RenderingPanel::DrawShadowSettings()
{
  static constexpr const char* kShadowQualityLabels[] = {
    "Low",
    "Medium",
    "High",
    "Ultra",
  };

  auto quality = static_cast<int>(vm_->GetShadowQualityTier());
  ImGui::SetNextItemWidth(180.0F);
  if (ImGui::Combo("Quality", &quality, kShadowQualityLabels,
        IM_ARRAYSIZE(kShadowQualityLabels))) {
    vm_->SetShadowQualityTier(static_cast<ShadowQualityTier>(quality));
  }

  ImGui::TextDisabled(
    "Renderer shadow quality is applied at startup; restart to apply changes.");
}

void RenderingPanel::DrawViewModeControls()
{
  using r = renderer::RenderMode;

  auto mode = vm_->GetRenderMode();

  if (ImGui::RadioButton("Solid", mode == r::kSolid)) {
    vm_->SetRenderMode(r::kSolid);
  }
  if (ImGui::RadioButton("Wireframe", mode == r::kWireframe)) {
    vm_->SetRenderMode(r::kWireframe);
  }
  if (ImGui::RadioButton("Overlay Wireframe", mode == r::kOverlayWireframe)) {
    vm_->SetRenderMode(r::kOverlayWireframe);
  }
}

void RenderingPanel::DrawWireframeColor()
{
  const auto color = vm_->GetWireframeColor();
  float wire_color[3] = { color.r, color.g, color.b };
  if (ImGui::ColorEdit3("Wire Color", wire_color,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    LOG_F(INFO, "RenderingPanel: UI wire_color changed ({}, {}, {})",
      wire_color[0], wire_color[1], wire_color[2]);
    vm_->SetWireframeColor(
      graphics::Color { wire_color[0], wire_color[1], wire_color[2], 1.0F });
  }
}

void RenderingPanel::DrawDebugModes()
{
  using engine::ShaderDebugMode;
  using RenderMode = renderer::RenderMode;

  const bool disable_debug_modes = vm_->SupportsRenderModeControls()
    && GetRenderMode() == RenderMode::kWireframe;
  if (disable_debug_modes) {
    ImGui::BeginDisabled();
  }

  const auto current_mode = vm_->GetDebugMode();

  const bool is_ui_debug_mode = current_mode != ShaderDebugMode::kDisabled
    && !engine::IsLightCullingDebugMode(current_mode);

  const bool normal_selected
    = current_mode == ShaderDebugMode::kDisabled || !is_ui_debug_mode;

  if (ImGui::RadioButton("Normal", normal_selected)) {
    vm_->SetDebugMode(ShaderDebugMode::kDisabled);
  }

  const auto draw_debug_mode
    = [&](const char* label, const ShaderDebugMode mode) -> void {
    if (!vm_->SupportsDebugMode(mode)) {
      return;
    }
    if (ImGui::RadioButton(label, current_mode == mode)) {
      vm_->SetDebugMode(mode);
    }
  };

  draw_debug_mode("Base Color", ShaderDebugMode::kBaseColor);
  draw_debug_mode("UV0", ShaderDebugMode::kUv0);
  draw_debug_mode("Opacity", ShaderDebugMode::kOpacity);
  draw_debug_mode("World Normals", ShaderDebugMode::kWorldNormals);
  draw_debug_mode("Roughness", ShaderDebugMode::kRoughness);
  draw_debug_mode("Metalness", ShaderDebugMode::kMetalness);
  draw_debug_mode("IBL Specular Dir", ShaderDebugMode::kIblSpecular);
  draw_debug_mode("IBL Irradiance Dir", ShaderDebugMode::kIblIrradiance);
  draw_debug_mode("IBL Raw Sky", ShaderDebugMode::kIblRawSky);
  draw_debug_mode("IBL Face Index", ShaderDebugMode::kIblFaceIndex);
  draw_debug_mode("IBL No BRDF LUT", ShaderDebugMode::kIblNoBrdfLut);
  draw_debug_mode("Direct Lighting Only", ShaderDebugMode::kDirectLightingOnly);
  draw_debug_mode("IBL Only", ShaderDebugMode::kIblOnly);
  draw_debug_mode("Direct + IBL", ShaderDebugMode::kDirectPlusIbl);
  draw_debug_mode("Direct Lighting Full", ShaderDebugMode::kDirectLightingFull);
  draw_debug_mode("Direct Light Gates", ShaderDebugMode::kDirectLightGates);
  draw_debug_mode("Direct BRDF Core", ShaderDebugMode::kDirectBrdfCore);
  draw_debug_mode(
    "Directional Shadow Mask", ShaderDebugMode::kDirectionalShadowMask);
  draw_debug_mode("Scene Depth Raw", ShaderDebugMode::kSceneDepthRaw);
  draw_debug_mode("Scene Depth Linear", ShaderDebugMode::kSceneDepthLinear);
  draw_debug_mode("Scene Depth Mismatch", ShaderDebugMode::kSceneDepthMismatch);
  draw_debug_mode(
    "Masked Alpha Coverage", ShaderDebugMode::kMaskedAlphaCoverage);

  if (disable_debug_modes) {
    ImGui::EndDisabled();
  }

  if (vm_->SupportsGpuDebugPassControl()) {
    auto gpu_debug_enabled = vm_->GetGpuDebugPassEnabled();
    if (ImGui::Checkbox("Show GPU Debug Pass", &gpu_debug_enabled)) {
      vm_->SetGpuDebugPassEnabled(gpu_debug_enabled);
    }
  }

  if (vm_->SupportsAtmosphereBlueNoiseControl()) {
    auto atmo_blue_noise = vm_->GetAtmosphereBlueNoiseEnabled();
    if (ImGui::Checkbox("Atmosphere Blue Noise", &atmo_blue_noise)) {
      vm_->SetAtmosphereBlueNoiseEnabled(atmo_blue_noise);
    }
  }
}

} // namespace oxygen::examples::ui
