//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/UiSettingsService.h"

namespace oxygen::examples {

auto UiSettingsService::GetAxesVisible() const -> bool
{
  const auto settings = ResolveSettings();
  CHECK_NOTNULL_F(settings.get(), "UiSettingsService requires SettingsService");

  const auto value = settings->GetBool(kAxesVisibleKey);
  return value.value_or(kDefaultAxesVisible);
}

auto UiSettingsService::SetAxesVisible(const bool visible) -> void
{
  SetBoolSetting(kAxesVisibleKey, visible, kDefaultAxesVisible);
}

auto UiSettingsService::GetStatsConfig() const -> ui::StatsOverlayConfig
{
  const auto settings = ResolveSettings();
  CHECK_NOTNULL_F(settings.get(), "UiSettingsService requires SettingsService");

  ui::StatsOverlayConfig config {};
  if (const auto show_fps = settings->GetBool(kStatsShowFpsKey)) {
    config.show_fps = *show_fps;
  }
  if (const auto show_detail = settings->GetBool(kStatsShowDetailKey)) {
    config.show_frame_timing_detail = *show_detail;
  }
  if (const auto show_engine = settings->GetBool(kStatsShowEngineKey)) {
    config.show_engine_timing = *show_engine;
  }
  if (const auto show_budget = settings->GetBool(kStatsShowBudgetKey)) {
    config.show_budget_stats = *show_budget;
  }

  return config;
}

auto UiSettingsService::SetStatsShowFps(const bool visible) -> void
{
  SetBoolSetting(kStatsShowFpsKey, visible, false);
}

auto UiSettingsService::SetStatsShowFrameTimingDetail(const bool visible)
  -> void
{
  SetBoolSetting(kStatsShowDetailKey, visible, false);
}

auto UiSettingsService::SetStatsShowEngineTiming(const bool visible) -> void
{
  SetBoolSetting(kStatsShowEngineKey, visible, false);
}

auto UiSettingsService::SetStatsShowBudgetStats(const bool visible) -> void
{
  SetBoolSetting(kStatsShowBudgetKey, visible, false);
}

auto UiSettingsService::GetActivePanelName() const -> std::optional<std::string>
{
  const auto settings = ResolveSettings();
  CHECK_NOTNULL_F(settings.get(), "UiSettingsService requires SettingsService");

  auto name = settings->GetString(kActivePanelKey);
  if (name.has_value() && name->empty()) {
    LOG_F(WARNING,
      "UiSettingsService: ignoring empty active panel name persistence");
    return std::nullopt;
  }

  return name;
}

auto UiSettingsService::SetActivePanelName(
  std::optional<std::string> panel_name) -> void
{
  const auto settings = ResolveSettings();
  CHECK_NOTNULL_F(settings.get(), "UiSettingsService requires SettingsService");

  if (panel_name.has_value() && panel_name->empty()) {
    LOG_F(WARNING,
      "UiSettingsService: ignoring empty active panel name persistence");
    return;
  }

  const std::string desired = panel_name.value_or(std::string {});
  const std::string current
    = settings->GetString(kActivePanelKey).value_or(std::string {});

  if (current == desired) {
    return;
  }

  settings->SetString(kActivePanelKey, desired);
  epoch_.fetch_add(1U, std::memory_order_acq_rel);
}

auto UiSettingsService::GetEpoch() const noexcept -> std::uint64_t
{
  return epoch_.load(std::memory_order_acquire);
}

auto UiSettingsService::ResolveSettings() const noexcept
  -> observer_ptr<SettingsService>
{
  return SettingsService::Default();
}

auto UiSettingsService::SetBoolSetting(
  std::string_view key, const bool value, const bool default_value) -> void
{
  const auto settings = ResolveSettings();
  CHECK_NOTNULL_F(settings.get(), "UiSettingsService requires SettingsService");

  const bool is_changed
    = settings->GetBool(key).value_or(default_value) != value;

  settings->SetBool(key, value);
  if (is_changed) {
    epoch_.fetch_add(1U, std::memory_order_acq_rel);
  }
}

} // namespace oxygen::examples
