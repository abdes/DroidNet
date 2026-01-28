//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <imgui.h>

#include "DemoShell/UI/SettingsPanel.h"

namespace oxygen::examples::render_scene::ui {

void SettingsPanel::Initialize(const SettingsPanelConfig& config)
{
  config_ = config;
}

void SettingsPanel::UpdateConfig(const SettingsPanelConfig& config)
{
  config_ = config;
}

void SettingsPanel::DrawContents()
{
  if (!config_.demo_knobs) {
    ImGui::TextUnformatted("No settings available");
    return;
  }

  auto& knobs = *config_.demo_knobs;
  ImGui::Checkbox("Axis visibility", &knobs.show_axes_widget);
  ImGui::Spacing();

  if (ImGui::CollapsingHeader("Show Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawStatsSection();
  }
}

void SettingsPanel::DrawStatsSection()
{
  if (!config_.demo_knobs) {
    return;
  }

  auto& knobs = *config_.demo_knobs;
  const bool hide_all
    = !knobs.show_stats_fps && !knobs.show_stats_frame_timing_detail;
  bool hide_all_toggle = hide_all;
  if (ImGui::Checkbox("Hide all", &hide_all_toggle) && hide_all_toggle) {
    knobs.show_stats_fps = false;
    knobs.show_stats_frame_timing_detail = false;
  }

  ImGui::Checkbox("FPS", &knobs.show_stats_fps);
  ImGui::Checkbox(
    "Frame timings detail", &knobs.show_stats_frame_timing_detail);
}

} // namespace oxygen::examples::render_scene::ui
