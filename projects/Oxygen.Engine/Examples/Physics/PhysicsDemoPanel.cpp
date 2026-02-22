//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <imgui.h>

#include <Oxygen/ImGui/Icons/IconsOxygenIcons.h>

#include "Physics/PhysicsDemoPanel.h"

namespace oxygen::examples::physics_demo {

void PhysicsDemoPanel::Initialize(const PhysicsDemoPanelConfig& config)
{
  config_ = config;
}

void PhysicsDemoPanel::UpdateConfig(const PhysicsDemoPanelConfig& config)
{
  config_ = config;
}

auto PhysicsDemoPanel::GetName() const noexcept -> std::string_view
{
  return "Physics";
}

auto PhysicsDemoPanel::GetPreferredWidth() const noexcept -> float
{
  return 430.0F;
}

auto PhysicsDemoPanel::GetIcon() const noexcept -> std::string_view
{
  return oxygen::imgui::icons::kIconDemoPanel;
}

auto PhysicsDemoPanel::OnLoaded() -> void { }

auto PhysicsDemoPanel::OnUnloaded() -> void { }

auto PhysicsDemoPanel::ActionState(
  const std::shared_ptr<oxygen::input::Action>& action) -> const char*
{
  if (!action) {
    return "n/a";
  }
  if (action->WasTriggeredThisFrame()) {
    return "triggered";
  }
  if (action->IsOngoing()) {
    return "ongoing";
  }
  if (action->WasReleasedThisFrame()) {
    return "released";
  }
  return "idle";
}

auto PhysicsDemoPanel::DrawContents() -> void
{
  if (config_.pending_launch && ImGui::Button("Launch (Up)")) {
    *config_.pending_launch = true;
  }
  ImGui::SameLine();
  if (config_.pending_reset && ImGui::Button("Reset (G)")) {
    *config_.pending_reset = true;
  }

  if (config_.launch_impulse) {
    ImGui::SliderFloat(
      "Launch impulse", config_.launch_impulse, 10.0F, 120.0F, "%.1f");
  }

  ImGui::Separator();
  ImGui::Text("Input");
  ImGui::BulletText("Launch: %s", ActionState(config_.launch_action));
  ImGui::BulletText("Reset: %s", ActionState(config_.reset_action));
  ImGui::BulletText(
    "Nudge Left (Left): %s", ActionState(config_.nudge_left_action));
  ImGui::BulletText(
    "Nudge Right (Right): %s", ActionState(config_.nudge_right_action));

  ImGui::Separator();
  ImGui::Text("Runtime");
  if (config_.player_speed) {
    ImGui::BulletText("Player speed: %.3f", *config_.player_speed);
  }
  if (config_.player_settled) {
    ImGui::BulletText(
      "Settled in bowl: %s", *config_.player_settled ? "yes" : "no");
  }
  if (config_.settle_progress) {
    const float denom = (std::max)(0.001F, config_.settle_target);
    const float progress
      = std::clamp(*config_.settle_progress / denom, 0.0F, 1.0F);
    ImGui::ProgressBar(progress, ImVec2(-1.0F, 0.0F), "settle progress");
  }

  ImGui::Separator();
  ImGui::Text("Counters");
  if (config_.launches_count) {
    ImGui::BulletText("Launches: %llu",
      static_cast<unsigned long long>(*config_.launches_count));
  }
  if (config_.resets_count) {
    ImGui::BulletText(
      "Resets: %llu", static_cast<unsigned long long>(*config_.resets_count));
  }
  if (config_.contact_events_count) {
    ImGui::BulletText("Contact events: %llu",
      static_cast<unsigned long long>(*config_.contact_events_count));
  }
  if (config_.flipper_trigger_count) {
    ImGui::BulletText("Flipper hits: %llu",
      static_cast<unsigned long long>(*config_.flipper_trigger_count));
  }
}

} // namespace oxygen::examples::physics_demo
