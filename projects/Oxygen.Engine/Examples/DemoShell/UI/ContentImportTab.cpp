//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <vector>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/UI/ContentImportProfile.h"
#include "DemoShell/UI/ContentImportTab.h"
#include "DemoShell/UI/ContentModelCatalog.h"
#include "DemoShell/UI/ContentPanelCommands.h"
#include "DemoShell/UI/ContentPanelWidgets.h"
#include "DemoShell/UI/ContentTextMatch.h"
#include "DemoShell/UI/ContentVm.h"
#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg) - ImGui

namespace oxygen::examples::ui {

namespace {
  namespace cc = content_commands;
  namespace cw = content_widgets;
  using content::import::ImportContentFlags;

  //! Where a model's name starts, from its row; its state dot sits before.
  constexpr auto kNameColumn = 20.0F;
  constexpr auto kDotColumn = 9.0F;
  constexpr auto kRightInset = 8.0F;
  //! The height of the model list, in units.
  constexpr auto kModelListHeight = 196.0F;

  struct StateLook final {
    ImVec4 color;
    bool filled { true };
  };

  auto LookOf(const ModelState state) -> StateLook
  {
    switch (state) {
    case ModelState::kImported:
      return { .color = cw::kGreen };
    case ModelState::kSettingsChanged:
      return { .color = cw::kAmber };
    case ModelState::kFailed:
      return { .color = cw::kRed };
    case ModelState::kImporting:
      return { .color = cw::kInfo };
    case ModelState::kNotImported:
      break;
    }
    return { .color = cw::Dim(), .filled = false };
  }

  auto HintOf(const ImportSettingsGroup group) -> std::string_view
  {
    switch (group) {
    case ImportSettingsGroup::kContent:
      return "what the import writes";
    case ImportSettingsGroup::kGeometry:
      return "units, up axis, normals, LODs";
    case ImportSettingsGroup::kLightsAndCameras:
      return "lights, cameras, intensity";
    case ImportSettingsGroup::kTextures:
      return "size, intent, mips, formats";
    case ImportSettingsGroup::kImporter:
      return "thread pool, pipeline queues";
    }
    return {};
  }

  auto HasFlag(const content::import::ImportOptions& options,
    const ImportContentFlags flag) -> bool
  {
    return (options.import_content & flag) != ImportContentFlags::kNone;
  }

  auto GroupNames(const std::vector<ImportSettingsGroup>& groups) -> std::string
  {
    auto names = std::string {};
    for (const auto group : groups) {
      if (!names.empty()) {
        names += ", ";
      }
      names += to_string(group);
    }
    return names;
  }

  auto PublishedText(const ModelRecordView& view) -> std::string
  {
    if (!view.published) {
      return "not yet";
    }
    const auto time = std::chrono::floor<std::chrono::minutes>(
      std::chrono::clock_cast<std::chrono::system_clock>(*view.published));
    // A day, spelled as its seconds.
    using Day = std::chrono::duration<int, std::ratio<86'400>>;
    const auto today
      = std::chrono::floor<Day>(std::chrono::system_clock::now());
    return std::chrono::floor<Day>(time) == today
      ? fmt::format("{:%H:%M}", time)
      : fmt::format("{:%Y-%m-%d %H:%M}", time);
  }

