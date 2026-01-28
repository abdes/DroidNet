//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include <imgui.h>

#include "DemoShell/Settings/SettingsService.h"
#include "DemoShell/SidePanel.h"

namespace oxygen::examples {

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

auto SidePanel::Initialize(const SidePanelConfig& config) -> void
{
  config_ = config;
}

auto SidePanel::Draw(float left_offset) -> void
{
  if (!config_.panel_registry) {
    return;
  }

  const auto active_panel = config_.panel_registry->GetActivePanel();
  if (!active_panel) {
    return;
  }

  const auto& io = ImGui::GetIO();

  // If the active panel changed, adopt its preferred width.
  const auto active_name = config_.panel_registry->GetActivePanelName();
  if (active_name != last_active_panel_name_) {
    last_active_panel_name_ = std::string(active_name);
    width_ = std::clamp(active_panel->GetPreferredWidth(), kMinPanelWidth,
      io.DisplaySize.x * kMaxPanelWidthRatio);

    if (const auto settings = SettingsService::Default()) {
      if (const auto saved_width
        = settings->GetFloat(MakePanelWidthKey(last_active_panel_name_))) {
        width_ = std::clamp(
          *saved_width, kMinPanelWidth, io.DisplaySize.x * kMaxPanelWidthRatio);
      }
    }
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

  if (const auto settings = SettingsService::Default()) {
    const std::string key = MakePanelWidthKey(last_active_panel_name_);
    const bool same_panel = last_saved_panel_name_ == last_active_panel_name_;
    const float delta = std::abs(width_ - last_saved_panel_width_);
    if (!same_panel || delta > 0.5F) {
      settings->SetFloat(key, width_);
      settings->Save();
      last_saved_panel_name_ = last_active_panel_name_;
      last_saved_panel_width_ = width_;
    }
  }

  active_panel->DrawContents();

  ImGui::End();
}

} // namespace oxygen::examples
