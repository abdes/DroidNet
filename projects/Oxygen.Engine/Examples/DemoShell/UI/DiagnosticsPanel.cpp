//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string>
#include <string_view>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>

#include "DemoShell/UI/DiagnosticsPanel.h"
#include "DemoShell/UI/DiagnosticsVm.h"

namespace oxygen::examples::ui {

namespace {

  [[nodiscard]] auto DisplayNameFor(const engine::ShaderDebugMode mode)
    -> std::string_view
  {
    const auto* info = vortex::FindShaderDebugModeInfo(mode);
    if (info == nullptr) {
      return "Unknown";
    }
    return info->display_name;
  }

  [[nodiscard]] auto MissingReason(
    const vortex::ShaderDebugModeInfo& info,
    const vortex::CapabilitySet capabilities) -> std::string
  {
    if (!info.supported) {
      return std::string(info.unsupported_reason);
    }

    const auto missing
      = vortex::MissingCapabilities(capabilities, info.required_capabilities);
    if (missing != vortex::RendererCapabilityFamily::kNone) {
      return "Missing renderer capabilities: " + vortex::to_string(missing);
    }

    return {};
  }

  [[nodiscard]] auto FamilyLabel(
    const vortex::ShaderDebugModeFamily family) noexcept -> const char*
  {
    using Family = vortex::ShaderDebugModeFamily;
    switch (family) {
    case Family::kLightCulling:
      return "Light Culling";
    case Family::kIbl:
      return "Image-Based Lighting";
    case Family::kMaterial:
      return "Material";
    case Family::kDirectLighting:
      return "Direct Lighting";
    case Family::kShadow:
      return "Shadows";
    case Family::kSceneDepth:
      return "Scene Depth";
    case Family::kMasked:
      return "Masked Geometry";
    case Family::kNone:
    default:
      return "Other";
    }
  }

  constexpr auto kDebugModeFamilies = std::to_array({
    vortex::ShaderDebugModeFamily::kMaterial,
    vortex::ShaderDebugModeFamily::kDirectLighting,
    vortex::ShaderDebugModeFamily::kShadow,
    vortex::ShaderDebugModeFamily::kSceneDepth,
    vortex::ShaderDebugModeFamily::kIbl,
    vortex::ShaderDebugModeFamily::kLightCulling,
    vortex::ShaderDebugModeFamily::kMasked,
  });

} // namespace

DiagnosticsPanel::DiagnosticsPanel(observer_ptr<DiagnosticsVm> vm)
  : vm_(vm)
{
  DCHECK_NOTNULL_F(vm, "DiagnosticsPanel requires DiagnosticsVm");
}

auto DiagnosticsPanel::DrawContents() -> void
{
  if (ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawRuntimeStatus();
  }

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

  if (ImGui::CollapsingHeader("Shader Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawDebugModes();
  }
}

auto DiagnosticsPanel::GetName() const noexcept -> std::string_view
{
  return "Diagnostics";
}

auto DiagnosticsPanel::GetPreferredWidth() const noexcept -> float
{
  return 380.0F;
}

auto DiagnosticsPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconRendering;
}

auto DiagnosticsPanel::OnRegistered() -> void { }

auto DiagnosticsPanel::OnLoaded() -> void { }

auto DiagnosticsPanel::OnUnloaded() -> void
{
  // Persistence is handled by RenderingSettingsService via the ViewModel.
}

auto DiagnosticsPanel::GetRenderMode() const -> renderer::RenderMode
{
  return vm_->GetRenderMode();
}

void DiagnosticsPanel::DrawRuntimeStatus()
{
  const auto capabilities = vm_->GetRendererCapabilities();
  const auto requested = vm_->GetRequestedDebugMode();
  const auto effective = vm_->GetEffectiveDebugMode();

  ImGui::Text("Renderer: %s", vm_->IsVortexRuntimeBound() ? "Vortex" : "None");
  ImGui::Text("Capabilities: %s", vortex::to_string(capabilities).c_str());
  ImGui::Text("Requested: %s", DisplayNameFor(requested).data());
  ImGui::Text("Effective: %s", DisplayNameFor(effective).data());
}

void DiagnosticsPanel::DrawShadowSettings()
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

void DiagnosticsPanel::DrawViewModeControls()
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

void DiagnosticsPanel::DrawWireframeColor()
{
  const auto color = vm_->GetWireframeColor();
  float wire_color[3] = { color.r, color.g, color.b };
  if (ImGui::ColorEdit3("Wire Color", wire_color,
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    LOG_F(INFO, "DiagnosticsPanel: UI wire_color changed ({}, {}, {})",
      wire_color[0], wire_color[1], wire_color[2]);
    vm_->SetWireframeColor(
      graphics::Color { wire_color[0], wire_color[1], wire_color[2], 1.0F });
  }
}

void DiagnosticsPanel::DrawDebugModes()
{
  using engine::ShaderDebugMode;
  using RenderMode = renderer::RenderMode;

  const bool disable_debug_modes = vm_->SupportsRenderModeControls()
    && GetRenderMode() == RenderMode::kWireframe;
  if (disable_debug_modes) {
    ImGui::BeginDisabled();
  }

  const auto requested_mode = vm_->GetRequestedDebugMode();
  const auto capabilities = vm_->GetRendererCapabilities();

  if (ImGui::RadioButton(
        "Disabled", requested_mode == ShaderDebugMode::kDisabled)) {
    vm_->SetDebugMode(ShaderDebugMode::kDisabled);
  }

  for (const auto family : kDebugModeFamilies) {
    if (!ImGui::TreeNodeEx(FamilyLabel(family), ImGuiTreeNodeFlags_DefaultOpen)) {
      continue;
    }

    for (const auto& info : vortex::EnumerateShaderDebugModes()) {
      if (info.mode == ShaderDebugMode::kDisabled || info.family != family) {
        continue;
      }

      const bool supported = vm_->SupportsDebugMode(info.mode);
      const auto reason = MissingReason(info, capabilities);

      ImGui::PushID(info.canonical_name.data());
      if (!supported) {
        ImGui::BeginDisabled();
      }
      if (ImGui::RadioButton(
            info.display_name.data(), requested_mode == info.mode)) {
        vm_->SetDebugMode(info.mode);
      }
      if (!supported) {
        ImGui::EndDisabled();
        if (!reason.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
          ImGui::SetTooltip("%s", reason.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Unavailable");
      }
      ImGui::PopID();
    }

    ImGui::TreePop();
  }

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
