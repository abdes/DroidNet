//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DemoShell/PanelCommands.h"
#include "TexturedCube/SceneSetup.h"
#include "TexturedCube/UI/MaterialsSandboxPanel.h"
#include "TexturedCube/UI/MaterialsSandboxVm.h"
#include <fmt/format.h>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Commands/Command.h>
#include <Oxygen/Commands/Params.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise) -
// ImGui

namespace oxygen::examples::textured_cube::ui {

namespace {

  using ImportState = MaterialsSandboxVm::ImportState;
  using WorkflowState = ImportState::WorkflowState;

  constexpr std::string_view kMode = "cube.material_mode";
  constexpr std::string_view kBaseColor = "cube.base_color";
  constexpr std::string_view kMetalness = "cube.metalness";
  constexpr std::string_view kRoughness = "cube.roughness";
  constexpr std::string_view kRefresh = "cube.refresh";
  constexpr std::string_view kImport = "cube.import";
  constexpr std::string_view kTexture = "cube.texture";
  constexpr std::string_view kSkybox = "cube.skybox";
  constexpr std::string_view kMaterial = "cube.material";
  constexpr std::string_view kClearMaterial = "cube.clear_material";

  //! What the sphere and the cube are shaded with.
  enum class MaterialMode : std::uint8_t {
    kFallback,
    kForcedError,
    kSolidColor,
    kCustomTexture,
    kGrid,
    kCustomMaterial,
  };

  struct ModeInfo {
    MaterialMode mode;
    const char* name;
    const char* label;
  };
  constexpr auto kModes = std::array {
    ModeInfo { .mode = MaterialMode::kFallback,
      .name = "fallback",
      .label = "Fallback" },
    ModeInfo { .mode = MaterialMode::kForcedError,
      .name = "forced_error",
      .label = "Forced Error" },
    ModeInfo { .mode = MaterialMode::kSolidColor,
      .name = "solid_color",
      .label = "Solid Color" },
    ModeInfo {
      .mode = MaterialMode::kCustomTexture,
      .name = "custom_texture",
      .label = "Custom Texture",
    },
    ModeInfo {
      .mode = MaterialMode::kGrid, .name = "grid", .label = "Grid Material" },
    ModeInfo {
      .mode = MaterialMode::kCustomMaterial,
      .name = "custom_material",
      .label = "Custom Material",
    },
  };

  constexpr auto kUsageNames
    = std::array { "auto", "albedo", "normal", "hdr", "ui" };
  constexpr auto kUsageLabels = std::array {
    "Auto-Detect",
    "Albedo / Diffuse",
    "Normal Map",
    "HDR Environment",
    "UI Element",
  };
  constexpr auto kQualityLabels = std::array { "Fast", "Balanced", "High" };
  constexpr auto kFormatLabels
    = std::array { "RGBA8 SRGB", "BC7 SRGB", "RGBA16F", "RGBA32F" };

  auto Name(const std::string_view name) -> std::string
  {
    return std::string(name);
  }

  auto Param(std::string name, const commands::ParamType type, std::string help)
    -> commands::CommandParam
  {
    return commands::CommandParam {
      .name = std::move(name),
      .type = type,
      .help = std::move(help),
      .required = true,
    };
  }

  auto ApplyMode(MaterialsSandboxVm& vm, const MaterialMode mode) -> void
  {
    vm.SetUseCustomMaterial(mode == MaterialMode::kCustomMaterial);
    if (mode != MaterialMode::kCustomMaterial) {
      auto slot_mode = TextureIndexMode::kFallback;
      switch (mode) {
      case MaterialMode::kForcedError:
        slot_mode = TextureIndexMode::kForcedError;
        break;
      case MaterialMode::kCustomTexture:
        slot_mode = TextureIndexMode::kCustom;
        break;
      case MaterialMode::kGrid:
        slot_mode = TextureIndexMode::kProceduralGrid;
        break;
      default:
        break;
      }
      vm.GetSphereTextureState().mode = slot_mode;
      vm.GetCubeTextureState().mode = slot_mode;
      // Solid color keeps the texture indices; fallback sampling ignores
      // them.
      vm.GetSurfaceState().use_constant_base_color
        = mode == MaterialMode::kSolidColor;
    }
    vm.SetCubeRebuildNeeded();
  }

