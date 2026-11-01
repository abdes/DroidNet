//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>

#include "DemoShell/PanelCommands.h"
#include "DemoShell/UI/ContentLog.h"
#include "DemoShell/UI/ContentLogTab.h"
#include "DemoShell/UI/ContentPanelCommands.h"
#include "DemoShell/UI/ContentPanelNavigation.h"
#include "DemoShell/UI/ContentPanelWidgets.h"
#include "DemoShell/UI/ContentVm.h"
#include <fmt/chrono.h> // IWYU pragma: keep
#include <fmt/format.h>
#include <imgui.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/ImGui/Styles/IconsFontAwesome.h>

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg) - ImGui

namespace oxygen::examples::ui {

namespace {
  namespace cc = content_commands;
  namespace cw = content_widgets;

  //! Where a row's code and message start, after the list's gutter; its dot
  //! sits before.
  constexpr auto kTextColumn = 14.0F;
  constexpr auto kDotColumn = 4.0F;
  //! The height of the rows' list, in units.
  constexpr auto kRowsHeight = 250.0F;

  auto SeverityColor(const LogSeverity severity) -> ImVec4
  {
    switch (severity) {
    case LogSeverity::kError:
      return cw::kRed;
    case LogSeverity::kWarning:
      return cw::kAmber;
    case LogSeverity::kInfo:
      break;
    }
    return cw::kInfo;
  }

  auto TimeText(const ContentLogEntry& entry) -> std::string
  {
    return fmt::format(
      "{:%H:%M}", std::chrono::floor<std::chrono::minutes>(entry.time));
  }

  auto SearchedText(const ContentLogEntry& entry) -> std::string
  {
    auto searched = std::string {};
    for (const auto& source : entry.searched) {
      if (!searched.empty()) {
        searched += ", ";
      }
      searched += source;
    }
    return searched;
  }

  auto OptionalRow(const char* label, const std::string& value) -> void
  {
    if (!value.empty()) {
      cw::PropertyRow(label, value);
    }
  }
} // namespace

ContentLogTab::ContentLogTab(const observer_ptr<ContentVm> vm,
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

auto ContentLogTab::Draw() -> void
{
  // The tab shows every error, so none stays unread while it is open.
  vm_->MarkLogSeen();
  DrawToolbar();
  DrawRows();
  if (selected_) {
    if (const auto* entry = vm_->Log().Find(*selected_)) {
      DrawDetails(*entry);
    }
  }
}

auto ContentLogTab::DrawToolbar() -> void
{
  const auto& log = vm_->Log();
  const auto errors = fmt::format(ICON_FA_CIRCLE_XMARK " Errors {}##LogErrors",
    log.Count(LogSeverity::kError));
  if (cw::Chip(errors.c_str(), filter_.errors)) {
    filter_.errors = !filter_.errors;
  }
  ImGui::SameLine(0.0F, 4.0F * cw::Unit());
  const auto warnings
    = fmt::format(ICON_FA_TRIANGLE_EXCLAMATION " Warnings {}##LogWarnings",
      log.Count(LogSeverity::kWarning));
  if (cw::Chip(warnings.c_str(), filter_.warnings)) {
    filter_.warnings = !filter_.warnings;
  }
  ImGui::SameLine(0.0F, 4.0F * cw::Unit());
  const auto info = fmt::format(
    ICON_FA_CIRCLE_INFO " Info {}##LogInfo", log.Count(LogSeverity::kInfo));
  if (cw::Chip(info.c_str(), filter_.info)) {
    filter_.info = !filter_.info;
  }
  const auto& style = ImGui::GetStyle();
  const float buttons = ImGui::CalcTextSize("Copy").x
    + ImGui::CalcTextSize("Clear").x + (style.FramePadding.x * 4.0F)
    + style.ItemSpacing.x;
  cw::AlignRight(buttons);
  if (ImGui::Button("Copy##LogCopy")) {
    ImGui::SetClipboardText(log.Text(filter_).c_str());
  }
  cw::ItemTooltip("Copy the rows the filter shows");
  ImGui::SameLine();
  if (ImGui::Button("Clear##LogClear")) {
    commands_->Run(cc::kClearLog);
    selected_.reset();
  }
  static_cast<void>(cw::SearchField(
    "##LogFilter", "Filter by code, path or text", filter_.text));
}

auto ContentLogTab::DrawRows() -> void
{
  const float unit = cw::Unit();
  if (cw::BeginFramedList("LogRows", kRowsHeight * unit)) {
    auto hidden = std::size_t { 0 };
    // Newest first.
    for (const auto& entry : vm_->Log().Entries() | std::views::reverse) {
      if (!ContentLog::Matches(entry, filter_)) {
        ++hidden;
        continue;
      }
      ImGui::PushID(static_cast<int>(entry.id));
      const bool selected = selected_ == entry.id;
      const auto row = cw::ListRow("##entry", selected, 2.0F);
      if (row.clicked) {
        selected_ = entry.id;
      }
      cw::RowDot(row, kDotColumn * unit, SeverityColor(entry.severity));
      const auto time = TimeText(entry);
      cw::RowText(row, 0.0F, time,
        {
          .color = selected ? cw::kWhite : cw::Dim(),
          .small = true,
          .right = true,
        });
      const float text_width = row.size.x - ((kTextColumn + 40.0F) * unit);
      cw::RowText(row, kTextColumn * unit, cw::Elide(entry.code, text_width),
        { .color = selected ? cw::kWhite : ImVec4 {} });
      cw::RowText(row, kTextColumn * unit,
        cw::Elide(entry.message, row.size.x - ((kTextColumn + 8.0F) * unit)),
        {
          .color = selected ? cw::kWhite : cw::Dim(),
          .small = true,
          .line = 1.0F,
        });
      ImGui::PopID();
    }
    if (hidden > 0) {
      ImGui::Dummy({ 0.0F, 6.0F * unit });
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (kTextColumn * unit));
      cw::DimSmall(fmt::format("\xE2\x80\xA6 {} {} hidden by the filter",
        hidden, hidden == 1 ? "row" : "rows"));
    }
  }
  cw::EndFramedList();
}

auto ContentLogTab::DrawDetails(const ContentLogEntry& entry) -> void
{
  ImGui::Dummy({ 0.0F, 4.0F * cw::Unit() });
  if (!cw::SectionHeader("Details###LogDetails", "log.details")) {
    return;
  }
  cw::PropertyRow("Code", entry.code);
  OptionalRow("Scene", entry.scene);
  OptionalRow("Asset", entry.asset);
  OptionalRow("Path", entry.path);
  OptionalRow("Searched", SearchedText(entry));
  {
    const auto font = cw::SmallFont {};
    ImGui::Dummy({ 0.0F, 2.0F * cw::Unit() });
    ImGui::TextWrapped("%s",
      entry.details.empty() ? entry.message.c_str() : entry.details.c_str());
  }
  ImGui::BeginDisabled(!entry.cooked_source);
  if (ImGui::Button("Go to the mount")) {
    navigation_->select_source = entry.cooked_source;
    navigation_->open_tab = ContentTab::kMounts;
  }
  ImGui::EndDisabled();
  cw::ItemTooltip(entry.cooked_source
      ? "Select the cooked source on the Mounts tab"
      : "The row concerns no cooked source");
  ImGui::SameLine();
  if (ImGui::Button("Copy##LogDetailsCopy")) {
    ImGui::SetClipboardText(
      fmt::format("{}: {}\n{}", entry.code, entry.message, entry.details)
        .c_str());
  }
}

} // namespace oxygen::examples::ui

// NOLINTEND(cppcoreguidelines-pro-type-vararg)
