//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/Services/ShowInFolder.h"
#include "DemoShell/UI/ContentLog.h"
#include "DemoShell/UI/ContentMountList.h"
#include "DemoShell/UI/ContentMountsTab.h"
#include "DemoShell/UI/ContentPanelCommands.h"
#include "DemoShell/UI/ContentPanelNavigation.h"
#include "DemoShell/UI/ContentPanelWidgets.h"
#include "DemoShell/UI/ContentTextMatch.h"
#include "DemoShell/UI/ContentVm.h"
#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise) -
// ImGui varargs and int flag enumerators

namespace oxygen::examples::ui {

namespace {
  namespace cc = content_commands;
  namespace cw = content_widgets;

  constexpr auto kRowPayload = "CONTENT_SOURCE_ROW";
  //! The rows the table shows before it scrolls.
  constexpr auto kVisibleRows = 6.0F;
  constexpr auto kRowHeight = 26.0F;

  auto StateColor(const SourceState state) -> ImVec4
  {
    switch (state) {
    case SourceState::kMounted:
      return cw::kGreen;
    case SourceState::kRefused:
    case SourceState::kUnreadable:
      return cw::kRed;
    case SourceState::kMissing:
      return cw::kAmber;
    case SourceState::kPending:
      break;
    }
    return cw::Dim();
  }

  auto FailureText(const content::RequirementFailure failure)
    -> std::string_view
  {
    switch (failure) {
    case content::RequirementFailure::kMissing:
      return "missing";
    case content::RequirementFailure::kTombstoned:
      return "tombstoned";
    case content::RequirementFailure::kTypeMismatch:
      return "wrong type";
    case content::RequirementFailure::kMountMismatch:
      return "wrong mount";
    }
    return "not met";
  }

  auto TypeText(const data::PakRequirement& requirement) -> std::string
  {
    return requirement.kind == data::KeyReferenceKind::kAsset
      ? std::string(data::to_string(requirement.type))
      : std::string("Texture");
  }

  //! `1 asset`, `2 assets`.
  auto Plural(const std::size_t count, const std::string_view noun)
    -> std::string
  {
    return fmt::format("{} {}{}", count, noun, count == 1 ? "" : "s");
  }

  auto RequiresText(const content::cooked::CookedSummary& summary)
    -> std::string
  {
    if (summary.requirements.empty()) {
      return "nothing";
    }
    auto text = std::string {};
    for (const auto& dependency : summary.requirements) {
      if (!text.empty()) {
        text += ", ";
      }
      text += fmt::format("{} from /{}",
        Plural(dependency.requirements.size(), "asset"), dependency.mount);
    }
    return text;
  }

  auto StateCodeOf(const SourceState state) -> std::string_view
  {
    switch (state) {
    case SourceState::kRefused:
      return log_codes::kMountRefused;
    case SourceState::kMissing:
      return log_codes::kMountSourceMissing;
    case SourceState::kUnreadable:
      return log_codes::kMountSourceUnreadable;
    case SourceState::kMounted:
    case SourceState::kPending:
      break;
    }
    return {};
  }

  auto OriginText(const SourceOrigin origin) -> std::string_view
  {
    switch (origin) {
    case SourceOrigin::kBuiltIn:
      return "Built in";
    case SourceOrigin::kImportRecord:
      return "Published by an import record";
    case SourceOrigin::kAdded:
      break;
    }
    return "Added on this tab";
  }

