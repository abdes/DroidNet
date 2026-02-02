//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/ObserverPtr.h>

#include "DemoShell/UI/DemoPanel.h"
#include "DemoShell/UI/PostProcessVm.h"

namespace oxygen::examples::ui {

//! Panel for controlling post-process effects (Compositing, Exposure,
//! Tonemapping).
class PostProcessPanel final : public DemoPanel {
public:
  explicit PostProcessPanel(observer_ptr<PostProcessVm> vm);

  // DemoPanel interface
  auto DrawContents() -> void override;
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override;
  [[nodiscard]] auto GetPreferredWidth() const noexcept -> float override;
  [[nodiscard]] auto GetIcon() const noexcept -> std::string_view override;

  auto OnRegistered() -> void override { }
  auto OnLoaded() -> void override { }
  auto OnUnloaded() -> void override { }

private:
  void DrawCompositingSection();
  void DrawExposureSection();
  void DrawTonemappingSection();

  observer_ptr<PostProcessVm> vm_;
};

} // namespace oxygen::examples::ui
