//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/ContentLoaderPanel.h"
#include "DemoShell/UI/ContentLog.h"
#include "DemoShell/UI/ContentPanelCommands.h"
#include "DemoShell/UI/ContentPanelNavigation.h"
#include "DemoShell/UI/ContentPanelWidgets.h"
#include "DemoShell/UI/ContentVm.h"
#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg) - ImGui

namespace oxygen::examples::ui {

namespace {
  namespace cc = content_commands;
  namespace cw = content_widgets;

  //! Opens the tab `tab` once, when `navigation` asks for it.
  auto TabFlags(ContentPanelNavigation& navigation, const ContentTab tab)
    -> ImGuiTabItemFlags
  {
    if (navigation.open_tab == tab) {
      navigation.open_tab.reset();
      return ImGuiTabItemFlags_SetSelected;
    }
    return ImGuiTabItemFlags_None;
  }

  //! The status strip's height, with the spacing above it.
  auto StatusStripHeight() -> float
  {
    return (28.0F * cw::Unit()) + ImGui::GetStyle().ItemSpacing.y;
  }

  //! A tab's content, scrolling between the tab bar and the status strip; in
  //! the tab item's ID scope, so each tab keeps its own scroll.
  auto DrawTabBody(const std::function<void()>& draw) -> void
  {
    if (cw::BeginTabBody("TabBody", -StatusStripHeight())) {
      draw();
    }
    cw::EndScrollArea();
  }

  auto StatusText(const std::string& text) -> void
  {
    const auto font = cw::SmallFont {};
    ImGui::TextUnformatted(text.c_str());
  }

  //! A count of the strip, such as `1 error`; true when clicked.
  auto StatusCount(const std::string& text, const ImVec4& color) -> bool
  {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      ImGui::SetTooltip("Open the Log");
    }
    return ImGui::IsItemClicked();
  }

  //! A tab label `<name>###<name>` with room for a badge of `units` after
  //! the name.
  auto BadgeRoom(const std::string_view name, const float units) -> std::string
  {
    const float space = std::max(1.0F, ImGui::CalcTextSize(" ").x);
    const auto count
      = static_cast<std::size_t>(std::ceil((units * cw::Unit()) / space));
    return fmt::format("{}{}###{}", name, std::string(count, ' '), name);
  }

  //! The red pill of the Log tab, with the count of errors not yet seen.
  auto DrawCountBadge(const std::size_t count) -> void
  {
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    const float unit = cw::Unit();
    const auto text = std::to_string(count);
    const float size = ImGui::GetFontSize() * (11.0F / 12.0F);
    const float width = std::max(24.0F * unit,
      ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0F, text.c_str()).x
        + (10.0F * unit));
    const float height = 12.0F * unit;
    const auto top_left = ImVec2 { max.x - width - (6.0F * unit),
      ((min.y + max.y) - height) * 0.5F };
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(top_left,
      { top_left.x + width, top_left.y + height }, ImGui::GetColorU32(cw::kRed),
      height * 0.5F);
    const float text_width
      = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0F, text.c_str()).x;
    draw_list->AddText(ImGui::GetFont(), size,
      { top_left.x + ((width - text_width) * 0.5F),
        top_left.y + ((height - size) * 0.5F) },
      ImGui::GetColorU32(cw::kWhite), text.c_str());
  }
} // namespace

ContentLoaderPanel::ContentLoaderPanel(
  const observer_ptr<ContentVm> vm, const observer_ptr<PanelCommands> commands)
  : vm_(vm)
  , commands_(commands)
  , scenes_tab_(vm, commands)
  , mounts_tab_(vm, commands, observer_ptr { &navigation_ })
  , import_tab_(vm, commands)
  , log_tab_(vm, commands, observer_ptr { &navigation_ })
{
  DCHECK_NOTNULL_F(vm_, "ContentLoaderPanel requires ContentVm");
  DCHECK_NOTNULL_F(commands_, "ContentLoaderPanel requires PanelCommands");
  RegisterContentCommands(*commands_, vm_);
}

auto ContentLoaderPanel::DrawContents() -> void
{
  RunBrowseSelection();
  // The tab bar and the status strip stay put; each tab scrolls between them.
  DrawTabs();
  DrawStatusStrip();
}