  //! Text vertically centered in a table row of `height`.
  auto CenterInRow(const float height) -> void
  {
    ImGui::SetCursorPosY(
      ImGui::GetCursorPosY() + ((height - ImGui::GetTextLineHeight()) * 0.5F));
  }
} // namespace

ContentMountsTab::ContentMountsTab(const observer_ptr<ContentVm> vm,
  const observer_ptr<PanelCommands> commands,
  const observer_ptr<ContentPanelNavigation> navigation)
  : vm_(vm)
  , commands_(commands)
  , navigation_(navigation)
{
  DCHECK_NOTNULL_F(vm_);
  DCHECK_NOTNULL_F(commands_);
  DCHECK_NOTNULL_F(navigation_);
}

auto ContentMountsTab::Draw() -> void
{
  if (navigation_->select_source) {
    selected_ = vm_->Mounts().IndexOf(*navigation_->select_source);
    navigation_->select_source.reset();
  }
  const auto& rows = vm_->Mounts().Rows();
  if (selected_ && *selected_ >= rows.size()) {
    selected_.reset();
  }
  DrawToolbar();
  DrawTable();
  if (selected_) {
    const auto index = *selected_;
    DrawDetails(rows.at(index), index);
  }
  DrawSourceFolders();
}

auto ContentMountsTab::DrawToolbar() -> void
{
  if (ImGui::Button("+ Pak\xE2\x80\xA6")) {
    vm_->BrowseForPak();
  }
  cw::ItemTooltip("Add a pak above the other sources");
  ImGui::SameLine();
  if (ImGui::Button("+ Cooked root\xE2\x80\xA6")) {
    vm_->BrowseForCookedRoot();
  }
  cw::ItemTooltip("Add a loose cooked root above the other sources");
  ImGui::SameLine();
  const bool busy = vm_->IsMounting() || vm_->IsSceneLoading();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Remount")) {
    commands_->Run(cc::kRemount);
  }
  ImGui::EndDisabled();
  cw::ItemTooltip(busy ? "Wait until the mounts or the scene load finish"
                       : "Apply the mount list again");
  ImGui::SameLine();
  if (ImGui::Button("Trim caches")) {
    commands_->Run(cc::kTrimCache);
  }
  cw::ItemTooltip("Trim the engine's asset caches without unmounting sources "
                  "or changing the scene");
  ImGui::Dummy({ 0.0F, 2.0F * cw::Unit() });
  const auto font = cw::SmallFont {};
  ImGui::PushStyleColor(ImGuiCol_Text, cw::Dim());
  ImGui::TextWrapped("The top row wins. Drag a row to change its precedence; "
                     "Engine stays last.");
  ImGui::PopStyleColor();
}

auto ContentMountsTab::DrawTable() -> void
{
  const auto& rows = vm_->Mounts().Rows();
  const float unit = cw::Unit();
  const float row_height = kRowHeight * unit;
  const float header_height = 22.0F * unit;
  const float visible
    = std::clamp(static_cast<float>(rows.size()), 1.0F, kVisibleRows);
  const float height = header_height + (row_height * visible) + (4.0F * unit);
  if (!cw::BeginFramedList("MountList", height)) {
    cw::EndFramedList();
    return;
  }
  constexpr auto kFlags
    = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX;
  if (ImGui::BeginTable("MountTable", 3, kFlags)) {
    ImGui::TableSetupColumn(
      "Priority", ImGuiTableColumnFlags_WidthFixed, 52.0F * unit);
    ImGui::TableSetupColumn("Mount", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(
      "State", ImGuiTableColumnFlags_WidthFixed, 84.0F * unit);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, header_height);
    for (const auto* name : { "Priority", "Mount", "State" }) {
      ImGui::TableNextColumn();
      CenterInRow(header_height);
      cw::DimSmall(name);
    }
    const auto line_y = ImGui::GetCursorScreenPos().y;
    const auto window = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine({ window.x, line_y },
      { window.x + ImGui::GetWindowWidth(), line_y },
      ImGui::GetColorU32(ImGuiCol_Border));
    for (std::size_t index = 0; index < rows.size(); ++index) {
      DrawRow(rows.at(index), index);
    }
    ImGui::EndTable();
  }
  cw::EndFramedList();
}

