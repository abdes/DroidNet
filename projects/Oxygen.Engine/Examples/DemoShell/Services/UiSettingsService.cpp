//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/Services/UiSettingsService.h"

#include <Oxygen/Base/Logging.h>

namespace oxygen::examples {

auto UiSettingsService::GetAxesVisible() const -> bool
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  const auto value = settings->GetBool(kAxesVisibleKey);
  return value.value_or(kDefaultAxesVisible);
}

auto UiSettingsService::SetAxesVisible(const bool visible) -> void
{
  SetBoolSetting(kAxesVisibleKey, visible, kDefaultAxesVisible);
}

auto UiSettingsService::GetActivePanelName() const -> std::optional<std::string>
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  auto name = settings->GetString(kActivePanelKey);
  if (name.has_value() && name->empty()) {
    return std::nullopt;
  }

  return name;
}

auto UiSettingsService::SetActivePanelName(
  std::optional<std::string> panel_name) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

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

auto UiSettingsService::SetBoolSetting(
  std::string_view key, const bool value, const bool default_value) -> void
{
  const auto settings = SettingsService::ForDemoApp();
  DCHECK_NOTNULL_F(settings);

  const bool is_changed
    = settings->GetBool(key).value_or(default_value) != value;

  settings->SetBool(key, value);
  if (is_changed) {
    epoch_.fetch_add(1U, std::memory_order_acq_rel);
  }
}

} // namespace oxygen::examples
