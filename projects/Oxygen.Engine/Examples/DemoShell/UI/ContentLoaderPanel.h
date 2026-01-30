//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "DemoShell/UI/DemoPanel.h"
#include <Oxygen/Base/ObserverPtr.h>

namespace oxygen::examples::ui {

class ContentVm;

//! Unified content loader panel refactored to use MVVM pattern.
/*!
 Provides an ImGui view for the ContentVm. Orchestrates the display of
 import workflows, mounted library browsing, and diagnostics.
*/
class ContentLoaderPanel final : public DemoPanel {
public:
  explicit ContentLoaderPanel(observer_ptr<ContentVm> vm);
  ~ContentLoaderPanel() override = default;

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

  observer_ptr<ContentVm> vm_ { nullptr };
};

} // namespace oxygen::examples::ui
