//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/PanelRegistry.h"

namespace oxygen::examples {

auto PanelRegistry::RegisterPanel(observer_ptr<DemoPanel> panel)
  -> std::expected<void, PanelRegistryError>
{
  if (!panel) {
    return std::unexpected(PanelRegistryError::kNullPanel);
  }

  const std::string_view name = panel->GetName();
  const auto it = std::ranges::find_if(
    panels_, [&](const PanelEntry& entry) { return entry.name == name; });
  if (it != panels_.end()) {
    return std::unexpected(PanelRegistryError::kDuplicateName);
  }

  panels_.push_back(PanelEntry { std::string(name), panel });

  return {};
}

auto PanelRegistry::SetActivePanelByName(std::string_view name)
  -> std::expected<void, PanelRegistryError>
{
  const auto it = std::ranges::find_if(
    panels_, [&](const PanelEntry& entry) { return entry.name == name; });
  if (it == panels_.end()) {
    return std::unexpected(PanelRegistryError::kPanelNotFound);
  }

  active_index_ = static_cast<std::size_t>(std::distance(panels_.begin(), it));
  return {};
}

auto PanelRegistry::ClearActivePanel() noexcept -> void
{
  active_index_.reset();
}

auto PanelRegistry::GetActivePanel() const noexcept -> observer_ptr<DemoPanel>
{
  if (!active_index_.has_value()) {
    return nullptr;
  }
  const auto index = *active_index_;
  if (index >= panels_.size()) {
    return nullptr;
  }
  return panels_[index].panel;
}

auto PanelRegistry::GetActivePanelName() const noexcept -> std::string_view
{
  if (!active_index_.has_value()) {
    return {};
  }
  const auto index = *active_index_;
  if (index >= panels_.size()) {
    return {};
  }
  return panels_[index].name;
}

auto PanelRegistry::GetPanels() const noexcept -> std::span<const PanelEntry>
{
  return panels_;
}

} // namespace oxygen::examples
