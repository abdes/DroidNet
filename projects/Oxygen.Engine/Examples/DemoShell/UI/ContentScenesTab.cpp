//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/UI/ContentMountList.h"
#include "DemoShell/UI/ContentPanelCommands.h"
#include "DemoShell/UI/ContentPanelWidgets.h"
#include "DemoShell/UI/ContentSceneCatalog.h"
#include "DemoShell/UI/ContentScenesTab.h"
#include "DemoShell/UI/ContentTextMatch.h"
#include "DemoShell/UI/ContentVm.h"
#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/AssetKey.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg) - ImGui

namespace oxygen::examples::ui {

namespace {
  namespace cc = content_commands;
  namespace cw = content_widgets;

  // Columns from the list's left edge, whatever the tree indents.
  //! Where a recent scene's name and its loaded dot are.
  constexpr auto kRecentNameColumn = 16.0F;
  constexpr auto kRecentDotColumn = 8.0F;
  //! Where a recent scene's virtual path starts, unless a name is wider.
  constexpr auto kRecentPathColumn = 122.0F;
  //! Where a scene's name starts; its loaded dot sits before.
  constexpr auto kNameColumn = 44.0F;
  constexpr auto kDotColumn = 32.0F;
  constexpr auto kColumnGap = 12.0F;
  constexpr auto kRightInset = 6.0F;

  //! The height kept below the scene list for the action bar.
  auto ActionBarHeight() -> float
  {
    const auto& style = ImGui::GetStyle();
    return ImGui::GetFrameHeight() + (style.ItemSpacing.y * 3.0F);
  }