auto ContentMountsTab::DrawRow(const CookedSourceRow& row, std::size_t index)
  -> void
{
  const float unit = cw::Unit();
  const float row_height = kRowHeight * unit;
  const bool selected = selected_ == index;
  const auto text_color
    = selected ? cw::kWhite : ImGui::GetStyle().Colors[ImGuiCol_Text];
  ImGui::PushID(static_cast<int>(index));
  ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
  ImGui::TableNextColumn();
  const auto row_top = ImGui::GetCursorPosY();
  if (ImGui::Selectable("##row", selected,
        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
        { 0.0F, row_height - (ImGui::GetStyle().CellPadding.y * 2.0F) })) {
    selected_ = index;
  }
  if (row.origin == SourceOrigin::kAdded) {
    if (ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload(kRowPayload, &index, sizeof(index));
      ImGui::Text("Move %s", SourceNameOf(row.path).c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const auto* payload = ImGui::AcceptDragDropPayload(kRowPayload)) {
        const auto from = *static_cast<const std::size_t*>(payload->Data);
        commands_->Run(cc::kMoveSource, { { "from", from }, { "to", index } });
      }
      ImGui::EndDragDropTarget();
    }
  }
  if (row.origin != SourceOrigin::kBuiltIn
    && ImGui::BeginPopupContextItem("RowMenu")) {
    if (ImGui::MenuItem("Remove")) {
      commands_->RunInt(cc::kRemoveSource, static_cast<std::int64_t>(index));
    }
    ImGui::EndPopup();
  }
  cw::ItemTooltip(row.path.generic_string());
  ImGui::SameLine(0.0F, 0.0F);
  ImGui::SetCursorPosY(row_top);
  CenterInRow(row_height - (ImGui::GetStyle().CellPadding.y * 2.0F));
  ImGui::TextColored(text_color, "%zu", vm_->Mounts().NumberOf(index));

  ImGui::TableNextColumn();
  CenterInRow(row_height - (ImGui::GetStyle().CellPadding.y * 2.0F));
  const auto mount = MountNameOf(row);
  const auto mount_name = mount.starts_with('/') ? mount.substr(1) : mount;
  ImGui::TextColored(text_color, "%s",
    cw::Elide(mount_name, ImGui::GetContentRegionAvail().x).c_str());

  ImGui::TableNextColumn();
  CenterInRow(row_height - (ImGui::GetStyle().CellPadding.y * 2.0F));
  cw::Dot(StateColor(row.state));
  ImGui::SameLine(0.0F, 2.0F * unit);
  const auto state = std::string(to_string(row.state));
  ImGui::TextColored(text_color, "%s", state.c_str());
  ImGui::PopID();
}

auto ContentMountsTab::DrawDetails(
  const CookedSourceRow& row, const std::size_t index) -> void
{
  ImGui::Dummy({ 0.0F, 4.0F * cw::Unit() });
  const auto header = fmt::format("{}###SourceDetails", SourceNameOf(row.path));
  if (!cw::SectionHeader(header.c_str(), "mounts.details")) {
    return;
  }
  auto error = std::string {};
  const auto* details
    = row.summary ? vm_->SourceDetailsOf(index, error) : nullptr;
  auto kind = std::string(
    row.kind == data::CookedSourceKind::kPak ? "Pak" : "Loose cooked root");
  if (details != nullptr && details->content_version) {
    kind += fmt::format(", content version {}", *details->content_version);
  }
  cw::PropertyRow("Kind", kind);
  cw::PropertyRow("Origin", OriginText(row.origin));
  cw::PropertyRow("Path", row.path.generic_string());
  if (row.origin == SourceOrigin::kImportRecord) {
    cw::PropertyRow("Record", row.import_record.generic_string());
  }
  if (const auto& summary = row.summary) {
    cw::PropertyRow("Source key", data::to_string(summary->source_key));
    cw::PropertyRow("Provides",
      fmt::format("{}, {}", Plural(summary->assets.size(), "asset"),
        Plural(summary->textures.size(), "texture")));
    cw::PropertyRow("Deletes",
      summary->deleted.empty() ? std::string("nothing")
                               : Plural(summary->deleted.size(), "asset"));
    cw::PropertyRow("Requires", RequiresText(*summary));
  }
  DrawFindings(row, index);
  DrawActions(row, index);
  DrawAssets(index);
}

auto ContentMountsTab::DrawFindings(
  const CookedSourceRow& row, const std::size_t index) -> void
{
  const bool problem = row.state == SourceState::kRefused
    || row.state == SourceState::kMissing
    || row.state == SourceState::kUnreadable;
  if (!problem) {
    if (!row.error.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, cw::kAmber);
      ImGui::TextWrapped("%s", row.error.c_str());
      ImGui::PopStyleColor();
    }
    return;
  }
  ImGui::Dummy({ 0.0F, 2.0F * cw::Unit() });
  const auto box = cw::Callout { cw::CalloutKind::kError };
  cw::Dot(cw::kRed);
  ImGui::SameLine(0.0F, 4.0F * cw::Unit());
  if (row.state == SourceState::kRefused) {
    ImGui::TextUnformatted(fmt::format("Not mounted: {} not met",
      row.failures.size() == 1
        ? std::string("1 requirement is")
        : fmt::format("{} requirements are", row.failures.size()))
        .c_str());
  } else {
    ImGui::TextWrapped("Not mounted: %s", row.error.c_str());
  }
  const auto code = std::string(StateCodeOf(row.state));
  ImGui::TextColored(cw::kErrorCode, "%s", code.c_str());
  if (row.state == SourceState::kRefused) {
    DrawRequirements(row, index);
  }
}