auto ContentLoaderPanel::DrawTabs() -> void
{
  if (!ImGui::BeginTabBar("ContentLoaderTabs")) {
    return;
  }
  if (ImGui::BeginTabItem("Scenes###Scenes", nullptr,
        TabFlags(navigation_, ContentTab::kScenes))) {
    DrawTabBody([this] -> void { scenes_tab_.Draw(); });
    ImGui::EndTabItem();
  }
  // The badges are drawn in the room the labels keep for them.
  const auto mounts_label = BadgeRoom("Mounts", 10.0F);
  const bool mounts_open = ImGui::BeginTabItem(
    mounts_label.c_str(), nullptr, TabFlags(navigation_, ContentTab::kMounts));
  if (vm_->Mounts().HasProblem()) {
    // A refused, missing or unreadable source marks the tab.
    const auto max = ImGui::GetItemRectMax();
    const auto min = ImGui::GetItemRectMin();
    cw::DrawDot(
      { max.x - (8.0F * cw::Unit()), min.y + (5.0F * cw::Unit()) }, cw::kAmber);
  }
  if (mounts_open) {
    DrawTabBody([this] -> void { mounts_tab_.Draw(); });
    ImGui::EndTabItem();
  }
  const bool import_open = ImGui::BeginTabItem(
    "Import###Import", nullptr, TabFlags(navigation_, ContentTab::kImport));
  if (import_open) {
    if (!import_tab_open_) {
      vm_->RefreshModels();
    }
    DrawTabBody([this] -> void { import_tab_.Draw(); });
    ImGui::EndTabItem();
  }
  import_tab_open_ = import_open;
  const auto unseen = vm_->Log().UnseenErrors();
  const auto log_label
    = unseen > 0 ? BadgeRoom("Log", 30.0F) : std::string("Log###Log");
  const bool log_open = ImGui::BeginTabItem(
    log_label.c_str(), nullptr, TabFlags(navigation_, ContentTab::kLog));
  if (unseen > 0) {
    DrawCountBadge(unseen);
  }
  if (log_open) {
    DrawTabBody([this] -> void { log_tab_.Draw(); });
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

auto ContentLoaderPanel::DrawStatusStrip() -> void
{
  const float unit = cw::Unit();
  const auto window_min = ImGui::GetWindowPos();
  const float window_width = ImGui::GetWindowSize().x;
  const auto top = ImGui::GetCursorScreenPos().y;
  const float height = 28.0F * unit;
  ImGui::GetWindowDrawList()->AddRectFilled({ window_min.x, top },
    { window_min.x + window_width, top + height },
    ImGui::GetColorU32(cw::kStatusFill));
  ImGui::SetCursorScreenPos({ ImGui::GetCursorScreenPos().x,
    top + ((height - ImGui::GetTextLineHeight()) * 0.5F) });

  const auto cancel
    = [this](const char* label, const std::string_view command) -> void {
    const auto font = cw::SmallFont {};
    if (ImGui::SmallButton(label)) {
      commands_->Run(command);
    }
  };
  const auto& log = vm_->Log();
  const auto errors = log.Count(LogSeverity::kError);
  const auto warnings = log.Count(LogSeverity::kWarning);
  if (vm_->IsImportInProgress()) {
    const auto progress = vm_->GetImportProgress();
    cw::Dot(cw::kInfo);
    ImGui::SameLine(0.0F, 4.0F * unit);
    StatusText(fmt::format("Importing {}  ·  {:.0f}%",
      progress.model.filename().string(), progress.progress * 100.0F));
    ImGui::SameLine();
    cancel("Cancel##StatusImport", cc::kCancelImport);
  } else if (const auto* loading = vm_->LoadingScene()) {
    cw::Dot(cw::kInfo);
    ImGui::SameLine(0.0F, 4.0F * unit);
    StatusText(fmt::format("Loading {}", loading->name));
    ImGui::SameLine();
    cancel("Cancel##StatusLoad", cc::kCancelSceneLoad);
  } else if (const auto& loaded = vm_->GetLoadedScene()) {
    cw::Dot(cw::kGreen);
    ImGui::SameLine(0.0F, 4.0F * unit);
    StatusText(fmt::format("{} loaded  ·  {} assets  ·  {} ms",
      loaded->scene.name, loaded->asset_count, loaded->load_time.count()));
  } else {
    cw::Dot(cw::Dim(), false);
    ImGui::SameLine(0.0F, 4.0F * unit);
    cw::DimSmall("No scene loaded");
  }

  // The counts open the Log.
  const auto error_text
    = fmt::format("{} {}", errors, errors == 1 ? "error" : "errors");
  const auto warning_text = fmt::format("{} warn", warnings);
  const auto font = cw::SmallFont {};
  const float spacing = 8.0F * unit;
  const float width = ImGui::CalcTextSize(error_text.c_str()).x + spacing
    + ImGui::CalcTextSize(warning_text.c_str()).x;
  cw::AlignRight(width);
  const bool errors_clicked
    = StatusCount(error_text, errors > 0 ? cw::kRed : cw::Dim());
  ImGui::SameLine(0.0F, spacing);
  const bool warnings_clicked
    = StatusCount(warning_text, warnings > 0 ? cw::kAmber : cw::Dim());
  if (errors_clicked || warnings_clicked) {
    navigation_.open_tab = ContentTab::kLog;
  }
}

auto ContentLoaderPanel::RunBrowseSelection() -> void
{
  const auto selection = vm_->ConsumeBrowseSelection();
  if (!selection) {
    return;
  }
  const auto path = selection->path.string();
  switch (selection->target) {
  case ContentVm::BrowseTarget::kPakFile:
    commands_->RunText(cc::kAddPak, path);
    break;
  case ContentVm::BrowseTarget::kCookedRoot:
    commands_->RunText(cc::kAddCookedRoot, path);
    break;
  case ContentVm::BrowseTarget::kSourceFolder:
    commands_->RunText(cc::kAddSourceFolder, path);
    break;
  case ContentVm::BrowseTarget::kModelFile:
    commands_->RunText(cc::kImportFile, path);
    break;
  }
}

auto ContentLoaderPanel::GetName() const noexcept -> std::string_view
{
  return "Content Loader";
}

auto ContentLoaderPanel::GetPreferredWidth() const noexcept -> float
{
  // The mockup's 430 units.
  constexpr auto kWidthInUnits = 430.0F;
  return kWidthInUnits * cw::Unit();
}

auto ContentLoaderPanel::GetIcon() const noexcept -> std::string_view
{
  return imgui::icons::kIconContentLoader;
}

auto ContentLoaderPanel::OnLoaded() -> void { }

auto ContentLoaderPanel::OnUnloaded() -> void { }

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
