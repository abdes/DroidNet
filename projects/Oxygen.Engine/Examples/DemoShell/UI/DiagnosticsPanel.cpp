//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/DiagnosticsPanel.h"
#include "DemoShell/UI/DiagnosticsVm.h"
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/Vortex/Environment/Types/StaticSkyLightProducts.h>
#include <Oxygen/Vortex/Types/SkyLightRuntimeState.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise) -
// ImGui

namespace oxygen::examples::ui {

namespace {

  constexpr std::string_view kFrameDiagnostics = "demo.diag.frame_diagnostics";
  constexpr std::string_view kDrawBounds = "demo.diag.draw_bounds";
  constexpr std::string_view kWireColor = "demo.diag.wire_color";

  constexpr auto kWarningColor = ImVec4(1.0F, 0.7F, 0.0F, 1.0F);

} // namespace

DiagnosticsPanel::DiagnosticsPanel(
  observer_ptr<DiagnosticsVm> vm, observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
{
  DCHECK_NOTNULL_F(vm_, "DiagnosticsPanel requires DiagnosticsVm");
  DCHECK_NOTNULL_F(commands_, "DiagnosticsPanel requires PanelCommands");
  commands_->AddBool(std::string(kFrameDiagnostics), "Frame Diagnostics",
    "Validate the renderer's GPU products each frame",
    [vm](
      const bool enabled) -> void { vm->SetFrameDiagnosticsEnabled(enabled); });
  commands_->AddBool(std::string(kDrawBounds), "Show Draw Bounds",
    "Draw every draw's bounds with the GPU debug pass",
    [vm](const bool enabled) -> void { vm->SetGpuDebugPassEnabled(enabled); });
  commands_->AddColor(std::string(kWireColor), "Wireframe Color",
    "Color of the wireframe view modes",
    [vm](
      const graphics::Color& color) -> void { vm->SetWireframeColor(color); });
}

auto DiagnosticsPanel::DrawContents() -> void
{
  DrawFrameDiagnostics();
  if (ImGui::CollapsingHeader("Sky Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSkyLight();
  }
  if (ImGui::CollapsingHeader(
        "Debug Drawing", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawDebugDrawing();
  }
}

auto DiagnosticsPanel::DrawFrameDiagnostics() -> void
{
  ImGui::BeginDisabled(!vm_->SupportsDiagnostics());
  bool enabled = vm_->GetFrameDiagnosticsEnabled();
  if (ImGui::Checkbox("Frame Diagnostics", &enabled)) {
    commands_->RunBool(kFrameDiagnostics, enabled);
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("Records the renderer's diagnostics each frame and reads "
                      "rendered products, such as the sky light, back from "
                      "the GPU to check them.%s",
      vm_->SupportsDiagnostics()
        ? ""
        : "\nThe renderer has no diagnostics capability.");
  }
}

auto DiagnosticsPanel::DrawSkyLight() -> void
{
  const auto state = vm_->GetSkyLightRuntimeState();
  if (!state.observed) {
    ImGui::TextDisabled("No rendered sky-light state");
    return;
  }
  ImGui::Text("Published / building: %u / %u", state.published_revision,
    state.building_revision);
  ImGui::Text("Snapshot age: %llu frames",
    static_cast<unsigned long long>(state.source_age_frames));
  ImGui::Text("Cube: %u x %u", state.face_size, state.face_size);
  if (state.gpu_validation != vortex::SkyLightGpuValidation::kInvalid
    && state.unavailable_reason
      == vortex::environment::StaticSkyLightUnavailableReason::
        kProcessingFailed) {
    ImGui::TextColored(kWarningColor,
      state.usable ? "Update failed; previous lighting retained"
                   : "Lighting update failed");
  }
  // The renderer keeps the last validation of the current products; it is
  // shown only while the check runs.
  if (!vm_->GetFrameDiagnosticsEnabled()) {
    ImGui::TextDisabled("Frame Diagnostics checks these products on the GPU");
    return;
  }
  using Validation = vortex::SkyLightGpuValidation;
  switch (state.gpu_validation) {
  case Validation::kNotRequested:
    ImGui::TextDisabled(state.usable ? "GPU validation pending"
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
    ImGui::TextColored(kWarningColor,
      "Invalid GPU generation %u; lighting is zero", state.validated_revision);
    break;
  case Validation::kUnavailable:
    ImGui::TextDisabled("GPU validation readback unavailable");
    break;
  }
  if (state.last_failed_gpu_revision != 0U
    && state.last_failed_gpu_revision != state.validated_revision) {
    ImGui::Text(
      "Last GPU failure: generation %u", state.last_failed_gpu_revision);
  }
}

auto DiagnosticsPanel::DrawDebugDrawing() -> void
{
  ImGui::BeginDisabled(!vm_->IsVortexRuntimeBound());
  bool draw_bounds = vm_->GetGpuDebugPassEnabled();
  if (ImGui::Checkbox("Show Draw Bounds", &draw_bounds)) {
    commands_->RunBool(kDrawBounds, draw_bounds);
  }
  if (draw_bounds) {
    // The occlusion cull pass draws the boxes in these fixed colors.
    constexpr auto kPhase1 = ImVec4(0.0F, 1.0F, 0.0F, 1.0F);
    constexpr auto kPhase2 = ImVec4(1.0F, 1.0F, 0.0F, 1.0F);
    ImGui::Indent();
    ImGui::TextColored(kPhase1, "Green");
    ImGui::SameLine();
    ImGui::TextUnformatted("drawn in the first pass");
    ImGui::TextColored(kPhase2, "Yellow");
    ImGui::SameLine();
    ImGui::TextUnformatted("held for the occlusion test");
    ImGui::Unindent();
  }

  // The color of the wireframe view modes, not of the draw bounds.
  ImGui::SeparatorText("Wireframe View Modes");
  const auto color = vm_->GetWireframeColor();
  std::array wire_color { color.r, color.g, color.b };
  if (ImGui::ColorEdit3("Line Color", wire_color.data(),
        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
    commands_->RunColor(kWireColor,
      graphics::Color {
        wire_color.at(0), wire_color.at(1), wire_color.at(2), 1.0F });
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
      "Lines of the toolbar's Wireframe and Lit Wireframe view modes.");
  }
  ImGui::EndDisabled();
}

auto DiagnosticsPanel::GetName() const noexcept -> std::string_view
{
  return "Diagnostics";
}

auto DiagnosticsPanel::GetPreferredWidth() const noexcept -> float
{
  return 340.0F;
}

auto DiagnosticsPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconRendering;
}

auto DiagnosticsPanel::OnRegistered() -> void { }

auto DiagnosticsPanel::OnLoaded() -> void { }

auto DiagnosticsPanel::OnUnloaded() -> void { }

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise)