  auto CurrentMode(const MaterialsSandboxVm& vm) -> MaterialMode
  {
    if (vm.IsUseCustomMaterial()) {
      return MaterialMode::kCustomMaterial;
    }
    if (vm.GetSurfaceState().use_constant_base_color) {
      return MaterialMode::kSolidColor;
    }
    const auto sphere = vm.GetSphereTextureState().mode;
    if (sphere != vm.GetCubeTextureState().mode) {
      // Mixed slots edit per slot, as custom textures do.
      return MaterialMode::kCustomTexture;
    }
    switch (sphere) {
    case TextureIndexMode::kForcedError:
      return MaterialMode::kForcedError;
    case TextureIndexMode::kCustom:
      return MaterialMode::kCustomTexture;
    case TextureIndexMode::kProceduralGrid:
      return MaterialMode::kGrid;
    case TextureIndexMode::kFallback:
    default:
      return MaterialMode::kFallback;
    }
  }

  auto RegisterImportCommand(PanelCommands& commands, MaterialsSandboxVm& vm)
    -> void
  {
    auto usage = Param("usage", commands::ParamType::kEnum, "Texture usage");
    usage.allowed_values = { kUsageNames.begin(), kUsageNames.end() };
    commands.Add({
      .name = Name(kImport),
      .label = "Import Texture",
      .help = "Import an image into the cooked root",
      .params = {
        Param("path", commands::ParamType::kPath, "Source image"),
        std::move(usage),
        Param("compress", commands::ParamType::kBool, "Compress to BC7"),
        Param("quality", commands::ParamType::kInt,
          "BC7 quality: 1 fast, 2 balanced, 3 high"),
        Param("hash", commands::ParamType::kBool, "Deduplicate by hash"),
        Param("flip_green", commands::ParamType::kBool,
          "Flip a normal map's green channel"),
        Param("exposure_ev", commands::ParamType::kDouble,
          "Exposure of an HDR source"),
        Param("mips", commands::ParamType::kBool, "Generate mips"),
        Param("format", commands::ParamType::kInt,
          "Output format index: RGBA8 sRGB, BC7 sRGB, RGBA16F, RGBA32F"),
      },
      .handler = [&vm](const commands::CommandArgs& args,
                   const commands::CommandContext&) -> commands::CommandResult {
        auto& state = vm.GetImportState();
        const auto& path = args.GetString("path");
        const auto length = std::min(path.size(), state.source_path.size() - 1);
        std::copy_n(path.begin(), length, state.source_path.begin());
        state.source_path.at(length) = '\0';
        const auto& usage_name = args.GetString("usage");
        for (std::size_t i = 0; i < kUsageNames.size(); ++i) {
          if (usage_name == kUsageNames.at(i)) {
            state.usage = static_cast<ImportState::Usage>(i);
          }
        }
        state.compress = args.GetBool("compress");
        // Usage and compression pick the defaults the other values override.
        vm.UpdateImportSettingsFromUsage();
        state.bc7_quality_idx = static_cast<int>(args.GetInt("quality"));
        state.compute_hash = args.GetBool("hash");
        state.flip_normal_green = args.GetBool("flip_green");
        state.exposure_ev = static_cast<float>(args.GetDouble("exposure_ev"));
        state.generate_mips = args.GetBool("mips");
        state.output_format_idx = static_cast<int>(args.GetInt("format"));
        vm.RequestImport();
        return commands::CommandResult::Success();
      },
    });
  }

