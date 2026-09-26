//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string>
#include <string_view>

#include "DemoShell/UI/DiagnosticsPanel.h"
#include "DemoShell/UI/DiagnosticsVm.h"
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>

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

  [[nodiscard]] auto MissingReason(const vortex::ShaderDebugModeInfo& info,
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

  struct CapabilityUiRow {
    vortex::RendererCapabilityFamily family;
    const char* label;
    const char* tooltip;
  };

  constexpr auto kCapabilityUiRows = std::to_array<CapabilityUiRow>({
    { vortex::RendererCapabilityFamily::kScenePreparation, "Scene preparation",
      "Scene extraction, view setup, and per-frame scene inputs." },
    { vortex::RendererCapabilityFamily::kGpuUploadAndAssetBinding,
      "GPU upload/binding",
      "GPU upload and asset descriptor binding support." },
    { vortex::RendererCapabilityFamily::kDeferredShading, "Deferred shading",
      "G-buffer, depth publication, and deferred debug views." },
    { vortex::RendererCapabilityFamily::kLightingData, "Lighting data",
      "Analytic light data, clustered lighting, and lighting buffers." },
    { vortex::RendererCapabilityFamily::kShadowing, "Shadowing",
      "Directional shadow map rendering and shadow-mask products." },
    { vortex::RendererCapabilityFamily::kEnvironmentLighting,
      "Environment lighting",
      "Sky, atmosphere, image-based lighting, and fog passes." },
    { vortex::RendererCapabilityFamily::kFinalOutputComposition,
      "Final composition", "Final scene composition and presentation output." },
    { vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling,
      "Diagnostics/profiling",
      "Built-in diagnostics service and GPU timeline profiling support." },
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
  if (ImGui::CollapsingHeader("Sky Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSkyLightDiagnostics();
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

void DiagnosticsPanel::DrawSkyLightDiagnostics()
{
  const auto state = vm_->GetSkyLightRuntimeState();
  if (!state.observed) {
    ImGui::TextDisabled("No rendered sky-light state");
    return;
  }
  ImGui::Text(
    "Scene frame: %llu", static_cast<unsigned long long>(state.frame_sequence));
  ImGui::Text("Published / building: %u / %u", state.published_revision,
    state.building_revision);
  ImGui::Text("Snapshot age: %llu frames",
    static_cast<unsigned long long>(state.source_age_frames));
  ImGui::Text("Cube: %u x %u", state.face_size, state.face_size);
  ImGui::Text("CPU decision: %.3f ms", state.cpu_update_ms);
  if (state.gpu_validation != vortex::SkyLightGpuValidation::kInvalid
    && state.unavailable_reason
      == vortex::environment::StaticSkyLightUnavailableReason::
        kProcessingFailed)
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
      state.usable ? "Update failed; previous lighting retained"
                   : "Lighting update failed");
  bool timing = vm_->GetGpuTimelineEnabled();
  const bool supported
    = vortex::HasAllCapabilities(vm_->GetRendererCapabilities(),
      vortex::RendererCapabilityFamily::kDiagnosticsAndProfiling);
  ImGui::BeginDisabled(!supported);
  bool validation = vm_->GetFrameDiagnosticsEnabled();
  if (ImGui::Checkbox("Frame Diagnostics", &validation))
    vm_->SetFrameDiagnosticsEnabled(validation);
  if (ImGui::Checkbox("GPU Timeline", &timing))
    vm_->SetGpuTimelineEnabled(timing);
  ImGui::EndDisabled();
  using Validation = vortex::SkyLightGpuValidation;
  switch (state.gpu_validation) {
  case Validation::kNotRequested:
    ImGui::TextDisabled(state.usable
        ? "Enable Frame Diagnostics to check GPU products"
        : "No submitted sky-light products");
    break;
  case Validation::kPending:
    ImGui::TextDisabled("GPU validation pending");
    break;
  case Validation::kValid:
    ImGui::Text("GPU validated: generation %u", state.validated_revision);
    ImGui::Text("Radiance scale: %.6g", state.source_radiance_scale);
    ImGui::Text("Average brightness: %.6g", state.average_brightness);
    break;
  case Validation::kInvalid:
    ImGui::TextColored(ImVec4(1.0F, 0.7F, 0.0F, 1.0F),
      "Invalid GPU generation %u; lighting is zero", state.validated_revision);
    break;
  case Validation::kUnavailable:
    ImGui::TextDisabled("GPU validation readback unavailable");
    break;
  }
  if (state.last_failed_gpu_revision != 0U
    && state.last_failed_gpu_revision != state.validated_revision)
    ImGui::Text(
      "Last GPU failure: generation %u", state.last_failed_gpu_revision);
  if (const auto gpu = vm_->GetLatestIblGpuTiming())
    ImGui::Text("IBL GPU (all scenes): %.3f ms [frame %llu]", gpu->producer_ms,
      static_cast<unsigned long long>(gpu->frame_sequence));
  else if (timing)
    ImGui::TextDisabled("Waiting for complete GPU timing");
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

auto DiagnosticsPanel::GetRenderMode() const -> vortex::RenderMode
{
  return vm_->GetRenderMode();
}

void DiagnosticsPanel::DrawRuntimeStatus()
{
  const auto requested = vm_->GetRequestedDebugMode();
  const auto effective = vm_->GetEffectiveDebugMode();

  ImGui::Text("Renderer: %s", vm_->IsVortexRuntimeBound() ? "Vortex" : "None");
  ImGui::Text("Selected debug view: %s", DisplayNameFor(requested).data());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("The debug view selected in this panel.");
  }
  ImGui::Text("Active debug view: %s", DisplayNameFor(effective).data());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("The debug view currently applied by the renderer after "
                      "capability checks.");
  }
  DrawRendererCapabilities();
}

void DiagnosticsPanel::DrawRendererCapabilities()
{
  const auto capabilities = vm_->GetRendererCapabilities();

  ImGui::Spacing();
  ImGui::TextUnformatted("Capabilities");
  ImGui::Indent();
  for (const auto& row : kCapabilityUiRows) {
    bool enabled = vortex::HasAllCapabilities(capabilities, row.family);
    ImGui::PushID(row.label);
    ImGui::BeginDisabled();
    ImGui::Checkbox(row.label, &enabled);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("%s", row.tooltip);
    }
    ImGui::PopID();
  }
  ImGui::Unindent();
}

void DiagnosticsPanel::DrawViewModeControls()
{
  using r = vortex::RenderMode;

  auto mode = vm_->GetRenderMode();

  if (ImGui::RadioButton("Solid", mode == r::kSolid)) {
    vm_->SetDebugMode(engine::ShaderDebugMode::kDisabled);
    vm_->SetRenderMode(r::kSolid);
  }
  if (ImGui::RadioButton("Wireframe", mode == r::kWireframe)) {
    vm_->SetDebugMode(engine::ShaderDebugMode::kDisabled);
    vm_->SetRenderMode(r::kWireframe);
  }
  if (ImGui::RadioButton("Overlay Wireframe", mode == r::kOverlayWireframe)) {
    vm_->SetDebugMode(engine::ShaderDebugMode::kDisabled);
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
  using RenderMode = vortex::RenderMode;

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
    if (!ImGui::TreeNodeEx(
          FamilyLabel(family), ImGuiTreeNodeFlags_DefaultOpen)) {
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
        if (!reason.empty()
          && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
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
