//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <string>

#include <imgui.h>

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/SidePanel.h"

namespace oxygen::examples::ui {

namespace {

  constexpr float kMinPanelWidth = 300.0F;
  constexpr float kMaxPanelWidthRatio = 0.6F;

  auto MakePanelWidthKey(std::string_view panel_name) -> std::string
  {
    std::string key;
    key.reserve(panel_name.size());
    for (const char ch : panel_name) {
      if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
        || (ch >= '0' && ch <= '9')) {
        key.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      } else {
        key.push_back('_');
      }
    }
    return "demo_shell.panels." + key + ".width";
  }

} // namespace

SidePanel::SidePanel(observer_ptr<PanelRegistry> panel_registry)
  : panel_registry_(panel_registry)
{
  DCHECK_NOTNULL_F(panel_registry, "expecting valid PanelRegistry");
}

auto SidePanel::Draw(float left_offset) -> void
{
  const auto active_panel = panel_registry_->GetActivePanel();
  if (!active_panel) {
    return;
  }

  const auto& io = ImGui::GetIO();

  // If the active panel changed, adopt its preferred width.
  const auto active_name = panel_registry_->GetActivePanelName();
  const bool panel_changed = active_name != last_active_panel_name_;
  if (panel_changed) {
    last_active_panel_name_ = std::string(active_name);
    width_ = std::clamp(active_panel->GetPreferredWidth(), kMinPanelWidth,
      io.DisplaySize.x * kMaxPanelWidthRatio);

    if (const auto settings = SettingsService::ForDemoApp()) {
      if (const auto saved_width
        = settings->GetFloat(MakePanelWidthKey(last_active_panel_name_))) {
        width_ = std::clamp(
          *saved_width, kMinPanelWidth, io.DisplaySize.x * kMaxPanelWidthRatio);
      }
    }

    last_saved_panel_width_ = width_;
  }

  const float height = std::max(0.0F, io.DisplaySize.y);
  const float max_width
    = std::max(kMinPanelWidth, io.DisplaySize.x * kMaxPanelWidthRatio);

  ImGui::SetNextWindowPos(ImVec2(left_offset, 0.0F), ImGuiCond_Always);
  ImGui::SetNextWindowSize(ImVec2(width_, height), ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(
    ImVec2(kMinPanelWidth, height), ImVec2(max_width, height));
  ImGui::SetNextWindowBgAlpha(0.45F);

  constexpr auto kFlags = ImGuiWindowFlags_NoMove
    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;

  const std::string panel_title { active_panel->GetName() };
  if (!ImGui::Begin(panel_title.c_str(), nullptr, kFlags)) {
    ImGui::End();
    return;
  }

  width_ = ImGui::GetWindowSize().x;

  const float delta = std::abs(width_ - last_saved_panel_width_);
  const bool resize_finished = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
  if (resize_finished && delta > 0.5F) {
    if (const auto settings = SettingsService::ForDemoApp()) {
      settings->SetFloat(MakePanelWidthKey(last_active_panel_name_), width_);
      last_saved_panel_width_ = width_;
    }
  }

  active_panel->DrawContents();

  ImGui::End();
}

} // namespace oxygen::examples::ui