  //! A folder node's text: `SourceMedia / DCC` for the folder
  //! `SourceMedia/DCC`.
  auto FolderLabel(std::string folder) -> std::string
  {
    for (auto at = folder.find('/'); at != std::string::npos;
      at = folder.find('/', at + 3)) {
      folder.replace(at, 1, " / ");
    }
    return folder;
  }
} // namespace

ContentImportTab::ContentImportTab(
  const observer_ptr<ContentVm> vm, const observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
  , defaults_(DefaultImportProfile())
{
  DCHECK_NOTNULL_F(vm_);
  DCHECK_NOTNULL_F(commands_);
}

auto ContentImportTab::Draw() -> void
{
  // A started import selects its model, wherever it was started from.
  if (vm_->IsImportInProgress()) {
    const auto model = vm_->GetImportProgress().model;
    if (model != importing_) {
      selected_ = model;
      importing_ = model;
    }
  } else {
    importing_.clear();
  }
  DrawSourceBar();
  DrawModels();
  DrawSelected();
  DrawRunningImport();
  DrawSettings();
  cw::CommandCheckbox(*commands_, cc::kLoadAfterImport,
    "Load the scene when an import finishes",
    vm_->GetImportTab().load_scene_after_import);
}

auto ContentImportTab::DrawSourceBar() -> void
{
  const auto& tab = vm_->GetImportTab();
  const auto& style = ImGui::GetStyle();
  // Row 1: the source folder, and a model file from anywhere.
  const auto mounts = vm_->Models().SourceMounts();
  const auto current = tab.source_mount.empty() && !mounts.empty()
    ? mounts.front().name
    : tab.source_mount;
  constexpr auto kImportFile = ICON_FA_FILE_IMPORT " Import file\xE2\x80\xA6";
  const float button_width
    = ImGui::CalcTextSize(kImportFile).x + (style.FramePadding.x * 2.0F);
  ImGui::SetNextItemWidth(
    ImGui::GetContentRegionAvail().x - button_width - style.ItemSpacing.x);
  const auto preview = "/" + current;
  if (ImGui::BeginCombo("##SourceMount", preview.c_str())) {
    for (const auto& mount : mounts) {
      const auto label = "/" + mount.name;
      if (ImGui::Selectable(label.c_str(), mount.name == current)
        && mount.name != current) {
        commands_->RunText(cc::kSourceMount, mount.name);
      }
      cw::ItemTooltip(mount.folder.generic_string());
    }
    ImGui::EndCombo();
  }
  cw::ItemTooltip("The source folder whose models are listed; add folders on "
                  "the Mounts tab");
  ImGui::SameLine();
  ImGui::BeginDisabled(vm_->IsImportInProgress());
  if (ImGui::Button(kImportFile)) {
    vm_->BrowseForModelFile();
  }
  ImGui::EndDisabled();
  cw::ItemTooltip("Import a model file from anywhere; a folder that is not a "
                  "source folder yet becomes one");

  // Row 2: the filter, and the formats listed.
  const auto toggles = std::array {
    cw::Toggle { .label = "FBX", .on = tab.fbx },
    cw::Toggle { .label = "GLB", .on = tab.glb },
    cw::Toggle { .label = "GLTF", .on = tab.gltf },
  };
  auto filter = tab.filter;
  if (cw::SearchField("##ModelFilter", "Filter models", filter,
        ImGui::GetContentRegionAvail().x - cw::ToggleGroupWidth(toggles)
          - style.ItemSpacing.x)) {
    commands_->RunText(cc::kModelFilter, filter);
  }
  ImGui::SameLine();
  if (const auto clicked = cw::ToggleGroup("Formats", toggles)) {
    constexpr auto kCommands
      = std::array { cc::kListFbx, cc::kListGlb, cc::kListGltf };
    commands_->RunBool(kCommands.at(*clicked), !toggles.at(*clicked).on);
  }
}

auto ContentImportTab::DrawModels() -> void
{
  const auto filter = TextFilter { vm_->GetImportTab().filter };
  auto counts = std::map<std::string, std::size_t> {};
  for (const auto& model : vm_->Models().Models()) {
    if (ContentModelCatalog::Matches(model, filter)) {
      ++counts[model.folder];
    }
  }
  if (cw::BeginFramedList("ModelList", kModelListHeight * cw::Unit())) {
    if (vm_->Models().Models().empty()) {
      cw::DimSmall("No model files in this mount.");
    }
    // Models are sorted by folder, so each folder's rows are together; a
    // model directly in its mount has no folder node.
    auto folder = std::optional<std::string> {};
    bool open = false;
    for (const auto& model : vm_->Models().Models()) {
      if (!ContentModelCatalog::Matches(model, filter)) {
        continue;
      }
      if (folder != model.folder) {
        if (open && folder && !folder->empty()) {
          ImGui::TreePop();
        }
        folder = model.folder;
        open = model.folder.empty()
          || cw::GroupHeader(model.folder.c_str(), FolderLabel(model.folder),
            { .count = counts.at(model.folder) });
      }
      if (open) {
        DrawModel(model);
      }
    }
    if (open && folder && !folder->empty()) {
      ImGui::TreePop();
    }
    ImGui::Dummy({ 0.0F, 4.0F * cw::Unit() });
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (4.0F * cw::Unit()));
    cw::DimSmall("One row per model file; its record is <file>.import.json in "
                 "/Scratch.");
  }
  cw::EndFramedList();
}

auto ContentImportTab::DrawModel(const ModelEntry& model) -> void
{
  const auto name = model.file.filename().string();
  const bool selected = selected_ == model.file;
  const auto id = "###" + name;
  const auto row = cw::ListRow(id.c_str(), selected);
  if (row.clicked) {
    selected_ = model.file;
  }
  cw::ItemTooltip(model.mount_path);
  const float unit = cw::Unit();
  const auto look = LookOf(model.state);
  cw::RowDot(row, kDotColumn * unit, look.color, look.filled);
  cw::RowText(row, kNameColumn * unit, name,
    { .color = selected ? cw::kWhite : ImVec4 {} });
  const auto state = std::string(to_string(model.state));
  auto state_color = selected ? cw::kWhite : cw::Dim();
  if (model.state == ModelState::kFailed) {
    state_color = cw::kRed;
  }
  cw::RowText(row, kRightInset * unit, state,
    { .color = state_color, .small = true, .right = true });
}

auto ContentImportTab::DrawSelected() -> void
{
  const auto* model = vm_->Models().Find(selected_);
  if (model == nullptr) {
    return;
  }
  ImGui::Dummy({ 0.0F, 2.0F * cw::Unit() });
  const auto header
    = fmt::format("{}###SelectedModel", model->file.filename().string());
  if (!cw::SectionHeader(header.c_str(), "import.selected")) {
    return;
  }
  const auto& view = vm_->ModelRecordOf(model->file);
  const bool has_record = view.error.empty() || !view.settings_json.empty();
  cw::PropertyRow("Record", model->record.generic_string());
  cw::PropertyRow("Source", model->mount_path);
  cw::PropertyRow("Published", PublishedText(view));
  cw::PropertyRow(
    "Scene", view.scene.empty() ? std::string("\xE2\x80\x93") : view.scene);
  if (has_record && !view.differing.empty()) {
    const auto box = cw::Callout { cw::CalloutKind::kWarning };
    const auto font = cw::SmallFont {};
    ImGui::PushStyleColor(ImGuiCol_Text, cw::kAmber);
    ImGui::TextWrapped("Panel settings differ from the record: %s",
      GroupNames(view.differing).c_str());
    ImGui::PopStyleColor();
  }
  if (!view.error.empty() && has_record) {
    ImGui::PushStyleColor(ImGuiCol_Text, cw::kRed);
    ImGui::TextWrapped("%s", view.error.c_str());
    ImGui::PopStyleColor();
  }

  ImGui::Dummy({ 0.0F, 2.0F * cw::Unit() });
  const bool busy = vm_->IsImportInProgress();
  ImGui::BeginDisabled(busy);
  const auto file = model->file.string();
  if (!has_record) {
    if (cw::PrimaryButton("Import")) {
      commands_->RunText(cc::kImport, file);
    }
    cw::ItemTooltip(busy ? "An import is running"
                         : "Import the model with the panel's settings");
  } else {
    if (cw::PrimaryButton("Reimport with panel")) {
      commands_->RunText(cc::kImport, file);
    }
    cw::ItemTooltip(busy ? "An import is running"
                         : "Import the model again with the panel's settings");
    ImGui::SameLine();
    if (ImGui::Button("Reimport as is")) {
      commands_->RunText(cc::kImportAsIs, file);
    }
    cw::ItemTooltip(busy ? "An import is running"
                         : "Import the model again with its record's settings");
  }
  ImGui::EndDisabled();
  if (!view.scene.empty()) {
    ImGui::SameLine();
    ImGui::BeginDisabled(vm_->IsSceneLoading());
    if (ImGui::Button("Load scene")) {
      commands_->Run(cc::kLoadScene, { { "scene", view.scene } });
    }
    ImGui::EndDisabled();
  }
}

auto ContentImportTab::DrawRunningImport() -> void
{
  if (!vm_->IsImportInProgress()) {
    return;
  }
  const float unit = cw::Unit();
  const auto progress = vm_->GetImportProgress();
  ImGui::Dummy({ 0.0F, 4.0F * unit });
  ImGui::Text("Importing %s", progress.model.filename().string().c_str());
  const auto step = progress.item.empty()
    ? progress.phase
    : fmt::format("{}  {}", progress.phase, progress.item);
  {
    const auto font = cw::SmallFont {};
    const auto shown = cw::Elide(step, ImGui::GetContentRegionAvail().x * 0.5F);
    cw::AlignRight(ImGui::CalcTextSize(shown.c_str()).x);
    ImGui::TextDisabled("%s", shown.c_str());
  }
  const auto font = cw::SmallFont {};
  const float cancel_width = ImGui::CalcTextSize("Cancel").x
    + (ImGui::GetStyle().FramePadding.x * 2.0F)
    + ImGui::GetStyle().ItemSpacing.x;
  ImGui::ProgressBar(progress.progress,
    { ImGui::GetContentRegionAvail().x - cancel_width, 10.0F * unit }, "");
  ImGui::SameLine();
  if (ImGui::SmallButton("Cancel##Import")) {
    commands_->Run(cc::kCancelImport);
  }
}

auto ContentImportTab::DrawSettings() -> void
{
  ImGui::Dummy({ 0.0F, 4.0F * cw::Unit() });
  const auto changed = vm_->ChangedImportGroups();
  const bool open = cw::SectionHeader(
    "Import settings###ImportSettings", "import.settings", false);
  if (!changed.empty()) {
    // The count and Reset sit at the band's right.
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    const auto after = ImGui::GetCursorScreenPos();
    const auto font = cw::SmallFont {};
    const auto count = fmt::format("{} changed  ·", changed.size());
    const float width = ImGui::CalcTextSize(count.c_str()).x
      + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Reset").x;
    ImGui::SetCursorScreenPos({ max.x - width - (8.0F * cw::Unit()),
      min.y + (((max.y - min.y) - ImGui::GetTextLineHeight()) * 0.5F) });
    ImGui::TextUnformatted(count.c_str());
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_TextLink, cw::kWhite);
    if (ImGui::TextLink("Reset##ImportReset")) {
      for (const auto group : changed) {
        commands_->RunText(cc::kResetGroup, to_string(group));
      }
    }
    ImGui::PopStyleColor();
    cw::ItemTooltip("Put every changed group back to its defaults");
    ImGui::SetCursorScreenPos(after);
  }
  if (!open) {
    return;
  }
  for (const auto group : {
         ImportSettingsGroup::kContent,
         ImportSettingsGroup::kGeometry,
         ImportSettingsGroup::kLightsAndCameras,
         ImportSettingsGroup::kTextures,
         ImportSettingsGroup::kImporter,
       }) {
    DrawGroup(group, changed);
  }
}

