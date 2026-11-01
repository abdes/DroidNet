//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include "DemoShell/UI/ContentImportTab.h"
#include "DemoShell/UI/ContentLogTab.h"
#include "DemoShell/UI/ContentMountsTab.h"
#include "DemoShell/UI/ContentPanelNavigation.h"
#include "DemoShell/UI/ContentScenesTab.h"
#include "DemoShell/UI/DemoPanel.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class ContentVm;

//! Puts content in front of the renderer: mounts cooked sources, loads their
//! scenes, imports models and says why something did not load.
/*!
 Four tabs by job (Scenes, Mounts, Import, Log) and a status strip shared by
 them. Every control that changes settings or state runs a `demo.content.*`
 command (`content_commands`); a file picked in a dialog runs the command the
 dialog was opened for.
*/
class ContentLoaderPanel final : public DemoPanel {
public:
  ContentLoaderPanel(
    observer_ptr<ContentVm> vm, observer_ptr<PanelCommands> commands);
  ~ContentLoaderPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ContentLoaderPanel)
  OXYGEN_MAKE_NON_MOVABLE(ContentLoaderPanel)

  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  auto DrawTabs() -> void;
  auto DrawStatusStrip() -> void;
  auto RunBrowseSelection() -> void;

  observer_ptr<ContentVm> vm_;
  observer_ptr<PanelCommands> commands_;
  ContentPanelNavigation navigation_;
  ContentScenesTab scenes_tab_;
  ContentMountsTab mounts_tab_;
  ContentImportTab import_tab_;
  ContentLogTab log_tab_;
  //! The Import tab was open last frame; its models are listed again when it
  //! opens.
  bool import_tab_open_ { false };
};

} // namespace oxygen::examples::ui
