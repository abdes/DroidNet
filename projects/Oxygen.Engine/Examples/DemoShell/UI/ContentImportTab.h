//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <vector>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/UI/ContentImportProfile.h"

#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples {
class PanelCommands;
} // namespace oxygen::examples

namespace oxygen::examples::ui {

class ContentVm;
struct ModelEntry;

//! The Import tab: pick a model of a source mount, see its record and import
//! it with the panel's settings or its record's.
class ContentImportTab final {
public:
  ContentImportTab(
    observer_ptr<ContentVm> vm, observer_ptr<PanelCommands> commands);

  auto Draw() -> void;

private:
  auto DrawSourceBar() -> void;
  auto DrawModels() -> void;
  auto DrawModel(const ModelEntry& model) -> void;
  auto DrawSelected() -> void;
  auto DrawRunningImport() -> void;
  auto DrawSettings() -> void;
  auto DrawGroup(ImportSettingsGroup group,
    const std::vector<ImportSettingsGroup>& changed) -> void;
  auto DrawContentGroup() -> void;
  auto DrawGeometryGroup() -> void;
  auto DrawLightsGroup() -> void;
  auto DrawTexturesGroup() -> void;
  auto DrawImporterGroup() -> void;

  observer_ptr<ContentVm> vm_;
  observer_ptr<PanelCommands> commands_;
  //! The values a field is compared with to mark it changed.
  ImportProfile defaults_;
  std::filesystem::path selected_;
  //! The model of the running import, which the tab selected when it started.
  std::filesystem::path importing_;
};

} // namespace oxygen::examples::ui