auto ContentImportTab::DrawGroup(const ImportSettingsGroup group,
  const std::vector<ImportSettingsGroup>& changed) -> void
{
  const auto name = std::string(to_string(group));
  if (!cw::GroupHeader(name.c_str(), name,
        {
          .hint = HintOf(group),
          .changed = std::ranges::contains(changed, group),
          .default_open = false,
        })) {
    return;
  }
  ImGui::Indent(8.0F * cw::Unit());
  switch (group) {
  case ImportSettingsGroup::kContent:
    DrawContentGroup();
    break;
  case ImportSettingsGroup::kGeometry:
    DrawGeometryGroup();
    break;
  case ImportSettingsGroup::kLightsAndCameras:
    DrawLightsGroup();
    break;
  case ImportSettingsGroup::kTextures:
    DrawTexturesGroup();
    break;
  case ImportSettingsGroup::kImporter:
    DrawImporterGroup();
    break;
  }
  ImGui::Unindent(8.0F * cw::Unit());
  ImGui::Dummy({ 0.0F, 4.0F * cw::Unit() });
  ImGui::TreePop();
}

auto ContentImportTab::DrawContentGroup() -> void
{
  const auto& options = vm_->GetImportProfile().options;
  const auto& defaults = defaults_.options;
  cw::CommandCheckbox(*commands_, cc::kImportTextures, "Textures",
    HasFlag(options, ImportContentFlags::kTextures));
  ImGui::SameLine();
  cw::CommandCheckbox(*commands_, cc::kImportMaterials, "Materials",
    HasFlag(options, ImportContentFlags::kMaterials));
  ImGui::SameLine();
  cw::CommandCheckbox(*commands_, cc::kImportGeometry, "Geometry",
    HasFlag(options, ImportContentFlags::kGeometry));
  ImGui::SameLine();
  cw::CommandCheckbox(*commands_, cc::kImportScene, "Scene",
    HasFlag(options, ImportContentFlags::kScene));
  cw::CommandCheckbox(
    *commands_, cc::kHashing, "Hash content", options.with_content_hashing);
  cw::CommandCheckbox(*commands_, cc::kIgnoreNonMesh, "Ignore points and lines",
    options.ignore_non_mesh_primitives);
  cw::ChoiceCombo(*commands_, cc::kNodePruning, "Node pruning",
    options.node_pruning, choices::kNodePruning,
    options.node_pruning != defaults.node_pruning);
}

