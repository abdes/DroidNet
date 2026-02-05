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

#include "TexturedCube/UI/MaterialsSandboxPanel.h"
#include "TexturedCube/UI/MaterialsSandboxVm.h"

namespace {

void DrawHelpMarker(const char* desc)
{
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0F);
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

auto to_string(MaterialsSandboxVm::UvOrigin origin) -> std::string
{
  switch (origin) {
  case MaterialsSandboxVm::UvOrigin::kBottomLeft:
    return "Bottom Left";
  case MaterialsSandboxVm::UvOrigin::kTopLeft:
    return "Top Left";
  default:
    return "Unknown";
  }
}

void MaterialsSandboxPanel::Initialize(observer_ptr<MaterialsSandboxVm> vm)
{
  vm_ = vm;
}

auto MaterialsSandboxPanel::GetName() const noexcept -> std::string_view
{
  return "Texture Browser";
}

auto MaterialsSandboxPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconDemoPanel;
}

auto MaterialsSandboxPanel::GetPreferredWidth() const noexcept -> float
{
  return 480.0F;
}

auto MaterialsSandboxPanel::OnLoaded() -> void
{
  if (vm_) {
    // Refresh cooked list just in case
    vm_->RequestRefresh();
  }
}

auto MaterialsSandboxPanel::OnUnloaded() -> void { }

auto MaterialsSandboxPanel::DrawContents() -> void
{
  if (!vm_) {
    ImGui::TextDisabled("No VM attached");
    return;
  }

  // Update VM loop
  vm_->Update();

  ImGui::PushID("Materials Sandbox");

  // Determine interaction state
  const auto state = vm_->GetImportState().workflow_state;
  // const bool is_configuring = ... (Moved to DrawBrowserSection logic)
  const bool is_idle
    = (state == MaterialsSandboxVm::ImportState::WorkflowState::Idle);

  // Inline Import Section
  // Moved to DrawBrowserSection

  if (ImGui::CollapsingHeader(
        "Custom Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginDisabled(!is_idle);
    DrawMaterialsSection();
    ImGui::EndDisabled();
  }

  // Show/Collapse Texture Browser depending on modes. Solid Color collapses
  // and disables the browser; Custom auto-expands and enables it.
  const auto& sphere_slot = vm_->GetSphereTextureState();
  const auto& cube_slot = vm_->GetCubeTextureState();
  const bool any_custom = (sphere_slot.mode
      == oxygen::examples::textured_cube::TextureIndexMode::kCustom
    || cube_slot.mode
      == oxygen::examples::textured_cube::TextureIndexMode::kCustom);
  const bool is_solid_color = vm_->GetSurfaceState().use_constant_base_color;

  // Only draw the Texture Browser when a slot is in Custom mode. Do not
  // render a disabled/collapsed header in other modes (prevents interactivity
  // and clutter).
  if (any_custom) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (ImGui::CollapsingHeader(
          "Texture Browser", ImGuiTreeNodeFlags_DefaultOpen)) {
      DrawBrowserSection();
    }
  }

  ImGui::PopID();
}