auto ContentMountsTab::DrawRequirements(
  const CookedSourceRow& row, const std::size_t index) -> void
{
  const auto& mounts = vm_->Mounts();
  cw::DimSmall(
    fmt::format("Checked against the sources below #{} when mounting:",
      mounts.NumberOf(index)));
  // The virtual path of a key, from the sources below the pak.
  const auto path_of = [this, index](const data::AssetKey& key) -> std::string {
    auto error = std::string {};
    for (auto below = index + 1; below < vm_->Mounts().Rows().size(); ++below) {
      if (const auto* details = vm_->SourceDetailsOf(below, error)) {
        const auto found
          = std::ranges::find(details->assets, key, &SourceAsset::key);
        if (found != details->assets.end() && !found->virtual_path.empty()) {
          return found->virtual_path;
        }
      }
    }
    return {};
  };
  const auto draw = [&path_of](const std::string& mount,
                      const data::PakRequirement& requirement,
                      const content::FailedRequirement* failure) -> void {
    const float unit = cw::Unit();
    ImGui::Indent(10.0F * unit);
    {
      const auto font = cw::SmallFont {};
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(failure != nullptr ? cw::kRed : cw::kGreen, "%s",
        failure != nullptr ? ICON_FA_XMARK : ICON_FA_CHECK);
    }
    ImGui::SameLine(0.0F, 6.0F * unit);
    auto path = path_of(requirement.key);
    if (path.empty()) {
      path = fmt::format("/{}  {}", mount, data::to_string(requirement.key));
    }
    const auto status = fmt::format("{}, {}", TypeText(requirement),
      failure != nullptr ? FailureText(failure->failure)
                         : std::string_view { "found" });
    const auto font = cw::SmallFont {};
    const float status_width = ImGui::CalcTextSize(status.c_str()).x;
    const auto shown = cw::Elide(
      path, ImGui::GetContentRegionAvail().x - status_width - (8.0F * unit));
    ImGui::AlignTextToFramePadding();
    if (failure != nullptr) {
      ImGui::TextUnformatted(shown.c_str());
    } else {
      ImGui::TextDisabled("%s", shown.c_str());
    }
    cw::ItemTooltip(path);
    cw::AlignRight(status_width);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::Unindent(10.0F * unit);
  };
  const auto failure_of
    = [&row](const std::string& mount, const data::PakRequirement& requirement)
    -> const content::FailedRequirement* {
    const auto found = std::ranges::find_if(
      row.failures, [&](const content::FailedRequirement& failure) -> bool {
        return failure.mount == mount && failure.requirement == requirement;
      });
    return found == row.failures.end() ? nullptr : &*found;
  };
  if (!row.summary) {
    for (const auto& failure : row.failures) {
      draw(failure.mount, failure.requirement, &failure);
    }
    return;
  }
  // The failed requirements first, then those the sources below meet.
  for (const bool failed : { true, false }) {
    for (const auto& dependency : row.summary->requirements) {
      for (const auto& requirement : dependency.requirements) {
        const auto* failure = failure_of(dependency.mount, requirement);
        if ((failure != nullptr) == failed) {
          draw(dependency.mount, requirement, failure);
        }
      }
    }
  }
}