auto ContentImportTab::DrawGeometryGroup() -> void
{
  using content::import::LodGenerationOptions;
  const auto& options = vm_->GetImportProfile().options;
  const auto& defaults = defaults_.options;
  {
    auto pair = cw::FieldPair {};
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kUnits, "Units",
      options.coordinate.unit_normalization, choices::kUnits,
      options.coordinate.unit_normalization
        != defaults.coordinate.unit_normalization);
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kUpAxis, "Up axis",
      options.coordinate.up_axis, choices::kUpAxes,
      options.coordinate.up_axis != defaults.coordinate.up_axis);
  }
  if (options.coordinate.unit_normalization
    == content::import::UnitNormalizationPolicy::kApplyCustomFactor) {
    cw::CommandDragFloat(*commands_, cc::kUnitScale, "Unit scale",
      options.coordinate.unit_scale, 0.1F, 0.001F, 1000.0F,
      options.coordinate.unit_scale != defaults.coordinate.unit_scale);
  }
  {
    auto pair = cw::FieldPair {};
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kNormalPolicy, "Normals",
      options.normal_policy, choices::kGeometryPolicies,
      options.normal_policy != defaults.normal_policy);
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kTangentPolicy, "Tangents",
      options.tangent_policy, choices::kGeometryPolicies,
      options.tangent_policy != defaults.tangent_policy);
  }
  cw::CommandDragFloat(*commands_, cc::kSmoothingAngle,
    "Smoothing angle (degrees)", options.normals_smoothing_angle_deg, 1.0F,
    0.0F, 180.0F,
    options.normals_smoothing_angle_deg
      != defaults.normals_smoothing_angle_deg);
  cw::CommandCheckbox(*commands_, cc::kGenerateLods, "Generate LODs",
    options.lod_generation.enabled);
  if (options.lod_generation.enabled) {
    auto pair = cw::FieldPair {};
    pair.Next();
    cw::CommandDragInt(*commands_, cc::kLodLevels, "LOD levels",
      static_cast<int>(options.lod_generation.levels), 0.1F,
      static_cast<int>(LodGenerationOptions::kMinLevels),
      static_cast<int>(LodGenerationOptions::kMaxLevels),
      options.lod_generation.levels != defaults.lod_generation.levels);
    pair.Next();
    cw::CommandDragFloat(*commands_, cc::kLodReduction, "Reduction per LOD (%)",
      options.lod_generation.reduction_percent, 1.0F,
      LodGenerationOptions::kMinReductionPercent,
      LodGenerationOptions::kMaxReductionPercent,
      options.lod_generation.reduction_percent
        != defaults.lod_generation.reduction_percent);
  }
}