auto MaterialsSandboxPanel::DrawImportSection() -> void
{
  auto& state = vm_->GetImportState();

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15F, 0.15F, 0.15F, 1.0F));
  ImGui::BeginChild("ImportPanel", ImVec2(0, 0),
    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY
      | ImGuiChildFlags_AlwaysAutoResize);

  ImGui::Text("Importing: %s", state.source_path.data());
  ImGui::Spacing();

  // 1. Configuration (Disable if importing)
  const bool is_importing = (state.workflow_state
    == MaterialsSandboxVm::ImportState::WorkflowState::Importing);

  ImGui::BeginDisabled(is_importing);
  {
    // Usage Presets
    const char* kUsages[] = { "Auto-Detect", "Albedo / Diffuse", "Normal Map",
      "HDR Environment", "UI Element" };
    int usage_idx = static_cast<int>(state.usage);
    if (ImGui::Combo(
          "Usage Preset", &usage_idx, kUsages, IM_ARRAYSIZE(kUsages))) {
      state.usage
        = static_cast<MaterialsSandboxVm::ImportState::Usage>(usage_idx);
      vm_->UpdateImportSettingsFromUsage();
    }

    // Contextual Toggles
    if (state.usage == MaterialsSandboxVm::ImportState::Usage::kNormal) {
      ImGui::Indent();
      ImGui::Checkbox("Flip Green Channel (Y)", &state.flip_normal_green);
      ImGui::Unindent();
    }
    if (state.usage
      == MaterialsSandboxVm::ImportState::Usage::kHdrEnvironment) {
      ImGui::Indent();
      ImGui::SliderFloat("Exposure (EV)", &state.exposure_ev, -5.0F, 5.0F);
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
  ImGui::SeparatorText("Import Status");
  ImGui::Spacing();

  // 2. Status / Progress (Bottom area)
  if (state.workflow_state
    == MaterialsSandboxVm::ImportState::WorkflowState::Importing) {
    ImGui::Text("Importing...");
    ImGui::ProgressBar(state.progress, ImVec2(-1, 0));
  } else if (state.workflow_state
    == MaterialsSandboxVm::ImportState::WorkflowState::Finished) {
    if (state.last_import_success) {
      ImGui::TextColored(
        ImVec4(0.2F, 1.0F, 0.2F, 1.0F), "%s", state.status_message.c_str());
    } else {
      ImGui::TextColored(ImVec4(1.0F, 0.2F, 0.2F, 1.0F), "Error: %s",
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
            == MaterialsSandboxVm::ImportState::WorkflowState::Finished
          ? "Retry Import"
          : "Import",
        ImVec2(-1, 0))) {
    vm_->RequestImport();
  }
  ImGui::EndDisabled();

  ImGui::EndChild();
  ImGui::PopStyleColor();
}

auto MaterialsSandboxPanel::DrawMaterialsSection() -> void
{
  auto& surface = vm_->GetSurfaceState();
  auto& uv = vm_->GetUvState();

  // Texture mode controls (global - applies to both sphere and cube)
  // 0 = Fallback, 1 = Forced Error, 2 = Solid Color, 3 = Custom
  int current_mode = 0;
  const auto& sphere_slot = vm_->GetSphereTextureState();
  const auto& cube_slot = vm_->GetCubeTextureState();

  // Solid color mode takes visual priority
  if (surface.use_constant_base_color) {
    current_mode = 2;
  } else if (sphere_slot.mode == cube_slot.mode) {
    switch (sphere_slot.mode) {
    case oxygen::examples::textured_cube::TextureIndexMode::kForcedError:
      current_mode = 1;
      break;
    case oxygen::examples::textured_cube::TextureIndexMode::kCustom:
      current_mode = 3;
      break;
    case oxygen::examples::textured_cube::TextureIndexMode::kFallback:
    default:
      current_mode = 0;
      break;
    }
  } else {
    // Mixed modes or other case: default to Custom to allow per-slot edits
    current_mode = 3;
  }

  auto apply_mode = [this](int mode) {
    switch (mode) {
    case 0: // Fallback
      vm_->GetSphereTextureState().mode
        = oxygen::examples::textured_cube::TextureIndexMode::kFallback;
      vm_->GetCubeTextureState().mode
        = oxygen::examples::textured_cube::TextureIndexMode::kFallback;
      vm_->GetSurfaceState().use_constant_base_color = false;
      break;
    case 1: // Forced Error
      vm_->GetSphereTextureState().mode
        = oxygen::examples::textured_cube::TextureIndexMode::kForcedError;
      vm_->GetCubeTextureState().mode
        = oxygen::examples::textured_cube::TextureIndexMode::kForcedError;
      vm_->GetSurfaceState().use_constant_base_color = false;
      break;
    case 2: // Solid Color
      vm_->GetSurfaceState().use_constant_base_color = true;
      // Keep resource indices but ensure textures are ignored by using
      // fallback mode for sampling
      vm_->GetSphereTextureState().mode
        = oxygen::examples::textured_cube::TextureIndexMode::kFallback;
      vm_->GetCubeTextureState().mode
        = oxygen::examples::textured_cube::TextureIndexMode::kFallback;
      break;
    case 3: // Custom
      vm_->GetSphereTextureState().mode
        = oxygen::examples::textured_cube::TextureIndexMode::kCustom;
      vm_->GetCubeTextureState().mode
        = oxygen::examples::textured_cube::TextureIndexMode::kCustom;
      vm_->GetSurfaceState().use_constant_base_color = false;
      break;
    default:
      break;
    }
    vm_->SetCubeRebuildNeeded();
  };

  // Vertical radio buttons (one per line)
  if (ImGui::RadioButton("Fallback", &current_mode, 0)) {
    apply_mode(0);
  }
  if (ImGui::RadioButton("Forced Error", &current_mode, 1)) {
    apply_mode(1);
  }
  if (ImGui::RadioButton("Solid Color", &current_mode, 2)) {
    apply_mode(2);
  }
  if (ImGui::RadioButton("Custom Texture", &current_mode, 3)) {
    apply_mode(3);
  }

  // Solid Color exposes the color picker
  if (surface.use_constant_base_color) {
    if (ImGui::ColorEdit3("Base Color", &surface.constant_base_color_rgb.x)) {
      vm_->SetCubeRebuildNeeded();
    }
  }

  if (ImGui::SliderFloat("Metalness", &surface.metalness, 0.0F, 1.0F)) {
    vm_->SetCubeRebuildNeeded();
  }
  if (ImGui::SliderFloat("Roughness", &surface.roughness, 0.0F, 1.0F)) {
    vm_->SetCubeRebuildNeeded();
  }

  // UV controls are only applicable when at least one slot is Custom
  const bool any_custom = (vm_->GetSphereTextureState().mode
      == oxygen::examples::textured_cube::TextureIndexMode::kCustom
    || vm_->GetCubeTextureState().mode
      == oxygen::examples::textured_cube::TextureIndexMode::kCustom);

  if (any_custom) {
    ImGui::Spacing();
    ImGui::SeparatorText("UV Transform");
    ImGui::Spacing();

    if (ImGui::Button("Reset UV")) {
      uv.scale = { 1.0F, 1.0F };
      uv.offset = { 0.0F, 0.0F };
    }
    ImGui::DragFloat2("Scale", &uv.scale.x, 0.01F);
    ImGui::DragFloat2("Offset", &uv.offset.x, 0.01F);

    // UV/Image Origin options
    static const std::array<MaterialsSandboxVm::UvOrigin, 2> kUvOrigins
      = { MaterialsSandboxVm::UvOrigin::kBottomLeft,
          MaterialsSandboxVm::UvOrigin::kTopLeft };
    DrawEnumCombo("UV Origin", uv.uv_origin, kUvOrigins);

    ImGui::Checkbox("Extra Flip U", &uv.extra_flip_u);
    ImGui::SameLine();
    ImGui::Checkbox("Extra Flip V", &uv.extra_flip_v);

    ImGui::Spacing();
  }
}

auto MaterialsSandboxPanel::DrawBrowserSection() -> void
{
  auto& cooked = vm_->GetCookedEntries();

  // Determine state for local UI logic
  const auto state = vm_->GetImportState().workflow_state;
  const bool is_idle
    = (state == MaterialsSandboxVm::ImportState::WorkflowState::Idle);
  const bool is_configuring
    = (state != MaterialsSandboxVm::ImportState::WorkflowState::Idle);

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
    ImGui::Spacing();
    ImGui::SeparatorText("Import");
    ImGui::Spacing();
    DrawImportSection();
    ImGui::Spacing();
    ImGui::SeparatorText("Import");
    ImGui::Spacing();
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

    // Enable selection buttons only if the respective slot is in Custom mode.
    const bool sphere_can_select = is_2d
      && (vm_->GetSphereTextureState().mode
        == oxygen::examples::textured_cube::TextureIndexMode::kCustom);
    const bool cube_can_select = is_2d
      && (vm_->GetCubeTextureState().mode
        == oxygen::examples::textured_cube::TextureIndexMode::kCustom);

    ImGui::BeginDisabled(!sphere_can_select);
    if (ImGui::Button("Sphere")) {
      vm_->SelectTextureForSlot(entry.index, true);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!cube_can_select);
    if (ImGui::Button("Cube")) {
      vm_->SelectTextureForSlot(entry.index, false);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    // Sky selection should be available for cubemap entries regardless of
    // per-slot texture modes (this applies a global skybox to the scene).
    const bool sky_can_select = is_cube;
    ImGui::BeginDisabled(!sky_can_select);
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
        ImGui::Spacing();
        ImGui::SeparatorText("Import Settings");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7F, 0.7F, 1.0F, 1.0F), "Import Settings:");
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
