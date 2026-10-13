//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <string_view>

#include "DemoShell/UI/DemoPanel.h"

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class ContentVm;

//! Imports sources, mounts libraries and loads their scenes.
/*!
 Every control that changes settings or state runs a `demo.content.*`
 command; a file picked in a dialog runs the command the dialog was opened
 for. Text fields keep a local draft until they lose focus.
*/
class ContentLoaderPanel final : public DemoPanel {
public:
  ContentLoaderPanel(
    observer_ptr<ContentVm> vm, observer_ptr<PanelCommands> commands);
  ~ContentLoaderPanel() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ContentLoaderPanel)
  OXYGEN_MAKE_NON_MOVABLE(ContentLoaderPanel)

  //! Draw the panel content.
  auto DrawContents() -> void override;

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;
  auto OnLoaded() -> void override;
  auto OnUnloaded() -> void override;

private:
  auto DrawSourcesSection() -> void;
  auto DrawLibrarySection() -> void;
  auto DrawDiagnosticsSection() -> void;
  auto DrawAdvancedSection() -> void;

  auto DrawWorkflowSettings() -> void;
  auto DrawImportSettings() -> void;
  auto DrawTextureTuningSettings() -> void;
  auto DrawImporterSettings() -> void;
  auto DrawOutputLayout() -> void;
  auto DrawProgress(bool is_importing, bool is_scene_loading) -> void;
  auto RunBrowseSelection() -> void;

  observer_ptr<ContentVm> vm_;
  observer_ptr<PanelCommands> commands_;
  std::array<char, 128> source_filter_ {};
  std::array<char, 128> scene_filter_ {};
  bool importer_restart_pending_ { false };
};

} // namespace oxygen::examples::ui
