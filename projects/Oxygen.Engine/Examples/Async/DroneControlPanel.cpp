//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <imgui.h>

#include "Async/DroneControlPanel.h"
#include "Async/MainModule.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples::async {

namespace {
  constexpr auto kSettingsPrefix = "async.drone_panel";

  auto MakeSettingsKey(const char* suffix) -> std::string
  {
    return std::string(kSettingsPrefix) + "." + suffix;
  }
} // namespace

DroneControlPanel::DroneControlPanel(observer_ptr<MainModule> owner)
  : owner_(owner)
{
  LoadSettings();
}

auto DroneControlPanel::DrawContents() -> void
{
  if (!owner_) {
    return;
  }

  ImGui::Text("Async Demo");
  ImGui::Separator();

  bool settings_changed = false;

  ImGui::SetNextItemOpen(scene_open_, ImGuiCond_Always);
  const bool scene_open = ImGui::CollapsingHeader("Scene");
  if (scene_open_ != scene_open) {
    scene_open_ = scene_open;
    settings_changed = true;
  }
  if (scene_open) {
    owner_->DrawSceneInfoPanel();
  }

  ImGui::SetNextItemOpen(spotlight_open_, ImGuiCond_Always);
  const bool spotlight_open = ImGui::CollapsingHeader("Spotlight");
  if (spotlight_open_ != spotlight_open) {
    spotlight_open_ = spotlight_open;
    settings_changed = true;
  }
  if (spotlight_open) {
    owner_->DrawSpotLightPanel();
  }

  ImGui::SetNextItemOpen(actions_open_, ImGuiCond_Always);
  const bool actions_open = ImGui::CollapsingHeader("Actions");
  if (actions_open_ != actions_open) {
    actions_open_ = actions_open;
    settings_changed = true;
  }
  if (actions_open) {
    owner_->DrawFrameActionsPanel();
  }

  if (settings_changed) {
    SaveSettings();
  }
}

auto DroneControlPanel::OnLoaded() -> void { LoadSettings(); }

auto DroneControlPanel::OnUnloaded() -> void { SaveSettings(); }

auto DroneControlPanel::LoadSettings() -> void
{
  const auto settings = SettingsService::Default();
  if (!settings) {
    return;
  }

  if (const auto value = settings->GetBool(MakeSettingsKey("scene_open"))) {
    scene_open_ = *value;
  }
  if (const auto value = settings->GetBool(MakeSettingsKey("spotlight_open"))) {
    spotlight_open_ = *value;
  }
  if (const auto value = settings->GetBool(MakeSettingsKey("actions_open"))) {
    actions_open_ = *value;
  }
}

auto DroneControlPanel::SaveSettings() -> void
{
  const auto settings = SettingsService::Default();
  if (!settings) {
    return;
  }

  settings->SetBool(MakeSettingsKey("scene_open"), scene_open_);
  settings->SetBool(MakeSettingsKey("spotlight_open"), spotlight_open_);
  settings->SetBool(MakeSettingsKey("actions_open"), actions_open_);
}

} // namespace oxygen::examples::async