  auto RegisterCommands(PanelCommands& commands, MaterialsSandboxVm& vm) -> void
  {
    std::vector<std::string> modes;
    modes.reserve(kModes.size());
    for (const auto& info : kModes) {
      modes.emplace_back(info.name);
    }
    commands.AddEnum(Name(kMode), "Material Mode",
      "What the sphere and the cube are shaded with", std::move(modes),
      [&vm](const std::size_t i) -> void { ApplyMode(vm, kModes.at(i).mode); });
    commands.AddRgb(Name(kBaseColor), "Base Color",
      "Base color of the solid color mode",
      [&vm](const glm::vec3& color) -> void {
        vm.GetSurfaceState().constant_base_color_rgb = color;
        vm.SetCubeRebuildNeeded();
      });
    commands.AddFloat(
      Name(kMetalness), "Metalness", "Surface metalness",
      [&vm](const float v) -> void {
        vm.GetSurfaceState().metalness = v;
        vm.SetCubeRebuildNeeded();
      },
      0.0F, 1.0F);
    commands.AddFloat(
      Name(kRoughness), "Roughness", "Surface roughness",
      [&vm](const float v) -> void {
        vm.GetSurfaceState().roughness = v;
        vm.SetCubeRebuildNeeded();
      },
      0.0F, 1.0F);
    commands.AddAction(Name(kRefresh), "Refresh Cooked Content",
      "Rescan the cooked textures and materials",
      [&vm] -> void { vm.RequestRefresh(); });
    commands.Add({
      .name = Name(kTexture),
      .label = "Assign Texture",
      .help = "Assign a cooked texture to the sphere or the cube",
      .params = {
        Param("index", commands::ParamType::kInt, "Cooked texture index"),
        commands::CommandParam {
          .name = "object",
          .type = commands::ParamType::kEnum,
          .help = "Object that takes the texture",
          .required = true,
          .allowed_values = { "sphere", "cube" },
        },
      },
      .handler = [&vm](const commands::CommandArgs& args,
                   const commands::CommandContext&) -> commands::CommandResult {
        const auto index = static_cast<std::uint32_t>(args.GetInt("index"));
        if (!vm.SelectTextureForSlot(
              index, args.GetString("object") == "sphere")) {
          return commands::CommandResult::Failure(
            commands::ExecutionStatus::kError, "The texture did not load");
        }
        return commands::CommandResult::Success();
      },
    });
    commands.AddInt(
      Name(kSkybox), "Assign Skybox", "Use a cooked cubemap as the skybox",
      [&vm](const int index) -> void {
        static_cast<void>(vm.SelectSkybox(static_cast<std::uint32_t>(index)));
      },
      0);
    commands.AddInt(
      Name(kMaterial), "Assign Material",
      "Shade both objects with a cooked material",
      [&vm](const int index) -> void {
        static_cast<void>(
          vm.SelectCustomMaterialByEntryIndex(static_cast<std::size_t>(index)));
      },
      0);
    commands.AddAction(Name(kClearMaterial), "Clear Material",
      "Clear the selected cooked material",
      [&vm] -> void { vm.ClearSelectedCustomMaterial(); });
    RegisterImportCommand(commands, vm);
  }

} // namespace

MaterialsSandboxPanel::MaterialsSandboxPanel(
  observer_ptr<MaterialsSandboxVm> vm, observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
{
  DCHECK_NOTNULL_F(vm_, "MaterialsSandboxPanel requires MaterialsSandboxVm");
  DCHECK_NOTNULL_F(commands_, "MaterialsSandboxPanel requires PanelCommands");
  RegisterCommands(*commands_, *vm_);
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

auto MaterialsSandboxPanel::OnLoaded() -> void { commands_->Run(kRefresh); }

auto MaterialsSandboxPanel::OnUnloaded() -> void { }

auto MaterialsSandboxPanel::DrawContents() -> void
{
  vm_->Update();

  ImGui::PushID("Materials Sandbox");
  const bool is_idle
    = vm_->GetImportState().workflow_state == WorkflowState::Idle;

  if (ImGui::CollapsingHeader(
        "Custom Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginDisabled(!is_idle);
    DrawMaterialsSection();
    ImGui::EndDisabled();
  }

  const bool use_custom_material = vm_->IsUseCustomMaterial();
  if (use_custom_material) {
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (ImGui::CollapsingHeader(
          "Material Browser", ImGuiTreeNodeFlags_DefaultOpen)) {
      DrawMaterialBrowserSection();
    }
  }

  // The texture browser serves the custom texture mode only.
  const bool any_custom
    = vm_->GetSphereTextureState().mode == TextureIndexMode::kCustom
    || vm_->GetCubeTextureState().mode == TextureIndexMode::kCustom;
  if (any_custom && !use_custom_material) {
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
  // The import settings are a draft; Import runs them as one command.
  auto& state = vm_->GetImportState();

  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15F, 0.15F, 0.15F, 1.0F));
  ImGui::BeginChild("ImportPanel", ImVec2(0, 0),
    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY
      | ImGuiChildFlags_AlwaysAutoResize);

  ImGui::Text("Importing: %s", state.source_path.data());
  ImGui::Spacing();

  const bool is_importing = state.workflow_state == WorkflowState::Importing;
  ImGui::BeginDisabled(is_importing);
  int usage_idx = static_cast<int>(state.usage);
  if (ImGui::Combo("Usage Preset", &usage_idx, kUsageLabels.data(),
        static_cast<int>(kUsageLabels.size()))) {
    state.usage = static_cast<ImportState::Usage>(usage_idx);
    vm_->UpdateImportSettingsFromUsage();
  }
  if (state.usage == ImportState::Usage::kNormal) {
    ImGui::Indent();
    ImGui::Checkbox("Flip Green Channel (Y)", &state.flip_normal_green);
    ImGui::Unindent();
  }
  if (state.usage == ImportState::Usage::kHdrEnvironment) {
    ImGui::Indent();
    ImGui::SliderFloat("Exposure (EV)", &state.exposure_ev, -5.0F, 5.0F);
    ImGui::Unindent();
  }
  if (ImGui::Checkbox("Compress", &state.compress)) {
    vm_->UpdateImportSettingsFromUsage();
  }
  if (state.compress) {
    ImGui::SameLine();
    int quality = std::clamp(state.bc7_quality_idx - 1, 0, 2);
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##Quality", &quality, kQualityLabels.data(),
          static_cast<int>(kQualityLabels.size()))) {
      state.bc7_quality_idx = quality + 1;
    }
  }
  ImGui::SameLine();
  ImGui::Checkbox("Deduplicate (Hash)", &state.compute_hash);
  if (ImGui::TreeNode("Advanced Settings")) {
    ImGui::Checkbox("Generate Mips", &state.generate_mips);
    ImGui::Combo("Format", &state.output_format_idx, kFormatLabels.data(),
      static_cast<int>(kFormatLabels.size()));
    ImGui::TreePop();
  }
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::SeparatorText("Import Status");
  ImGui::Spacing();
  if (is_importing) {
    ImGui::Text("Importing...");
    ImGui::ProgressBar(state.progress, ImVec2(-1, 0));
  } else if (state.workflow_state == WorkflowState::Finished) {
    if (state.last_import_success) {
      ImGui::TextColored(
        ImVec4(0.2F, 1.0F, 0.2F, 1.0F), "%s", state.status_message.c_str());
    } else {
      ImGui::TextColored(ImVec4(1.0F, 0.2F, 0.2F, 1.0F), "Error: %s",
        state.status_message.c_str());
    }
  }

  // Cancel closes the draft; a running import completes in the background.
  if (ImGui::Button("Cancel", ImVec2(100, 0))) {
    vm_->CancelImport();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(is_importing);
  const bool finished = state.workflow_state == WorkflowState::Finished;
  if (ImGui::Button(finished ? "Retry Import" : "Import", ImVec2(-1, 0))) {
    commands_->Run(kImport,
      {
        { "path", std::string_view(state.source_path.data()) },
        { "usage", kUsageNames.at(static_cast<std::size_t>(state.usage)) },
        { "compress", state.compress },
        { "quality", state.bc7_quality_idx },
        { "hash", state.compute_hash },
        { "flip_green", state.flip_normal_green },
        { "exposure_ev", state.exposure_ev },
        { "mips", state.generate_mips },
        { "format", state.output_format_idx },
      });
  }
  ImGui::EndDisabled();

  ImGui::EndChild();
  ImGui::PopStyleColor();
}

auto MaterialsSandboxPanel::DrawMaterialsSection() -> void
{
  const auto current = CurrentMode(*vm_);
  for (const auto& info : kModes) {
    if (ImGui::RadioButton(info.label, current == info.mode)
      && current != info.mode) {
      commands_->RunText(kMode, info.name);
    }
  }
  if (current == MaterialMode::kCustomMaterial) {
    return;
  }

  ImGui::Separator();
  ImGui::Spacing();

  const auto& surface = vm_->GetSurfaceState();
  if (surface.use_constant_base_color) {
    auto color = surface.constant_base_color_rgb;
    if (ImGui::ColorEdit3("Base Color", &color.x)) {
      commands_->RunRgb(kBaseColor, color);
    }
  }
  float metalness = surface.metalness;
  if (ImGui::SliderFloat("Metalness", &metalness, 0.0F, 1.0F)) {
    commands_->RunFloat(kMetalness, metalness);
  }
  float roughness = surface.roughness;
  if (ImGui::SliderFloat("Roughness", &roughness, 0.0F, 1.0F)) {
    commands_->RunFloat(kRoughness, roughness);
  }
}

auto MaterialsSandboxPanel::DrawBrowserSection() -> void
{
  const bool is_idle
    = vm_->GetImportState().workflow_state == WorkflowState::Idle;

  if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT " Refresh List")) {
    commands_->Run(kRefresh);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!is_idle);
  if (ImGui::Button(ICON_FA_FILE_IMPORT " Import Texture...")) {
    vm_->StartImportFlow();
  }
  ImGui::EndDisabled();