  //! The number of scenes in each folder of `group` that `filter` admits.
  auto CountByFolder(const SceneGroup& group, const TextFilter& filter)
    -> std::map<std::string, std::size_t>
  {
    auto counts = std::map<std::string, std::size_t> {};
    for (const auto& scene : group.scenes) {
      if (ContentSceneCatalog::Matches(scene, filter)) {
        ++counts[scene.folder];
      }
    }
    return counts;
  }
} // namespace

ContentScenesTab::ContentScenesTab(
  const observer_ptr<ContentVm> vm, const observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
{
  DCHECK_NOTNULL_F(vm_);
  DCHECK_NOTNULL_F(commands_);
}

auto ContentScenesTab::Draw() -> void
{
  DrawFilters();
  DrawRecent();
  const auto filter = TextFilter { search_ };
  if (cw::BeginScrollList("SceneList", -ActionBarHeight())) {
    if (vm_->Scenes().Groups().empty()) {
      cw::DimSmall("No mounted source holds a scene. Add one on the Mounts "
                   "tab.");
    }
    for (const auto& group : vm_->Scenes().Groups()) {
      if (mount_.empty() || mount_ == group.mount) {
        DrawGroup(group, filter);
      }
    }
  }
  cw::EndScrollArea();
  DrawActions();
}

auto ContentScenesTab::DrawFilters() -> void
{
  static_cast<void>(cw::SearchField(
    "##SceneSearch", "Search scenes by name or virtual path", search_));
  const auto& scenes = vm_->Scenes();
  const auto all = fmt::format("All  {}##AllScenes", scenes.Count());
  if (cw::Chip(all.c_str(), mount_.empty())) {
    mount_.clear();
  }
  for (const auto& group : scenes.Groups()) {
    ImGui::SameLine(0.0F, 4.0F * cw::Unit());
    const auto chip
      = fmt::format("{}  {}##chip", group.mount.substr(1), group.scenes.size());
    // Pushed without its slash, so a test path can name the group's rows.
    const auto group_id = group.mount.substr(1);
    ImGui::PushID(group_id.c_str());
    if (cw::Chip(chip.c_str(), mount_ == group.mount)) {
      mount_ = group.mount;
    }
    ImGui::PopID();
  }
  constexpr auto kChipNote = "mounts that hold scenes";
  const auto font = cw::SmallFont {};
  ImGui::SameLine(0.0F, 8.0F * cw::Unit());
  if (ImGui::GetContentRegionAvail().x >= ImGui::CalcTextSize(kChipNote).x) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", kChipNote);
  } else {
    ImGui::NewLine();
  }
}

auto ContentScenesTab::DrawRecent() -> void
{
  const auto recent = vm_->RecentScenes();
  if (recent.empty() || !cw::GroupHeader("##Recent", "Recent", {})) {
    return;
  }
  const float unit = cw::Unit();
  const auto name_of = [](const RecentSceneSetting& scene) -> std::string {
    return std::filesystem::path(scene.virtual_path).stem().string();
  };
  // The paths start after the widest name, within half the list.
  float path_column = kRecentPathColumn * unit;
  for (const auto& scene : recent) {
    const auto name = name_of(scene);
    path_column = std::max(path_column,
      ((kRecentNameColumn + kColumnGap) * unit)
        + ImGui::CalcTextSize(name.c_str()).x);
  }
  path_column = std::min(path_column, ImGui::GetContentRegionAvail().x * 0.5F);
  const auto& loaded = vm_->GetLoadedScene();
  for (const auto& scene : recent) {
    const auto id = fmt::format("##recent{}", scene.scene_key);
    const bool selected
      = selected_in_recent_ && selected_path_ == scene.virtual_path;
    const auto row = cw::ListRow(id.c_str(), selected);
    if (row.clicked) {
      selected_path_ = scene.virtual_path;
      selected_in_recent_ = true;
    }
    if (row.double_clicked) {
      commands_->Run(cc::kLoadScene, { { "scene", scene.virtual_path } });
    }
    if (loaded && loaded->scene.virtual_path == scene.virtual_path) {
      cw::RowDot(row, (kRecentDotColumn * unit) - row.indent, cw::kGreen);
    }
    const float name_x = (kRecentNameColumn * unit) - row.indent;
    const float path_x = path_column - row.indent;
    cw::RowText(row, name_x,
      cw::Elide(name_of(scene), path_x - name_x - (kColumnGap * unit)),
      { .color = selected ? cw::kWhite : ImVec4 {} });
    cw::RowText(row, path_x,
      cw::Elide(scene.virtual_path, row.size.x - path_x - (kRightInset * unit)),
      { .color = selected ? cw::kWhite : cw::Dim(), .small = true });
    cw::ItemTooltip(scene.virtual_path);
  }
  ImGui::TreePop();
}

auto ContentScenesTab::DrawGroup(
  const SceneGroup& group, const TextFilter& filter) -> void
{
  // Pushed without its slash, so a test path can name the group's rows.
  const auto group_id = group.mount.substr(1);
  ImGui::PushID(group_id.c_str());
  auto kind = std::string_view { "loose root" };
  if (group.from_pak) {
    kind = group.from_loose_root ? "pak and loose root" : "pak";
  }
  const auto count = group.scenes.size();
  const auto note
    = fmt::format("{}  ·  {} {}", kind, count, count == 1 ? "scene" : "scenes");
  const auto header = fmt::format("{}###group", group.mount);
  if (cw::SectionHeader(
        header.c_str(), "scenes.mount" + group.mount, true, note)) {
    const auto counts = CountByFolder(group, filter);
    // Scenes are sorted by virtual path, so each folder's rows are together;
    // a scene directly in its mount has no folder node.
    auto folder = std::optional<std::string> {};
    bool open = false;
    for (const auto& scene : group.scenes) {
      if (!ContentSceneCatalog::Matches(scene, filter)) {
        continue;
      }
      if (folder != scene.folder) {
        if (open && folder && !folder->empty()) {
          ImGui::TreePop();
        }
        folder = scene.folder;
        open = scene.folder.empty()
          || cw::GroupHeader(scene.folder.c_str(), scene.folder,
            { .count = counts.at(scene.folder) });
      }
      if (open) {
        DrawScene(scene);
      }
    }
    if (open && folder && !folder->empty()) {
      ImGui::TreePop();
    }
  }
  ImGui::PopID();
}

auto ContentScenesTab::DrawScene(const SceneEntry& scene) -> void
{
  const auto& loaded = vm_->GetLoadedScene();
  const bool is_loaded = loaded && loaded->scene.key == scene.key;
  const bool selected
    = !selected_in_recent_ && selected_path_ == scene.virtual_path;
  // The ID is the key alone: a label's slashes would split the item path.
  const auto id = fmt::format("###{}", data::to_string(scene.key));
  const auto row = cw::ListRow(id.c_str(), selected);
  if (row.clicked) {
    selected_path_ = scene.virtual_path;
    selected_in_recent_ = false;
  }
  if (row.double_clicked) {
    Load(scene);
  }
  const float unit = cw::Unit();
  const auto& mounts = vm_->Mounts();
  const auto source = mounts.IndexOf(scene.source_path);
  if (is_loaded) {
    cw::RowDot(row, (kDotColumn * unit) - row.indent, cw::kGreen);
  }
  if (source
    && mounts.Rows().at(*source).origin == SourceOrigin::kImportRecord) {
    cw::RowText(row, kRightInset * unit, "from its record",
      {
        .color = selected ? cw::kWhite : cw::Dim(),
        .small = true,
        .right = true,
      });
  }
  cw::RowText(row, (kNameColumn * unit) - row.indent, scene.label,
    { .color = selected ? cw::kWhite : ImVec4 {} });
  if (row.hovered) {
    DrawTooltip(scene);
  }
}

auto ContentScenesTab::DrawTooltip(const SceneEntry& scene) const -> void
{
  if (!ImGui::BeginTooltip()) {
    return;
  }
  {
    // Popped before EndTooltip: the font stack belongs to the tooltip window.
    const auto font = cw::SmallFont {};
    ImGui::TextUnformatted(scene.name.c_str());
    ImGui::TextDisabled("%s", scene.virtual_path.c_str());
    const auto source = SourceNameOf(scene.source_path);
    const auto index = vm_->Mounts().IndexOf(scene.source_path);
    const auto from = index
      ? fmt::format("From {} (#{}). Double-click loads.", source,
          vm_->Mounts().NumberOf(*index))
      : fmt::format("From {}. Double-click loads.", source);
    ImGui::TextDisabled("%s", from.c_str());
  }
  ImGui::EndTooltip();
}

auto ContentScenesTab::DrawActions() -> void
{
  const auto* selected = static_cast<const SceneEntry*>(nullptr);
  for (const auto& group : vm_->Scenes().Groups()) {
    for (const auto& scene : group.scenes) {
      if (scene.virtual_path == selected_path_) {
        selected = &scene;
      }
    }
  }
  const bool loading = vm_->IsSceneLoading();
  constexpr auto kLoading = "A scene is loading";
  ImGui::Separator();
  ImGui::BeginDisabled(loading || selected == nullptr);
  if (cw::PrimaryButton("Load") && selected != nullptr) {
    Load(*selected);
  }
  ImGui::EndDisabled();
  cw::ItemTooltip(loading ? kLoading : "Load the selected scene");
  ImGui::SameLine();
  ImGui::BeginDisabled(loading || !vm_->GetLoadedScene());
  if (ImGui::Button("Reload")) {
    commands_->Run(cc::kReloadScene);
  }
  ImGui::SameLine();
  if (ImGui::Button("Unload")) {
    commands_->Run(cc::kUnloadScene);
  }
  ImGui::EndDisabled();
}

auto ContentScenesTab::Load(const SceneEntry& scene) const -> void
{
  commands_->Run(cc::kLoadScene, { { "scene", scene.virtual_path } });
}

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