auto ContentImportTab::DrawLightsGroup() -> void
{
  const auto& options = vm_->GetImportProfile().options;
  cw::CommandCheckbox(
    *commands_, cc::kImportLights, "Import lights", options.import_lights);
  ImGui::SameLine();
  cw::CommandCheckbox(
    *commands_, cc::kImportCameras, "Import cameras", options.import_cameras);
  cw::CommandDragFloat(*commands_, cc::kLightScale, "Light intensity scale",
    options.light_intensity_scale, 0.05F, 0.0F, 1000.0F,
    options.light_intensity_scale != defaults_.options.light_intensity_scale);
}

auto ContentImportTab::DrawTexturesGroup() -> void
{
  const auto& tuning = vm_->GetImportProfile().tuning;
  const auto& defaults = defaults_.tuning;
  // The size limit applies whether or not tuning is enabled.
  cw::ChoiceCombo(*commands_, cc::kMaxSize, "Max size", tuning.max_size,
    choices::kMaxSizes, tuning.max_size != defaults.max_size);
  cw::CommandCheckbox(*commands_, cc::kTuning, "Tune textures", tuning.enabled);
  {
    auto pair = cw::FieldPair {};
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kIntent, "Intent", tuning.intent,
      choices::kIntents, tuning.intent != defaults.intent);
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kColorSpace, "Color space",
      tuning.source_color_space, choices::kColorSpaces,
      tuning.source_color_space != defaults.source_color_space);
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kMipPolicy, "Mips", tuning.mip_policy,
      choices::kMipPolicies, tuning.mip_policy != defaults.mip_policy);
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kBc7Quality, "BC7 quality",
      tuning.bc7_quality, choices::kBc7Qualities,
      tuning.bc7_quality != defaults.bc7_quality);
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kMipFilter, "Mip filter", tuning.mip_filter,
      choices::kMipFilters, tuning.mip_filter != defaults.mip_filter);
    pair.Next();
    if (tuning.mip_policy == content::import::MipPolicy::kMaxCount) {
      cw::CommandDragInt(*commands_, cc::kMaxMips, "Max mip levels",
        tuning.max_mip_levels, 0.1F, 1, 16,
        tuning.max_mip_levels != defaults.max_mip_levels);
    }
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kColorFormat, "Color format",
      tuning.color_output_format, choices::kFormats,
      tuning.color_output_format != defaults.color_output_format);
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kDataFormat, "Data format",
      tuning.data_output_format, choices::kFormats,
      tuning.data_output_format != defaults.data_output_format);
  }
  cw::ChoiceCombo(*commands_, cc::kHdrHandling, "HDR handling",
    tuning.hdr_handling, choices::kHdrHandlings,
    tuning.hdr_handling != defaults.hdr_handling);
  if (tuning.hdr_handling != content::import::HdrHandling::kKeepFloat) {
    cw::CommandCheckbox(
      *commands_, cc::kBakeHdr, "Bake HDR to LDR", tuning.bake_hdr_to_ldr);
    if (tuning.bake_hdr_to_ldr) {
      cw::CommandDragFloat(*commands_, cc::kExposureEv, "Exposure (EV)",
        tuning.exposure_ev, 0.1F, -10.0F, 10.0F,
        tuning.exposure_ev != defaults.exposure_ev);
    }
  }
  cw::CommandCheckbox(*commands_, cc::kFlipGreen, "Flip normal map green",
    tuning.flip_normal_green);
  cw::CommandCheckbox(*commands_, cc::kRenormalize, "Renormalize normal mips",
    tuning.renormalize_normals_in_mips);
  cw::CommandCheckbox(
    *commands_, cc::kCubemap, "Import as cubemap", tuning.import_cubemap);
  if (tuning.import_cubemap) {
    cw::CommandCheckbox(*commands_, cc::kEquirect, "Equirect to cubemap",
      tuning.equirect_to_cubemap);
    auto pair = cw::FieldPair {};
    if (tuning.equirect_to_cubemap) {
      pair.Next();
      cw::CommandDragInt(*commands_, cc::kFaceSize, "Face size",
        static_cast<int>(tuning.cubemap_face_size), 256.0F, 0, 8192,
        tuning.cubemap_face_size != defaults.cubemap_face_size);
    }
    pair.Next();
    cw::ChoiceCombo(*commands_, cc::kCubeLayout, "Cube layout",
      tuning.cubemap_layout, choices::kCubeLayouts,
      tuning.cubemap_layout != defaults.cubemap_layout);
  }
}

