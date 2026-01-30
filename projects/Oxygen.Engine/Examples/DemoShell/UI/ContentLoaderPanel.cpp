//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <fmt/format.h>
#include <functional>
#include <string>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/Naming.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/ContentVm.h"

namespace oxygen::examples::ui {

namespace {

  auto HelpMarker(const char* description) -> void
  {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0F);
      ImGui::TextUnformatted(description);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
  }

  auto InputTextString(const char* label, std::string& value) -> bool
  {
    char buffer[1024];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (ImGui::InputText(label, buffer, sizeof(buffer))) {
      value = buffer;
      return true;
    }
    return false;
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

}

void ContentLoaderPanel::Initialize(observer_ptr<ContentVm> vm) { vm_ = vm; }

auto ContentLoaderPanel::DrawContents() -> void
{
  if (!vm_) {
    ImGui::TextDisabled("No Content VM connected");
    return;
  }

  // Global Progress (rendered at bottom)
  const bool isImporting = vm_->IsImportInProgress();
  const bool isSceneLoading = vm_->IsSceneLoading();
  const bool shouldShowProgress
    = isImporting || isSceneLoading || vm_->ShouldShowSceneLoadProgress();

  const auto& style = ImGui::GetStyle();
  const float status_height = ImGui::GetFrameHeight() + style.ItemSpacing.y;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float main_height = std::max(0.0f, avail.y - status_height);

  // Disable interactions except for the status area when an operation is in
  // flight.
  ImGui::BeginDisabled(isImporting || isSceneLoading);
  ImGui::PushStyleVar(
    ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
  if (ImGui::BeginChild("ContentLoaderMain", ImVec2(0.0f, main_height))) {
    if (ImGui::BeginTabBar("ContentLoaderTabs", ImGuiTabBarFlags_None)) {
      if (ImGui::BeginTabItem("Sources")) {
        DrawImportWorkflow();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Library")) {
        DrawLibraryWorkflow();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Diagnostics")) {
        DrawDiagnosticsWorkflow();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Advanced")) {
        DrawAdvancedSettings();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::EndChild();
  ImGui::EndDisabled();

  if (ImGui::BeginChild("ContentLoaderStatus", ImVec2(0.0f, status_height),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    if (shouldShowProgress) {
      const float progress = isImporting ? vm_->GetActiveImportProgress()
                                         : vm_->GetSceneLoadProgress();
      const std::string message = isImporting ? vm_->GetActiveImportMessage()
                                              : vm_->GetSceneLoadMessage();

      const ImVec4 fill_color = isImporting ? ImVec4(0.2f, 0.7f, 0.4f, 1.0f)
                                            : ImVec4(0.2f, 0.5f, 0.85f, 1.0f);
      const ImVec4 frame_color = isImporting ? ImVec4(0.1f, 0.3f, 0.2f, 1.0f)
                                             : ImVec4(0.1f, 0.2f, 0.35f, 1.0f);

      const char* cancel_label = nullptr;
      if (isImporting) {
        cancel_label = "Cancel Import";
      } else if (isSceneLoading) {
        cancel_label = "Cancel Scene Load";
      }

      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
      ImGui::PushStyleColor(ImGuiCol_FrameBg, frame_color);
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, fill_color);

      float button_width = 0.0f;
      if (cancel_label != nullptr) {
        const ImVec2 label_size = ImGui::CalcTextSize(cancel_label);
        button_width = label_size.x + ImGui::GetStyle().FramePadding.x * 2.0f;
      }
      const float progress_width = ImGui::GetContentRegionAvail().x
        - (cancel_label != nullptr
            ? button_width + ImGui::GetStyle().ItemSpacing.x
            : 0.0f);

      ImGui::ProgressBar(
        progress, ImVec2(progress_width, 0.0f), message.c_str());
      ImGui::PopStyleColor(2);
      ImGui::PopStyleVar();

      if (cancel_label != nullptr) {
        ImGui::SameLine();
        if (ImGui::Button(cancel_label)) {
          if (isImporting) {
            vm_->CancelActiveImport();
          } else if (isSceneLoading) {
            vm_->CancelSceneLoad();
          }
        }
      }
    } else {
      ImGui::TextDisabled("Ready");
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();

  // Ensure the file browser is drawn if open
  if (auto browser = vm_->GetFileBrowser()) {
    browser->UpdateAndDraw();
  }
}

auto ContentLoaderPanel::DrawImportWorkflow() -> void
{
  auto explorer = vm_->GetExplorerSettings();
  bool explorer_changed = false;

  if (ImGui::CollapsingHeader(
        "Source Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
    std::string root_path = explorer.model_root.string();
    if (InputTextString("Model Root", root_path)) {
      explorer.model_root = root_path;
      explorer_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...##root")) {
      vm_->BrowseForModelRoot();
    }

    if (ImGui::Checkbox("FBX", &explorer.include_fbx))
      explorer_changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("GLB", &explorer.include_glb))
      explorer_changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("GLTF", &explorer.include_gltf))
      explorer_changed = true;

    if (ImGui::Button("Refresh Sources")) {
      vm_->RefreshSources();
    }
    ImGui::SameLine();
    if (ImGui::Button("Choose File...")) {
      vm_->BrowseForSourceFile();
    }
  }

  if (explorer_changed) {
    vm_->SetExplorerSettings(explorer);
  }

  ImGui::Separator();
  ImGui::Text("Task Configuration:");
  DrawWorkflowSettings();
  DrawImportSettings();
  DrawTextureTuningSettings();

  ImGui::Separator();
  ImGui::Text("Available Discoverable Sources:");

  static char source_filter[128] = "";
  ImGui::InputTextWithHint("##SourceFilter", "Filter sources...", source_filter,
    sizeof(source_filter));

  const auto& sources = vm_->GetSources();
  if (sources.empty()) {
    ImGui::TextDisabled("No sources found. Check your Model Root.");
  }

  if (ImGui::BeginChild("SourcesList", ImVec2(0, 0), true)) {
    for (const auto& src : sources) {
      std::string filename = src.path.filename().string();
      if (strlen(source_filter) > 0
        && filename.find(source_filter) == std::string::npos)
        continue;

      if (ImGui::Selectable(filename.c_str())) {
        vm_->StartImport(src.path);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", src.path.string().c_str());
      }
    }
  }
  ImGui::EndChild();
}

auto ContentLoaderPanel::DrawLibraryWorkflow() -> void
{
  if (ImGui::Button("Refresh Files"))
    vm_->RefreshLibrary();
  ImGui::SameLine();
  if (ImGui::Button("Browse for PAK..."))
    vm_->BrowseForPak();
  ImGui::SameLine();
  if (ImGui::Button("Browse for Index..."))
    vm_->BrowseForIndex();
  ImGui::SameLine();
  if (ImGui::Button("Unload All"))
    vm_->UnloadAllLibrary();

  if (ImGui::TreeNode("Mounted Items")) {
    for (const auto& pak : vm_->GetLoadedPaks()) {
      ImGui::BulletText("PAK: %s", pak.filename().string().c_str());
    }
    for (const auto& idx : vm_->GetLoadedIndices()) {
      ImGui::BulletText("Index: %s", idx.filename().string().c_str());
    }
    ImGui::TreePop();
  }

  ImGui::Separator();
  ImGui::Text("Library Scenes (Ready to Load):");
  static char scene_filter[128] = "";
  ImGui::InputTextWithHint(
    "##SceneFilter", "Search scenes...", scene_filter, sizeof(scene_filter));

  if (ImGui::BeginChild("LibraryScenes", ImVec2(0, 0), true)) {
    for (const auto& scene : vm_->GetAvailableScenes()) {
      if (strlen(scene_filter) > 0
        && scene.name.find(scene_filter) == std::string::npos)
        continue;

      if (ImGui::Selectable(scene.name.c_str())) {
        vm_->RequestSceneLoad(scene.key);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Virtual Path: %s\nKey: %s", scene.name.c_str(),
          std::string(nostd::to_string(scene.key)).c_str());
      }
    }
  }
  ImGui::EndChild();
}

auto ContentLoaderPanel::DrawDiagnosticsWorkflow() -> void
{
  if (ImGui::Button("Clear All"))
    vm_->ClearDiagnostics();
  ImGui::Separator();

  if (ImGui::BeginChild("DiagnosticsList", ImVec2(0, 0), true)) {
    for (const auto& diag : vm_->GetDiagnostics()) {
      ImVec4 color = ImVec4(1, 1, 1, 1);

      // Check for diagnostic markers (operation boundaries)
      bool is_marker = diag.message.find("---") != std::string::npos;
      if (is_marker) {
        color = ImVec4(0.4f, 1.0f, 0.8f, 1.0f); // Teal for markers
        ImGui::Separator();
        ImGui::TextColored(color, "%s", diag.message.c_str());
        ImGui::Separator();
        continue;
      }

      if (diag.severity == content::import::ImportSeverity::kError)
        color = ImVec4(1, 0.4f, 0.4f, 1);
      else if (diag.severity == content::import::ImportSeverity::kWarning)
        color = ImVec4(1, 0.8f, 0.4f, 1);

      ImGui::TextColored(color, "[%s] %s: %s",
        std::string(nostd::to_string(diag.severity)).c_str(), diag.code.c_str(),
        diag.message.c_str());
    }
  }
  ImGui::EndChild();
}

auto ContentLoaderPanel::DrawWorkflowSettings() -> void
{
  if (ImGui::CollapsingHeader("Workflow Settings")) {
    auto explorer = vm_->GetExplorerSettings();
    bool changed = false;

    if (ImGui::Checkbox(
          "Auto-load scene after import", &explorer.auto_load_on_import))
      changed = true;
    if (ImGui::Checkbox(
          "Auto-dump texture VRAM", &explorer.auto_dump_texture_memory))
      changed = true;
    if (explorer.auto_dump_texture_memory) {
      ImGui::Indent();
      if (ImGui::SliderInt("Dump Top N", &explorer.dump_top_n, 1, 100))
        changed = true;
      if (ImGui::SliderInt(
            "Delay (frames)", &explorer.auto_dump_delay_frames, 0, 600))
        changed = true;
      ImGui::Unindent();
    }

    if (changed) {
      vm_->SetExplorerSettings(explorer);
    }
  }
}

auto ContentLoaderPanel::DrawImportSettings() -> void
{
  if (ImGui::CollapsingHeader("Import Configuration")) {
    auto options = vm_->GetImportOptions();
    bool changed = false;

    if (ImGui::TreeNodeEx("Identifiers", ImGuiTreeNodeFlags_DefaultOpen)) {
      static constexpr std::array<content::import::AssetKeyPolicy, 2>
        kKeyPolicies
        = { content::import::AssetKeyPolicy::kDeterministicFromVirtualPath,
            content::import::AssetKeyPolicy::kRandom };
      if (DrawEnumCombo(
            "Asset Key Policy", options.asset_key_policy, kKeyPolicies))
        changed = true;
      ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(
          "Content Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
      bool textures = (options.import_content
                        & content::import::ImportContentFlags::kTextures)
        != content::import::ImportContentFlags::kNone;
      bool materials = (options.import_content
                         & content::import::ImportContentFlags::kMaterials)
        != content::import::ImportContentFlags::kNone;
      bool geometry = (options.import_content
                        & content::import::ImportContentFlags::kGeometry)
        != content::import::ImportContentFlags::kNone;
      bool scene
        = (options.import_content & content::import::ImportContentFlags::kScene)
        != content::import::ImportContentFlags::kNone;

      if (ImGui::Checkbox("Textures", &textures))
        changed = true;
      ImGui::SameLine();
      if (ImGui::Checkbox("Materials", &materials))
        changed = true;
      ImGui::SameLine();
      if (ImGui::Checkbox("Geometry", &geometry))
        changed = true;
      ImGui::SameLine();
      if (ImGui::Checkbox("Scene", &scene))
        changed = true;

      if (changed) {
        options.import_content = content::import::ImportContentFlags::kNone;
        if (textures)
          options.import_content
            |= content::import::ImportContentFlags::kTextures;
        if (materials)
          options.import_content
            |= content::import::ImportContentFlags::kMaterials;
        if (geometry)
          options.import_content
            |= content::import::ImportContentFlags::kGeometry;
        if (scene)
          options.import_content |= content::import::ImportContentFlags::kScene;
      }
      ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx("Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox("Enable Hashing", &options.with_content_hashing))
        changed = true;

      static constexpr std::array<content::import::UnitNormalizationPolicy, 3>
        kUnitPolicies = {
          content::import::UnitNormalizationPolicy::kNormalizeToMeters,
          content::import::UnitNormalizationPolicy::kPreserveSource,
          content::import::UnitNormalizationPolicy::kApplyCustomFactor,
        };
      if (DrawEnumCombo(
            "Units", options.coordinate.unit_normalization, kUnitPolicies))
        changed = true;

      static constexpr std::array<content::import::GeometryAttributePolicy, 4>
        kAttrPolicies = {
          content::import::GeometryAttributePolicy::kNone,
          content::import::GeometryAttributePolicy::kPreserveIfPresent,
          content::import::GeometryAttributePolicy::kGenerateMissing,
          content::import::GeometryAttributePolicy::kAlwaysRecalculate,
        };
      if (DrawEnumCombo("Normals", options.normal_policy, kAttrPolicies))
        changed = true;
      if (DrawEnumCombo("Tangents", options.tangent_policy, kAttrPolicies))
        changed = true;

      ImGui::TreePop();
    }

    if (changed) {
      vm_->SetImportOptions(options);
    }
  }
}

auto ContentLoaderPanel::DrawTextureTuningSettings() -> void
{
  if (ImGui::CollapsingHeader("Texture Tuning")) {
    auto tuning = vm_->GetTextureTuning();
    bool changed = false;

    if (ImGui::TreeNodeEx(
          "Texture Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::Checkbox("Enabled", &tuning.enabled))
        changed = true;

      static constexpr std::array<content::import::TextureIntent, 8> kIntents
        = { content::import::TextureIntent::kAlbedo,
            content::import::TextureIntent::kNormalTS,
            content::import::TextureIntent::kRoughness,
            content::import::TextureIntent::kMetallic,
            content::import::TextureIntent::kAO,
            content::import::TextureIntent::kEmissive,
            content::import::TextureIntent::kORMPacked,
            content::import::TextureIntent::kData };
      if (DrawEnumCombo("Intent", tuning.intent, kIntents))
        changed = true;

      static constexpr std::array<ColorSpace, 2> kColorSpaces
        = { ColorSpace::kLinear, ColorSpace::kSRGB };
      static constexpr std::array<content::import::MipPolicy, 3> kMipPolicies
        = { content::import::MipPolicy::kNone,
            content::import::MipPolicy::kFullChain,
            content::import::MipPolicy::kMaxCount };
      static constexpr std::array<content::import::MipFilter, 3> kMipFilters
        = { content::import::MipFilter::kBox,
            content::import::MipFilter::kKaiser,
            content::import::MipFilter::kLanczos };

      if (DrawEnumCombo(
            "Source Color Space", tuning.source_color_space, kColorSpaces))
        changed = true;
      if (DrawEnumCombo("Mip Policy", tuning.mip_policy, kMipPolicies))
        changed = true;
      if (tuning.mip_policy == content::import::MipPolicy::kMaxCount) {
        int max_mips = (int)tuning.max_mip_levels;
        if (ImGui::SliderInt("Max Mips", &max_mips, 1, 16)) {
          tuning.max_mip_levels = (uint8_t)max_mips;
          changed = true;
        }
      }
      if (DrawEnumCombo("Mip Filter", tuning.mip_filter, kMipFilters))
        changed = true;

      static constexpr std::array<Format, 2> kFormats
        = { Format::kBC7UNorm, Format::kBC7UNormSRGB };
      if (DrawEnumCombo("Color Format", tuning.color_output_format, kFormats))
        changed = true;
      if (DrawEnumCombo("Data Format", tuning.data_output_format, kFormats))
        changed = true;

      static constexpr std::array<content::import::Bc7Quality, 4> kBc7Tiers
        = { content::import::Bc7Quality::kNone,
            content::import::Bc7Quality::kFast,
            content::import::Bc7Quality::kDefault,
            content::import::Bc7Quality::kHigh };
      if (DrawEnumCombo("BC7 Quality", tuning.bc7_quality, kBc7Tiers))
        changed = true;

      static constexpr std::array<content::import::HdrHandling, 3> kHdrModes
        = { content::import::HdrHandling::kError,
            content::import::HdrHandling::kTonemapAuto,
            content::import::HdrHandling::kKeepFloat };
      if (DrawEnumCombo("HDR Handling", tuning.hdr_handling, kHdrModes))
        changed = true;

      ImGui::TreePop();
    }

    if (changed) {
      vm_->SetTextureTuning(tuning);
    }
  }
}

auto ContentLoaderPanel::DrawAdvancedSettings() -> void
{
  static bool service_dirty = false;

  if (ImGui::CollapsingHeader(
        "Pipeline Concurrency", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto cfg = vm_->GetServiceConfig();
    bool changed = false;

    auto draw_pipe = [&](const char* label,
                       content::import::ImportPipelineConcurrency& pipe) {
      ImGui::PushID(label);
      ImGui::Text("%s", label);
      ImGui::SameLine(100);
      int w = (int)pipe.workers;
      if (ImGui::DragInt("Workers", &w, 0.1f, 1, 64)) {
        pipe.workers = (uint32_t)w;
        changed = true;
      }
      ImGui::SameLine();
      int q = (int)pipe.queue_capacity;
      if (ImGui::DragInt("Queue", &q, 1.0f, 1, 256)) {
        pipe.queue_capacity = (uint32_t)q;
        changed = true;
      }
      ImGui::PopID();
    };

    int pool = (int)cfg.thread_pool_size;
    if (ImGui::DragInt("Global Thread Pool", &pool, 0.1f, 1, 128)) {
      cfg.thread_pool_size = (uint32_t)pool;
      changed = true;
    }

    draw_pipe("Texture", cfg.concurrency.texture);
    draw_pipe("Buffer", cfg.concurrency.buffer);
    draw_pipe("Material", cfg.concurrency.material);
    draw_pipe("Mesh", cfg.concurrency.mesh_build);
    draw_pipe("Geometry", cfg.concurrency.geometry);
    draw_pipe("Scene", cfg.concurrency.scene);

    if (changed) {
      vm_->SetServiceConfig(cfg);
      service_dirty = true;
    }

    if (service_dirty) {
      ImGui::TextColored(
        ImVec4(1, 0.5f, 0, 1), "Changes require service restart.");
      if (ImGui::Button("Restart Import Service")) {
        vm_->RestartImportService();
        service_dirty = false;
      }
    }
  }

  if (ImGui::CollapsingHeader("Output Layout")) {
    auto layout = vm_->GetLayout();
    bool changed = false;

    if (InputTextString("Virtual Root", layout.virtual_mount_root))
      changed = true;
    if (InputTextString("Index Name", layout.index_file_name))
      changed = true;
    if (InputTextString("Resources Dir", layout.resources_dir))
      changed = true;
    if (InputTextString("Descriptors Dir", layout.descriptors_dir))
      changed = true;
    if (InputTextString("Scenes Subdir", layout.scenes_subdir))
      changed = true;
    if (InputTextString("Geometry Subdir", layout.geometry_subdir))
      changed = true;
    if (InputTextString("Materials Subdir", layout.materials_subdir))
      changed = true;

    if (changed) {
      vm_->SetLayout(layout);
    }
  }

  ImGui::Separator();
  if (ImGui::Button("Force Trim Asset Caches")) {
    vm_->ForceTrimCaches();
  }
  HelpMarker("Trims engine-side asset caches without unmounting content "
             "sources or changing the active scene.");
}

auto ContentLoaderPanel::GetName() const noexcept -> std::string_view
{
  return "Content Loader";
}
auto ContentLoaderPanel::GetPreferredWidth() const noexcept -> float
{
  return 520.0F;
}
auto ContentLoaderPanel::GetIcon() const noexcept -> std::string_view
{
  return oxygen::imgui::icons::kIconContentLoader;
}

auto ContentLoaderPanel::OnLoaded() -> void
{
  if (vm_) {
    vm_->RefreshSources();
    vm_->RefreshLibrary();
  }
}

auto ContentLoaderPanel::OnUnloaded() -> void { }

} // namespace oxygen::examples::ui
