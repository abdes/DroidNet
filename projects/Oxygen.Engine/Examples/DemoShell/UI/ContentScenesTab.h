//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class ContentVm;
class TextFilter;
struct SceneEntry;
struct SceneGroup;

//! The Scenes tab: find a mounted scene and load it.
/*!
 A search field, mount chips, the recent scenes, then one band per mount
 with a node per folder of its scenes. Double-click loads; the action bar loads,
 reloads and unloads.
*/
class ContentScenesTab final {
public:
  ContentScenesTab(
    observer_ptr<ContentVm> vm, observer_ptr<PanelCommands> commands);

  auto Draw() -> void;

private:
  auto DrawFilters() -> void;
  auto DrawRecent() -> void;
  auto DrawGroup(const SceneGroup& group, const TextFilter& filter) -> void;
  auto DrawScene(const SceneEntry& scene) -> void;
  //! The scene's name, virtual path and source, as its row's tooltip.
  auto DrawTooltip(const SceneEntry& scene) const -> void;
  auto DrawActions() -> void;
  auto Load(const SceneEntry& scene) const -> void;

  observer_ptr<ContentVm> vm_;
  observer_ptr<PanelCommands> commands_;
  std::string search_;
  //! The mount chip picked; empty for All.
  std::string mount_;
  std::string selected_path_;
  //! Whether the selection was clicked in Recent; the scene's row in its
  //! mount group is then not highlighted, and the other way round.
  bool selected_in_recent_ { false };
};

} // namespace oxygen::examples::ui
