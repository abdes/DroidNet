//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string>

#include <imgui.h>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

#include "TexturedCube/UI/TextureBrowserPanel.h"
#include "TexturedCube/UI/TextureBrowserVm.h"

namespace {

void DrawHelpMarker(const char* desc)
{
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(desc);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

template <typename EnumT, std::size_t N>
auto DrawEnumCombo(
  const char* label, EnumT& value, const std::array<EnumT, N>& items) -> bool
{
  std::string preview_s(nostd::to_string(value));
  bool changed = false;
  if (ImGui::BeginCombo(label, preview_s.c_str())) {
    for (const auto candidate : items) {
      const bool is_selected = (candidate == value);
      std::string item_s(nostd::to_string(candidate));
      if (ImGui::Selectable(item_s.c_str(), is_selected)) {
        value = candidate;
        changed = true;
      }
      if (is_selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

auto InputTextString(const char* label, std::array<char, 512>& value) -> bool
{
  return ImGui::InputText(label, value.data(), value.size());
}

} // namespace

namespace oxygen::examples::textured_cube::ui {

auto to_string(TextureBrowserVm::UvOrigin origin) -> std::string
{
  switch (origin) {
  case TextureBrowserVm::UvOrigin::kBottomLeft:
    return "Bottom Left";
  case TextureBrowserVm::UvOrigin::kTopLeft:
    return "Top Left";
  default:
    return "Unknown";
  }
}

void TextureBrowserPanel::Initialize(observer_ptr<TextureBrowserVm> vm)
{
  vm_ = vm;
}

auto TextureBrowserPanel::GetName() const noexcept -> std::string_view
{
  return "Texture Browser";
}

auto TextureBrowserPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconDemoPanel;
}

auto TextureBrowserPanel::GetPreferredWidth() const noexcept -> float
{
  return 480.0F;
}

auto TextureBrowserPanel::OnLoaded() -> void
{
  if (vm_) {
    // Refresh cooked list just in case
    vm_->RequestRefresh();
  }
}

auto TextureBrowserPanel::OnUnloaded() -> void { }

auto TextureBrowserPanel::DrawContents() -> void
{
  if (!vm_) {
    ImGui::TextDisabled("No VM attached");
    return;
  }

  // Update VM loop
  vm_->Update();

  ImGui::PushID("TextureBrowser");

  // Determine interaction state
  const auto state = vm_->GetImportState().workflow_state;
  // const bool is_configuring = ... (Moved to DrawBrowserSection logic)
  const bool is_idle
    = (state == TextureBrowserVm::ImportState::WorkflowState::Idle);

  // Inline Import Section
  // Moved to DrawBrowserSection

  if (ImGui::CollapsingHeader(
        "Materials & UVs", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginDisabled(!is_idle);
    DrawMaterialsSection();
    ImGui::EndDisabled();
  }

  if (ImGui::CollapsingHeader(
        "Texture Browser", ImGuiTreeNodeFlags_DefaultOpen)) {
    // Note: Disabling handled inside DrawBrowserSection now to allow Import
    // interaction
    DrawBrowserSection();
  }

  ImGui::PopID();
}

auto TextureBrowserPanel::DrawImportSection() -> void
{
  auto& state = vm_->GetImportState();

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
  ImGui::BeginChild("ImportPanel", ImVec2(0, 0),
    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY
      | ImGuiChildFlags_AlwaysAutoResize);

  ImGui::Text("Importing: %s", state.source_path.data());
  ImGui::Spacing();

  // 1. Configuration (Disable if importing)
  const bool is_importing = (state.workflow_state
    == TextureBrowserVm::ImportState::WorkflowState::Importing);

  ImGui::BeginDisabled(is_importing);
  {
    // Usage Presets
    const char* kUsages[] = { "Auto-Detect", "Albedo / Diffuse", "Normal Map",
      "HDR Environment", "UI Element" };
    int usage_idx = static_cast<int>(state.usage);
    if (ImGui::Combo(
          "Usage Preset", &usage_idx, kUsages, IM_ARRAYSIZE(kUsages))) {
      state.usage
        = static_cast<TextureBrowserVm::ImportState::Usage>(usage_idx);
      vm_->UpdateImportSettingsFromUsage();
    }

    // Contextual Toggles
    if (state.usage == TextureBrowserVm::ImportState::Usage::kNormal) {
      ImGui::Indent();
      ImGui::Checkbox("Flip Green Channel (Y)", &state.flip_normal_green);
      ImGui::Unindent();
    }
    if (state.usage == TextureBrowserVm::ImportState::Usage::kHdrEnvironment) {
      ImGui::Indent();
      ImGui::SliderFloat("Exposure (EV)", &state.exposure_ev, -5.0f, 5.0f);
      ImGui::Unindent();
    }

    // Tuning
    if (ImGui::Checkbox("Compress", &state.compress)) {
      vm_->UpdateImportSettingsFromUsage();
    }
    if (state.compress) {
      ImGui::SameLine();
      const char* kQuality[] = { "Fast", "Balanced", "High" };
      int q_ui = state.bc7_quality_idx - 1;
      if (q_ui < 0)
        q_ui = 1;

      ImGui::SetNextItemWidth(100);
      if (ImGui::Combo("##Quality", &q_ui, kQuality, IM_ARRAYSIZE(kQuality))) {
        state.bc7_quality_idx = q_ui + 1;
      }
    }

    ImGui::SameLine();
    ImGui::Checkbox("Deduplicate (Hash)", &state.compute_hash);

    // Fold for advanced
    if (ImGui::TreeNode("Advanced Settings")) {
      // ... (Keep existing advanced logic if needed, simplified for inline)
      ImGui::Checkbox("Generate Mips", &state.generate_mips);
      const char* kFormats[]
        = { "RGBA8 SRGB", "BC7 SRGB", "RGBA16F", "RGBA32F" };
      ImGui::Combo(
        "Format", &state.output_format_idx, kFormats, IM_ARRAYSIZE(kFormats));
      ImGui::TreePop();
    }
  }
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // 2. Status / Progress (Bottom area)
  if (state.workflow_state
    == TextureBrowserVm::ImportState::WorkflowState::Importing) {
    ImGui::Text("Importing...");
    ImGui::ProgressBar(state.progress, ImVec2(-1, 0));
  } else if (state.workflow_state
    == TextureBrowserVm::ImportState::WorkflowState::Finished) {
    if (state.last_import_success) {
      ImGui::TextColored(
        ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "%s", state.status_message.c_str());
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "Error: %s",
        state.status_message.c_str());
    }
  }

  // 3. Actions (Bottom Right)
  // Spacer to push buttons right? Or just fill width.

  // if Finished (Error), user can Retry (Import) or Cancel.
  // if Finished (Success), actually we should have closed?
  //   If we auto-close on success, we won't see this state easily unless we
  //   delay. Let's auto-close on success in VM Update().

  if (ImGui::Button("Cancel", ImVec2(100, 0))) {
    vm_->CancelImport();
  }

  ImGui::SameLine();

  ImGui::BeginDisabled(is_importing);
  if (ImGui::Button(state.workflow_state
            == TextureBrowserVm::ImportState::WorkflowState::Finished
          ? "Retry Import"
          : "Import",
        ImVec2(-1, 0))) {
    vm_->RequestImport();
  }
  ImGui::EndDisabled();

  ImGui::EndChild();
  ImGui::PopStyleColor();
}

auto TextureBrowserPanel::DrawMaterialsSection() -> void
{
  auto& surface = vm_->GetSurfaceState();
  auto& uv = vm_->GetUvState();

  ImGui::TextDisabled("Surface Props");
  ImGui::SliderFloat("Metalness", &surface.metalness, 0.0f, 1.0f);
  ImGui::SliderFloat("Roughness", &surface.roughness, 0.0f, 1.0f);
  ImGui::Checkbox("Use Constant Base Color", &surface.use_constant_base_color);
  if (surface.use_constant_base_color) {
    ImGui::ColorEdit3("Base Color", &surface.constant_base_color_rgb.x);
  }

  ImGui::Separator();
  ImGui::TextDisabled("UV Transform");

  if (ImGui::Button("Reset UV")) {
    uv.scale = { 1.0f, 1.0f };
    uv.offset = { 0.0f, 0.0f };
  }
  ImGui::DragFloat2("Scale", &uv.scale.x, 0.01f);
  ImGui::DragFloat2("Offset", &uv.offset.x, 0.01f);

  // UV/Image Origin options
  static const std::array<TextureBrowserVm::UvOrigin, 2> kUvOrigins
    = { TextureBrowserVm::UvOrigin::kBottomLeft,
        TextureBrowserVm::UvOrigin::kTopLeft };
  DrawEnumCombo("UV Origin", uv.uv_origin, kUvOrigins);

  ImGui::Checkbox("Extra Flip U", &uv.extra_flip_u);
  ImGui::SameLine();
  ImGui::Checkbox("Extra Flip V", &uv.extra_flip_v);

  // Calculate and show effective logic for debugging
  auto [eff_s, eff_o] = vm_->GetEffectiveUvTransform();
  ImGui::TextDisabled("Effective: S(%.2f, %.2f) O(%.2f, %.2f)", eff_s.x,
    eff_s.y, eff_o.x, eff_o.y);

  if (ImGui::Button("Apply/Rebuild Cube")) {
    vm_->SetCubeRebuildNeeded();
  }
}

auto TextureBrowserPanel::DrawBrowserSection() -> void
{
  auto& cooked = vm_->GetCookedEntries();

  // Determine state for local UI logic
  const auto state = vm_->GetImportState().workflow_state;
  const bool is_idle
    = (state == TextureBrowserVm::ImportState::WorkflowState::Idle);
  const bool is_configuring
    = (state != TextureBrowserVm::ImportState::WorkflowState::Idle);

  if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT " Refresh List")) {
    vm_->RequestRefresh();
  }

  ImGui::SameLine();

  ImGui::BeginDisabled(!is_idle);
  if (ImGui::Button(ICON_FA_FILE_IMPORT " Import Texture...")) {
    vm_->StartImportFlow();
  }
  ImGui::EndDisabled();

  // Inline Import Section (pushes content down)
  if (is_configuring) {
    ImGui::Separator();
    DrawImportSection();
    ImGui::Separator();
  }

  ImGui::BeginDisabled(!is_idle);

  ImGui::BeginChild("CookedList", ImVec2(0, 300), true);
  for (const auto& entry : cooked) {
    ImGui::PushID((int)entry.index);

    std::string label = entry.name.empty()
      ? fmt::format("[{}] <unnamed>", entry.index)
      : fmt::format("[{}] {}", entry.index, entry.name);

    std::string metadata = fmt::format("{}x{} {} mips={} {}", entry.width,
      entry.height, nostd::to_string(entry.format), entry.mip_levels,
      nostd::to_string(entry.texture_type));

    // Selection Actions
    const bool is_2d = (entry.texture_type == oxygen::TextureType::kTexture2D);
    const bool is_cube
      = (entry.texture_type == oxygen::TextureType::kTextureCube);

    ImGui::BeginDisabled(!is_2d);
    if (ImGui::Button("Sphere")) {
      vm_->SelectTextureForSlot(entry.index, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Cube")) {
      vm_->SelectTextureForSlot(entry.index, false);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!is_cube);
    if (ImGui::Button("Sky")) {
      vm_->SelectSkybox(entry.index, nullptr);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(metadata.c_str());

      std::string settings_json = vm_->GetMetadataJson(entry.index);
      if (!settings_json.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Import Settings:");
        ImGui::TextUnformatted(settings_json.c_str());
      }
      ImGui::EndTooltip();
    }

    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::EndDisabled();
}

} // namespace oxygen::examples::textured_cube::ui
