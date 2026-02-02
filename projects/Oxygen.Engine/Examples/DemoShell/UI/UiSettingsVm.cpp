//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Logging.h>

#include "DemoShell/Services/CameraLifecycleService.h"
#include "DemoShell/Services/UiSettingsService.h"
#include "DemoShell/UI/UiSettingsVm.h"

namespace oxygen::examples::ui {

UiSettingsVm::UiSettingsVm(observer_ptr<UiSettingsService> service,
  observer_ptr<CameraLifecycleService> camera_lifecycle)
  : service_(service)
  , camera_lifecycle_(camera_lifecycle)
{
  Refresh();
}

auto UiSettingsVm::GetAxesVisible() -> bool
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return axes_visible_;
}

auto UiSettingsVm::GetStatsConfig() -> StatsOverlayConfig
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return stats_config_;
}

auto UiSettingsVm::GetActivePanelName() -> std::optional<std::string>
{
  std::lock_guard lock(mutex_);
  if (IsStale()) {
    Refresh();
  }
  return active_panel_name_;
}

auto UiSettingsVm::GetActiveCamera() const -> observer_ptr<scene::SceneNode>
{
  if (!camera_lifecycle_) {
    return nullptr;
  }

  return observer_ptr { &camera_lifecycle_->GetActiveCamera() };
}

auto UiSettingsVm::SetAxesVisible(const bool visible) -> void
{
  std::lock_guard lock(mutex_);
  if (axes_visible_ == visible) {
    return;
  }

  axes_visible_ = visible;
  axes_dirty_ = true;
  service_->SetAxesVisible(visible);
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::SetStatsShowFps(const bool visible) -> void
{
  std::lock_guard lock(mutex_);
  if (stats_config_.show_fps == visible) {
    return;
  }

  stats_config_.show_fps = visible;
  stats_dirty_ = true;
  service_->SetStatsShowFps(visible);
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::SetStatsShowFrameTimingDetail(const bool visible) -> void
{
  std::lock_guard lock(mutex_);
  if (stats_config_.show_frame_timing_detail == visible) {
    return;
  }

  stats_config_.show_frame_timing_detail = visible;
  stats_dirty_ = true;
  service_->SetStatsShowFrameTimingDetail(visible);
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::SetStatsShowEngineTiming(const bool visible) -> void
{
  std::lock_guard lock(mutex_);
  if (stats_config_.show_engine_timing == visible) {
    return;
  }

  stats_config_.show_engine_timing = visible;
  stats_dirty_ = true;
  service_->SetStatsShowEngineTiming(visible);
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::SetStatsShowBudgetStats(const bool visible) -> void
{
  std::lock_guard lock(mutex_);
  if (stats_config_.show_budget_stats == visible) {
    return;
  }

  stats_config_.show_budget_stats = visible;
  stats_dirty_ = true;
  service_->SetStatsShowBudgetStats(visible);
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::SetActivePanelName(std::optional<std::string> panel_name)
  -> void
{
  std::lock_guard lock(mutex_);
  if (panel_name.has_value()) {
    DCHECK_F(!panel_name->empty(), "expecting non-empty panel names");
  }
  if (active_panel_name_ == panel_name) {
    return;
  }

  active_panel_name_ = panel_name;
  active_panel_dirty_ = true;
  service_->SetActivePanelName(std::move(panel_name));
  epoch_ = service_->GetEpoch();
}

auto UiSettingsVm::Refresh() -> void
{
  axes_visible_ = service_->GetAxesVisible();
  stats_config_ = service_->GetStatsConfig();
  active_panel_name_ = service_->GetActivePanelName();
  DCHECK_F(!active_panel_name_.has_value() || !active_panel_name_->empty(),
    "expecting non-empty panel names");
  epoch_ = service_->GetEpoch();
  axes_dirty_ = false;
  stats_dirty_ = false;
  active_panel_dirty_ = false;
}

auto UiSettingsVm::IsStale() const -> bool
{
  return epoch_ != service_->GetEpoch();
}

} // namespace oxygen::examples::ui