auto ContentImportTab::DrawImporterGroup() -> void
{
  const auto& importer = vm_->GetImportProfile().importer;
  const auto& defaults = defaults_.importer;
  cw::CommandDragInt(*commands_, cc::kThreadPool, "Thread pool",
    static_cast<int>(importer.thread_pool_size), 0.1F, 1, 128,
    importer.thread_pool_size != defaults.thread_pool_size);
  for (const auto& pipeline : kImporterPipelines) {
    const auto& concurrency = importer.concurrency.*pipeline.member;
    const auto& default_concurrency = defaults.concurrency.*pipeline.member;
    const auto prefix
      = std::string(cc::kPipelinePrefix) + std::string(pipeline.name);
    const auto label = std::string(pipeline.label);
    ImGui::PushID(label.c_str());
    auto pair = cw::FieldPair {};
    pair.Next();
    const auto workers_label = label + " workers";
    cw::CommandDragInt(*commands_, prefix + ".workers", workers_label.c_str(),
      static_cast<int>(concurrency.workers), 0.1F, 1, 64,
      concurrency.workers != default_concurrency.workers);
    pair.Next();
    const auto queue_label = label + " queue";
    cw::CommandDragInt(*commands_, prefix + ".queue", queue_label.c_str(),
      static_cast<int>(concurrency.queue_capacity), 1.0F, 1, 256,
      concurrency.queue_capacity != default_concurrency.queue_capacity);
    ImGui::PopID();
  }
  if (vm_->IsImporterRestartPending()) {
    ImGui::PushStyleColor(ImGuiCol_Text, cw::kAmber);
    ImGui::TextWrapped("Changes take effect when the importer restarts.");
    ImGui::PopStyleColor();
    ImGui::BeginDisabled(vm_->IsImportInProgress());
    if (ImGui::Button("Restart importer")) {
      commands_->Run(cc::kRestartImporter);
    }
    ImGui::EndDisabled();
  }
}

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
