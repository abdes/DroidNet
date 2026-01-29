//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <stdexcept>

#include "DemoShell/PanelRegistry.h"

namespace oxygen::examples {

auto PanelRegistry::RegisterPanel(std::shared_ptr<DemoPanel> panel)
  -> std::expected<void, PanelRegistryError>
{
  if (!panel) {
    return std::unexpected(PanelRegistryError::kNullPanel);
  }

  const std::string_view name = panel->GetName();
  if (name.empty()) {
    throw std::invalid_argument("PanelRegistry: empty panel name");
  }
  const auto it = std::ranges::find_if(
    panels_, [&](const PanelEntry& entry) { return entry.name == name; });
  if (it != panels_.end()) {
    throw std::invalid_argument("PanelRegistry: duplicate panel name");
  }

  panels_.push_back(PanelEntry { std::string(name), std::move(panel) });

  if (panels_.back().panel) {
    panels_.back().panel->OnRegistered();
  }

  return {};
}

auto PanelRegistry::SetActivePanelByName(std::string_view name)
  -> std::expected<void, PanelRegistryError>
{
  if (name.empty()) {
    throw std::invalid_argument("PanelRegistry: empty panel name");
  }
  const auto it = std::ranges::find_if(
    panels_, [&](const PanelEntry& entry) { return entry.name == name; });
  if (it == panels_.end()) {
    return std::unexpected(PanelRegistryError::kPanelNotFound);
  }

  const auto new_index
    = static_cast<std::size_t>(std::distance(panels_.begin(), it));
  if (active_index_.has_value() && *active_index_ == new_index) {
    return {};
  }

  if (active_index_.has_value()) {
    const auto old_index = *active_index_;
    if (old_index < panels_.size() && panels_[old_index].panel) {
      panels_[old_index].panel->OnUnloaded();
    }
  }

  active_index_ = new_index;
  if (panels_[new_index].panel) {
    panels_[new_index].panel->OnLoaded();
  }
  return {};
}

auto PanelRegistry::ClearActivePanel() noexcept -> void
{
  if (active_index_.has_value()) {
    const auto old_index = *active_index_;
    if (old_index < panels_.size() && panels_[old_index].panel) {
      panels_[old_index].panel->OnUnloaded();
    }
  }
  active_index_.reset();
}

auto PanelRegistry::GetActivePanel() const noexcept
  -> std::shared_ptr<DemoPanel>
{
  if (!active_index_.has_value()) {
    return {};
  }
  const auto index = *active_index_;
  if (index >= panels_.size()) {
    return {};
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