auto ContentMountsTab::DrawActions(
  const CookedSourceRow& row, const std::size_t index) -> void
{
  ImGui::Dummy({ 0.0F, 2.0F * cw::Unit() });
  if (ImGui::Button("Show in folder")) {
    ShowInFolder(row.path);
  }
  cw::ItemTooltip(row.path.generic_string());
  if (row.state != SourceState::kMounted) {
    ImGui::SameLine();
    ImGui::BeginDisabled(vm_->IsMounting());
    if (ImGui::Button("Retry")) {
      commands_->Run(cc::kRemount);
    }
    ImGui::EndDisabled();
    cw::ItemTooltip("Apply the mount list again");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(row.origin == SourceOrigin::kBuiltIn);
  if (ImGui::Button("Remove")) {
    commands_->RunInt(cc::kRemoveSource, static_cast<std::int64_t>(index));
  }
  ImGui::EndDisabled();
  cw::ItemTooltip(row.origin == SourceOrigin::kBuiltIn
      ? "The engine pak stays beneath every source"
      : "Remove the source from the mount list");
  ImGui::Dummy({ 0.0F, 4.0F * cw::Unit() });
}

auto ContentMountsTab::DrawAssets(const std::size_t index) -> void
{
  auto error = std::string {};
  const auto& row = vm_->Mounts().Rows().at(index);
  const auto count = row.summary ? row.summary->assets.size() : 0U;
  const auto header
    = fmt::format("Assets of this source  ({})###SourceAssets", count);
  if (!cw::SectionHeader(header.c_str(), "mounts.assets", false)) {
    return;
  }
  const auto* details = vm_->SourceDetailsOf(index, error);
  if (details == nullptr) {
    ImGui::TextColored(cw::kRed, "%s", error.c_str());
    return;
  }
  static_cast<void>(
    cw::SearchField("##AssetSearch", "Search virtual paths", asset_search_));
  const auto filter = TextFilter { asset_search_ };
  const float unit = cw::Unit();
  // As tall as its rows: the tab scrolls, not the list.
  if (cw::BeginFramedBlock("AssetList")) {
    for (const auto& asset : details->assets) {
      if (!filter.Matches(asset.virtual_path)) {
        continue;
      }
      const auto type = std::string(data::to_string(asset.type));
      const auto font = cw::SmallFont {};
      const float type_width = ImGui::CalcTextSize(type.c_str()).x;
      const auto path = cw::Elide(asset.virtual_path,
        ImGui::GetContentRegionAvail().x - type_width - (12.0F * unit));
      ImGui::TextUnformatted(path.c_str());
      cw::ItemTooltip(asset.virtual_path);
      cw::AlignRight(type_width);
      ImGui::TextDisabled("%s", type.c_str());
    }
  }
  cw::EndFramedBlock();
}

auto ContentMountsTab::DrawSourceFolders() -> void
{
  const float unit = cw::Unit();
  ImGui::Dummy({ 0.0F, 4.0F * unit });
  if (!cw::SectionHeader("Source folders###SourceFolders",
        "mounts.source_folders", true, "models to import")) {
    return;
  }
  if (ImGui::Button(ICON_FA_FOLDER_PLUS " Source folder\xE2\x80\xA6")) {
    vm_->BrowseForSourceFolder();
  }
  cw::ItemTooltip("Add a folder of models; the Import tab lists its models");
  {
    const auto font = cw::SmallFont {};
    ImGui::PushStyleColor(ImGuiCol_Text, cw::Dim());
    ImGui::TextWrapped("Added folders belong to this machine: the scratch "
                       "project's .mounts.json keeps them.");
    ImGui::PopStyleColor();
  }
  if (!cw::BeginFramedBlock("SourceFolderList")) {
    cw::EndFramedBlock();
    return;
  }
  const auto& style = ImGui::GetStyle();
  for (const auto& folder : vm_->SourceFolders()) {
    ImGui::PushID(folder.name.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(("/" + folder.name).c_str());
    const auto remove_width
      = ImGui::CalcTextSize(ICON_FA_XMARK).x + (style.FramePadding.x * 2.0F);
    const auto path = folder.folder.generic_string();
    ImGui::SameLine(0.0F, 8.0F * unit);
    const float path_width = ImGui::GetContentRegionAvail().x
      - (folder.removable ? remove_width + style.ItemSpacing.x : 0.0F);
    {
      const auto font = cw::SmallFont {};
      ImGui::AlignTextToFramePadding();
      ImGui::TextDisabled("%s", cw::Elide(path, path_width).c_str());
    }
    cw::ItemTooltip(path);
    if (folder.removable) {
      cw::AlignRight(remove_width);
      if (ImGui::Button(ICON_FA_XMARK "##Remove")) {
        commands_->RunText(cc::kRemoveSourceFolder, folder.name);
      }
      cw::ItemTooltip(fmt::format("Remove /{} from the source folders; its "
                                  "files are not touched",
        folder.name));
    }
    ImGui::PopID();
  }
  cw::EndFramedBlock();
}

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg, bugprone-signed-bitwise)
