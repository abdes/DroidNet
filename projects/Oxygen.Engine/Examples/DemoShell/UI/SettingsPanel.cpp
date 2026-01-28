//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/SettingsPanel.h"

namespace oxygen::examples::ui {

void SettingsPanel::Initialize(const SettingsPanelConfig& config)
{
  config_ = config;
  LoadSettings();
}

void SettingsPanel::UpdateConfig(const SettingsPanelConfig& config)
{
  config_ = config;
  LoadSettings();
}

void SettingsPanel::DrawContents()
{
  if (!config_.axes_widget && !config_.stats_overlay) {
    ImGui::TextUnformatted("No settings available");
    return;
  }

  if (config_.axes_widget) {
    bool visible = config_.axes_widget->IsVisible();
    if (ImGui::Checkbox("Axis visibility", &visible)) {
      config_.axes_widget->SetVisible(visible);
      SaveAxesVisibleSetting(visible);
    }
  } else {
    ImGui::TextDisabled("Axis visibility (no widget)");
  }
  ImGui::Spacing();

  if (ImGui::CollapsingHeader("Show Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawStatsSection();
  }
}

void SettingsPanel::DrawStatsSection()
{
  if (!config_.stats_overlay) {
    return;
  }

  auto config = config_.stats_overlay->GetConfig();
  const bool hide_all = !config.show_fps && !config.show_frame_timing_detail;
  bool hide_all_toggle = hide_all;
  if (ImGui::Checkbox("Hide all", &hide_all_toggle) && hide_all_toggle) {
    config.show_fps = false;
    config.show_frame_timing_detail = false;
  }

  ImGui::Checkbox("FPS", &config.show_fps);
  ImGui::Checkbox("Frame timings detail", &config.show_frame_timing_detail);
  config_.stats_overlay->SetConfig(config);
  SaveStatsSettings(config);
}

auto SettingsPanel::LoadSettings() -> void
{
  if (settings_loaded_) {
    return;
  }
  const auto settings = oxygen::examples::SettingsService::Default();
  if (!settings) {
    return;
  }

  settings_loaded_ = true;

  if (config_.axes_widget) {
    if (const auto visible = settings->GetString("ui.axes.visible")) {
      const bool is_visible = (*visible == "true");
      config_.axes_widget->SetVisible(is_visible);
    }
  }

  if (config_.stats_overlay) {
    auto config = config_.stats_overlay->GetConfig();
    if (const auto show_fps = settings->GetString("ui.stats.show_fps")) {
      config.show_fps = (*show_fps == "true");
    }
    if (const auto show_detail
      = settings->GetString("ui.stats.show_frame_timing_detail")) {
      config.show_frame_timing_detail = (*show_detail == "true");
    }
    config_.stats_overlay->SetConfig(config);
  }
}

auto SettingsPanel::SaveAxesVisibleSetting(const bool visible) const -> void
{
  const auto settings = oxygen::examples::SettingsService::Default();
  if (!settings) {
    return;
  }

  settings->SetString("ui.axes.visible", visible ? "true" : "false");
  settings->Save();
}

auto SettingsPanel::SaveStatsSettings(const StatsOverlayConfig& config) const
  -> void
{
  const auto settings = oxygen::examples::SettingsService::Default();
  if (!settings) {
    return;
  }

  settings->SetString("ui.stats.show_fps", config.show_fps ? "true" : "false");
  settings->SetString("ui.stats.show_frame_timing_detail",
    config.show_frame_timing_detail ? "true" : "false");
  settings->Save();
}

} // namespace oxygen::examples::ui