  if (!is_idle) {
    ImGui::Spacing();
    ImGui::SeparatorText("Import");
    ImGui::Spacing();
    DrawImportSection();
    ImGui::Spacing();
  }

  ImGui::BeginDisabled(!is_idle);
  const float texture_list_height
    = std::max(160.0F, ImGui::GetContentRegionAvail().y);
  ImGui::BeginChild("CookedList", ImVec2(0, texture_list_height), 1);
  for (const auto& entry : vm_->GetCookedEntries()) {
    ImGui::PushID(static_cast<int>(entry.index));

    const std::string label = entry.name.empty()
      ? fmt::format("[{}] <unnamed>", entry.index)
      : fmt::format("[{}] {}", entry.index, entry.name);
    const std::string metadata = fmt::format("{}x{} {} mips={} {}", entry.width,
      entry.height, nostd::to_string(entry.format), entry.mip_levels,
      nostd::to_string(entry.texture_type));

    const bool is_2d = entry.texture_type == TextureType::kTexture2D;
    const bool is_cube = entry.texture_type == TextureType::kTextureCube;
    const bool sphere_can_select
      = is_2d && vm_->GetSphereTextureState().mode == TextureIndexMode::kCustom;
    const bool cube_can_select
      = is_2d && vm_->GetCubeTextureState().mode == TextureIndexMode::kCustom;

    ImGui::BeginDisabled(!sphere_can_select);
    if (ImGui::Button("Sphere")) {
      commands_->Run(
        kTexture, { { "index", entry.index }, { "object", "sphere" } });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!cube_can_select);
    if (ImGui::Button("Cube")) {
      commands_->Run(
        kTexture, { { "index", entry.index }, { "object", "cube" } });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // A cubemap is the scene's skybox whatever the slots' modes are.
    ImGui::BeginDisabled(!is_cube);
    if (ImGui::Button("Sky")) {
      commands_->RunInt(kSkybox, entry.index);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextUnformatted(label.c_str());
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(metadata.c_str());
      const std::string settings_json = vm_->GetMetadataJson(entry.index);
      if (!settings_json.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Import Settings");
        ImGui::TextUnformatted(settings_json.c_str());
      }
      ImGui::EndTooltip();
    }

    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::EndDisabled();
}

auto MaterialsSandboxPanel::DrawMaterialBrowserSection() -> void
{
  if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT " Refresh Materials")) {
    commands_->Run(kRefresh);
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear Selection")) {
    commands_->Run(kClearMaterial);
  }

  const auto selected_key = vm_->GetSelectedCustomMaterialKey();
  const auto& entries = vm_->GetCookedMaterialEntries();

  const float material_list_height
    = std::max(160.0F, ImGui::GetContentRegionAvail().y);
  ImGui::BeginChild("CookedMaterialList", ImVec2(0, material_list_height), 1);
  if (entries.empty()) {
    ImGui::TextDisabled("No cooked materials found in current cooked root.");
  }
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    ImGui::PushID(static_cast<int>(i));
    const bool is_selected
      = selected_key.has_value() && selected_key.value() == entry.key;
    const std::string label = entry.virtual_path.empty()
      ? fmt::format("material:{} ({})", i, entry.descriptor_relpath)
      : entry.virtual_path;
    if (ImGui::Selectable(label.c_str(), is_selected) && !is_selected) {
      commands_->RunInt(kMaterial, static_cast<std::int64_t>(i));
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
}

} // namespace oxygen::examples::textured_cube::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise)
